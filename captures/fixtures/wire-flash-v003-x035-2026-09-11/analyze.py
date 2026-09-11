#!/usr/bin/env python3
"""Reproduce the structural checks in README.ja.md using only the stdlib."""

from __future__ import annotations

import array
import json
import statistics
import sys
import zipfile
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parent
RATE = 50_000_000


def read_sr(name: str) -> array.array:
    with zipfile.ZipFile(ROOT / name) as archive:
        chunks = sorted(
            (item for item in archive.namelist() if item.startswith("logic-1-")),
            key=lambda item: int(item.rsplit("-", 1)[1]),
        )
        samples = array.array("H")
        for chunk in chunks:
            samples.frombytes(archive.read(chunk))
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def bits_to_int(bits: list[int]) -> int:
    value = 0
    for bit in bits:
        value = (value << 1) | bit
    return value


def pattern_words(count: int) -> list[int]:
    return [
        int.from_bytes(bytes(((4 * index + byte) % 256 for byte in range(4))), "little")
        for index in range(count)
    ]


def percentile(values: list[int], fraction: float) -> int:
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def format_ns_stats(values: list[int]) -> str:
    return (
        f"min/median/p99/max="
        f"{percentile(values, 0) * 20}/"
        f"{percentile(values, 0.5) * 20}/"
        f"{percentile(values, 0.99) * 20}/"
        f"{percentile(values, 1) * 20} ns, "
        f"mean={statistics.mean(values) * 20:.1f} ns"
    )


def find_subsequence(records: list[tuple], expected: list[int], value_index: int) -> int:
    wanted = len(expected)
    for start in range(len(records) - wanted + 1):
        if [record[value_index] for record in records[start : start + wanted]] == expected:
            return start
    raise AssertionError("known pattern was not found")


def split_by_start_gap(items: list, gap_samples: int, start_index) -> list[list]:
    groups: list[list] = []
    start = 0
    for index in range(len(items) - 1):
        if start_index(items[index + 1]) - start_index(items[index]) > gap_samples:
            groups.append(items[start : index + 1])
            start = index + 1
    groups.append(items[start:])
    return groups


def analyze_usb(name: str) -> tuple[int, list[tuple[int, int, int, int]]]:
    rows = [json.loads(line) for line in (ROOT / name).read_text().splitlines()]
    transfers = [row for row in rows if "seq" in row]
    dmi: list[tuple[int, int, int, int]] = []
    for index, row in enumerate(transfers):
        payload = bytes.fromhex(row["data"])
        if row["dir"] != "out" or len(payload) < 9 or payload[1] != 0x08:
            continue
        response = bytes.fromhex(transfers[index + 1]["data"])
        dmi.append(
            (
                payload[3],
                int.from_bytes(payload[4:8], "big"),
                payload[8],
                int.from_bytes(response[4:8], "big"),
            )
        )
    return len(transfers), dmi


