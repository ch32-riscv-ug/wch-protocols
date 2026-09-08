"""E025: find the basic software-trigger sample-rate boundary."""

import re

import pexpect

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base
from e024_p4_sump_swar_trigger_80mhz import (
    e024_p4_sump_swar_trigger_80mhz as trigger,
)


BANNER = re.compile(
    rb"# EXP E025 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
RATE = re.compile(rb"RATE run=(\d+) sample_rate_hz=(\d+)\r?\n")
RATES = (16_000_000, 20_000_000, 24_000_000, 28_000_000, 32_000_000)


def test_sump_trigger_rate_boundary(dut):
    """Require a valid sweep with a passing low tier and failing high tier."""
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
    passed_by_rate = {rate: [] for rate in RATES}
    for expected_run in range(len(RATES) * 3):
        expected_rate = RATES[expected_run // 3]
        expected_mode = trigger.MODES[expected_run % 3]
        expected_found = trigger.FOUND[expected_run % 3]

        rate_match = dut.expect(RATE, timeout=5)
        assert tuple(map(int, rate_match.groups())) == (expected_run, expected_rate)

        case_match = dut.expect(base.CASE, timeout=10)
        values = [value.decode() for value in case_match.groups()]
        assert int(values[0]) == expected_run
        assert all(result == "ESP_OK" for result in values[2:9])
        metrics = [int(value) for value in values[9:]]
        assert metrics[0] >= metrics[1] > 1
        assert metrics[2] >= metrics[3] == base.DESTINATION_SIZE
        assert 0 <= metrics[4]
        assert 0 < metrics[6] <= metrics[7] <= 4092
        assert metrics[11] > 0

        firmware_ok = values[1] == "ESP_OK"
        if firmware_ok:
            assert metrics[5] == 0 and metrics[8] <= 64
            expected_mbps_milli = expected_rate // 1000
            assert metrics[12] <= 30000
            expected_edges = base.DESTINATION_SIZE * 200000 // expected_rate
            assert metrics[13] >= expected_edges * 8 // 10
            assert metrics[14] >= metrics[13]
        else:
            assert metrics[5] > 0 and metrics[8] == 64
            assert metrics[12:15] == [0, 0, 0]
        expected_mbps_milli = expected_rate // 1000
        sustained = (
            firmware_ok
            and metrics[8] < 16
            and expected_mbps_milli * 95 // 100 <= metrics[10]
            and metrics[10] <= expected_mbps_milli * 102 // 100
        )
        passed_by_rate[expected_rate].append(sustained)

        trigger_match = dut.expect(trigger.TRIGGER, timeout=5)
        fields = [value.decode() for value in trigger_match.groups()]
        run, mode, found, index, scanned, scan_us, scan_rate = fields
        assert (int(run), mode, int(found)) == (
            expected_run,
            expected_mode,
            expected_found,
        )
        assert int(scanned) == base.DESTINATION_SIZE
        assert 0 < int(scan_us) and 0 < int(scan_rate)
        if expected_found:
            assert 0 <= int(index) < base.DESTINATION_SIZE
        else:
            assert int(index) == 0
        observations.append(
            (
                expected_rate,
                mode,
                firmware_ok,
                sustained,
                metrics[5],
                metrics[8],
                int(scan_rate),
            )
        )

    dut.expect_exact("DONE status=ok", timeout=5)
    assert all(passed_by_rate[RATES[0]])
    assert not all(passed_by_rate[RATES[-1]])
    print(f"\nE025 trigger_rate_observations={observations}")
