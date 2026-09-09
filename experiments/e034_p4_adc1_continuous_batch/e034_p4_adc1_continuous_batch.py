"""E034: ADC1 continuous DMA to PSRAM over channel count and rate."""

import re


BANNER = re.compile(rb"# EXP E034 v1 git=\S+ probe=esp32p4_adc target=floating build=[^\r\n]+\r?\n")
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) adc_units=(\d+) "
    rb"adc1_channels=(\d+) result_bytes=(\d+) rate_min=(\d+) rate_max=(\d+) "
    rb"target_conversions=(\d+)"
)
CASE = re.compile(
    rb"CASE channels=(\d+) rate_hz=(\d+) result=(\S+) create=(\S+) "
    rb"config=(\S+) callbacks=(\S+) start=(\S+) read=(\S+) stop=(\S+) "
    rb"deinit=(\S+) psram=(\d+) target=(\d+) copied=(\d+) reads=(\d+) "
    rb"timeouts=(\d+) overflows=(\d+) elapsed_us=(\d+) effective_rate_hz=(\d+) "
    rb"invalid_unit=(\d+) invalid_channel=(\d+) order_errors=(\d+)([^\r\n]*)"
)
EXPECTED = tuple((channels, rate) for channels in (1, 2, 4, 8) for rate in (10000, 40000, 83333))
TARGET_CONVERSIONS = 262144


def test_adc1_continuous_batch(dut):
    dut.write("?")
    dut.expect(BANNER, timeout=10)
    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[1] >= 32 * 1024 * 1024
    assert env[2:] == (2, 8, 4, 611, 83333, TARGET_CONVERSIONS)

    observations = []
    for channels, requested_rate in EXPECTED:
        match = dut.expect(CASE, timeout=40)
        fields = [value.decode() for value in match.groups()]
        assert (int(fields[0]), int(fields[1])) == (channels, requested_rate)
        result = fields[2]
        api = fields[3:10]
        numbers = list(map(int, fields[10:21]))
        psram, target, copied, reads, timeouts, overflows, elapsed_us, effective_rate, invalid_unit, invalid_channel, order_errors = numbers
        suffix = fields[21]
        assert result == "ESP_OK" and all(value == "ESP_OK" for value in api)
        assert psram == 1 and target == copied == TARGET_CONVERSIONS * 4
        assert reads > 0 and timeouts == overflows == 0 and elapsed_us > 0
        assert requested_rate * 90 // 100 <= effective_rate <= requested_rate * 110 // 100
        assert invalid_unit == invalid_channel == order_errors == 0

        channel_fields = re.findall(r"c(\d+)=(\d+):(\d+):(\d+)", suffix)
        assert len(channel_fields) == channels
        for index, samples, minimum, maximum in channel_fields:
            assert int(index) < channels
            assert int(samples) == TARGET_CONVERSIONS // channels
            assert 0 <= int(minimum) <= int(maximum) <= 4095
        observations.append((channels, requested_rate, effective_rate, reads))

    dut.expect_exact("DONE status=ok", timeout=5)
    print(f"\nE034 adc_observations={observations}")
