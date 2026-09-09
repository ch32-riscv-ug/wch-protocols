"""E055: is the ring or the chunk queue the buffer in a gated capture?

E036's ungated spool corrupted as soon as unread ring bytes passed the ring
size, but E050's gated capture stayed clean at 1.4-1.6x the ring. The 64-entry
queue holds about 258 KiB of chunk pointers, so shortening it to 8 should break
the very case that survives today - and doubling the ring should change nothing
if the ring is not the constraint.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E055 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
CASE = re.compile(
    rb"CASE ring_size=(\d+) queue_depth=(\d+) gate_words=(\d+) "
    rb"window_bytes=(\d+) expected_high=(\d+) expected_duty_ppc=(\d+) "
    rb"config=(\S+) delimiter=(\S+) rmt=(\S+) rmt_enable=(\S+) source=(\S+) "
    rb"source_start=(\S+) rmt_receive=(\S+) enable=(\S+) receive=(\S+) "
    rb"disable=(\S+) rmt_disable=(\S+) sync=(\S+) rmt_callbacks=(\d+) "
    rb"rmt_stored=(\d+) high_count=(\d+) high_min=(\d+) high_max=(\d+) "
    rb"low_count=(\d+) low_min=(\d+) low_max=(\d+) parlio_callbacks=(\d+) "
    rb"dequeues=(\d+) harvested=(\d+) overflow=(\d+) violations=(\d+) "
    rb"shared_low=(\d+) inflight_max=(\d+) ring_bytes=(\d+) "
    rb"ring_overrun=(\d+) max_chunk_offset=(\d+) matched_steps=(\d+) "
    rb"compared_steps=(\d+) harvest_us=(-?\d+) harvest_kbps=(\d+)\r?\n"
)
CASES = ((65536, 64), (65536, 8), (131072, 64), (131072, 8))
GATE_WORDS = 6000
WINDOW_BYTES = 192000


def _expect_list(dut, label, ring, depth, item, count):
    pattern = re.compile(
        (rf"{label} ring_size={ring} queue_depth={depth}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=120).group(1).split()


def test_gated_buffer_source(dut):
    """Vary ring size and queue depth independently and see which one bites."""
    for attempt in range(3):
        dut.write("?")
        try:
            dut.expect(BANNER, timeout=20)
            break
        except pexpect.exceptions.TIMEOUT:
            if attempt == 2:
                raise

    dut.expect_exact(
        "BUFFERS ring_ok=1 source_ok=1 destination_ok=1 rmt_ok=1", timeout=10
    )

    observations = []
    for ring_size, queue_depth in CASES:
        values = [v.decode() for v in dut.expect(CASE, timeout=120).groups()]
        assert (int(values[0]), int(values[1])) == (ring_size, queue_depth)
        assert int(values[2]) == GATE_WORDS
        assert int(values[3]) == WINDOW_BYTES
        api = values[6:18]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         max_chunk_offset, matched_steps, compared_steps, harvest_us,
         harvest_kbps) = map(int, values[18:40])
        _expect_list(dut, "SYMBOLS", ring_size, queue_depth, r"\d+:\d+", 16)
        _expect_list(dut, "VIOLS", ring_size, queue_depth, r"\d+", 16)
        _expect_list(dut, "RMTCB", ring_size, queue_depth, r"-?\d+:\d+", 16)
        offsets = [
            int(v) for v in
            _expect_list(dut, "OFFSETS", ring_size, queue_depth, r"\d+", 16)
        ]

        assert all(v == "ESP_OK" for v in api[:9]), api
        assert ring_bytes == ring_size
        assert harvest_us > 0
        # Chunk pointers must stay inside the ring we handed over.
        assert max_chunk_offset < ring_size, (max_chunk_offset, ring_size)
        clean = (
            overflow == 0
            and shared_low == 0
            and compared_steps > 0
            and matched_steps == compared_steps
        )
        observations.append(
            (ring_size, queue_depth, clean, overflow, violations, shared_low,
             inflight_max, ring_overrun, max_chunk_offset, matched_steps,
             compared_steps, harvested, harvest_kbps, offsets[:8])
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE055 ring={row[0]} queue={row[1]} clean={row[2]} "
            f"overflow={row[3]} violations={row[4]} shared_low={row[5]} "
            f"inflight={row[6]} ring_overrun={row[7]} "
            f"max_offset={row[8]} steps={row[9]}/{row[10]} "
            f"harvested={row[11]} offsets={row[13]}"
        )
    print(f"\nE055 buffer_source_observations={observations}")

    # The control repeats E050 and is the harness check.
    assert observations[0][2], f"control is not clean: {observations[0]}"