def analyze_v003() -> None:
    samples = read_sr("wire-v003.sr")
    pulses: list[tuple[int, int, int]] = []
    state = samples[0] & 1
    falling: int | None = None
    for index, sample in enumerate(samples[1:], 1):
        current = sample & 1
        if state and not current:
            falling = index
        elif not state and current and falling is not None:
            pulses.append((falling, index, index - falling))
            falling = None
        state = current

    groups = split_by_start_gap(pulses, 200, lambda pulse: pulse[0])
    frames = []
    for group in groups:
        if len(group) != 41:
            continue
        bits = [int(pulse[2] < 25) for pulse in group]
        if bits[0] != 1:
            continue
        frames.append(
            (
                group,
                bits_to_int(bits[1:8]),
                bits[8],
                bits_to_int(bits[9:]),
            )
        )

    short = [pulse[2] for frame, *_ in frames for pulse in frame if pulse[2] < 25]
    long = [pulse[2] for frame, *_ in frames for pulse in frame if pulse[2] >= 25]
    read_turnaround = [
        frame[9][0] - frame[8][1] for frame, _, rw, _ in frames if rw == 0
    ]
    write_boundary = [
        frame[9][0] - frame[8][1] for frame, _, rw, _ in frames if rw == 1
    ]

    writes = [record for record in frames if record[1:3] == (4, 1)]
    expected_page = pattern_words(256)
    write_runs = []
    search_at = 0
    while search_at <= len(writes) - len(expected_page):
        try:
            relative = find_subsequence(writes[search_at:], expected_page, 3)
        except AssertionError:
            break
        start = search_at + relative
        run = writes[start : start + 256]
        write_runs.append(run)
        search_at = start + 256
    assert len(write_runs) == 4

    read_records = []
    for group in groups:
        if not (0.888 <= group[0][0] / RATE <= 0.951):
            continue
        bits = [int(pulse[2] < 25) for pulse in group]
        if (
            len(group) == 41
            and bits[0] == 1
            and bits_to_int(bits[1:8]) == 4
            and bits[8] == 0
        ):
            read_records.append((group, "full", bits_to_int(bits[9:])))
        elif len(group) == 33 and bits[0] == 0:
            read_records.append((group, "short", bits_to_int(bits[1:])))

    expected_image = pattern_words(1024)
    read_start = find_subsequence(read_records, expected_image, 2)
    read_records = read_records[read_start : read_start + 1024]
    assert [record[2] for record in read_records] == expected_image
    layouts = {
        "".join("F" if record[1] == "full" else "s" for record in read_records[index : index + 16])
        for index in range(0, 1024, 16)
    }
    assert layouts == {"FssssssssssssssF"}

    transfers, usb_dmi = analyze_usb("usb-v003.ndjson")
    print("V003 / SWIO")
    print(f"  samples={len(samples)}, duration={len(samples) / RATE:.8f} s")
    print(f"  pulses={len(pulses)}, decoded 41-pulse frames={len(frames)}")
    print(f"  bit 1 LOW: {format_ns_stats(short)}")
    print(f"  bit 0 LOW: {format_ns_stats(long)}")
    print(f"  read R/W-to-data turnaround: {format_ns_stats(read_turnaround)}")
    print(f"  write R/W-to-data boundary: {format_ns_stats(write_boundary)}")
    for number, run in enumerate(write_runs, 1):
        start = run[0][0][0][0] / RATE
        end = run[-1][0][-1][1] / RATE
        print(
            f"  write page {number}: {start:.8f}-{end:.8f} s, "
            f"1 KiB exact, {1 / (end - start):.2f} KiB/s"
        )
    start = read_records[0][0][0][0] / RATE
    end = read_records[-1][0][-1][1] / RATE
    print(
        f"  readback: {start:.8f}-{end:.8f} s, 4096 bytes exact, "
        f"full/short={Counter(record[1] for record in read_records)}, "
        f"layout={next(iter(layouts))}, {4 / (end - start):.2f} KiB/s"
    )
    print(f"  USB transfers={transfers}, DMI={usb_dmi}")


def rising_edges(samples: array.array) -> list[int]:
    result = []
    state = samples[0] & 1
    for index, sample in enumerate(samples[1:], 1):
        current = sample & 1
        if not state and current:
            result.append(index)
        state = current
    return result


def decode_x035_frame(samples: array.array, clocks: list[int]) -> tuple[int, int, str, int, str]:
    bits = [(samples[index] >> 1) & 1 for index in clocks]
    return (
        bits_to_int(bits[:7]),
        bits[7],
        "".join(map(str, bits[8:14])),
        bits_to_int(bits[14:46]),
        "".join(map(str, bits[46:])),
    )


