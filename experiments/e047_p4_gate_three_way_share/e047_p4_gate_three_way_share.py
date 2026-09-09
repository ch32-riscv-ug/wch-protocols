"""E047: share one GPIO between a data line, the valid line and RMT RX.

E041 shared a pin between data and valid; E045 shared the gate with RMT. If all
three work at once, then 8 channels plus hardware qualification plus hardware
window timestamps fit in 8 pins with no extra channel and no CPU. At
data_width 8 a window's byte length equals its RMT tick count directly.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E047 v1 git=\S+ probe=esp32p4_parlio target=internal "
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
    rb"violations=(\d+) shared_low=(\d+) harvest_us=(-?\d+) "
    rb"harvest_kbps=(\d+)\r?\n"
)
SYMBOLS = re.compile(rb"SYMBOLS((?: \d+:\d+){16})\r?\n")
VIOLS = re.compile(rb"VIOLS((?: \d+){16})\r?\n")

WIDTHS = (300, 700, 1500, 2044)
SOURCE_WORDS = 16384
SOURCE_DIVIDER = 4
DATA_WIDTH = 8
# One sample per byte and one RMT tick per sample, so these are the same list.
EXPECTED_HIGH = tuple(w * SOURCE_DIVIDER for w in WIDTHS)


def test_gate_three_way_share(dut):
    """Record whether one pin can serve data, valid and RMT at once."""
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
    gate_total, expected_loop_bytes, expected_duty_ppc = map(int, values[1:4])
    assert gate_total == sum(WIDTHS)
    assert expected_loop_bytes == gate_total * SOURCE_DIVIDER
    api = values[4:16]
    (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
     low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
     violations, shared_low, harvest_us, harvest_kbps) = map(
        int, values[16:32]
    )

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
    steps = [b - a for a, b in zip(offsets, offsets[1:]) if b > a]
    matched = sum(1 for s in steps if s in set(highs))

    print(
        f"\nE047 shared_low={shared_low} violations={violations}"
        f"\nE047 highs={highs[:8]} expected={list(EXPECTED_HIGH)}"
        f"\nE047 offsets={offsets}"
        f"\nE047 steps={steps}"
        f"\nE047 matched_steps={matched}/{len(steps)} harvested={harvested} "
        f"duty_ppc={expected_duty_ppc} kbps={harvest_kbps} "
        f"rmt_callbacks={rmt_callbacks}"
    )

    # The shared pin must read back as data on every single sample, or it is
    # not really serving as a channel.
    assert shared_low == 0, f"{shared_low} samples had the shared bit low"
    assert sorted(set(highs)) == sorted(EXPECTED_HIGH), sorted(set(highs))
    assert steps, "no break spacing to compare"
    assert matched == len(steps), f"{len(steps) - matched} steps unexplained"
