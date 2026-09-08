"""E028: validate a four-stage SUMP-style software trigger."""

import re

import pexpect

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base


BANNER = re.compile(
    rb"# EXP E028 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
STAGED = re.compile(
    rb"STAGED run=(\d+) found=(\d+) stage=(\d+) index0=(\d+) "
    rb"index1=(\d+) index2=(\d+) index3=(\d+) rising_count=(\d+) "
    rb"scanned=(\d+) scan_us=(\d+) scan_mbps_milli=(\d+)\r?\n"
)
ARM_AFTER = 256 * 1024
POST_SAMPLES = 256 * 1024


def test_sump_four_stage_trigger(dut):
    """Require three valid four-stage triggers and post-trigger stops."""
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
    for run in range(3):
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
        assert 15000 <= metrics[10] <= 16500
        assert metrics[11] > 0 and metrics[12] <= 30000
        expected_edges = copied * 200000 // 16000000
        assert metrics[13] >= expected_edges * 8 // 10
        assert metrics[14] >= metrics[13]

        staged_match = dut.expect(STAGED, timeout=5)
        fields = tuple(map(int, staged_match.groups()))
        (staged_run, found, stage, index0, index1, index2, index3,
         rising_count, scanned, scan_us, scan_rate) = fields
        assert (staged_run, found, stage, rising_count) == (run, 1, 4, 4)
        assert ARM_AFTER <= index0 < index1 < index2 < index3
        assert scanned == copied
        assert 0 < scan_us and scan_rate >= 16000
        requested_stop = index3 + POST_SAMPLES
        assert requested_stop <= copied
        assert copied - requested_stop < metrics[7]
        observations.append(fields + (copied, copied - requested_stop))

    dut.expect_exact("DONE status=ok", timeout=5)
    print(f"\nE028 four_stage_observations={observations}")
