"""E043: can a gated stream be cut back into its gate windows?

E040 showed hardware gating thins the stream to the gate duty, which matters
because this bench can only download over a CH343 UART bridge. But the kept
samples arrive concatenated, so this asks whether the window boundaries survive
- either as callback boundaries or as a visible break in the source ramp.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E043 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rx_units=(\d+) data_width=(\d+) valid_pin=(\d+) "
    rb"sample_rate_hz=(\d+) source_words=(\d+) gate_index=(\d+) "
    rb"reported_chunks=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE gate_words=(\d+) expected_window=(\d+) config=(\S+) "
    rb"delimiter=(\S+) source=(\S+) source_start=(\S+) enable=(\S+) "
    rb"receive=(\S+) disable=(\S+) sync=(\S+) callbacks=(\d+) dequeues=(\d+) "
    rb"harvested=(\d+) overflow=(\d+) runs=(\d+) run_min=(\d+) run_max=(\d+) "
    rb"violations=(\d+) aligned_violations=(\d+) harvest_us=(-?\d+) "
    rb"harvest_kbps=(\d+)\r?\n"
)
GATE_WIDTHS = (2044, 1020)
SOURCE_DIVIDER = 4
DATA_WIDTH = 4
REPORTED_CHUNKS = 24


def _expect_list(dut, label, gate_words, count):
    pattern = re.compile(
        (rf"{label} gate_words={gate_words}" + r"((?: \d+){%d})\r?\n" % count
         ).encode()
    )
    return [int(v) for v in dut.expect(pattern, timeout=30).group(1).split()]


def test_parlio_gate_window_boundary(dut):
    """Record chunk lengths and ramp breaks against the expected window."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[:3] == (1, 1, DATA_WIDTH)
    assert env[4] == 20_000_000 and env[5] == 16384
    assert env[7] == REPORTED_CHUNKS
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 queue_ok=1", timeout=5
    )

    observations = []
    for gate_words in GATE_WIDTHS:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        assert int(values[0]) == gate_words
        expected_window = int(values[1])
        assert expected_window == gate_words * SOURCE_DIVIDER * DATA_WIDTH // 8
        api = values[2:10]
        (callbacks, dequeues, harvested, overflow, runs, run_min, run_max,
         violations, aligned, harvest_us, harvest_kbps) = map(
            int, values[10:21]
        )
        chunks = _expect_list(dut, "CHUNKS", gate_words, REPORTED_CHUNKS)
        viols = _expect_list(dut, "VIOLS", gate_words, 8)

        assert all(v == "ESP_OK" for v in api[:6]), api
        assert harvest_us > 0
        assert harvested > 0 and callbacks >= dequeues > 0
        assert aligned <= violations
        # Nibble packing only stays consistent if every window ends on a byte,
        # so a window length in samples must be even.
        assert expected_window * 2 % 2 == 0

        observations.append(
            (gate_words, expected_window, callbacks, dequeues, harvested,
             overflow, runs, run_min, run_max, violations, aligned,
             harvest_kbps, chunks[:8], viols)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        gate_words, expected_window = row[0], row[1]
        print(
            f"\nE043 gate_words={gate_words} expected_window={expected_window} "
            f"callbacks={row[2]} harvested={row[4]} violations={row[9]} "
            f"aligned={row[10]} chunks={row[12]} viols={row[13]}"
        )
    print(f"\nE043 boundary_observations={observations}")
