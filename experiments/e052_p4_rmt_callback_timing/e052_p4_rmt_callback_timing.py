"""E052: when does RMT's partial callback actually fire, and how often?

E051 eliminated the user buffer size and mem_block_symbols as the trigger and
left only "more time helps", with callback counts that fit no simple model.
Timestamping each callback and recording its symbol count measures the startup
delay and the spacing directly, and toggling the PSRAM copy loop tests whether
a saturated CPU is what holds RMT back.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E052 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
CASE = re.compile(
    rb"CASE gate_words=(\d+) copy=(\d+) harvest_target_us=(-?\d+) "
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
CASES = ((6000, 1, 400000), (6000, 0, 400000), (1000, 1, 400000))
SOURCE_RATE_HZ = 5_000_000
RMT_RESOLUTION_HZ = 20_000_000


def _expect_list(dut, label, gate_words, copy, item, count):
    pattern = re.compile(
        (rf"{label} gate_words={gate_words} copy={copy}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=120).group(1).split()


def test_rmt_callback_timing(dut):
    """Measure the first-fire delay, the spacing and symbols per callback."""
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
    for gate_words, copy, harvest_target_us in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=120).groups()]
        assert (int(values[0]), int(values[1]), int(values[2])) == (
            gate_words, copy, harvest_target_us
        )
        window_bytes, expected_high, expected_duty_ppc = map(int, values[3:6])
        assert expected_high == gate_words * RMT_RESOLUTION_HZ // SOURCE_RATE_HZ
        api = values[6:18]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         matched_steps, compared_steps, harvest_us, harvest_kbps) = map(
            int, values[18:39]
        )
        _expect_list(dut, "SYMBOLS", gate_words, copy, r"\d+:\d+", 16)
        _expect_list(dut, "VIOLS", gate_words, copy, r"\d+", 16)
        fires = [
            tuple(int(v) for v in item.split(b":"))
            for item in _expect_list(dut, "RMTCB", gate_words, copy,
                                     r"-?\d+:\d+", 16)
        ]

        assert all(v == "ESP_OK" for v in api), api
        assert harvest_us > 0
        real = [(t, n) for t, n in fires if n > 0]
        # Fire times must be monotonic and inside the harvest window.
        times = [t for t, _ in real]
        assert times == sorted(times), times
        assert all(0 <= t <= harvest_us + 50_000 for t in times), times
        gaps = [b - a for a, b in zip(times, times[1:])]
        observations.append(
            (gate_words, copy, rmt_callbacks, len(real),
             times[0] if times else None, gaps[:6],
             [n for _, n in real][:6], high_min, high_max, expected_high,
             overflow, shared_low, violations, matched_steps, compared_steps,
             harvested, harvest_kbps)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE052 gate={row[0]} copy={row[1]} callbacks={row[2]} "
            f"tracked={row[3]} first_fire={row[4]}us gaps={row[5]} "
            f"symbols_per_cb={row[6]} high={row[7]}..{row[8]} "
            f"(expect {row[9]}) parlio_overflow={row[10]} "
            f"shared_low={row[11]} steps={row[13]}/{row[14]} "
            f"harvested={row[15]}"
        )
    print(f"\nE052 timing_observations={observations}")

    # At 400 ms every case must fire at least once, or there is nothing to time.
    for row in observations:
        assert row[2] > 0, f"no callback at all: {row}"
