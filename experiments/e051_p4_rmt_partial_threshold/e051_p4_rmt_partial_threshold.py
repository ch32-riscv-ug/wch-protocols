"""E051: what threshold makes RMT report partial reception?

E045 concluded the user buffer filling is the trigger, but E050 saw zero
callbacks at a 2.4 ms gate period where a 32-symbol buffer should have filled
in 76.8 ms of a 100 ms harvest. Neither the buffer nor mem_block_symbols alone
explains all of E050's five points, so this moves one knob at a time on the
failing case.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E051 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
CASE = re.compile(
    rb"CASE buffer_symbols=(\d+) mem_block_symbols=(\d+) "
    rb"harvest_target_us=(-?\d+) gate_words=(\d+) window_bytes=(\d+) "
    rb"expected_high=(\d+) expected_duty_ppc=(\d+) config=(\S+) "
    rb"delimiter=(\S+) rmt=(\S+) rmt_enable=(\S+) source=(\S+) "
    rb"source_start=(\S+) rmt_receive=(\S+) enable=(\S+) receive=(\S+) "
    rb"disable=(\S+) rmt_disable=(\S+) sync=(\S+) rmt_callbacks=(\d+) "
    rb"rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) high_max=(\d+) "
    rb"low_count=(\d+) low_min=(\d+) low_max=(\d+) parlio_callbacks=(\d+) "
    rb"dequeues=(\d+) harvested=(\d+) overflow=(\d+) violations=(\d+) "
    rb"shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) matched_steps=(\d+) compared_steps=(\d+) "
    rb"harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
CASES = ((32, 48, 100000), (8, 48, 100000), (32, 48, 400000), (32, 96, 100000))
GATE_WORDS = 6000
LOOP_US = 2400  # 12,000 source words at 5 MHz
EXPECTED_HIGH = 24000


def _expect_list(dut, label, buffer_symbols, item, count):
    pattern = re.compile(
        (rf"{label} buffer_symbols={buffer_symbols}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=60).group(1).split()


def test_rmt_partial_threshold(dut):
    """Move buffer size, block size and harvest time one at a time."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=10,
    )

    observations = []
    for buffer_symbols, mem_block_symbols, harvest_target_us in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=120).groups()]
        assert (int(values[0]), int(values[1]), int(values[2])) == (
            buffer_symbols, mem_block_symbols, harvest_target_us
        )
        assert int(values[3]) == GATE_WORDS
        window_bytes, expected_high, expected_duty_ppc = map(int, values[4:7])
        assert expected_high == EXPECTED_HIGH
        api = values[7:19]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         matched_steps, compared_steps, harvest_us, harvest_kbps) = map(
            int, values[19:40]
        )
        _expect_list(dut, "SYMBOLS", buffer_symbols, r"\d+:\d+", 16)
        _expect_list(dut, "VIOLS", buffer_symbols, r"\d+", 16)

        assert all(v == "ESP_OK" for v in api), api
        assert harvest_us > 0 and harvested > 0
        # The capture side must survive whatever RMT is configured to do.
        parlio_ok = (
            overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
        )
        rmt_fired = rmt_callbacks > 0
        rmt_exact = (
            high_count > 0
            and high_min == high_max == expected_high
            and low_count > 0
            and low_min == low_max == expected_high
        )
        fill_us = buffer_symbols * LOOP_US
        observations.append(
            (buffer_symbols, mem_block_symbols, harvest_target_us, rmt_fired,
             rmt_exact, parlio_ok, rmt_callbacks, rmt_stored, high_min,
             high_max, low_min, low_max, fill_us, harvest_us, violations,
             inflight_max, ring_overrun)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE051 buffer={row[0]} block={row[1]} harvest={row[2]}us "
            f"fired={row[3]} exact={row[4]} parlio_ok={row[5]} "
            f"callbacks={row[6]} stored={row[7]} "
            f"high={row[8]}..{row[9]} low={row[10]}..{row[11]} "
            f"buffer_fill={row[12]}us actual_harvest={row[13]}us"
        )
    print(f"\nE051 threshold_observations={observations}")

    # The capture side is the harness check; RMT behaviour is the result.
    for row in observations:
        assert row[5], f"PARLIO side broke: {row}"
