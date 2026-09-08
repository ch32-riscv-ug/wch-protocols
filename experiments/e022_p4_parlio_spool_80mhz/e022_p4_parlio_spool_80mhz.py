"""E022: run the E021 spool path at 80 MHz."""

import re

from e021_p4_parlio_psram_spool import e021_p4_parlio_psram_spool as base


base.BANNER = re.compile(
    rb"# EXP E022 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
base.RATE_MIN_MBPS_MILLI = 75000
base.RATE_MAX_MBPS_MILLI = 85000
base.CAPTURE_US_MIN = 5000
base.CAPTURE_US_MAX = 50000
base.MIN_EDGES = 2000


def test_parlio_psram_spool_80mhz(dut):
    """Require the reusable spool path to sustain 80 MHz three times."""
    base.test_parlio_psram_spool(dut)
