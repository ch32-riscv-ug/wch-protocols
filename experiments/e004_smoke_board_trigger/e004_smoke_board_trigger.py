"""E004 smoke test on real hardware, driven by a host trigger.

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e004_smoke_board_trigger/e004_smoke_board_trigger.py
"""

import re

import pexpect

BANNER = re.compile(
    rb"# EXP E004 v1 core=esp32:esp32 probe=s3_peer_host target=none build=[^\r\n]+"
)
MAX_TRIGGERS = 3


def test_smoke_board_trigger(dut):
    """The board answers a host trigger, so the reply does not depend on attach timing."""
    attempts = 0
    for attempt in range(1, MAX_TRIGGERS + 1):
        attempts = attempt
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == MAX_TRIGGERS:
                raise
    print(f"\nE004: banner received after {attempts} trigger(s)")

    dut.expect_exact("SMOKE A", timeout=5)
    dut.expect_exact("SMOKE B", timeout=5)
    dut.expect_exact("SMOKE C", timeout=5)

    match = dut.expect(re.compile(rb"CLOCK delta=(\d+)"), timeout=5)
    delta_us = int(match.group(1))
    print(f"E004: micros() delta = {delta_us} us")
    assert 900 <= delta_us <= 1500, f"micros() delta not a real clock: {delta_us} us"

    dut.expect_exact("SMOKE done", timeout=5)
