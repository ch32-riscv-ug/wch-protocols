"""E026: validate trigger-aware stopping and three pre/post ratios."""

import re

import pexpect

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base
from e024_p4_sump_swar_trigger_80mhz import (
    e024_p4_sump_swar_trigger_80mhz as trigger,
)


BANNER = re.compile(
    rb"# EXP E026 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
WINDOW_SIZE = 512 * 1024
PRE_SAMPLES = (128 * 1024, 256 * 1024, 384 * 1024)
POST_SAMPLES = tuple(WINDOW_SIZE - pre for pre in PRE_SAMPLES)


def test_sump_prepost_stop(dut):
    """Require three valid windows with sub-chunk physical stop error."""
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
    assert int(env[0]) == 1 and int(env[1]) >= base.DESTINATION_SIZE
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
    for run, (pre_samples, post_samples) in enumerate(
        zip(PRE_SAMPLES, POST_SAMPLES, strict=True)
    ):
        case_match = dut.expect(base.CASE, timeout=10)
        values = [value.decode() for value in case_match.groups()]
        assert int(values[0]) == run
        assert all(result == "ESP_OK" for result in values[1:9])
        metrics = [int(value) for value in values[9:]]
        copied = metrics[3]
        assert metrics[0] >= metrics[1] > 1
        assert metrics[2] >= copied
        assert 0 < copied < base.DESTINATION_SIZE
        assert metrics[5] == 0 and metrics[8] < 16
        assert 0 < metrics[6] <= metrics[7] <= 4092
        assert 19000 <= metrics[10] <= 20500
        assert metrics[11] > 0
        assert metrics[12] <= 30000
        expected_edges = copied * 200000 // 20000000
        assert metrics[13] >= expected_edges * 8 // 10
        assert metrics[14] >= metrics[13]

        trigger_match = dut.expect(trigger.TRIGGER, timeout=5)
        fields = [value.decode() for value in trigger_match.groups()]
        trigger_run, mode, found, index, scanned, scan_us, scan_rate = fields
        trigger_index = int(index)
        assert (int(trigger_run), mode, int(found)) == (run, "rising", 1)
        assert int(scanned) == copied
        assert 0 < int(scan_us) and int(scan_rate) >= 25000
        assert trigger_index >= pre_samples
        requested_stop = trigger_index + post_samples
        assert requested_stop <= copied
        overshoot = copied - requested_stop
        assert overshoot < metrics[7]
        window_start = trigger_index - pre_samples
        window_end = trigger_index + post_samples
        assert 0 <= window_start < window_end <= copied
        assert window_end - window_start == WINDOW_SIZE
        observations.append(
            (pre_samples, post_samples, trigger_index, copied, overshoot)
        )

    dut.expect_exact("DONE status=ok", timeout=5)
    print(f"\nE026 prepost_observations={observations}")
