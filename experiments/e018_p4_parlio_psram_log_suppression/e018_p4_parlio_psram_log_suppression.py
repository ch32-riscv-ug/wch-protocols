"""E018: suppress cache errors and validate a 1 MiB PSRAM capture."""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E018 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) alignment_result=(\S+) "
    rb"ext_align=(\d+)\r?\n"
)
BUFFER = re.compile(
    rb"BUFFER ptr=([0-9A-F]+) aligned=(\d+) external=(\d+) dma_ext=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE kind=(\S+) run=(\d+) size=(\d+) result=(\S+) "
    rb"receive=(\S+) start=(\S+) wait=(\S+) stop=(\S+) sync=(\S+) "
    rb"capture_us=(\d+) sync_us=(\d+) max_error_ppm=(\d+) "
    rb"min_edges=(\d+) max_edges=(\d+)\r?\n"
)
CACHE_ERROR = b"cache: esp_cache_msync"
CAPTURE_SIZE = 1024 * 1024
RUNS = 3


def _case(dut, expected_kind, expected_run, expected_size, timeout):
    match = dut.expect(CASE, timeout=timeout)
    warnings = dut.pexpect_proc.before.count(CACHE_ERROR)
    values = [value.decode() for value in match.groups()]
    kind = values[0]
    run, size = (int(value) for value in values[1:3])
    assert (kind, run, size) == (expected_kind, expected_run, expected_size)
    results = values[3:9]
    capture_us, sync_us, max_error, min_edges, max_edges = (
        int(value) for value in values[9:]
    )
    assert all(result == "ESP_OK" for result in results)
    assert capture_us > 0 and sync_us >= 0
    assert max_error <= 30000
    assert min_edges >= 2 and max_edges >= min_edges
    return warnings, capture_us, sync_us, max_error, min_edges, max_edges


def test_psram_cache_log_suppression(dut):
    """Record whether a 1 MiB soft-delimited transaction is accepted."""
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
    assert int(psram_found) == 1 and int(psram_size) >= CAPTURE_SIZE
    assert alignment_result == "ESP_OK" and int(ext_align) == 128
    match = dut.expect(BUFFER, timeout=5)
    pointer, aligned, external, dma_ext = (
        value.decode() for value in match.groups()
    )
    assert int(pointer, 16) != 0
    assert (int(aligned), int(external), int(dma_ext)) == (1, 1, 1)
    dut.expect_exact("CONFIG pwm=ESP_OK receiver=ESP_OK", timeout=5)
    dut.expect_exact(
        "DELIMITERS control=ESP_OK capture=ESP_ERR_INVALID_ARG", timeout=5
    )
    dut.expect_exact("DONE status=ok", timeout=5)
    print("\nE018 1 MiB soft delimiter rejected with ESP_ERR_INVALID_ARG")
