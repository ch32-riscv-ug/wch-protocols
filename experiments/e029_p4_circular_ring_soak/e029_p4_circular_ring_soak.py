"""E029: short soak of the circular pre-trigger ring."""

import re

import pexpect

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base
from e024_p4_sump_swar_trigger_80mhz import (
    e024_p4_sump_swar_trigger_80mhz as trigger,
)
from e027_p4_sump_circular_pretrigger import (
    e027_p4_sump_circular_pretrigger as ring_base,
)


BANNER = re.compile(
    rb"# EXP E029 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
RUNS = 30


def test_circular_ring_soak(dut):
    """Require 30 valid captures with repeated one/two/four ring wraps."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    match = dut.expect(base.ENV, timeout=5)
    env = [value.decode() for value in match.groups()]
    assert int(env[0]) == 1 and int(env[1]) >= ring_base.RING_SIZE
    assert env[2] == "ESP_OK" and int(env[3]) == 64
    assert env[4] == "ESP_OK" and int(env[5]) == 128
    match = dut.expect(base.BUFFERS, timeout=5)
    buffers = [value.decode() for value in match.groups()]
    assert int(buffers[0], 16) and int(buffers[3], 16)
    assert tuple(map(int, (buffers[1], buffers[2], buffers[4], buffers[5]))) == (
        1,
        1,
        1,
        1,
    )
    dut.expect_exact("CONFIG pwm=ESP_OK", timeout=5)

    rates = []
    overshoots = []
    for run in range(RUNS):
        expected_wraps = 1 << (run % 3)
        case_match = dut.expect(base.CASE, timeout=10)
        values = [value.decode() for value in case_match.groups()]
        assert int(values[0]) == run
        assert all(result == "ESP_OK" for result in values[1:9])
        metrics = [int(value) for value in values[9:]]
        assert metrics[0] == metrics[1] > 1
        assert metrics[2] == metrics[3] > ring_base.RING_SIZE
        assert metrics[4] == 0 and metrics[5] == 0 and metrics[8] < 16
        assert 0 < metrics[6] <= metrics[7] <= 4092
        assert 19000 <= metrics[10] <= 20500
        assert metrics[11] > 0 and metrics[12] <= 30000
        assert metrics[13] >= 10000 and metrics[14] >= metrics[13]

        trigger_match = dut.expect(trigger.TRIGGER, timeout=5)
        fields = [value.decode() for value in trigger_match.groups()]
        assert (int(fields[0]), fields[1], int(fields[2])) == (run, "rising", 1)
        assert int(fields[4]) == metrics[3]
        assert 0 < int(fields[5]) and int(fields[6]) >= 25000

        ring_match = dut.expect(ring_base.RING, timeout=5)
        ring = tuple(map(int, ring_match.groups()))
        (ring_run, wraps, total, trigger_index, requested_stop, overshoot,
         retained_start, window_start, physical_start, max_error,
         min_edges, max_edges) = ring
        assert ring_run == run and wraps == expected_wraps
        assert total == metrics[3] and trigger_index == int(fields[3])
        assert requested_stop == trigger_index + ring_base.HALF_WINDOW
        assert 0 <= overshoot < metrics[7]
        assert retained_start == total - ring_base.RING_SIZE
        assert window_start == trigger_index - ring_base.HALF_WINDOW
        assert retained_start <= window_start
        assert physical_start == window_start % ring_base.RING_SIZE
        assert window_start + 2 * ring_base.HALF_WINDOW <= total
        assert max_error <= 30000
        assert min_edges >= 4000 and max_edges >= min_edges
        rates.append(metrics[10])
        overshoots.append(overshoot)

    dut.expect_exact("DONE status=ok", timeout=5)
    print(
        f"\nE029 captures={RUNS} rate_MBps={min(rates) / 1000:.3f}.."
        f"{max(rates) / 1000:.3f} overshoot={min(overshoots)}.."
        f"{max(overshoots)}"
    )
