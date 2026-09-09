"""E044: timestamp gate windows in hardware by fanning the gate line to RMT RX.

E043 showed the gated stream carries no window boundaries, so a variable-width
gate loses its time axis. The gate line is a GPIO and the matrix can fan one
input to several peripherals (E041), so RMT RX may be able to record the high
and low durations alongside the PARLIO capture, at no CPU cost.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E044 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rmt_rx_candidates=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) sample_rate_hz=(\d+) source_words=(\d+) "
    rb"rmt_resolution_hz=(\d+) rmt_symbols=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE gate_words=(\d+) expected_window=(\d+) expected_high=(\d+) "
    rb"expected_low=(\d+) config=(\S+) delimiter=(\S+) rmt=(\S+) "
    rb"rmt_enable=(\S+) source=(\S+) source_start=(\S+) rmt_receive=(\S+) "
    rb"enable=(\S+) receive=(\S+) disable=(\S+) rmt_disable=(\S+) sync=(\S+) "
    rb"rmt_callbacks=(\d+) rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) "
    rb"high_max=(\d+) low_count=(\d+) low_min=(\d+) low_max=(\d+) "
    rb"parlio_callbacks=(\d+) dequeues=(\d+) harvested=(\d+) overflow=(\d+) "
    rb"violations=(\d+) aligned_violations=(\d+) harvest_us=(-?\d+) "
    rb"harvest_kbps=(\d+)\r?\n"
)
GATE_WIDTHS = (2044, 1020)
SOURCE_WORDS = 8192
SOURCE_DIVIDER = 4
DATA_WIDTH = 4
REPORTED_SYMBOLS = 16


def _expect_symbols(dut, gate_words):
    pattern = re.compile(
        (rf"SYMBOLS gate_words={gate_words}" + r"((?: \d+:\d+){%d})\r?\n"
         % REPORTED_SYMBOLS).encode()
    )
    raw = dut.expect(pattern, timeout=30).group(1).split()
    return [tuple(int(v) for v in item.split(b":")) for item in raw]


def test_gate_rmt_timestamp(dut):
    """Record whether RMT RX reads back the gate high and low durations."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[1] >= 1 and env[2] == DATA_WIDTH
    assert env[4] == 20_000_000 and env[5] == SOURCE_WORDS
    assert env[6] == 20_000_000 and env[7] == REPORTED_SYMBOLS
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=5,
    )

    observations = []
    for gate_words in GATE_WIDTHS:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        assert int(values[0]) == gate_words
        expected_window, expected_high, expected_low = map(int, values[1:4])
        assert expected_window == gate_words * SOURCE_DIVIDER * DATA_WIDTH // 8
        assert expected_high == gate_words * SOURCE_DIVIDER
        assert expected_low == (SOURCE_WORDS - gate_words) * SOURCE_DIVIDER
        api = values[4:20]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, aligned, harvest_us, harvest_kbps) = map(
            int, values[20:36]
        )
        symbols = _expect_symbols(dut, gate_words)

        assert harvest_us > 0
        # Every API up to the two receives must succeed, or the case says
        # nothing about whether the two peripherals can share the pin.
        assert all(v == "ESP_OK" for v in api[:9]), api
        assert aligned <= violations

        rmt_ok = (
            rmt_stored > 0
            and high_count > 0
            and high_min == high_max == expected_high
            and low_count > 0
            and low_min == low_max == expected_low
        )
        parlio_ok = harvested > 0 and violations > 0 and aligned == violations
        observations.append(
            (gate_words, rmt_ok, parlio_ok, rmt_callbacks, rmt_stored,
             high_count, high_min, high_max, expected_high, low_count, low_min,
             low_max, expected_low, harvested, violations, aligned,
             harvest_kbps, symbols[:6])
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE044 gate_words={row[0]} rmt_ok={row[1]} parlio_ok={row[2]} "
            f"high={row[6]}..{row[7]} (expect {row[8]}) "
            f"low={row[10]}..{row[11]} (expect {row[12]}) "
            f"symbols={row[17]}"
        )
    print(f"\nE044 rmt_observations={observations}")
