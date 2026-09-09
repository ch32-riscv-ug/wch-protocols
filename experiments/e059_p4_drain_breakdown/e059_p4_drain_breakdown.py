"""E059: is the drain loss memory contention or ISR overhead?

E058 measured the in-window drain falling from over 100 MB/s to 86.1 at
160 MHz, while E020 clocked a DMA-free internal-to-PSRAM copy at 138-183.
Timing the memcpy itself separates the two candidates: if its bandwidth falls
with the rate the DMA and the CPU are contending for the ring, and if it holds
while the time share drops then the per-chunk ISR is eating the CPU.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E059 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rmt_rx_candidates=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) source_rate_hz=(\d+) source_words=(\d+) "
    rb"rmt_resolution_hz=(\d+) rmt_symbols=(\d+) ring_size=(\d+) "
    rb"window_bytes=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE sample_rate_hz=(\d+) gate_words=(\d+) loop_words=(\d+) "
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
    rb"memcpy_us=(-?\d+) memcpy_bytes=(\d+)\r?\n"
)
CASES = ((80, 6000), (100, 4800), (120, 4000), (160, 3000))
WINDOW_BYTES = 96000
RING_BYTES = 62720
SOURCE_RATE_HZ = 5_000_000
CHUNK_BYTES = 4032
DESTINATION = 4 * 1024 * 1024


def _expect_list(dut, label, gate_words, item, count):
    pattern = re.compile(
        (rf"{label} gate_words={gate_words}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=120).group(1).split()


def test_drain_breakdown(dut):
    """Recover the in-window drain at four sample rates."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=10).groups()))
    assert env[0] == 1 and env[2] == 8
    assert env[4] == SOURCE_RATE_HZ
    assert env[8] == RING_BYTES and env[9] == WINDOW_BYTES
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=10,
    )

    observations = []
    for rate_mhz, gate_words in CASES:
        rate_hz = rate_mhz * 1_000_000
        values = [v.decode() for v in dut.expect(CASE, timeout=120).groups()]
        assert (int(values[0]), int(values[1])) == (rate_hz, gate_words)
        loop_words, window_bytes, predicted_overload = map(int, values[2:5])
        pattern_period, ring_mod_period, expected_run, expected_duty_ppc = map(
            int, values[5:9]
        )
        assert loop_words == gate_words * 2
        assert expected_run == rate_hz // SOURCE_RATE_HZ
        # The window must be identical across rates, or the copy work moves too.
        assert window_bytes == WINDOW_BYTES
        assert pattern_period == 128 * expected_run
        # No aliasing, or a ring overwrite would leave no trace (E056).
        assert ring_mod_period == RING_BYTES % pattern_period != 0
        api = values[9:21]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, task_inflight, ring_bytes, ring_overrun,
         isr_inflight, matched_steps, compared_steps, harvest_us,
         harvest_kbps, memcpy_us, memcpy_bytes) = map(int, values[21:45])
        _expect_list(dut, "SYMBOLS", gate_words, r"\d+:\d+", 16)
        _expect_list(dut, "VIOLS", gate_words, r"\d+", 16)

        assert all(v == "ESP_OK" for v in api), api
        assert ring_bytes == RING_BYTES
        assert harvest_us > 0 and harvested > 0
        expected_breaks = DESTINATION // window_bytes
        clean = (
            overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
            and violations <= expected_breaks + 1
        )
        # drain = rate * (1 - peak / window), in MB/s.
        drain = (
            rate_mhz * (window_bytes - isr_inflight) // window_bytes
            if isr_inflight < window_bytes else 0
        )
        # Bandwidth seen from inside memcpy, and the share of wall time it got.
        memcpy_mbps = memcpy_bytes // memcpy_us if memcpy_us > 0 else 0
        memcpy_share_ppc = memcpy_us * 100 // harvest_us
        observations.append(
            (rate_mhz, gate_words, clean, isr_inflight, task_inflight,
             drain, memcpy_mbps, memcpy_share_ppc, memcpy_us, memcpy_bytes,
             violations, expected_breaks, matched_steps, compared_steps,
             overflow)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE059 rate={row[0]}MHz gate={row[1]} clean={row[2]} "
            f"isr_peak={row[3]} task_peak={row[4]} drain={row[5]}MB/s "
            f"memcpy_bw={row[6]}MB/s share={row[7]}% "
            f"(memcpy {row[9]}B in {row[8]}us) "
            f"violations={row[10]} (expect {row[11]}) "
            f"steps={row[12]}/{row[13]} overflow={row[14]}"
        )
    print(f"\nE059 breakdown_observations={observations}")

    # Every case is meant to be inside both conditions, so a break means the
    # peak measurement is contaminated and says nothing about the drain.
    for row in observations:
        assert row[2], f"case did not capture cleanly: {row}"
        assert row[3] >= row[4], f"ISR peak below task peak: {row}"
