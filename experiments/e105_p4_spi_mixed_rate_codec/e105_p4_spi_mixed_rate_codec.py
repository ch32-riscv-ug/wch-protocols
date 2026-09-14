"""Host reference checks for the E105 mixed-rate SPI codec."""

from __future__ import annotations

import random

from codec import (
    Channel,
    calculate_budget,
    decode,
    decode_mixed,
    encode,
    encode_mixed,
    samples_for_budget,
    wire_bytes_for_samples,
)


def unpack(raw: bytes) -> list[int]:
    return [sample for value in raw for sample in (value & 15, value >> 4)]


def pack(samples: list[int]) -> bytes:
    return bytes(samples[i] | samples[i + 1] << 4 for i in range(0, len(samples), 2))


def expected_cs(samples: list[int], policy: str) -> list[int]:
    result = samples.copy()
    for start in range(0, len(samples), 8):
        source = [(sample >> 3) & 1 for sample in samples[start : start + 8]]
        cs = source[0] if policy == "sample0" else int(all(source))
        for index in range(start, start + 8):
            result[index] = (result[index] & 7) | (cs << 3)
    return result


def test_reference_round_trip() -> None:
    rng = random.Random(105)
    samples = [rng.randrange(16) for _ in range(64 * 257)]
    raw = pack(samples)
    for policy in ("sample0", "active_low_any"):
        wire = encode(raw, policy)
        restored = unpack(decode(wire))
        assert len(wire) == 25 * 257
        assert all((a & 7) == (b & 7) for a, b in zip(samples, restored))
        assert restored == expected_cs(samples, policy)

    assert wire_bytes_for_samples(60_000_000) == 23_437_500
    assert samples_for_budget(23_437_500) == 60_000_000
    spi = calculate_budget(
        60_000_000,
        [Channel("raw"), Channel("raw"), Channel("raw"), Channel("any_active", 8)],
    )
    assert (spi.block_samples, spi.wire_bytes_per_block) == (64, 25)
    assert spi.wire_bytes_per_second == 23_437_500
    controls = calculate_budget(
        60_000_000,
        [
            Channel("raw"),
            Channel("raw"),
            Channel("any_active", 8),
            Channel("edge_latch", 64),
            Channel("decimate_hold", 1024),
        ],
    )
    print("E105_REFERENCE PASS blocks=257 fast_mismatches=0 cs_rule_mismatches=0")
    print("60Msps wire=23.4375MB/s raw=30.0000MB/s reduction=21.875%")
    print(
        f"generic block={controls.block_samples} samples "
        f"wire={controls.wire_bytes_per_block} bytes "
        f"rate={controls.wire_bytes_per_second / 1e6:.6f}MB/s"
    )


def test_three_fast_eight_slow_share_one_byte() -> None:
    channels = [Channel("raw") for _ in range(3)] + [
        Channel("decimate_hold", 64) for _ in range(8)
    ]
    budget = calculate_budget(60_000_000, channels, block_samples=64)
    assert budget.payload_bits_per_block == 200
    assert budget.padding_bits_per_block == 0
    assert budget.wire_bytes_per_block == 25
    assert budget.wire_bytes_per_second == 23_437_500

    rng = random.Random(105_308)
    samples = [rng.randrange(1 << 11) for _ in range(64 * 31)]
    wire = encode_mixed(samples, channels, block_samples=64)
    restored = decode_mixed(wire, channels, block_samples=64)
    assert len(wire) == 25 * 31

    for block_start in range(0, len(samples), 64):
        slow_at_start = samples[block_start] & ~0x07
        for index in range(block_start, block_start + 64):
            assert (restored[index] & 0x07) == (samples[index] & 0x07)
            assert (restored[index] & ~0x07) == slow_at_start

        # Three 64-bit fast planes consume 24 bytes.  The eight one-bit slow
        # planes share byte 24 instead of each consuming a padded byte.
        block_number = block_start // 64
        assert wire[block_number * 25 + 24] == (samples[block_start] >> 3) & 0xFF


def test_padding_occurs_once_per_whole_block() -> None:
    channels = [Channel("raw") for _ in range(3)] + [
        Channel("decimate_hold", 64) for _ in range(7)
    ]
    budget = calculate_budget(60_000_000, channels, block_samples=64)
    assert budget.payload_bits_per_block == 199
    assert budget.padding_bits_per_block == 1
    assert budget.wire_bytes_per_block == 25

    # The automatic independent block is allowed to grow just enough to remove
    # even that single trailing pad bit.
    aligned = calculate_budget(60_000_000, channels)
    assert aligned.block_samples == 512
    assert aligned.padding_bits_per_block == 0


def test_power_of_two_decimation_round_trip() -> None:
    """All UI ratios through 1/64 preserve fast lanes and obey slow policy."""
    for divisor in (2, 4, 8, 16, 32, 64):
        channels = [Channel("raw") for _ in range(3)] + [
            Channel("decimate_hold", divisor),
            Channel("any_active", divisor, active_level=0),
        ]
        block_samples = divisor * 8
        samples: list[int] = []
        for index in range(block_samples * 2):
            bucket_at = index % divisor
            fast = (index ^ (index >> 1)) & 7
            held = ((index // divisor) & 1) << 3
            # Normally inactive high, with one active-low point per other
            # bucket. any_active must retain it after coarse reconstruction.
            active = int(not ((index // divisor) & 1 and bucket_at == divisor // 2)) << 4
            samples.append(fast | held | active)

        wire = encode_mixed(samples, channels, block_samples=block_samples)
        restored = decode_mixed(wire, channels, block_samples=block_samples)
        assert all((before & 7) == (after & 7) for before, after in zip(samples, restored))
        for start in range(0, len(samples), divisor):
            bucket = samples[start : start + divisor]
            assert all((value & 8) == (bucket[0] & 8) for value in restored[start : start + divisor])
            expected_active = 0 if any((value & 16) == 0 for value in bucket) else 16
            assert all((value & 16) == expected_active for value in restored[start : start + divisor])


def test_sixteen_channel_40m_profile() -> None:
    """3 full + 1 D8 + 12 D64 fits a 135 Mbps safe USB budget."""
    channels = [Channel("raw") for _ in range(3)]
    channels += [Channel("any_active", 8, active_level=0)]
    channels += [Channel("decimate_hold", 64) for _ in range(12)]

    padded = calculate_budget(40_000_000, channels, block_samples=64)
    assert padded.payload_bits_per_block == 212
    assert padded.padding_bits_per_block == 4
    assert padded.wire_bytes_per_second == 16_875_000  # 135 Mbps

    aligned = calculate_budget(40_000_000, channels)
    assert aligned.block_samples == 128
    assert aligned.payload_bits_per_block == 424
    assert aligned.padding_bits_per_block == 0
    assert aligned.wire_bytes_per_block == 53
    assert aligned.wire_bytes_per_second == 16_562_500  # 132.5 Mbps

    samples = [
        ((index ^ (index >> 1)) & 7)
        | (int(index % 8 != 4) << 3)
        | sum((((index // 64) + lane) & 1) << lane for lane in range(4, 16))
        for index in range(128 * 3)
    ]
    wire = encode_mixed(samples, channels)
    restored = decode_mixed(wire, channels)
    assert len(wire) == 53 * 3
    assert all((before & 7) == (after & 7) for before, after in zip(samples, restored))
    # The D8 active-low pulse is preserved and expanded over its bucket.
    for start in range(0, len(restored), 8):
        assert all((sample & 8) == 0 for sample in restored[start : start + 8])
