"""E032: coarsely locate the raw batch rate boundary for each width."""

import re

import pexpect

from e031_p4_parlio_channel_width import e031_p4_parlio_channel_width as base


BANNER = re.compile(
    rb"# EXP E032 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
WIDTHS = (1, 2, 4, 8, 16)
RATES = (20_000_000, 40_000_000, 80_000_000, 120_000_000, 160_000_000)


def test_parlio_width_rate_coarse(dut):
    """Record all conditions and require every width to have a valid baseline."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(base.ENV, timeout=5).groups()))
    assert env[0] == 1 and env[1] >= 2 * 1024 * 1024
    assert env[2:] == (1, 1, 16)
    dut.expect_exact("BUFFERS ring_ok=1 destination_ok=1 queue_ok=1", timeout=5)

    observations = []
    passed = {width: [] for width in WIDTHS}
    for width in WIDTHS:
        for rate in RATES:
            values = [
                value.decode() for value in dut.expect(base.CASE, timeout=30).groups()
            ]
            assert (int(values[0]), int(values[1])) == (width, rate)
            result = values[2]
            api_results = values[3:11]
            metrics = list(map(int, values[11:]))
            (target_bytes, copied, callbacks, dequeues, extra_bytes, overflows,
             max_queue, capture_us, sample_rate_khz, byte_rate_kbps,
             max_error_ppm, min_edges, max_edges, duplicate_mismatches) = metrics
            assert target_bytes == copied == base.SAMPLE_COUNT * width // 8
            assert callbacks >= dequeues > 1
            assert extra_bytes >= 0
            assert 0 < capture_us < 1_000_000
            expected_khz = rate // 1000
            sustained = (
                result == "ESP_OK"
                and all(value == "ESP_OK" for value in api_results)
                and overflows == 0
                and max_queue < 16
                and expected_khz * 95 // 100 <= sample_rate_khz
                and sample_rate_khz <= expected_khz * 105 // 100
                and max_error_ppm <= 30_000
                and min_edges >= 1_000
                and max_edges >= min_edges
                and duplicate_mismatches == 0
            )
            passed[width].append(sustained)
            if sustained:
                assert extra_bytes < 65536
            observations.append(
                (width, rate, sustained, result, overflows, max_queue,
                 sample_rate_khz, byte_rate_kbps, max_error_ppm)
            )

    dut.expect_exact("DONE status=ok", timeout=5)
    assert all(results[0] for results in passed.values())
    for results in passed.values():
        if False in results:
            first_failure = results.index(False)
            assert not any(results[first_failure + 1:])
    print(f"\nE032 rate_observations={observations}")
