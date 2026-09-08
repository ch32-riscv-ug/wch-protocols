"""E024: validate 32-bit software trigger searches during 80 MHz capture."""

import re

import pexpect

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base


BANNER = re.compile(
    rb"# EXP E024 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
TRIGGER = re.compile(
    rb"TRIGGER run=(\d+) mode=(\S+) found=(\d+) index=(\d+) "
    rb"scanned=(\d+) scan_us=(\d+) scan_mbps_milli=(\d+)\r?\n"
)
MODES = ("nomatch", "rising", "pattern")
FOUND = (0, 1, 1)


def test_sump_swar_trigger_80mhz(dut):
    """Require correct word-wise triggers without dropping 80 MHz data."""
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
    for expected_run, expected_mode, expected_found in zip(
        range(3), MODES, FOUND, strict=True
    ):
        match = dut.expect(base.CASE, timeout=10)
        values = [value.decode() for value in match.groups()]
        assert int(values[0]) == expected_run
        assert values[1] == "ESP_FAIL"
        assert all(result == "ESP_OK" for result in values[2:9])
        metrics = [int(value) for value in values[9:]]
        assert metrics[0] > metrics[1] > 1
        assert metrics[2] > metrics[3] == base.DESTINATION_SIZE
        assert metrics[4] > 0
        assert metrics[5] > 0
        assert 0 < metrics[6] <= metrics[7] <= 4092
        assert metrics[8] == 64
        assert metrics[9] < 75000
        assert metrics[10] < 75000
        assert metrics[11] > 0
        assert metrics[12:15] == [0, 0, 0]

        trigger = dut.expect(TRIGGER, timeout=5)
        trigger_values = [value.decode() for value in trigger.groups()]
        run, mode, found, index, scanned, scan_us, scan_rate = trigger_values
        assert (int(run), mode, int(found)) == (
            expected_run,
            expected_mode,
            expected_found,
        )
        assert int(scanned) == base.DESTINATION_SIZE
        assert 0 < int(scan_us)
        assert 0 < int(scan_rate) < 80000
        if expected_found:
            assert 0 <= int(index) < base.DESTINATION_SIZE
        else:
            assert int(index) == 0
        observations.append(
            (mode, int(found), int(index), int(scan_us), int(scan_rate), metrics[8])
        )

    dut.expect_exact("DONE status=ok", timeout=5)
    print(f"\nE024 trigger_observations={observations}")
