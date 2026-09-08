"""E020: sustained internal RAM and PSRAM memcpy bandwidth."""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E020 v1 git=\S+ probe=esp32p4 target=memory "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) psram_free=(\d+) "
    rb"alignment_result=(\S+) ext_align=(\d+)\r?\n"
)
BUFFERS = re.compile(
    rb"BUFFERS internal=([0-9A-F]+) psram=([0-9A-F]+) "
    rb"psram_aligned=(\d+) psram_external=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE chunk=(\d+) run=(\d+) flush=(\S+) invalidate=(\S+) "
    rb"write_copy_us=(\d+) flush_us=(\d+) write_copy_mbps_milli=(\d+) "
    rb"write_total_mbps_milli=(\d+) read_us=(\d+) read_mbps_milli=(\d+) "
    rb"write_mismatch_chunks=(\d+) read_mismatch_chunks=(\d+) sink=(\d+)\r?\n"
)
CHUNKS = (4096, 16384, 65536)
RUNS = 3


def test_psram_copy_bandwidth(dut):
    """Measure copy bandwidth and require byte-for-byte chunk equality."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    match = dut.expect(ENV, timeout=5)
    psram_found, psram_size, psram_free, alignment_result, ext_align = (
        value.decode() for value in match.groups()
    )
    assert int(psram_found) == 1 and int(psram_size) >= 8 * 1024 * 1024
    assert int(psram_free) >= 8 * 1024 * 1024
    assert alignment_result == "ESP_OK" and int(ext_align) == 128
    match = dut.expect(BUFFERS, timeout=5)
    internal, psram, aligned, external = (
        value.decode() for value in match.groups()
    )
    assert int(internal, 16) != 0 and int(psram, 16) != 0
    assert (int(aligned), int(external)) == (1, 1)

    observations = []
    for expected_chunk in CHUNKS:
        for expected_run in range(RUNS):
            match = dut.expect(CASE, timeout=20)
            values = [value.decode() for value in match.groups()]
            chunk, run = (int(value) for value in values[:2])
            flush_result, invalidate_result = values[2:4]
            metrics = [int(value) for value in values[4:]]
            assert (chunk, run) == (expected_chunk, expected_run)
            assert flush_result == invalidate_result == "ESP_OK"
            assert all(value > 0 for value in metrics[:6])
            assert metrics[6:8] == [0, 0]
            observations.append((chunk, *metrics[:6]))

    dut.expect_exact("DONE status=ok", timeout=5)
    for chunk in CHUNKS:
        rows = [row for row in observations if row[0] == chunk]
        print(
            f"\nE020 chunk={chunk}: "
            f"write_copy_MBps={[row[3] / 1000 for row in rows]} "
            f"write_total_MBps={[row[4] / 1000 for row in rows]} "
            f"read_MBps={[row[6] / 1000 for row in rows]} "
            f"flush_us={[row[2] for row in rows]}"
        )
