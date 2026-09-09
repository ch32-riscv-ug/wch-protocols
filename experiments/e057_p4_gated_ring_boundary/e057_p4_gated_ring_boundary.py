"""E057: where is condition 2's boundary on a non-aliasing ring?

E056 showed every ring size used so far was an exact multiple of the 4,096-byte
sample pattern period, hiding ring overwrites. With the ring at 63,488 - 15.5
periods - overwrites show up, so condition 2's boundary can finally be measured
instead of inferred: peak backlog is window * (1 - 82/160), which crosses the
ring between gate 4,000 and 5,000.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E057 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rmt_rx_candidates=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) source_rate_hz=(\d+) source_words=(\d+) "
    rb"rmt_resolution_hz=(\d+) rmt_symbols=(\d+) sample_rate_hz=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE gate_words=(\d+) loop_words=(\d+) window_bytes=(\d+) "
    rb"predicted_overload=(\d+) ring_mod_period=(\d+) tick_scale=(\d+) "
    rb"expected_run=(\d+) "
    rb"expected_duty_ppc=(\d+) config=(\S+) delimiter=(\S+) rmt=(\S+) "
    rb"rmt_enable=(\S+) source=(\S+) source_start=(\S+) rmt_receive=(\S+) "
    rb"enable=(\S+) receive=(\S+) disable=(\S+) rmt_disable=(\S+) sync=(\S+) "
    rb"rmt_callbacks=(\d+) rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) "
    rb"high_max=(\d+) low_count=(\d+) low_min=(\d+) low_max=(\d+) "
    rb"parlio_callbacks=(\d+) dequeues=(\d+) harvested=(\d+) overflow=(\d+) "
    rb"violations=(\d+) shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) matched_steps=(\d+) compared_steps=(\d+) "
    rb"harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
GATE_WIDTHS = (1000, 2000, 3000, 4000, 5000)
RING_BYTES = 63488
PATTERN_PERIOD = 4096
DRAIN_MBPS = 82
RATE_MBPS = 160
DESTINATION = 4 * 1024 * 1024
CHUNK_BYTES = 4032

SAMPLE_RATE_HZ = 160_000_000
SOURCE_RATE_HZ = 5_000_000
EXPECTED_RUN = SAMPLE_RATE_HZ // SOURCE_RATE_HZ


def _expect_list(dut, label, gate_words, item, count):
    pattern = re.compile(
        (rf"{label} gate_words={gate_words}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=30).group(1).split()


def test_gated_ring_boundary(dut):
    """Sweep the window at a fixed duty, where the models disagree."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[2] == 8
    assert env[4] == SOURCE_RATE_HZ and env[8] == SAMPLE_RATE_HZ
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=5,
    )

    observations = []
    for gate_words in GATE_WIDTHS:
        values = [v.decode() for v in dut.expect(CASE, timeout=60).groups()]
        assert int(values[0]) == gate_words
        loop_words, window_bytes, predicted_overload = map(int, values[1:4])
        ring_mod_period, tick_scale, expected_run, expected_duty_ppc = map(
            int, values[4:8]
        )
        assert loop_words == gate_words * 2
        assert expected_run == EXPECTED_RUN
        assert window_bytes == gate_words * EXPECTED_RUN
        # The ring must not alias with the pattern period (E056), or an
        # overwrite leaves no trace and nothing is being measured.
        assert ring_mod_period == RING_BYTES % PATTERN_PERIOD != 0
        api = values[8:20]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         matched_steps, compared_steps, harvest_us, harvest_kbps) = map(
            int, values[20:41]
        )
        _expect_list(dut, "SYMBOLS", gate_words, r"\d+:\d+", 16)
        offsets = [
            int(v) for v in _expect_list(dut, "VIOLS", gate_words, r"\d+", 16)
        ]

        assert all(v == "ESP_OK" for v in api), api
        assert harvest_us > 0 and harvested > 0
        assert ring_bytes == RING_BYTES
        assert ring_overrun == (1 if inflight_max > ring_bytes else 0)
        # Check the firmware's arithmetic independently.
        assert predicted_overload == (
            window_bytes * (RATE_MBPS - DRAIN_MBPS) // RATE_MBPS
        )

        # Usable capacity is whole chunks only: the tail of the ring cannot
        # hold a full 4,032-byte descriptor.
        usable = ring_bytes // CHUNK_BYTES * CHUNK_BYTES
        model_says_ok = predicted_overload < usable
        expected_breaks = DESTINATION // window_bytes
        # Breaks beyond the window-boundary count mean samples were lost.
        measured_ok = (
            overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
            and violations <= expected_breaks + 1
        )
        observations.append(
            (gate_words, window_bytes, predicted_overload, model_says_ok,
             measured_ok, inflight_max, ring_overrun, overflow, shared_low,
             violations, expected_breaks, matched_steps, compared_steps,
             harvest_kbps)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE057 gate={row[0]} window={row[1]}B peak_backlog={row[2]}B "
            f"model_ok={row[3]} measured_ok={row[4]} inflight={row[5]} "
            f"overrun={row[6]} overflow={row[7]} shared_low={row[8]} "
            f"violations={row[9]} (expect {row[10]}) "
            f"steps={row[11]}/{row[12]} kbps={row[13]}"
        )
    agreement = [(r[0], r[3], r[4]) for r in observations]
    print(f"\nE057 model_vs_measured={agreement}")
    print(f"E057 ring_boundary_observations={observations}")

    # The smallest window is the harness check.
    assert observations[0][4], f"1000 baseline is not clean: {observations[0]}"
