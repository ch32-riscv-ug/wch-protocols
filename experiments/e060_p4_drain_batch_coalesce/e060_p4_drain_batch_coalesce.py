"""E060: can batching the dequeue and coalescing memcpy raise the drain?

E059 pinned the drain loss on a fixed 3.6-5.2 us per chunk of ISR, queue and
loop work rather than a slower memcpy. The driver walks the ring linearly
(E055), so chunks arriving back to back are contiguous and can share a single
memcpy - and the queue can be drained without blocking per chunk. Three cases
separate the two savings.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E060 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
CASE = re.compile(
    rb"CASE batch=(\d+) coalesce=(\d+) sample_rate_hz=(\d+) gate_words=(\d+) "
    rb"loop_words=(\d+) window_bytes=(\d+) predicted_overload=(\d+) "
    rb"pattern_period=(\d+) ring_mod_period=(\d+) expected_run=(\d+) "
    rb"expected_duty_ppc=(\d+) config=(\S+) delimiter=(\S+) rmt=(\S+) "
    rb"rmt_enable=(\S+) source=(\S+) source_start=(\S+) rmt_receive=(\S+) "
    rb"enable=(\S+) receive=(\S+) disable=(\S+) rmt_disable=(\S+) sync=(\S+) "
    rb"rmt_callbacks=(\d+) rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) "
    rb"high_max=(\d+) low_count=(\d+) low_min=(\d+) low_max=(\d+) "
    rb"parlio_callbacks=(\d+) dequeues=(\d+) harvested=(\d+) overflow=(\d+) "
    rb"violations=(\d+) shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) isr_inflight_max=(\d+) matched_steps=(\d+) "
    rb"compared_steps=(\d+) harvest_us=(-?\d+) harvest_kbps=(\d+) "
    rb"memcpy_us=(-?\d+) memcpy_bytes=(\d+) memcpy_calls=(\d+) "
    rb"max_batch=(\d+)\r?\n"
)
CASES = ((0, 0), (1, 0), (1, 1))
RATE_MHZ = 160
WINDOW_BYTES = 96000
CHUNK_BYTES = 4032
DESTINATION = 4 * 1024 * 1024
FLOOR_BYTES = 8064


def _skip_list(dut, label, batch, coalesce, item, count):
    pattern = re.compile(
        (rf"{label} batch={batch} coalesce={coalesce}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    dut.expect(pattern, timeout=120)


def test_drain_batch_coalesce(dut):
    """Compare the baseline against batched and coalesced harvesting."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=10,
    )

    observations = []
    for batch, coalesce in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=120).groups()]
        assert (int(values[0]), int(values[1])) == (batch, coalesce)
        assert int(values[2]) == RATE_MHZ * 1_000_000
        assert int(values[5]) == WINDOW_BYTES
        api = values[11:23]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, task_inflight, ring_bytes, ring_overrun,
         isr_inflight, matched_steps, compared_steps, harvest_us,
         harvest_kbps, memcpy_us, memcpy_bytes, memcpy_calls,
         max_batch) = map(int, values[23:49])
        _skip_list(dut, "SYMBOLS", batch, coalesce, r"\d+:\d+", 16)
        _skip_list(dut, "VIOLS", batch, coalesce, r"\d+", 16)

        assert all(v == "ESP_OK" for v in api), api
        assert harvest_us > 0 and harvested > 0 and memcpy_us > 0
        expected_breaks = DESTINATION // WINDOW_BYTES
        clean = (
            overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
            and violations <= expected_breaks + 1
        )
        # drain from the overload growth above the pipeline floor.
        growth = max(isr_inflight - FLOOR_BYTES, 0)
        window_us = WINDOW_BYTES * 1000 // (RATE_MHZ * 1000)
        deficit = growth // window_us if window_us else 0
        drain = RATE_MHZ - deficit
        memcpy_mbps = memcpy_bytes // memcpy_us
        avg_copy = memcpy_bytes // memcpy_calls if memcpy_calls else 0
        observations.append(
            (batch, coalesce, clean, isr_inflight, drain, memcpy_mbps,
             avg_copy, memcpy_calls, dequeues, max_batch,
             memcpy_us * 100 // harvest_us, violations, expected_breaks,
             matched_steps, compared_steps)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE060 batch={row[0]} coalesce={row[1]} clean={row[2]} "
            f"isr_peak={row[3]} drain={row[4]}MB/s memcpy_bw={row[5]}MB/s "
            f"avg_copy={row[6]}B calls={row[7]} dequeues={row[8]} "
            f"max_batch={row[9]} share={row[10]}% "
            f"violations={row[11]} (expect {row[12]}) "
            f"steps={row[13]}/{row[14]}"
        )
    print(f"\nE060 batch_observations={observations}")

    # Every case must capture cleanly, or its peak says nothing about drain.
    for row in observations:
        assert row[2], f"case did not capture cleanly: {row}"
