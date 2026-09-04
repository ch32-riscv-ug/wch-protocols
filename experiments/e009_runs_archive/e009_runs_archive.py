"""E009 archive: check that _runs/ is populated automatically.

Plan and report: README.ja.md

Two entry points share one sketch:
  uv run --env-file .env pytest e009_runs_archive/e009_runs_archive.py -k pass_case
  E009_FORCE_FAIL=1 uv run --env-file .env pytest e009_runs_archive/e009_runs_archive.py -k fail_case
"""

import os


def test_pass_case(dut):
    """A run that succeeds must leave its log in _runs/."""
    if os.environ.get("E009_FORCE_FAIL"):
        import pytest

        pytest.skip("running the failing case instead")
    dut.expect_exact("SMOKE done", timeout=30)


def test_fail_case(dut):
    """A run that fails must leave its log in _runs/ as well."""
    if not os.environ.get("E009_FORCE_FAIL"):
        import pytest

        pytest.skip("set E009_FORCE_FAIL=1 to exercise the failing path")
    dut.expect_exact("THIS LINE IS NEVER PRINTED", timeout=5)
