"""E001 harness smoke test (no hardware).

Plan and report: README.ja.md

The file name has no `test_` prefix, so a bare `pytest` run does not collect it.
Run it by naming the file explicitly:

    uv run --env-file .env pytest e001_smoke_host/e001_smoke_host.py

The DUT is created per test function, so keep one experiment in one function.
"""

import re


def test_smoke_host(dut):
    """The DUT starts, the banner arrives as one line, known lines follow in order."""
    dut.expect(
        re.compile(
            rb"# EXP E001 v1 core=lang-ship:host probe=host target=none "
            rb"build=[^\r\n]+"
        ),
        timeout=30,
    )
    dut.expect_exact("SMOKE A", timeout=10)
    dut.expect_exact("SMOKE B", timeout=10)
    dut.expect_exact("SMOKE C", timeout=10)
    dut.expect_exact("SMOKE done", timeout=10)
