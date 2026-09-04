"""E012 banner: version facts injected at build time.

Plan and report: README.ja.md

Run:  uv run --env-file .env pytest e012_banner_autofill/e012_banner_autofill.py
"""

import os
import re
import subprocess

HERE = os.path.dirname(__file__)


def test_banner_autofill(dut):
    """The banner names the exact commit the firmware was built from."""
    match = dut.expect(
        re.compile(rb"# EXP E012 v1 git=(\S+) stamp=(\S+) probe=host target=none build=[^\r\n]+"),
        timeout=30,
    )
    git_in_banner = match.group(1).decode()
    stamp_in_banner = match.group(2).decode()

    head = subprocess.run(
        ["git", "rev-parse", "--short", "HEAD"],
        cwd=HERE, capture_output=True, text=True, check=True,
    ).stdout.strip()
    print(f"\nE012: banner git={git_in_banner} stamp={stamp_in_banner}  (HEAD={head})")
    assert git_in_banner.startswith(head), f"banner git {git_in_banner} does not name HEAD {head}"
    dut.expect_exact("SMOKE done", timeout=10)
