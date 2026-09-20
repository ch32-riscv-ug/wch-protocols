#!/usr/bin/env python3
"""Extract the LinkE V003 flash-loader DMI sequence from the real wire capture."""

from __future__ import annotations

import array
import statistics
import sys
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CAPTURE = ROOT / "captures/fixtures/wire-flash-v003-x035-2026-09-11/wire-v003.sr"
RATE = 50_000_000


def bits_to_int(bits: list[int]) -> int:
    value = 0
    for bit in bits:
        value = (value << 1) | bit
    return value


def load_samples() -> array.array:
    with zipfile.ZipFile(CAPTURE) as archive:
        chunks = sorted(
            (name for name in archive.namelist() if name.startswith("logic-1-")),
            key=lambda name: int(name.rsplit("-", 1)[1]),
        )
        result = array.array("H")
        for name in chunks:
            result.frombytes(archive.read(name))
    if sys.byteorder != "little":
        result.byteswap()
    return result


def decode_frames(samples: array.array) -> list[tuple[int, int, int, int, int]]:
    pulses: list[tuple[int, int, int]] = []
    state = samples[0] & 1
    falling = None
    for index, sample in enumerate(samples[1:], 1):
        current = sample & 1
        if state and not current:
            falling = index
        elif not state and current and falling is not None:
            pulses.append((falling, index, index - falling))
            falling = None
        state = current

    groups: list[list[tuple[int, int, int]]] = []
    start = 0
    for index in range(len(pulses) - 1):
        if pulses[index + 1][0] - pulses[index][0] > 200:
            groups.append(pulses[start : index + 1])
            start = index + 1
    groups.append(pulses[start:])

    frames = []
    for group in groups:
        if len(group) != 41:
            continue
        bits = [int(pulse[2] < 25) for pulse in group]
        if bits[0] != 1:
            continue
        frames.append(
            (group[0][0], group[-1][1], bits_to_int(bits[1:8]), bits[8], bits_to_int(bits[9:]))
        )
    return frames


def percentile(values: list[int], fraction: float) -> int:
    values = sorted(values)
    return values[round((len(values) - 1) * fraction)]


def main() -> None:
    frames = decode_frames(load_samples())
    pattern_start = next(
        index for index, frame in enumerate(frames) if frame[2:] == (0x04, 1, 0x03020100)
    )

    # LinkE establishes a two-instruction RAM-write progbuf, then streams DATA1,
    # DATA0, COMMAND for each word.  Assert the first captured word exactly.
    expected_prefix = [
        (0x05, 1, 0x20000200),
        (0x04, 1, 0x03020100),
        (0x20, 1, 0x7B251073),
        (0x21, 1, 0x7B359073),
        (0x22, 1, 0xE0000537),
        (0x23, 1, 0x0F852583),
        (0x24, 1, 0x0F452503),
        (0x25, 1, 0x2573C188),
        (0x26, 1, 0x25F37B20),
        (0x27, 1, 0x90027B30),
        (0x17, 1, 0x00040000),
        (0x05, 1, 0x20000204),
        (0x04, 1, 0x07060504),
    ]
    actual = [frame[2:] for frame in frames[pattern_start - 1 : pattern_start + 12]]
    assert actual == expected_prefix

    adjacent_gaps_ns = [
        (frames[index][0] - frames[index - 1][1]) * 20
        for index in range(pattern_start, pattern_start + 260)
    ]
    normal_gaps = [gap for gap in adjacent_gaps_ns if gap < 20_000]

    resume = next(
        index for index in range(pattern_start, len(frames))
        if frames[index][2:] == (0x10, 1, 0x40000001)
    )
    polls = []
    index = resume + 1
    while index < len(frames) and frames[index][2:4] == (0x11, 0):
        polls.append(frames[index])
        index += 1
    assert polls
    poll_gaps_ns = [
        (polls[index][0] - polls[index - 1][1]) * 20 for index in range(1, len(polls))
    ]

    print(f"capture={CAPTURE.relative_to(ROOT)}")
    print(f"decoded_frames={len(frames)}")
    print("loader_data_base=0x20000200")
    print("ram_write_command=0x00040000")
    print("resume=DMCONTROL(0x10)<-0x40000001")
    print(
        "adjacent_gap_ns="
        f"p50:{percentile(normal_gaps, .5)} p95:{percentile(normal_gaps, .95)} "
        f"mean:{statistics.mean(normal_gaps):.1f}"
    )
    print(
        "dmstatus_poll_gap_ns="
        f"count:{len(poll_gaps_ns)} p50:{percentile(poll_gaps_ns, .5)} "
        f"range:{min(poll_gaps_ns)}..{max(poll_gaps_ns)}"
    )


if __name__ == "__main__":
    main()
