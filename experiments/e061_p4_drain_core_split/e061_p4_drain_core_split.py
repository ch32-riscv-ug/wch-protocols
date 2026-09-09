"""E061: does splitting the ISR and the memcpy across cores raise the drain?

E059 put the drain loss on a fixed per-chunk cost and E060 showed neither
batching the dequeue nor coalescing the memcpy touches it, so the cost belongs
to the driver's ISR. P4 has two HP cores and the interrupt lands on whichever
core created the driver, so pinning only the harvest elsewhere is the last
task-side lever.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E061 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
CASE = re.compile(
    rb"CASE harvest_core_req=(-?\d+) setup_core=(-?\d+) harvest_core=(-?\d+) "
    rb"sample_rate_hz=(\d+) gate_words=(\d+) loop_words=(\d+) "
    rb"window_bytes=(\d+) predicted_overload=(\d+) pattern_period=(\d+) "
    rb"ring_mod_period=(\d+) expected_run=(\d+) expected_duty_ppc=(\d+) "
    rb"config=(\S+) delimiter=(\S+) rmt=(\S+) rmt_enable=(\S+) source=(\S+) "
    rb"source_start=(\S+) rmt_receive=(\S+) enable=(\S+) receive=(\S+) "
    rb"disable=(\S+) rmt_disable=(\S+) sync=(\S+) rmt_callbacks=(\d+) "
    rb"rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) high_max=(\d+) "
    rb"low_count=(\d+) low_min=(\d+) low_max=(\d+) parlio_callbacks=(\d+) "
    rb"dequeues=(\d+) harvested=(\d+) overflow=(\d+) violations=(\d+) "
    rb"shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) isr_inflight_max=(\d+) matched_steps=(\d+) "
    rb"compared_steps=(\d+) harvest_us=(-?\d+) harvest_kbps=(\d+) "
    rb"memcpy_us=(-?\d+) memcpy_bytes=(\d+) memcpy_calls=(\d+)\r?\n"
)
CASES = (-1, 0, 1)
RATE_MHZ = 160
WINDOW_BYTES = 96000
DESTINATION = 4 * 1024 * 1024
FLOOR_BYTES = 8064


def _skip_list(dut, label, req, item, count):
    pattern = re.compile(
        (rf"{label} harvest_core_req={req}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    dut.expect(pattern, timeout=120)


def test_drain_core_split(dut):
    """Compare the harvest on the caller's core against each pinned core."""
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
    for req in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=120).groups()]
        assert int(values[0]) == req
        setup_core, harvest_core = int(values[1]), int(values[2])
        assert int(values[3]) == RATE_MHZ * 1_000_000
        assert int(values[6]) == WINDOW_BYTES
        api = values[12:24]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, task_inflight, ring_bytes, ring_overrun,
         isr_inflight, matched_steps, compared_steps, harvest_us,
         harvest_kbps, memcpy_us, memcpy_bytes, memcpy_calls) = map(
            int, values[24:49]
        )
        _skip_list(dut, "SYMBOLS", req, r"\d+:\d+", 16)
        _skip_list(dut, "VIOLS", req, r"\d+", 16)

        assert all(v == "ESP_OK" for v in api), api
        assert harvest_us > 0 and harvested > 0 and memcpy_us > 0
        # The harvest must actually have landed where it was asked to.
        if req >= 0:
            assert harvest_core == req, (req, harvest_core)
        else:
            assert harvest_core == setup_core, (setup_core, harvest_core)
        expected_breaks = DESTINATION // WINDOW_BYTES
        clean = (
            overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
            and violations <= expected_breaks + 1
        )
        growth = max(isr_inflight - FLOOR_BYTES, 0)
        window_us = WINDOW_BYTES // RATE_MHZ
        drain = RATE_MHZ - (growth // window_us if window_us else 0)
        observations.append(
            (req, setup_core, harvest_core, harvest_core != setup_core, clean,
             isr_inflight, drain, memcpy_bytes // memcpy_us, memcpy_calls,
             memcpy_us * 100 // harvest_us, violations, expected_breaks,
             matched_steps, compared_steps)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE061 req={row[0]} setup_core={row[1]} harvest_core={row[2]} "
            f"split={row[3]} clean={row[4]} isr_peak={row[5]} "
            f"drain={row[6]}MB/s memcpy_bw={row[7]}MB/s calls={row[8]} "
            f"share={row[9]}% violations={row[10]} (expect {row[11]}) "
            f"steps={row[12]}/{row[13]}"
        )
    print(f"\nE061 core_split_observations={observations}")

    for row in observations:
        assert row[4], f"case did not capture cleanly: {row}"
