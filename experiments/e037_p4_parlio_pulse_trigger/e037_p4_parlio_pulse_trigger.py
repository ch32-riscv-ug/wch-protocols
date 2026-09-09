"""E037: bring up a hardware frame trigger from the PARLIO RX pulse delimiter.

Every trigger measured so far scans samples in software. The pulse delimiter
starts the frame from a valid line in hardware, stops it at eof_data_len and
carries its own timeout, so this test only asks whether that path works at all
at the safe 20 MHz tier -- not how fast it can go.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E037 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) tx_units=(\d+) rx_units=(\d+) "
    rb"max_rx_width=(\d+) data_width=(\d+) valid_pin=(\d+) "
    rb"sample_rate_hz=(\d+) frame_samples=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE valid_line=(\d+) pulse=(\d+) timeout_ticks=(\d+) config=(\S+) "
    rb"delimiter=(\S+) source=(\S+) source_start=(\S+) enable=(\S+) "
    rb"receive=(\S+) wait=(\S+) disable=(\S+) sync=(\S+) receive_done=(\d+) "
    rb"timeout_events=(\d+) received_bytes=(\d+) frame_bytes=(\d+) "
    rb"runs=(\d+) run_min=(\d+) run_max=(\d+) violations=(\d+) "
    rb"head=([0-9a-f]{8}) elapsed_us=(\d+)\r?\n"
)
CASES = ((4, 1, 0), (5, 1, 0), (4, 0, 60000), (5, 0, 60000))
FRAME_BYTES = 16 * 1024


def test_parlio_pulse_trigger(dut):
    """Record whether a hardware pulse starts and ends a frame."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[:5] == (1, 1, 1, 16, 4)
    assert env[6] == 20_000_000 and env[7] == FRAME_BYTES * 2
    dut.expect_exact("BUFFERS source_ok=1 payload_ok=1", timeout=5)

    observations = []
    for valid_line, pulse, timeout_ticks in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        assert (int(values[0]), int(values[1]), int(values[2])) == (
            valid_line, pulse, timeout_ticks
        )
        api = dict(
            config=values[3], delimiter=values[4], source=values[5],
            source_start=values[6], enable=values[7], receive=values[8],
            wait=values[9], disable=values[10], sync=values[11],
        )
        (receive_done, timeout_events, received_bytes, frame_bytes, runs,
         run_min, run_max, violations) = map(int, values[12:20])
        head, elapsed_us = values[20], int(values[21])

        assert frame_bytes == FRAME_BYTES
        assert elapsed_us > 0
        # A frame that reported done must be a complete frame.
        if receive_done:
            assert received_bytes >= FRAME_BYTES
        triggered = api["wait"] == "ESP_OK" and receive_done > 0
        clean = triggered and violations == 0 and runs > 0
        observations.append(
            (valid_line, pulse, timeout_ticks, api["delimiter"], api["receive"],
             api["wait"], receive_done, timeout_events, received_bytes, runs,
             run_min, run_max, violations, head, elapsed_us)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    # The experiment answers a yes/no, so both outcomes are recorded rather
    # than asserted. The one invariant is that a frame must never appear
    # without a trigger pulse.
    for row in observations:
        if row[1] == 0:
            assert row[6] == 0, f"frame arrived without a pulse: {row}"

    print(f"\nE037 pulse_observations={observations}")
