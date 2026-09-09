"""E054: what is the smallest mem_block_symbols DMA mode accepts?

E053 saw DMA reject 8 and 16 and accept 64, whose 156,935 us first fire is
worse than non-DMA's 115.2 us equivalent at 48. Anything under 48 that DMA
accepts would beat it, since in DMA mode the first fire lands at
mem_block_symbols times the symbol period.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E054 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
CASE = re.compile(
    rb"CASE with_dma=(\d+) mem_block_symbols=(\d+) gate_words=(\d+) "
    rb"window_bytes=(\d+) expected_high=(\d+) expected_duty_ppc=(\d+) "
    rb"config=(\S+) delimiter=(\S+) rmt=(\S+) rmt_enable=(\S+) source=(\S+) "
    rb"source_start=(\S+) rmt_receive=(\S+) enable=(\S+) receive=(\S+) "
    rb"disable=(\S+) rmt_disable=(\S+) sync=(\S+) rmt_callbacks=(\d+) "
    rb"rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) high_max=(\d+) "
    rb"low_count=(\d+) low_min=(\d+) low_max=(\d+) parlio_callbacks=(\d+) "
    rb"dequeues=(\d+) harvested=(\d+) overflow=(\d+) violations=(\d+) "
    rb"shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) matched_steps=(\d+) compared_steps=(\d+) "
    rb"harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
CASES = ((1, 24), (1, 32), (1, 40), (1, 48))
GATE_WORDS = 6000
SYMBOL_PERIOD_US = 2400  # 12,000 source words at 5 MHz
EXPECTED_HIGH = 24000
HARVEST_US = 400000


def _expect_list(dut, label, with_dma, block, item, count):
    pattern = re.compile(
        (rf"{label} with_dma={with_dma} mem_block_symbols={block}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=120).group(1).split()


def test_rmt_dma_block_min(dut):
    """Find the smallest DMA block accepted and time its first fire."""
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
    for with_dma, block in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=120).groups()]
        assert (int(values[0]), int(values[1])) == (with_dma, block)
        assert int(values[2]) == GATE_WORDS
        assert int(values[4]) == EXPECTED_HIGH
        api = dict(
            config=values[6], delimiter=values[7], rmt=values[8],
            rmt_enable=values[9], source=values[10], source_start=values[11],
            rmt_receive=values[12], enable=values[13], receive=values[14],
            disable=values[15], rmt_disable=values[16], sync=values[17],
        )
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         matched_steps, compared_steps, harvest_us, harvest_kbps) = map(
            int, values[18:39]
        )
        _expect_list(dut, "SYMBOLS", with_dma, block, r"\d+:\d+", 16)
        _expect_list(dut, "VIOLS", with_dma, block, r"\d+", 16)
        fires = [
            tuple(int(v) for v in item.split(b":"))
            for item in _expect_list(dut, "RMTCB", with_dma, block,
                                     r"-?\d+:\d+", 16)
        ]

        rmt_created = api["rmt"] == "ESP_OK"
        # The capture side must keep working whatever RMT is asked to do.
        parlio_ok = (
            api["config"] == "ESP_OK"
            and api["receive"] == "ESP_OK"
            and overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
        )
        real = [(t, n) for t, n in fires if n > 0]
        times = [t for t, _ in real]
        assert times == sorted(times), times
        gaps = [b - a for a, b in zip(times, times[1:])]
        predicted_first = block * SYMBOL_PERIOD_US
        predicted_gap = block // 2 * SYMBOL_PERIOD_US
        observations.append(
            (with_dma, block, rmt_created, parlio_ok, rmt_callbacks,
             times[0] if times else None, predicted_first, gaps[:4],
             predicted_gap, [n for _, n in real][:4], high_min, high_max,
             api["rmt"], api["rmt_receive"], harvest_us)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE054 dma={row[0]} block={row[1]} created={row[2]} "
            f"parlio_ok={row[3]} callbacks={row[4]} first={row[5]}us "
            f"(predict {row[6]}) gaps={row[7]} (predict {row[8]}) "
            f"symbols_per_cb={row[9]} high={row[10]}..{row[11]} "
            f"rmt={row[12]} rmt_receive={row[13]}"
        )
    print(f"\nE054 dma_block_observations={observations}")

    # Both outcomes are results, so record rather than assert. The invariant
    # is that a channel which was created must not break the capture side.
    for row in observations:
        if row[2]:
            assert row[3], f"capture broke with a working channel: {row}"
