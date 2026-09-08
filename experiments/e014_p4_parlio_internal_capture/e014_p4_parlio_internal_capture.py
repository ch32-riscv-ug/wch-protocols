"""E014: ESP32-P4 PARLIO capture of eight internal LEDC signals.

Plan and report: README.ja.md

Run: uv run --env-file .env pytest \
    e014_p4_parlio_internal_capture/e014_p4_parlio_internal_capture.py
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E014 v1 git=\S+ probe=esp32p4_parlio target=internal build=[^\r\n]+"
)
LANE = re.compile(
    rb"LANE run=(\d+) lane=(\d+) pin=(\d+) duty=(\d+) "
    rb"high=(\d+) low=(\d+) edges=(\d+) ratio_ppm=(\d+)"
)
RUNS = 3
LANES = 8
SAMPLES = 8192
RATIO_TOLERANCE_PPM = 30000


def test_internal_parallel_capture(dut):
    """LEDC output and PARLIO input coexist on all eight GPIOs."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    dut.expect_exact("INIT result=ESP_OK", timeout=5)
    dut.expect(
        re.compile(
            rb"CONFIG pwm_hz=100000 sample_hz=8000000 "
            rb"samples=8192 pins=[^\r\n]+"
        ),
        timeout=5,
    )

    observations = []
    for expected_run in range(RUNS):
        dut.expect_exact(f"CAPTURE run={expected_run} result=ESP_OK", timeout=5)
        for expected_lane in range(LANES):
            match = dut.expect(LANE, timeout=5)
            run, lane, pin, duty, high, low, edges, ratio_ppm = (
                int(value) for value in match.groups()
            )
            assert run == expected_run
            assert lane == expected_lane
            assert high + low == SAMPLES
            assert high > 0 and low > 0
            assert edges >= 2
            expected_ratio_ppm = duty * 1_000_000 // 256
            error_ppm = abs(ratio_ppm - expected_ratio_ppm)
            assert error_ppm <= RATIO_TOLERANCE_PPM, (
                f"lane {lane} pin {pin}: observed {ratio_ppm} ppm, "
                f"expected {expected_ratio_ppm} ppm"
            )
            observations.append((run, lane, pin, duty, ratio_ppm, edges))

    dut.expect_exact("DONE result=PASS", timeout=5)
    print("\nE014 observations:")
    for observation in observations:
        print(
            "run=%d lane=%d pin=%d duty=%d ratio_ppm=%d edges=%d"
            % observation
        )
