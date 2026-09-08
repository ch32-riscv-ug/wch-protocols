"""E017: PSRAM cache-sync boundary of finite PARLIO RX transactions."""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E017 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) alignment_result=(\S+) "
    rb"ext_align=(\d+)\r?\n"
)
BUFFER = re.compile(
    rb"BUFFER ptr=([0-9A-F]+) aligned=(\d+) external=(\d+) dma_ext=(\d+)\r?\n"
)
PROFILE = re.compile(
    rb"PROFILE burst=(\d+) size=(\d+) result=(\S+)\r?\n"
)
CASE = re.compile(
    rb"CASE burst=(\d+) size=(\d+) run=(\d+) result=(\S+) "
    rb"receive=(\S+) start=(\S+) wait=(\S+) stop=(\S+) sync=(\S+) "
    rb"max_error_ppm=(\d+) min_edges=(\d+) max_edges=(\d+)\r?\n"
)
BURSTS = (0, 64, 128)
SIZES = (3968, 4096, 7936, 8064, 8192)
RUNS = 3
RATIO_TOLERANCE_PPM = 30000


def test_psram_cache_sync_boundary(dut):
    """Record data validity and cache warnings for every burst/size pair."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    match = dut.expect(ENV, timeout=5)
    psram_found, psram_size, alignment_result, ext_align = (
        value.decode() for value in match.groups()
    )
    assert int(psram_found) == 1 and int(psram_size) > 0
    assert alignment_result == "ESP_OK" and int(ext_align) == 128
    match = dut.expect(BUFFER, timeout=5)
    pointer, aligned, external, dma_ext = (
        value.decode() for value in match.groups()
    )
    assert int(pointer, 16) != 0
    assert (int(aligned), int(external), int(dma_ext)) == (1, 1, 1)
    dut.expect_exact("PWM result=ESP_OK", timeout=5)

    observations = []
    for expected_burst in BURSTS:
        for expected_size in SIZES:
            profile = dut.expect(PROFILE, timeout=5)
            burst, size, profile_result = (
                value.decode() for value in profile.groups()
            )
            assert (int(burst), int(size)) == (expected_burst, expected_size)
            for expected_run in range(RUNS):
                case = dut.expect(CASE, timeout=5)
                warnings = dut.pexpect_proc.before.count(b"cache: esp_cache_msync")
                values = [value.decode() for value in case.groups()]
                burst, size, run = (int(value) for value in values[:3])
                assert (burst, size, run) == (
                    expected_burst,
                    expected_size,
                    expected_run,
                )
                result, receive, start, wait, stop, sync = values[3:9]
                max_error, min_edges, max_edges = (
                    int(value) for value in values[9:]
                )
                data_ok = (
                    profile_result == "ESP_OK"
                    and all(
                        value == "ESP_OK"
                        for value in (result, receive, start, wait, stop, sync)
                    )
                    and max_error <= RATIO_TOLERANCE_PPM
                    and min_edges >= 2
                    and max_edges >= min_edges
                )
                observations.append(
                    (burst, size, run, warnings, max_error, min_edges, max_edges, data_ok)
                )

    dut.expect_exact("DONE status=ok", timeout=5)
    for burst in BURSTS:
        for size in SIZES:
            rows = [row for row in observations if row[0:2] == (burst, size)]
            print(
                f"\nE017 burst={burst} size={size}: "
                f"warnings={[row[3] for row in rows]} "
                f"max_error_ppm={max(row[4] for row in rows)} "
                f"edges={min(row[5] for row in rows)}..{max(row[6] for row in rows)} "
                f"data_ok={all(row[7] for row in rows)}"
            )
    assert len(observations) == len(BURSTS) * len(SIZES) * RUNS
