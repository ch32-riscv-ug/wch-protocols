"""E045: which creation order and harvest window make RMT return gate durations?

E044 shared one GPIO between PARLIO's valid input and RMT RX -- every API
returned ESP_OK and the gated capture stayed intact, but RMT delivered no
symbols. Two candidates remain: PARLIO TX claiming the pin as an output after
RMT armed, and a harvest window shorter than the ping-pong threshold.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E045 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rmt_rx_candidates=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) sample_rate_hz=(\d+) source_words=(\d+) "
    rb"rmt_resolution_hz=(\d+) rmt_symbols=(\d+) gate_words=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE rmt_after_source=(\d+) harvest_target_us=(-?\d+) gate_words=(\d+) "
    rb"expected_window=(\d+) expected_high=(\d+) expected_low=(\d+) "
    rb"config=(\S+) delimiter=(\S+) rmt=(\S+) rmt_enable=(\S+) source=(\S+) "
    rb"source_start=(\S+) rmt_receive=(\S+) enable=(\S+) receive=(\S+) "
    rb"disable=(\S+) rmt_disable=(\S+) sync=(\S+) rmt_callbacks=(\d+) "
    rb"rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) high_max=(\d+) "
    rb"low_count=(\d+) low_min=(\d+) low_max=(\d+) parlio_callbacks=(\d+) "
    rb"dequeues=(\d+) harvested=(\d+) overflow=(\d+) violations=(\d+) "
    rb"aligned_violations=(\d+) harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
CASES = ((0, 50000), (0, 300000), (1, 50000), (1, 300000))
GATE_WORDS = 2044
SOURCE_WORDS = 8192
SOURCE_DIVIDER = 4
REPORTED_SYMBOLS = 16


def _expect_symbols(dut, rmt_after_source, harvest_target_us):
    pattern = re.compile(
        (rf"SYMBOLS rmt_after_source={rmt_after_source} "
         rf"harvest_target_us={harvest_target_us}"
         + r"((?: \d+:\d+){%d})\r?\n" % REPORTED_SYMBOLS).encode()
    )
    raw = dut.expect(pattern, timeout=30).group(1).split()
    return [tuple(int(v) for v in item.split(b":")) for item in raw]


def test_gate_rmt_order(dut):
    """Record which of the four conditions yields RMT symbols."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[1] >= 1 and env[2] == 4
    assert env[4] == 20_000_000 and env[5] == SOURCE_WORDS
    assert env[6] == 20_000_000 and env[8] == GATE_WORDS
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=5,
    )

    observations = []
    for rmt_after_source, harvest_target_us in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        assert int(values[0]) == rmt_after_source
        assert int(values[1]) == harvest_target_us
        assert int(values[2]) == GATE_WORDS
        expected_window, expected_high, expected_low = map(int, values[3:6])
        assert expected_high == GATE_WORDS * SOURCE_DIVIDER
        assert expected_low == (SOURCE_WORDS - GATE_WORDS) * SOURCE_DIVIDER
        api = values[6:18]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, aligned, harvest_us, harvest_kbps) = map(
            int, values[18:34]
        )
        symbols = _expect_symbols(dut, rmt_after_source, harvest_target_us)

        assert harvest_us > 0
        assert all(v == "ESP_OK" for v in api[:11]), api
        assert aligned <= violations

        rmt_exact = (
            rmt_stored > 0
            and high_count > 0
            and high_min == high_max == expected_high
            and low_count > 0
            and low_min == low_max == expected_low
        )
        # The PARLIO side must survive whichever order is used.
        parlio_ok = harvested > 0 and violations > 0 and aligned == violations
        observations.append(
            (rmt_after_source, harvest_target_us, rmt_stored > 0, rmt_exact,
             parlio_ok, rmt_callbacks, rmt_stored, high_count, high_min,
             high_max, expected_high, low_count, low_min, low_max,
             expected_low, harvested, violations, aligned, harvest_kbps,
             symbols[:6])
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    # Both outcomes are results, so record rather than assert. The invariant
    # is that the gated capture keeps working in every order.
    for row in observations:
        assert row[4], f"PARLIO gating broke in this order: {row[:5]}"
        print(
            f"\nE045 rmt_after_source={row[0]} harvest={row[1]}us "
            f"symbols={row[2]} exact={row[3]} callbacks={row[5]} "
            f"stored={row[6]} high={row[8]}..{row[9]} (expect {row[10]}) "
            f"low={row[12]}..{row[13]} (expect {row[14]}) first={row[19]}"
        )
    print(f"\nE045 order_observations={observations}")
