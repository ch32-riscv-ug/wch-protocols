"""E030: retain a continuous 16 MiB, 20 MHz capture in PSRAM."""

import re

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base


base.BANNER = re.compile(
    rb"# EXP E030 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
base.DESTINATION_SIZE = 16 * 1024 * 1024
base.RUNS = 1
base.RATE_MIN_MBPS_MILLI = 19_000
base.RATE_MAX_MBPS_MILLI = 21_000
base.CAPTURE_US_MIN = 700_000
base.CAPTURE_US_MAX = 1_200_000
base.MIN_EDGES = 150_000
base.CASE_TIMEOUT = 60
base.REPORT_ID = "E030"


def test_deep_batch_capture(dut):
    """Require one overflow-free, data-valid 16 MiB capture at 20 MHz."""
    base.test_parlio_psram_spool(dut)
