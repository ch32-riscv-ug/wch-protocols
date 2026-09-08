"""E021: spool a 64 KiB internal PARLIO ring into 1 MiB PSRAM."""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E021 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
ENV = re.compile(
    rb"ENV psram_found=(\d+) psram_size=(\d+) int_align_result=(\S+) "
    rb"int_align=(\d+) ext_align_result=(\S+) ext_align=(\d+)\r?\n"
)
BUFFERS = re.compile(
    rb"BUFFERS ring=([0-9A-F]+) ring_internal=(\d+) ring_dma=(\d+) "
    rb"destination=([0-9A-F]+) destination_external=(\d+) queue=(\d+)\r?\n"
)
CASE = re.compile(
    rb"CASE run=(\d+) result=(\S+) config=(\S+) enable=(\S+) receive=(\S+) start=(\S+) "
    rb"stop=(\S+) disable=(\S+) sync=(\S+) callbacks=(\d+) dequeues=(\d+) "
    rb"callback_bytes=(\d+) copied=(\d+) extra_bytes=(\d+) overflows=(\d+) "
    rb"min_chunk=(\d+) max_chunk=(\d+) max_queue=(\d+) capture_us=(\d+) "
    rb"rate_mbps_milli=(\d+) sync_us=(\d+) max_error_ppm=(\d+) "
    rb"min_edges=(\d+) max_edges=(\d+)\r?\n"
)
DESTINATION_SIZE = 1024 * 1024
RUNS = 3
RATE_MIN_MBPS_MILLI = 7000
RATE_MAX_MBPS_MILLI = 9000
CAPTURE_US_MIN = 50000
CAPTURE_US_MAX = 500000
MIN_EDGES = 20000


def test_parlio_psram_spool(dut):
    """Require three overflow-free 1 MiB captures with valid PWM data."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=5)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    match = dut.expect(ENV, timeout=5)
    values = [value.decode() for value in match.groups()]
    assert int(values[0]) == 1 and int(values[1]) >= DESTINATION_SIZE
    assert values[2] == "ESP_OK" and int(values[3]) == 64
    assert values[4] == "ESP_OK" and int(values[5]) == 128
    match = dut.expect(BUFFERS, timeout=5)
    ring, internal, dma, destination, external, queue = (
        value.decode() for value in match.groups()
    )
    assert int(ring, 16) != 0 and int(destination, 16) != 0
    assert (int(internal), int(dma), int(external), int(queue)) == (1, 1, 1, 1)
    dut.expect_exact("CONFIG pwm=ESP_OK", timeout=5)

    observations = []
    for expected_run in range(RUNS):
        match = dut.expect(CASE, timeout=10)
        values = [value.decode() for value in match.groups()]
        run = int(values[0])
        results = values[1:9]
        metrics = [int(value) for value in values[9:]]
        (callbacks, dequeues, callback_bytes, copied, extra_bytes, overflows,
         min_chunk, max_chunk, max_queue, capture_us, rate_mbps_milli,
         sync_us, max_error, min_edges, max_edges) = metrics
        assert run == expected_run
        assert all(result == "ESP_OK" for result in results)
        assert callbacks >= dequeues > 1
        assert callback_bytes >= copied == DESTINATION_SIZE
        assert 0 <= extra_bytes < 65536
        assert overflows == 0
        assert 0 < min_chunk <= max_chunk <= 4092
        assert max_queue < 64
        assert CAPTURE_US_MIN <= capture_us <= CAPTURE_US_MAX
        assert RATE_MIN_MBPS_MILLI <= rate_mbps_milli <= RATE_MAX_MBPS_MILLI
        assert sync_us > 0
        assert max_error <= 30000
        assert min_edges >= MIN_EDGES and max_edges >= min_edges
        observations.append(metrics)

    dut.expect_exact("DONE status=ok", timeout=5)
    print(
        f"\nE021 callbacks={[row[0] for row in observations]} "
        f"dequeues={[row[1] for row in observations]} "
        f"extra_bytes={[row[4] for row in observations]} "
        f"max_queue={[row[8] for row in observations]} "
        f"capture_us={[row[9] for row in observations]} "
        f"rate_MBps={[row[10] / 1000 for row in observations]} "
        f"max_error_ppm={max(row[12] for row in observations)} "
        f"edges={min(row[13] for row in observations)}.."
        f"{max(row[14] for row in observations)}"
    )