def analyze_x035() -> None:
    samples = read_sr("wire-x035.sr")
    rising = rising_edges(samples)
    fast_groups = split_by_start_gap(rising, 100, lambda item: item)
    frames = []
    bursts = []
    for group in fast_groups:
        if len(group) == 53:
            frames.append((group, *decode_x035_frame(samples, group)))
        elif len(group) == 585:
            bursts.append(group)

    writes = [record for record in frames if record[1:3] == (4, 1)]
    expected_image = pattern_words(1024)
    write_start = find_subsequence(writes, expected_image, 4)
    writes = writes[write_start : write_start + 1024]
    assert {record[3] for record in writes} == {"000000"}
    assert {record[5] for record in writes} == {"0000110"}

    frame_bits = [[(samples[edge] >> 1) & 1 for edge in record[0]] for record in frames]
    header_parity_errors = sum(
        bits[8] != (sum(bits[:8]) & 1) for bits in frame_bits
    )
    header_mirror_errors = sum(bits[9] != bits[8] for bits in frame_bits)
    data_parity_errors = sum(
        bits[46] != (sum(bits[14:46]) & 1) for bits in frame_bits
    )
    data_mirror_errors = sum(bits[47] != bits[46] for bits in frame_bits)
    host_control = Counter("".join(map(str, bits[10:14])) for bits in frame_bits)
    status = Counter("".join(map(str, bits[48:50])) for bits in frame_bits)
    target_padding = Counter("".join(map(str, bits[50:52])) for bits in frame_bits)
    stop_sample = Counter(bits[52] for bits in frame_bits)
    assert header_parity_errors == 0
    assert header_mirror_errors == 0
    assert data_parity_errors == 0
    assert target_padding == {"11": len(frames)}
    assert stop_sample == {0: len(frames)}

    # Bulk read mixes a faster 585-clock burst with slower 53-clock frames.
    # Re-segment only that interval using a 5 us gap so both rates stay intact.
    read_edges = [edge for edge in rising if 0.524 <= edge / RATE <= 0.596]
    read_groups = split_by_start_gap(read_edges, 250, lambda item: item)
    read_words = []
    separators = []
    burst_headers = []
    burst_trailers = []
    final_words = []
    for index, group in enumerate(read_groups):
        if len(group) != 585:
            continue
        bits = [(samples[edge] >> 1) & 1 for edge in group]
        burst_headers.append("".join(map(str, bits[:14])))
        burst_trailers.append("".join(map(str, bits[-7:])))
        for word_index in range(15):
            offset = 14 + 38 * word_index
            read_words.append(bits_to_int(bits[offset : offset + 32]))
            if word_index < 14:
                separators.append("".join(map(str, bits[offset + 32 : offset + 38])))
        for following in read_groups[index + 1 :]:
            if len(following) == 585:
                break
            if len(following) != 53:
                continue
            decoded = decode_x035_frame(samples, following)
            if decoded[0:2] == (4, 0):
                final_words.append(decoded[3])
                read_words.append(decoded[3])
                break

    assert len(bursts) == 64
    assert len(final_words) == 64
    assert read_words == expected_image

    write_begin = writes[0][0][0] / RATE
    write_end = writes[-1][0][-1] / RATE
    read_begin = bursts[0][0] / RATE
    read_final_frames = [
        group
        for group in read_groups
        if len(group) == 53 and decode_x035_frame(samples, group)[:2] == (4, 0)
    ]
    read_end = read_final_frames[-1][-1] / RATE
    write_periods = [
        group[index + 1] - group[index]
        for group, *_ in writes
        for index in range(52)
    ]
    burst_periods = [
        group[index + 1] - group[index]
        for group in bursts
        for index in range(584)
    ]

    transfers, usb_dmi = analyze_usb("usb-x035.ndjson")
    print("X035 / RVSWD")
    print(f"  samples={len(samples)}, duration={len(samples) / RATE:.8f} s")
    print(f"  rising clocks={len(rising)}, 53-clock frames={len(frames)}, 585-clock bursts={len(bursts)}")
    print("  53 rising edges: 52-bit short packet + STOP-associated edge (MSB first)")
    print(
        "  short packet: addr7 + R/W + header parity1 + park1 + padding4 + "
        "data32 + data parity1 + park1 + status2 + padding2"
    )
    print(
        f"  parity checks over all frames: header errors={header_parity_errors}, "
        f"observed header park!=parity={header_mirror_errors}, "
        f"data errors={data_parity_errors}, observed data park!=parity={data_mirror_errors}"
    )
    print(
        f"  control4={host_control}, status2={status}, "
        f"padding2={target_padding}, STOP sample={stop_sample}"
    )
    print(f"  exact pattern writes: aux={Counter(record[3] for record in writes)}, trailer={Counter(record[5] for record in writes)}")
    print(f"  write clock period: {format_ns_stats(write_periods)}")
    print(f"  bulk-read clock period: {format_ns_stats(burst_periods)}")
    print(
        f"  write: {write_begin:.8f}-{write_end:.8f} s, 4096 bytes exact, "
        f"{4 / (write_end - write_begin):.2f} KiB/s"
    )
    print(
        f"  read: {read_begin:.8f}-{read_end:.8f} s, 4096 bytes exact, "
        f"64 x (15-word burst + 1-word frame), {4 / (read_end - read_begin):.2f} KiB/s"
    )
    print(f"  burst headers={Counter(burst_headers)}")
    print(f"  inter-word aux6={Counter(separators)}")
    print(f"  burst trailers={Counter(burst_trailers)}")
    print(f"  USB transfers={transfers}, DMI={usb_dmi}")


def main() -> None:
    payload = (ROOT / "pattern-4k.bin").read_bytes()
    assert payload == bytes(range(256)) * 16
    analyze_v003()
    print()
    analyze_x035()


if __name__ == "__main__":
    main()
