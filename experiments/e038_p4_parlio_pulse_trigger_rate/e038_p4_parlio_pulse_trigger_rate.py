"""E038: find the rate ceiling of a hardware-triggered finite frame.

E036 showed the ~98 MB/s ceiling belongs to the ring-to-PSRAM copy. A finite
frame captured straight into internal RAM has no copy stage, so this sweep asks
how far the hardware-triggered path goes. The source loop is exactly twice the
frame, so the gap between two frame completions is one loop period and yields
the sample rate directly.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E038 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) tx_units=(\d+) rx_units=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) valid_line_id=(\d+) frame_samples=(\d+) "
    rb"loop_samples=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE rx_rate_hz=(\d+) tx_rate_hz=(\d+) config=(\S+) delimiter=(\S+) "
    rb"source=(\S+) source_start=(\S+) enable=(\S+) receive=(\S+) wait=(\S+) "
    rb"disable=(\S+) sync=(\S+) receive_done=(\d+) bytes0=(\d+) bytes1=(\d+) "
    rb"frame_bytes=(\d+) gap_us=(-?\d+) measured_rate_hz=(\d+) "
    rb"ratio_ppk=(\d+) runs0=(\d+) runs1=(\d+) run_min=(\d+) run_max=(\d+) "
    rb"violations=(\d+) head0=([0-9a-f]{8}) head1=([0-9a-f]{8}) "
    rb"elapsed_us=(\d+)\r?\n"
)
RATES = (20, 40, 80, 100, 120, 160)
FRAME_BYTES = 16 * 1024
FRAME_SAMPLES = FRAME_BYTES * 2
LOOP_SAMPLES = 16384 * 4


def test_parlio_pulse_trigger_rate(dut):
    """Record, for every rate, whether the triggered frame stayed intact."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[:4] == (1, 1, 1, 4)
    assert env[6] == FRAME_SAMPLES and env[7] == LOOP_SAMPLES
    dut.expect_exact("BUFFERS source_ok=1 payload_ok=1", timeout=5)

    observations = []
    for rate_mhz in RATES:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        rx_rate, tx_rate = int(values[0]), int(values[1])
        assert rx_rate == rate_mhz * 1_000_000
        assert tx_rate == rx_rate // 4
        api = values[2:11]
        (receive_done, bytes0, bytes1, frame_bytes, gap_us, measured_rate_hz,
         ratio_ppk, runs0, runs1, run_min, run_max, violations) = map(
            int, values[11:23]
        )
        head0, head1, elapsed_us = values[23], values[24], int(values[25])

        assert frame_bytes == FRAME_BYTES
        assert elapsed_us > 0
        if receive_done >= 2:
            assert bytes0 == bytes1 == FRAME_BYTES
            assert gap_us > 0

        clean = (
            all(v == "ESP_OK" for v in api)
            and receive_done == 2
            and violations == 0
            and run_min == run_max == 4
            and runs0 == runs1 == FRAME_SAMPLES // 4
            and 950 <= ratio_ppk <= 1050
        )
        observations.append(
            (rate_mhz, clean, receive_done, gap_us, measured_rate_hz,
             ratio_ppk, runs0, runs1, run_min, run_max, violations, head0,
             head1, elapsed_us)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    # 20 MHz is the harness check: E037 already proved that tier, so if it
    # breaks here the sweep says nothing about the rates above it.
    baseline = observations[0]
    assert baseline[1], f"20 MHz baseline is not clean: {baseline}"
    print(f"\nE038 trigger_rate_observations={observations}")
