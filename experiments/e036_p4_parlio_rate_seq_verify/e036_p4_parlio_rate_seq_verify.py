"""E036: re-verify the 8-channel raw batch boundary at sample level.

E031-E033 checked capture data only through per-lane duty and edge counts on a
stationary 100 kHz PWM source, and reported drops only when the 64-deep chunk
queue overflowed. Here the source is a PARLIO TX gray-code ramp, so every lost
or repeated sample shows up as a broken +1 step, and the ring overrun is read
directly from the unread byte count.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E036 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) parlio_groups=(\d+) "
    rb"tx_units=(\d+) rx_units=(\d+) max_tx_width=(\d+) max_rx_width=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE rx_rate_hz=(\d+) tx_rate_hz=(\d+) result=(\S+) config=(\S+) "
    rb"source=(\S+) source_start=(\S+) enable=(\S+) receive=(\S+) start=(\S+) "
    rb"stop=(\S+) disable=(\S+) sync=(\S+) target_bytes=(\d+) copied=(\d+) "
    rb"callbacks=(\d+) dequeues=(\d+) extra_bytes=(\d+) overflows=(\d+) "
    rb"max_queue=(\d+) inflight_max=(\d+) ring_bytes=(\d+) ring_overrun=(\d+) "
    rb"runs=(\d+) run_min=(\d+) run_max=(\d+) violations=(\d+) "
    rb"first_violation=(\d+) capture_us=(\d+) sampling_kbps=(\d+) "
    rb"spool_kbps=(\d+)\r?\n"
)
RATES = (20, 80, 96, 100, 104, 112, 120)
SAMPLE_COUNT = 1024 * 1024
SOURCE_DIVIDER = 4


def test_parlio_rate_seq_verify(dut):
    """Record, for every rate, whether the ring overran and the ramp broke."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    env = tuple(map(int, dut.expect(ENV, timeout=5).groups()))
    assert env[0] == 1 and env[1] >= 2 * 1024 * 1024
    assert env[2:] == (1, 1, 1, 16, 16)
    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 queue_ok=1", timeout=5
    )

    observations = []
    for rate_mhz in RATES:
        values = [v.decode() for v in dut.expect(CASE, timeout=60).groups()]
        rx_rate, tx_rate = int(values[0]), int(values[1])
        assert rx_rate == rate_mhz * 1_000_000
        assert tx_rate == rx_rate // SOURCE_DIVIDER
        result, api = values[2], values[3:12]
        metrics = list(map(int, values[12:]))
        (target, copied, callbacks, dequeues, extra, overflows, max_queue,
         inflight_max, ring_bytes, ring_overrun, runs, run_min, run_max,
         violations, first_violation, capture_us, sampling_kbps,
         spool_kbps) = metrics

        assert target == SAMPLE_COUNT
        assert result == "ESP_OK" and all(v == "ESP_OK" for v in api)
        assert copied == SAMPLE_COUNT
        assert callbacks >= dequeues > 1
        assert ring_bytes == 64 * 1024
        assert capture_us > 0 and sampling_kbps > 0 and spool_kbps > 0
        assert ring_overrun == (1 if inflight_max > ring_bytes else 0)
        # A gray ramp at a quarter of the sample rate must collapse into
        # roughly SAMPLE_COUNT / 4 runs; far off means the source is wrong,
        # not that the capture dropped.
        assert SAMPLE_COUNT // 8 < runs < SAMPLE_COUNT // 2
        assert (violations == 0) == (first_violation == 0)

        clean = violations == 0 and ring_overrun == 0 and overflows == 0
        observations.append(
            (rate_mhz, clean, ring_overrun, inflight_max, max_queue, overflows,
             violations, first_violation, runs, run_min, run_max,
             sampling_kbps, spool_kbps)
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    # The lowest rate is the harness check: source, capture and verifier must
    # agree there, otherwise nothing above it can be read.
    baseline = observations[0]
    assert baseline[1], f"20 MHz baseline is not clean: {baseline}"
    print(f"\nE036 seq_observations={observations}")
