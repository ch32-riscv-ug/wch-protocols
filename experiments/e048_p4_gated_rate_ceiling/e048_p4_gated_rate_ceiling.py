"""E048: how fast can a gated capture run in the shared-pin configuration?

Raw capture saturates at the ~98 MB/s ring-to-PSRAM copy (E036). Gating only
sends the gate's samples to DMA, so the copy sees duty x the byte rate - which
should let the sample rate reach the 160 MHz clock ceiling. The source is held
at 5 MHz so RMT's tick counts stay invariant and only the PARLIO window sample
counts scale.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E048 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rmt_rx_candidates=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) source_rate_hz=(\d+) source_words=(\d+) "
    rb"rmt_resolution_hz=(\d+) rmt_symbols=(\d+) windows=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE sample_rate_hz=(\d+) tick_scale=(\d+) expected_run=(\d+) "
    rb"gate_total=(\d+) expected_loop_bytes=(\d+) expected_duty_ppc=(\d+) "
    rb"expected_kbps=(\d+) config=(\S+) delimiter=(\S+) rmt=(\S+) "
    rb"rmt_enable=(\S+) source=(\S+) source_start=(\S+) rmt_receive=(\S+) "
    rb"enable=(\S+) receive=(\S+) disable=(\S+) rmt_disable=(\S+) sync=(\S+) "
    rb"rmt_callbacks=(\d+) rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) "
    rb"high_max=(\d+) low_count=(\d+) low_min=(\d+) low_max=(\d+) "
    rb"parlio_callbacks=(\d+) dequeues=(\d+) harvested=(\d+) overflow=(\d+) "
    rb"violations=(\d+) shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
RATES = (20, 40, 80, 100, 120, 160)
WIDTHS = (300, 700, 1500, 2044)
SOURCE_RATE_HZ = 5_000_000
SOURCE_WORDS = 16384
RMT_RESOLUTION_HZ = 20_000_000
# RMT ticks are invariant because the source rate is fixed.
EXPECTED_HIGH = tuple(w * RMT_RESOLUTION_HZ // SOURCE_RATE_HZ for w in WIDTHS)
TOLERANCE = 2


def _expect_list(dut, label, rate_hz, pattern_item, count):
    pattern = re.compile(
        (rf"{label} sample_rate_hz={rate_hz}"
         + r"((?: %s){%d})\r?\n" % (pattern_item, count)).encode()
    )
    return dut.expect(pattern, timeout=30).group(1).split()


def test_gated_rate_ceiling(dut):
    """Record, for every rate, whether the gated capture stayed intact."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[2] == 8
    assert env[4] == SOURCE_RATE_HZ and env[5] == SOURCE_WORDS
    assert env[6] == RMT_RESOLUTION_HZ and env[8] == len(WIDTHS)
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=5,
    )

    observations = []
    for rate_mhz in RATES:
        rate_hz = rate_mhz * 1_000_000
        values = [v.decode() for v in dut.expect(CASE, timeout=60).groups()]
        assert int(values[0]) == rate_hz
        tick_scale, expected_run, gate_total = map(int, values[1:4])
        expected_loop_bytes, expected_duty_ppc, expected_kbps = map(
            int, values[4:7]
        )
        assert tick_scale == rate_hz // RMT_RESOLUTION_HZ
        assert expected_run == rate_hz // SOURCE_RATE_HZ
        assert gate_total == sum(WIDTHS)
        api = values[7:19]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         harvest_us, harvest_kbps) = map(int, values[19:38])

        symbols = [
            tuple(int(v) for v in item.split(b":"))
            for item in _expect_list(dut, "SYMBOLS", rate_hz, r"\d+:\d+", 16)
        ]
        offsets = [
            int(v) for v in _expect_list(dut, "VIOLS", rate_hz, r"\d+", 16)
        ]

        assert all(v == "ESP_OK" for v in api), api
        assert harvest_us > 0 and harvested > 0
        assert ring_bytes == 64 * 1024
        assert ring_overrun == (1 if inflight_max > ring_bytes else 0)

        highs = [d for level, d in symbols if level == 1 and d]
        # RMT ticks must not move with the sample rate.
        rmt_invariant = sorted(set(highs)) == sorted(EXPECTED_HIGH)
        expected_windows = {h * tick_scale for h in highs}
        steps = [b - a for a, b in zip(offsets, offsets[1:]) if b > a]
        deviations = [
            min(abs(s - w) for w in expected_windows) for s in steps
        ] or [0]
        clean = (
            violations > 0
            and shared_low == 0
            and ring_overrun == 0
            and overflow == 0
            and rmt_invariant
            and max(deviations) <= TOLERANCE
        )
        observations.append(
            (rate_mhz, clean, ring_overrun, inflight_max, overflow, shared_low,
             violations, rmt_invariant, max(deviations), harvest_kbps,
             expected_kbps, sorted(set(highs)), steps[:6])
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE048 rate={row[0]}MHz clean={row[1]} overrun={row[2]} "
            f"inflight={row[3]} shared_low={row[5]} violations={row[6]} "
            f"rmt_invariant={row[7]} max_dev={row[8]} "
            f"kbps={row[9]} (expect {row[10]}) highs={row[11]} steps={row[12]}"
        )
    print(f"\nE048 gated_rate_observations={observations}")

    # 20 MHz is the harness check: E047 already proved that tier.
    assert observations[0][1], f"20 MHz baseline is not clean: {observations[0]}"
