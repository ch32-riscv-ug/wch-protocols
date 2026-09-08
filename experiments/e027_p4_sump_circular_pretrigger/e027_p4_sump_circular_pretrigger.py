"""E027: validate pre-trigger history across repeated PSRAM ring wraps."""

import re

import pexpect

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base
from e024_p4_sump_swar_trigger_80mhz import (
    e024_p4_sump_swar_trigger_80mhz as trigger,
)


BANNER = re.compile(
    rb"# EXP E027 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
RING = re.compile(
    rb"RING run=(\d+) wraps=(\d+) total=(\d+) trigger=(\d+) "
    rb"requested_stop=(\d+) overshoot=(\d+) retained_start=(\d+) "
    rb"window_start=(\d+) physical_start=(\d+) max_error_ppm=(\d+) "
    rb"min_edges=(\d+) max_edges=(\d+)\r?\n"
)
RING_SIZE = 1024 * 1024
HALF_WINDOW = 256 * 1024


def test_sump_circular_pretrigger(dut):
    """Require valid 50/50 windows after one, two, and four ring wraps."""
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
    assert int(env[0]) == 1 and int(env[1]) >= RING_SIZE
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

    observations = []
    for run, expected_wraps in enumerate((1, 2, 4)):
        case_match = dut.expect(base.CASE, timeout=10)
        values = [value.decode() for value in case_match.groups()]
        assert int(values[0]) == run
        assert all(result == "ESP_OK" for result in values[1:9])
        metrics = [int(value) for value in values[9:]]
        assert metrics[0] >= metrics[1] > 1
        assert metrics[2] >= metrics[3] > RING_SIZE
        assert metrics[5] == 0 and metrics[8] < 16
        assert 0 < metrics[6] <= metrics[7] <= 4092
        assert 19000 <= metrics[10] <= 20500
        assert metrics[11] > 0

        trigger_match = dut.expect(trigger.TRIGGER, timeout=5)
        fields = [value.decode() for value in trigger_match.groups()]
        assert (int(fields[0]), fields[1], int(fields[2])) == (run, "rising", 1)
        assert int(fields[4]) == metrics[3]
        assert 0 < int(fields[5]) and int(fields[6]) >= 25000

        ring_match = dut.expect(RING, timeout=5)
        ring = tuple(map(int, ring_match.groups()))
        (ring_run, wraps, total, trigger_index, requested_stop, overshoot,
         retained_start, window_start, physical_start, max_error,
         min_edges, max_edges) = ring
        assert ring_run == run and wraps == expected_wraps
        assert total == metrics[3]
        assert trigger_index == int(fields[3])
        assert requested_stop == trigger_index + HALF_WINDOW
        assert 0 <= overshoot < metrics[7]
        assert retained_start == total - RING_SIZE
        assert window_start == trigger_index - HALF_WINDOW
        assert retained_start <= window_start
        assert physical_start == window_start % RING_SIZE
        assert window_start + 2 * HALF_WINDOW <= total
        assert max_error <= 30000
        assert min_edges >= 4000 and max_edges >= min_edges
        observations.append(ring)

    dut.expect_exact("DONE status=ok", timeout=5)
    print(f"\nE027 circular_ring_observations={observations}")
