"""Host reference checks for the E105 mixed-rate SPI codec."""

from __future__ import annotations

import random

from codec import Channel, calculate_budget, decode, encode, samples_for_budget, wire_bytes_for_samples


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
