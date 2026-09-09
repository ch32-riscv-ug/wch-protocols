"""E050: does window length matter once the duty is held fixed?

E049 varied the gate width with a fixed gap, so the duty moved with it and the
window-length model could not be separated from the mean-rate one. Tying the gap
to the window pins the duty at 50% - 80 MB/s at 160 MHz, well under the copy
ceiling - and there the two models predict opposite outcomes for the long
windows.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E050 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV parlio_groups=(\d+) rmt_rx_candidates=(\d+) data_width=(\d+) "
    rb"valid_pin=(\d+) source_rate_hz=(\d+) source_words=(\d+) "
    rb"rmt_resolution_hz=(\d+) rmt_symbols=(\d+) sample_rate_hz=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE gate_words=(\d+) loop_words=(\d+) window_bytes=(\d+) "
    rb"predicted_overload=(\d+) tick_scale=(\d+) expected_run=(\d+) "
    rb"expected_duty_ppc=(\d+) config=(\S+) delimiter=(\S+) rmt=(\S+) "
    rb"rmt_enable=(\S+) source=(\S+) source_start=(\S+) rmt_receive=(\S+) "
    rb"enable=(\S+) receive=(\S+) disable=(\S+) rmt_disable=(\S+) sync=(\S+) "
    rb"rmt_callbacks=(\d+) rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) "
    rb"high_max=(\d+) low_count=(\d+) low_min=(\d+) low_max=(\d+) "
    rb"parlio_callbacks=(\d+) dequeues=(\d+) harvested=(\d+) overflow=(\d+) "
    rb"violations=(\d+) shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) matched_steps=(\d+) compared_steps=(\d+) "
    rb"harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
GATE_WIDTHS = (1000, 2000, 4000, 6000, 7000)

SAMPLE_RATE_HZ = 160_000_000
SOURCE_RATE_HZ = 5_000_000
RING_BYTES = 64 * 1024
SPOOL_MBPS = 98
EXPECTED_RUN = SAMPLE_RATE_HZ // SOURCE_RATE_HZ


def _expect_list(dut, label, gate_words, item, count):
    pattern = re.compile(
        (rf"{label} gate_words={gate_words}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=30).group(1).split()


def test_gated_window_at_fixed_duty(dut):
    """Sweep the window at a fixed duty, where the models disagree."""
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
    assert env[4] == SOURCE_RATE_HZ and env[8] == SAMPLE_RATE_HZ
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1 queue_ok=1",
        timeout=5,
    )

    observations = []
    for gate_words in GATE_WIDTHS:
        values = [v.decode() for v in dut.expect(CASE, timeout=60).groups()]
        assert int(values[0]) == gate_words
        loop_words, window_bytes, predicted_overload = map(int, values[1:4])
        tick_scale, expected_run, expected_duty_ppc = map(int, values[4:7])
        assert loop_words == gate_words * 2
        assert expected_run == EXPECTED_RUN
        assert window_bytes == gate_words * EXPECTED_RUN
        api = values[7:19]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         matched_steps, compared_steps, harvest_us, harvest_kbps) = map(
            int, values[19:40]
        )
        _expect_list(dut, "SYMBOLS", gate_words, r"\d+:\d+", 16)
        offsets = [
            int(v) for v in _expect_list(dut, "VIOLS", gate_words, r"\d+", 16)
        ]

        assert all(v == "ESP_OK" for v in api), api
        assert harvest_us > 0 and harvested > 0
        assert ring_bytes == RING_BYTES
        assert ring_overrun == (1 if inflight_max > ring_bytes else 0)
        # Check the firmware's model arithmetic independently.
        rate_mbps = SAMPLE_RATE_HZ // 1_000_000
        assert predicted_overload == (
            window_bytes * (rate_mbps - SPOOL_MBPS) // rate_mbps
        )

        model_says_ok = predicted_overload < RING_BYTES
        # Every break must sit exactly one window apart, or samples were lost
        # inside a window.
        measured_ok = (
            ring_overrun == 0
            and overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
        )
        observations.append(
            (gate_words, window_bytes, predicted_overload, model_says_ok,
             measured_ok, inflight_max, ring_overrun, overflow, shared_low,
             violations, matched_steps, compared_steps, harvest_kbps,
             offsets[:5])
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE050 gate={row[0]} window={row[1]}B overload={row[2]}B "
            f"model_ok={row[3]} measured_ok={row[4]} inflight={row[5]} "
            f"overrun={row[6]} overflow={row[7]} shared_low={row[8]} "
            f"violations={row[9]} steps={row[10]}/{row[11]} kbps={row[12]}"
        )
    agreement = [(r[0], r[3], r[4]) for r in observations]
    print(f"\nE050 model_vs_measured={agreement}")
    print(f"E050 absorption_observations={observations}")

    # The smallest window is the harness check.
    assert observations[0][4], f"1000 baseline is not clean: {observations[0]}"
