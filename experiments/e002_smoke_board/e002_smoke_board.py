"""E002 smoke test on real hardware (standing bench v1).

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e002_smoke_board/e002_smoke_board.py
"""

import re


def test_smoke_board(dut):
    """Upload lands, the banner arrives as one line, and the clock is real."""
    dut.expect(
        re.compile(
            rb"# EXP E002 v1 core=esp32:esp32 probe=s3_peer_host target=none "
            rb"build=[^\r\n]+"
        ),
        timeout=30,
    )
    dut.expect_exact("SMOKE A", timeout=10)
    dut.expect_exact("SMOKE B", timeout=10)
    dut.expect_exact("SMOKE C", timeout=10)

    match = dut.expect(re.compile(rb"CLOCK delta=(\d+)"), timeout=10)
    delta_us = int(match.group(1))
    assert 900 <= delta_us <= 1500, f"micros() delta not a real clock: {delta_us} us"

    dut.expect_exact("SMOKE done", timeout=10)
    dut.expect(re.compile(rb"HEARTBEAT \d+"), timeout=10)
