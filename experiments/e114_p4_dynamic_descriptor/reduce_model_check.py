"""Offline check of the E114 device encoder's decimated planes against the E105 reference.

Models, bit for bit, what `encodeDynamicBlock` does after the fast lanes: the
packed-word bucket reductions (`dynWordFold` / `dynReduceQuantity`) and the
per-channel bit gathering, then compares with `codec.encode_mixed` from E105 for
random descriptors. The fast lanes are excluded: the reference stores raw lanes
plane-major, the device wire is sample-major (the host's Reference class covers
that part during capture). Run: `python3 reduce_model_check.py [trials]`.
"""

from __future__ import annotations

import random
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "e105_p4_spi_mixed_rate_codec"))
from codec import Channel, encode_mixed  # noqa: E402

M32 = 0xFFFFFFFF
BLOCK = 128


def word_fold(wide: bool, q: int, w: int) -> int:
    """OR/AND over the samples in one word, rise/fall over transitions inside it."""
    if wide:
        if q == 0:
            return (w | (w >> 16)) & 0xFFFF
        if q == 1:
            return (w & (w >> 16)) & 0xFFFF
        if q == 2:
            return ((~w) & (w >> 16)) & 0xFFFF
        return (w & ~(w >> 16)) & 0xFFFF
    if q == 0:
        x = w | (w >> 16)
        x |= x >> 8
    elif q == 1:
        x = w & (w >> 16)
        x &= x >> 8
    elif q == 2:
        x = (~w & M32) & (w >> 8)
        x |= x >> 16
        x |= x >> 8
    else:
        x = (w & (~(w >> 8) & M32)) & 0x00FFFFFF
        x |= x >> 16
        x |= x >> 8
    return x & 0xFF


def reduce_quantity(wide: bool, q: int, samples: list[int], max_log2d: int, need_level1: bool) -> dict[int, list[int]]:
    if wide:
        words = [samples[2 * j] | (samples[2 * j + 1] << 16) for j in range(64)]
    else:
        words = [
            samples[4 * j] | (samples[4 * j + 1] << 8) | (samples[4 * j + 2] << 16) | (samples[4 * j + 3] << 24)
            for j in range(32)
        ]
    word_count = 64 if wide else 32
    word_level = 1 if wide else 2
    edge = q >= 2
    levels: dict[int, list[int]] = {}
    base = [0] * word_count
    cross = [0] * word_count
    prev = 0
    for j in range(word_count):
        w = words[j]
        base[j] = word_fold(wide, q, w)
        if edge:
            if wide:
                cross[j] = ((((~prev) & M32) >> 16) & w if q == 2 else (prev >> 16) & (~w & M32)) & 0xFFFF
            else:
                cross[j] = ((((~prev) & M32) >> 24) & w if q == 2 else (prev >> 24) & (~w & M32)) & 0xFF
            prev = w
    levels[word_level] = base
    if not wide and need_level1:
        level1 = [0] * 64
        for j in range(word_count):
            w = words[j]
            y = [w | (w >> 8), w & (w >> 8), (~w & M32) & (w >> 8), w & (~(w >> 8) & M32)][q]
            level1[2 * j] = y & 0xFF
            level1[2 * j + 1] = (y >> 16) & 0xFF
        levels[1] = level1
    for level in range(word_level + 1, max_log2d + 1):
        buckets = BLOCK >> level
        low = levels[level - 1]
        high = [0] * buckets
        shift = level - word_level - 1
        for k in range(buckets):
            v = (low[2 * k] & low[2 * k + 1]) if q == 1 else (low[2 * k] | low[2 * k + 1])
            if edge:
                v |= cross[(2 * k + 1) << shift]
            high[k] = v
        levels[level] = high
    return levels


def encode_decimated(wide: bool, chans: list[tuple[int, str, int, int, int]], samples: list[int]) -> bytes:
    out = bytearray()
    acc = 0
    bits = 0

    def append(v: int, n: int) -> None:
        nonlocal acc, bits
        acc |= v << bits
        bits += n
        while bits >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            bits -= 8

    dec = [c for c in chans if c[1] != "raw"]
    max_log2d = max([c[2] for c in dec if c[2] >= 1 and c[1] != "hold"], default=0)
    need_level1 = any(c[2] == 1 and c[1] != "hold" for c in dec)
    levels: dict[int, dict[int, list[int]]] = {}
    for q, (mode, active) in enumerate([("any", 1), ("any", 0), ("edge", 1), ("edge", 0)]):
        if any(c[1] == mode and c[4] == active and c[2] >= 1 for c in dec):
            levels[q] = reduce_quantity(wide, q, samples, max_log2d, need_level1)
    for lane, mode, log2d, phase, active in dec:
        d = 1 << log2d
        buckets = BLOCK // d
        if mode == "hold" or (mode == "any" and log2d == 0):
            for k in range(buckets):
                append((samples[k * d + phase] >> lane) & 1, 1)
        elif mode == "any":
            vec = levels[0 if active else 1][log2d]
            for k in range(buckets):
                append((vec[k] >> lane) & 1, 1)
        else:
            vec = [0] * BLOCK if log2d == 0 else levels[2 if active else 3][log2d]
            for k in range(buckets):
                level = (samples[k * d + d - 1] >> lane) & 1
                append(level | (((vec[k] >> lane) & 1) << 1), 2)
    if bits:
        out.append(acc & 0xFF)
    return bytes(out)


def main() -> int:
    trials = int(sys.argv[1]) if len(sys.argv) > 1 else 1500
    rng = random.Random(7)
    bad = 0
    combos: set[tuple[str, int]] = set()
    for _ in range(trials):
        wide = rng.random() < 0.5
        width = 16 if wide else 8
        fast = rng.randint(0, min(width - 1, 8))
        dec_count = rng.randint(1, width - fast)
        chans = [(i, "raw", 0, 0, 0) for i in range(fast)]
        for i in range(dec_count):
            mode = rng.choice(["hold", "any", "edge"])
            log2d = rng.randint(0, 7)
            chans.append((fast + i, mode, log2d, rng.randrange(1 << log2d), rng.randint(0, 1)))
            combos.add((mode, log2d))
        samples = [rng.randrange(1 << width) for _ in range(BLOCK)]
        if rng.random() < 0.5:  # runs of constant values so edges are sparse
            for i in range(1, BLOCK):
                if rng.random() < 0.7:
                    samples[i] = samples[i - 1]
        names = {"raw": "raw", "hold": "decimate_hold", "any": "any_active", "edge": "edge_latch"}
        reference = encode_mixed(samples, [Channel(names[m], 1 << l, ph, ac) for _, m, l, ph, ac in chans], BLOCK)
        got = encode_decimated(wide, chans, samples)
        if reference[16 * fast :] != got:
            bad += 1
            if bad <= 5:
                print("MISMATCH", wide, [c for c in chans if c[1] != "raw"])
    print(f"trials={trials} bad={bad} mode_log2d_combos={len(combos)}/24")
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
