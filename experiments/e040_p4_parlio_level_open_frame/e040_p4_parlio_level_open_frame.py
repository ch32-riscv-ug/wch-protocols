"""E040: does an open-ended level delimiter actually run the DMA?

E039 saw no completion for eof_data_len = 0, but never looked at the payload,
so it could not tell "never started" from "started, no EOF". Here the payload
is inspected whatever happened, and a partial_rx_en case checks whether the
data can be harvested and whether the gate really thins the stream.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E040 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rx_units=(\d+) data_width=(\d+) valid_pin=(\d+) "
    rb"sample_rate_hz=(\d+) gate_words=(\d+) source_words=(\d+) "
    rb"fill=([0-9a-f]{2})\r?\n"
)
CASE = re.compile(
    rb"CASE partial=(\d+) gate=(\d+) config=(\S+) delimiter=(\S+) source=(\S+) "
    rb"source_start=(\S+) enable=(\S+) receive=(\S+) wait=(\S+) disable=(\S+) "
    rb"sync=(\S+) inspect_sync=(\S+) receive_done=(\d+) partial_count=(\d+) "
    rb"partial_bytes=(\d+) dequeues=(\d+) harvested=(\d+) overflow=(\d+) "
    rb"written_bytes=(\d+) first_written=(\d+) last_written=(\d+) "
    rb"payload_size=(\d+) head=([0-9a-f]{16}) harvest_us=(-?\d+) "
    rb"harvest_kbps=(\d+) raw_kbps=(\d+) raw_ratio_ppc=(\d+) "
    rb"elapsed_us=(\d+)\r?\n"
)
CASES = ((0, 1), (1, 1), (1, 0))
PAYLOAD_SIZE = 16 * 1024
# 20 MHz at data_width 4 packs two samples per byte.
RAW_KBPS = 20_000 * 4 // 8
GATE_DUTY_PPC = 2048 * 100 // 16384


def test_parlio_level_open_frame(dut):
    """Record whether the DMA writes, and whether the gate thins the stream."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()[:7]))
    assert env[:3] == (1, 1, 4)
    assert env[4] == 20_000_000 and env[5] == 2048 and env[6] == 16384
    dut.expect_exact("BUFFERS source_ok=1 payload_ok=1 queue_ok=1", timeout=5)

    observations = []
    for partial, gate in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=30).groups()]
        assert (int(values[0]), int(values[1])) == (partial, gate)
        api = values[2:12]
        (receive_done, partial_count, partial_bytes, dequeues, harvested,
         overflow, written, first_written, last_written,
         payload_size) = map(int, values[12:22])
        head = values[22]
        harvest_us, harvest_kbps, raw_kbps, raw_ratio_ppc, elapsed_us = map(
            int, values[23:28]
        )

        assert payload_size == PAYLOAD_SIZE
        assert raw_kbps == RAW_KBPS
        assert elapsed_us > 0
        assert written <= payload_size
        assert first_written <= last_written or written == 0
        # Every API up to and including receive must succeed, otherwise the
        # case says nothing about the DMA.
        assert all(v == "ESP_OK" for v in api[:6]), api
        observations.append(
            (partial, gate, api[6], receive_done, partial_count, partial_bytes,
             dequeues, harvested, overflow, written, first_written,
             last_written, head, harvest_kbps, raw_ratio_ppc, elapsed_us)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    # The one invariant: with no gate at all, nothing may be captured.
    no_gate = observations[-1]
    assert no_gate[7] == 0 and no_gate[9] == 0, f"data without a gate: {no_gate}"

    print(f"\nE040 open_frame_observations={observations}")
    print(f"E040 gate_duty_ppc={GATE_DUTY_PPC}")
