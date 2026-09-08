"""E016: finite PARLIO RX directly into ESP32-P4 PSRAM.

Plan and report: README.ja.md

Run: uv run --env-file .env pytest \
    e016_p4_parlio_psram_direct/e016_p4_parlio_psram_direct.py
"""

import re

import pexpect
import pytest


BANNER = re.compile(
    rb"# EXP E016 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV idf=(\S+) psram_found=(\d+) psram_size=(\d+) psram_free=(\d+) "
    rb"int_align_result=(\S+) int_align=(\d+) "
    rb"ext_align_result=(\S+) ext_align=(\d+)\r?\n"
)
BUFFER = re.compile(
    rb"BUFFER kind=(\S+) ptr=([0-9A-F]+) size=(\d+) alignment=(\d+) "
    rb"aligned=(\d+) internal=(\d+) external=(\d+) dma=(\d+) dma_ext=(\d+)\r?\n"
)
CAPTURE = re.compile(
    rb"CAPTURE kind=(\S+) run=(\d+) result=(\S+) receive=(\S+) "
    rb"start=(\S+) wait=(\S+) stop=(\S+) sync=(\S+)\r?\n"
)
LANE = re.compile(
    rb"LANE kind=(\S+) run=(\d+) lane=(\d+) duty=(\d+) "
    rb"high=(\d+) low=(\d+) edges=(\d+) ratio_ppm=(\d+)\r?\n"
)
KINDS = ("internal", "psram")
RUNS = 3
LANES = 8
SAMPLES = 8192
RATIO_TOLERANCE_PPM = 30000


def test_parlio_direct_psram(dut):
    """Finite PARLIO RX writes the same PWM samples to internal RAM and PSRAM."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    match = dut.expect(ENV, timeout=5)
    (
        idf,
        psram_found,
        psram_size,
        psram_free,
        int_align_result,
        int_align,
        ext_align_result,
        ext_align,
    ) = (value.decode() for value in match.groups())
    psram_found, psram_size, psram_free, int_align, ext_align = (
        int(value)
        for value in (psram_found, psram_size, psram_free, int_align, ext_align)
    )
    print(
        f"\nE016 env: idf={idf} psram_found={psram_found} "
        f"psram_size={psram_size} psram_free={psram_free} "
        f"alignments={int_align}/{ext_align}"
    )
    if not psram_found or psram_size == 0:
        dut.expect_exact("DONE status=no-psram", timeout=5)
        pytest.skip("connected ESP32-P4 has no initialized PSRAM")

    assert int_align_result == "ESP_OK"
    assert ext_align_result == "ESP_OK"
    assert int_align >= 64 and int_align & (int_align - 1) == 0
    assert ext_align >= 64 and ext_align & (ext_align - 1) == 0
    assert psram_free >= SAMPLES

    buffers = {}
    for expected_kind in KINDS:
        match = dut.expect(BUFFER, timeout=5)
        values = [value.decode() for value in match.groups()]
        kind = values[0]
        assert kind == expected_kind
        pointer = int(values[1], 16)
        size, alignment, aligned, internal, external, dma, dma_ext = (
            int(value) for value in values[2:]
        )
        assert pointer != 0 and size == SAMPLES and aligned == 1
        if kind == "internal":
            assert internal == 1 and external == 0 and dma == 1
        else:
            assert internal == 0 and external == 1 and dma_ext == 1
        buffers[kind] = (pointer, alignment, dma, dma_ext)
    dut.expect_exact("CONFIG pwm=ESP_OK parlio=ESP_OK", timeout=5)

    observations = []
    cache_warnings = {kind: 0 for kind in KINDS}
    for expected_kind in KINDS:
        for expected_run in range(RUNS):
            match = dut.expect(CAPTURE, timeout=5)
            cache_warnings[expected_kind] += dut.pexpect_proc.before.count(
                b"cache: esp_cache_msync"
            )
            values = [value.decode() for value in match.groups()]
            assert values[0] == expected_kind
            assert int(values[1]) == expected_run
            assert all(value == "ESP_OK" for value in values[2:])
            for expected_lane in range(LANES):
                match = dut.expect(LANE, timeout=5)
                values = [value.decode() for value in match.groups()]
                kind = values[0]
                run, lane, duty, high, low, edges, ratio_ppm = (
                    int(value) for value in values[1:]
                )
                assert kind == expected_kind
                assert run == expected_run and lane == expected_lane
                expected_ratio = duty * 1_000_000 // 256
                error = abs(ratio_ppm - expected_ratio)
                assert high + low == SAMPLES
                assert high > 0 and low > 0 and edges >= 2
                assert error <= RATIO_TOLERANCE_PPM
                observations.append(
                    (kind, run, lane, ratio_ppm, expected_ratio, error, edges)
                )

    dut.expect_exact("DONE status=ok", timeout=5)
    for kind in KINDS:
        kind_observations = [row for row in observations if row[0] == kind]
        print(
            f"E016 {kind}: buffer={buffers[kind]} "
            f"max_ratio_error_ppm={max(row[5] for row in kind_observations)} "
            f"edge_range={min(row[6] for row in kind_observations)}.."
            f"{max(row[6] for row in kind_observations)} "
            f"driver_cache_warnings={cache_warnings[kind]}"
        )
