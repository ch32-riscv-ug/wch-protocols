"""E056: were the over-ring "clean" results an aliasing artifact?

The gray7 source repeats every 128 words, which at run 32 is 4,096 bytes of
samples - and every ring size used so far (65,536 and 131,072) is an exact
multiple of that. A ring overwrite would then write back the same value and
leave no trace. Ring sizes at half-period offsets must expose it.
"""

import re

import pexpect


BANNER = re.compile(
    rb"# EXP E056 v1 git=\S+ probe=esp32p4_parlio target=internal "
    rb"build=[^\r\n]+\r?\n"
)
CASE = re.compile(
    rb"CASE ring_size=(\d+) queue_depth=(\d+) pattern_period=(\d+) "
    rb"ring_mod_period=(\d+) gate_words=(\d+) "
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
CASES = ((65536, 64), (63488, 64), (61440, 64), (59392, 64))
PATTERN_PERIOD = 4096
# 4,194,304 harvested bytes / 192,000 per window
EXPECTED_BREAKS = 22
GATE_WORDS = 6000
WINDOW_BYTES = 192000


def _expect_list(dut, label, ring, depth, item, count):
    pattern = re.compile(
        (rf"{label} ring_size={ring} queue_depth={depth}"
         + r"((?: %s){%d})\r?\n" % (item, count)).encode()
    )
    return dut.expect(pattern, timeout=120).group(1).split()


def test_ring_period_alias(dut):
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
        pattern_period, ring_mod_period = int(values[2]), int(values[3])
        assert pattern_period == PATTERN_PERIOD
        assert ring_mod_period == ring_size % PATTERN_PERIOD
        assert int(values[4]) == GATE_WORDS
        assert int(values[5]) == WINDOW_BYTES
        api = values[8:20]
        (rmt_callbacks, rmt_stored, high_count, high_min, high_max, low_count,
         low_min, low_max, parlio_callbacks, dequeues, harvested, overflow,
         violations, shared_low, inflight_max, ring_bytes, ring_overrun,
         max_chunk_offset, matched_steps, compared_steps, harvest_us,
         harvest_kbps) = map(int, values[20:42])
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
        aliased = ring_mod_period == 0
        observations.append(
            (ring_size, ring_mod_period, aliased, clean, overflow, violations,
             EXPECTED_BREAKS, shared_low, inflight_max, max_chunk_offset,
             matched_steps, compared_steps, harvested, offsets[:6])
        )

    dut.expect_exact("DONE status=ok", timeout=10)

    for row in observations:
        print(
            f"\nE056 ring={row[0]} mod_period={row[1]} aliased={row[2]} "
            f"clean={row[3]} overflow={row[4]} violations={row[5]} "
            f"(expect {row[6]}) shared_low={row[7]} inflight={row[8]} "
            f"max_offset={row[9]} steps={row[10]}/{row[11]} "
            f"harvested={row[12]} offsets={row[13]}"
        )
    print(f"\nE056 alias_observations={observations}")

    # Both outcomes are results. The invariant is that the queue must not be
    # the limiter, or the ring question is not being tested.
    for row in observations:
        assert row[4] == 0, f"queue overflowed, ring not isolated: {row}"
