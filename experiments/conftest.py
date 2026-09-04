"""Shared hooks for the experiments in this directory.

Rules: README.ja.md
- Results are archived under _runs/<ID>_<UTC>_<profile>/ and never overwritten
  (section 3.4). The archive runs on teardown, so a failing run is kept too.
- build/ holds throwaway build products only.
"""

import datetime
import os
import re
import shutil
import subprocess
from pathlib import Path

import pytest

HERE = Path(__file__).parent
RUNS = HERE / "_runs"
ID_RE = re.compile(r"^(e\d{3})_")


def _experiment_id(test_path: Path) -> str | None:
    match = ID_RE.match(test_path.parent.name)
    return match.group(1).upper() if match else None


def _git_describe() -> str:
    """Short commit plus a dirty marker, so a log names the exact firmware."""
    try:
        head = subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=HERE, capture_output=True, text=True, timeout=5, check=True,
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "status", "--porcelain"],
            cwd=HERE, capture_output=True, text=True, timeout=5, check=True,
        ).stdout.strip()
    except (subprocess.SubprocessError, OSError):
        return "unknown"
    return head + ("+dirty" if dirty else "")


def pytest_configure(config):
    """Publish build-time facts so build_config.toml can inject them as defines.

    Only values that change rarely go in: a value that changes every run would
    force a full rebuild each time (see e012_banner_autofill).
    """
    os.environ.setdefault("TEST_BANNER_GIT", _git_describe())
    if "E012_STAMP_EVERY_RUN" in os.environ:
        os.environ["TEST_BANNER_STAMP"] = datetime.datetime.now(
            datetime.timezone.utc
        ).strftime("%Y%m%dT%H%M%SZ")
    else:
        os.environ.setdefault("TEST_BANNER_STAMP", "fixed")


@pytest.fixture(autouse=True)
def archive_run(request, test_case_tempdir):
    """Copy this test's logs into _runs/ once it finishes, pass or fail."""
    yield
    exp_id = _experiment_id(Path(request.node.fspath))
    if exp_id is None:
        return
    source = Path(test_case_tempdir)
    if not source.is_dir() or not any(source.iterdir()):
        return
    profile = request.config.getoption("profile", None) or "default"
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    target = RUNS / f"{exp_id}_{stamp}_{profile}" / request.node.name
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, target, dirs_exist_ok=True)
