"""Shared hooks for the experiments in this directory.

Rules: README.ja.md
- Results are archived under _runs/<ID>_<UTC>_<profile>/ and never overwritten
  (section 3.4). The archive runs on teardown, so a failing run is kept too.
- build/ holds throwaway build products only.
"""

import datetime
import re
import shutil
from pathlib import Path

import pytest

HERE = Path(__file__).parent
RUNS = HERE / "_runs"
ID_RE = re.compile(r"^(e\d{3})_")


def _experiment_id(test_path: Path) -> str | None:
    match = ID_RE.match(test_path.parent.name)
    return match.group(1).upper() if match else None


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
