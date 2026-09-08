"""E019: validate one turn of a direct 1 MiB PSRAM PARLIO ring."""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E019 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) alignment_result=(\S+) "
    rb"ext_align=(\d+)\r?\n"
)
BUFFER = re.compile(
    rb"BUFFER ptr=([0-9A-F]+) size=(\d+) aligned=(\d+) external=(\d+) "
    rb"dma_ext=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE run=(\d+) result=(\S+) config=(\S+) receive=(\S+) "
    rb"start=(\S+) notified=(\d+) stop=(\S+) disable=(\S+) sync=(\S+) "
    rb"callbacks=(\d+) bytes=(\d+) extra_bytes=(\d+) capture_us=(\d+) "
    rb"sync_us=(\d+) max_error_ppm=(\d+) min_edges=(\d+) max_edges=(\d+)\r?\n"
)
CAPTURE_SIZE = 1024 * 1024
RUNS = 3
CACHE_ERROR = b"cache: esp_cache_msync"


def test_psram_partial_ring(dut):
    """Record whether the partial ring survives descriptor cache callbacks."""
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
    pointer, size, aligned, external, dma_ext = (
        value.decode() for value in match.groups()
    )
    assert int(pointer, 16) != 0 and int(size) == CAPTURE_SIZE
    assert (int(aligned), int(external), int(dma_ext)) == (1, 1, 1)
    dut.expect_exact("PWM result=ESP_OK", timeout=5)
    dut.expect_exact("LOG tag=cache level=none", timeout=5)

    dut.expect_exact("Guru Meditation Error: Core  1 panic'ed", timeout=10)
    cache_errors = dut.pexpect_proc.before.count(CACHE_ERROR)
    dut.expect_exact("Interrupt wdt timeout on CPU1", timeout=5)
    print(f"\nE019 cache_errors_before_interrupt_wdt={cache_errors}")
    assert cache_errors >= 1
