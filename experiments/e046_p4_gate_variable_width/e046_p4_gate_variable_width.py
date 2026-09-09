"""E046: reconstruct variable-width gate windows from the RMT duration list.

E045 measured a fixed gate, so it only showed RMT agreeing with an arithmetic
expectation. Here one source loop carries four different widths, and the test
predicts the byte offsets of the ramp breaks from RMT's high durations alone -
if those land on the breaks the capture actually shows, the two hardware paths
can be joined into a time axis.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E046 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rmt_rx_candidates=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) sample_rate_hz=(\d+) source_words=(\d+) "
    rb"rmt_resolution_hz=(\d+) rmt_symbols=(\d+) windows=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE windows=(\d+) gate_total=(\d+) expected_loop_bytes=(\d+) "
    rb"expected_duty_ppc=(\d+) config=(\S+) delimiter=(\S+) rmt=(\S+) "
    rb"rmt_enable=(\S+) source=(\S+) source_start=(\S+) rmt_receive=(\S+) "
    rb"enable=(\S+) receive=(\S+) disable=(\S+) rmt_disable=(\S+) sync=(\S+) "
    rb"rmt_callbacks=(\d+) rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) "
    rb"high_max=(\d+) low_count=(\d+) low_min=(\d+) low_max=(\d+) "
    rb"parlio_callbacks=(\d+) dequeues=(\d+) harvested=(\d+) overflow=(\d+) "
    rb"violations=(\d+) harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
SYMBOLS = re.compile(rb"SYMBOLS((?: \d+:\d+){16})\r?\n")
VIOLS = re.compile(rb"VIOLS((?: \d+){16})\r?\n")

WIDTHS = (300, 700, 1500, 2044)
SOURCE_WORDS = 16384
SOURCE_DIVIDER = 4
DATA_WIDTH = 4
EXPECTED_HIGH = tuple(w * SOURCE_DIVIDER for w in WIDTHS)
EXPECTED_WINDOW_BYTES = tuple(h * DATA_WIDTH // 8 for h in EXPECTED_HIGH)


def test_gate_variable_width(dut):
    """Predict the ramp breaks from RMT durations and compare with the data."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[2] == DATA_WIDTH
    assert env[4] == 20_000_000 and env[5] == SOURCE_WORDS
    assert env[6] == 20_000_000 and env[8] == len(WIDTHS)
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=5,
    )

    values = [v.decode() for v in dut.expect(CASE, timeout=60).groups()]
    assert int(values[0]) == len(WIDTHS)
    gate_total, expected_loop_bytes, expected_duty_ppc = map(int, values[1:4])
    assert gate_total == sum(WIDTHS)
    assert expected_loop_bytes == gate_total * SOURCE_DIVIDER * DATA_WIDTH // 8
    api = values[4:16]
    (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
     low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
     violations, harvest_us, harvest_kbps) = map(int, values[16:31])

    symbols = [
        tuple(int(v) for v in item.split(b":"))
        for item in dut.expect(SYMBOLS, timeout=30).group(1).split()
    ]
    offsets = [int(v) for v in dut.expect(VIOLS, timeout=30).group(1).split()]
    dut.expect_exact("DONE status=ok", timeout=10)

    assert all(v == "ESP_OK" for v in api), api
    assert harvest_us > 0 and harvested > 0
    assert rmt_stored > 0 and high_count > 0 and low_count > 0

    highs = [duration for level, duration in symbols if level == 1 and duration]
    lows = [duration for level, duration in symbols if level == 0 and duration]
    # Every high duration must be one of the four configured widths, and the
    # set of widths must all show up.
    high_set = sorted(set(highs))
    widths_ok = high_set == sorted(EXPECTED_HIGH)

    # Predict break spacing from RMT alone: window bytes = ticks * width / 8.
    predicted = [h * DATA_WIDTH // 8 for h in highs]
    steps = [b - a for a, b in zip(offsets, offsets[1:]) if b > a]
    # The harvest can start mid-window, so match the steps against the cyclic
    # window sequence rather than a fixed phase.
    matched = sum(1 for s in steps if s in set(predicted))

    print(
        f"\nE046 highs={highs[:8]} expected_high={list(EXPECTED_HIGH)}"
        f"\nE046 lows={lows[:8]}"
        f"\nE046 offsets={offsets}"
        f"\nE046 steps={steps}"
        f"\nE046 predicted_window_bytes={sorted(set(predicted))} "
        f"expected={list(EXPECTED_WINDOW_BYTES)}"
        f"\nE046 widths_ok={widths_ok} matched_steps={matched}/{len(steps)} "
        f"harvested={harvested} duty_ppc={expected_duty_ppc} "
        f"kbps={harvest_kbps} violations={violations} "
        f"rmt_callbacks={rmt_callbacks}"
    )

    # The harness check: the four widths must be distinguishable in the RMT
    # stream, otherwise nothing can be predicted from it.
    assert widths_ok, f"RMT high durations {high_set} != {sorted(EXPECTED_HIGH)}"
    assert steps, "no break spacing to compare"
    assert matched == len(steps), f"{len(steps) - matched} steps unexplained"
