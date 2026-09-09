"""E031: validate PARLIO packing for 1/2/4/8/16 channels."""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E031 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) parlio_groups=(\d+) "
    rb"rx_units_per_group=(\d+) max_width=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE width=(\d+) result=(\S+) config=(\S+) pwm=(\S+) enable=(\S+) "
    rb"receive=(\S+) start=(\S+) stop=(\S+) disable=(\S+) sync=(\S+) "
    rb"target_bytes=(\d+) copied=(\d+) callbacks=(\d+) dequeues=(\d+) "
    rb"extra_bytes=(\d+) overflows=(\d+) max_queue=(\d+) capture_us=(\d+) "
    rb"sample_rate_khz=(\d+) byte_rate_kbps=(\d+) max_error_ppm=(\d+) "
    rb"min_edges=(\d+) max_edges=(\d+) duplicate_mismatches=(\d+)\r?\n"
)
WIDTHS = (1, 2, 4, 8, 16)
SAMPLE_COUNT = 1024 * 1024


def test_parlio_channel_width(dut):
    """Require all hardware-supported widths to unpack into valid samples."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[1] >= 2 * 1024 * 1024
    assert env[2:] == (1, 1, 16)
    dut.expect_exact("BUFFERS ring_ok=1 destination_ok=1 queue_ok=1", timeout=5)

    observations = []
    for width in WIDTHS:
        values = [value.decode() for value in dut.expect(CASE, timeout=30).groups()]
        assert int(values[0]) == width
        assert all(value == "ESP_OK" for value in values[1:10])
        metrics = list(map(int, values[10:]))
        (target_bytes, copied, callbacks, dequeues, extra_bytes, overflows,
         max_queue, capture_us, sample_rate_khz, byte_rate_kbps,
         max_error_ppm, min_edges, max_edges, duplicate_mismatches) = metrics
        assert target_bytes == copied == SAMPLE_COUNT * width // 8
        assert callbacks >= dequeues > 1
        assert 0 <= extra_bytes < 65536
        assert overflows == 0 and max_queue < 16
        assert 0 < capture_us < 500_000
        assert 7_500 <= sample_rate_khz <= 8_500
        expected_byte_rate = 8_000 * width // 8
        assert expected_byte_rate * 95 // 100 <= byte_rate_kbps
        assert byte_rate_kbps <= expected_byte_rate * 105 // 100
        assert max_error_ppm <= 30_000
        assert 20_000 <= min_edges <= max_edges
        assert duplicate_mismatches == 0
        observations.append((width, *metrics))

    dut.expect_exact("DONE status=ok", timeout=5)
    print(f"\nE031 width_observations={observations}")
