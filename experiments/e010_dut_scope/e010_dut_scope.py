"""E010 harness: can one experiment file hold more than one test function?

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e010_dut_scope/e010_dut_scope.py
"""

import re

import pexpect

BANNER = re.compile(
    rb"# EXP E010 v1 core=esp32:esp32 probe=s3_peer_host target=none build=[^\r\n]+"
)
MAX_TRIGGERS = 3


def _ask(dut):
    for attempt in range(1, MAX_TRIGGERS + 1):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            return attempt
        except pexpect.exceptions.TIMEOUT:
            if attempt == MAX_TRIGGERS:
                raise
    return MAX_TRIGGERS


def test_first_function(dut):
    """The first function gets a DUT, as every experiment so far has."""
    print(f"\nE010: first function answered after {_ask(dut)} trigger(s)")
    dut.expect_exact("SMOKE done", timeout=5)


def test_second_function(dut):
    """The second function needs its own DUT; this is what E001 saw fail."""
    print(f"E010: second function answered after {_ask(dut)} trigger(s)")
    dut.expect_exact("SMOKE done", timeout=5)
