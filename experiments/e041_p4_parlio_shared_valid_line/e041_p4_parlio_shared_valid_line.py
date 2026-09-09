"""E041: can the valid line share a GPIO with a data line?

E037/E038 gave the valid signal its own pin, so hardware trigger was priced at
one channel. The GPIO matrix can fan one input GPIO out to several peripheral
input signals, so the same pin may serve as both data channel 7 and the trigger.
If it does, 8 channels plus a hardware trigger fit in 8 pins.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E041 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rx_units=(\d+) data_width=(\d+) shared_bit=(\d+) "
    rb"valid_line_id=(\d+) frame_samples=(\d+) gate_words=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE rx_rate_hz=(\d+) tx_rate_hz=(\d+) shared_pin=(\d+) "
    rb"valid_line_id=(\d+) config=(\S+) delimiter=(\S+) source=(\S+) "
    rb"source_start=(\S+) enable=(\S+) receive=(\S+) wait=(\S+) disable=(\S+) "
    rb"sync=(\S+) receive_done=(\d+) received_bytes=(\d+) frame_bytes=(\d+) "
    rb"runs=(\d+) run_min=(\d+) run_max=(\d+) violations=(\d+) "
    rb"shared_low=(\d+) head=([0-9a-f]{8}) elapsed_us=(\d+)\r?\n"
)
RATES = (20, 80, 160)
FRAME_BYTES = 4096


def test_parlio_shared_valid_line(dut):
    """Record whether one GPIO can be both data channel 7 and the trigger."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[:5] == (1, 1, 8, 7, 8)
    assert env[5] == FRAME_BYTES and env[6] == 2048
    dut.expect_exact("BUFFERS source_ok=1 payload_ok=1", timeout=5)

    observations = []
    for rate_mhz in RATES:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        rx_rate, tx_rate = int(values[0]), int(values[1])
        assert rx_rate == rate_mhz * 1_000_000
        assert tx_rate == rx_rate // 4
        shared_pin, valid_line_id = int(values[2]), int(values[3])
        api = values[4:13]
        (receive_done, received, frame_bytes, runs, run_min, run_max,
         violations, shared_low) = map(int, values[13:21])
        head, elapsed_us = values[21], int(values[22])

        assert frame_bytes == FRAME_BYTES
        assert elapsed_us > 0
        if receive_done:
            assert received >= FRAME_BYTES

        clean = (
            all(v == "ESP_OK" for v in api)
            and receive_done == 1
            and violations == 0
            # The shared line must read back as data on every sample.
            and shared_low == 0
            # 4 samples per source word.
            and run_min == run_max == 4
        )
        observations.append(
            (rate_mhz, clean, shared_pin, valid_line_id, api[4], api[6],
             receive_done, received, runs, run_min, run_max, violations,
             shared_low, head, elapsed_us)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    # Both outcomes are a result, so the sweep is recorded rather than
    # asserted. Only report what happened.
    print(f"\nE041 shared_valid_observations={observations}")
