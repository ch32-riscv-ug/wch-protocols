"""E033: refine the raw batch boundary for 8 and 16 channels."""

import re
import pexpect

from e031_p4_parlio_channel_width import e031_p4_parlio_channel_width as capture

BANNER = re.compile(rb"# EXP E033 v1 git=\S+ probe=esp32p4_parlio target=internal build=[^\r\n]+\r?\n")
CASES = tuple((8, rate) for rate in range(84_000_000, 120_000_001, 4_000_000)) + tuple((16, rate) for rate in range(44_000_000, 80_000_001, 4_000_000))


def test_parlio_width_rate_fine(dut):
    dut.write("?")
    dut.expect(BANNER, timeout=45)
    env = tuple(map(int, dut.expect(capture.ENV, timeout=5).groups()))
    assert env[0] == 1 and env[2:] == (1, 1, 16)
    dut.expect_exact("BUFFERS ring_ok=1 destination_ok=1 queue_ok=1", timeout=5)

    passed = {8: [], 16: []}
    observations = []
    for width, rate in CASES:
        values = [v.decode() for v in dut.expect(capture.CASE, timeout=30).groups()]
        assert (int(values[0]), int(values[1])) == (width, rate)
        result, api = values[2], values[3:11]
        m = list(map(int, values[11:]))
        target, copied, callbacks, dequeues, extra, overflow, queue, usec, sample_khz, byte_kbps, error, min_edges, max_edges, duplicates = m
        assert target == copied == capture.SAMPLE_COUNT * width // 8
        assert callbacks >= dequeues > 1 and extra >= 0 and usec > 0
        expected = rate // 1000
        ok = (result == "ESP_OK" and all(x == "ESP_OK" for x in api) and overflow == 0 and queue < 16 and expected * 95 // 100 <= sample_khz <= expected * 105 // 100 and error <= 30000 and min_edges >= 1000 and max_edges >= min_edges and duplicates == 0)
        if ok:
            assert extra < 65536
        passed[width].append(ok)
        observations.append((width, rate, ok, overflow, queue, sample_khz, byte_kbps))
    dut.expect_exact("DONE status=ok", timeout=5)
    for results in passed.values():
        assert results[0] and not results[-1]
        first_failure = results.index(False)
        assert not any(results[first_failure + 1:])
    print(f"\nE033 fine_observations={observations}")
