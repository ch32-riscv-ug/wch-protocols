"""E039: bring up hardware gating from the PARLIO RX level delimiter.

The pulse delimiter (E037/E038) gives a fixed-length frame started by an edge.
The level delimiter instead treats the valid line as "data is valid here", so
it should give gating and, with eof_data_len = 0, a variable-length frame whose
size follows the gate width. Both outcomes are recorded rather than assumed.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E039 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rx_units=(\d+) data_width=(\d+) valid_pin=(\d+) "
    rb"valid_line_id=(\d+) sample_rate_hz=(\d+) gate_words=(\d+) "
    rb"gate_bytes=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE active_low=(\d+) eof_data_len=(\d+) gate=(\d+) config=(\S+) "
    rb"delimiter=(\S+) source=(\S+) source_start=(\S+) enable=(\S+) "
    rb"receive=(\S+) wait=(\S+) disable=(\S+) sync=(\S+) receive_done=(\d+) "
    rb"received_bytes=(\d+) expected_bytes=(\d+) delta_bytes=(-?\d+) "
    rb"runs=(\d+) run_min=(\d+) run_max=(\d+) violations=(\d+) "
    rb"head=([0-9a-f]{8}) elapsed_us=(\d+)\r?\n"
)
FIXED = 2048
GATE_BYTES = 2048 * 4 * 4 // 8
CASES = ((0, FIXED, 1), (0, 0, 1), (1, FIXED, 1), (0, FIXED, 0))


def test_parlio_level_gate(dut):
    """Record whether an enable level gates capture and bounds the frame."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[:3] == (1, 1, 4)
    assert env[5] == 20_000_000 and env[7] == GATE_BYTES
    dut.expect_exact("BUFFERS source_ok=1 payload_ok=1", timeout=5)

    observations = []
    for active_low, eof_data_len, gate in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        assert (int(values[0]), int(values[1]), int(values[2])) == (
            active_low, eof_data_len, gate
        )
        api = dict(
            config=values[3], delimiter=values[4], source=values[5],
            source_start=values[6], enable=values[7], receive=values[8],
            wait=values[9], disable=values[10], sync=values[11],
        )
        (receive_done, received, expected, delta, runs, run_min, run_max,
         violations) = map(int, values[12:20])
        head, elapsed_us = values[20], int(values[21])

        assert elapsed_us > 0
        if receive_done:
            assert received > 0
        observations.append(
            (active_low, eof_data_len, gate, api["delimiter"], api["wait"],
             receive_done, received, expected, delta, runs, run_min, run_max,
             violations, head, elapsed_us)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    # The one invariant: no gate must mean no frame.
    no_gate = observations[-1]
    assert no_gate[5] == 0, f"frame arrived without a gate: {no_gate}"

    print(f"\nE039 level_gate_observations={observations}")
