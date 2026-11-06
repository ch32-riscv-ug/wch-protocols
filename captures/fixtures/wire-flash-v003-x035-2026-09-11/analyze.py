#!/usr/bin/env python3
"""Check the structure in README.ja.md on WireSkein's decode of the two wire captures (stdlib only).

The pulses, sampling edges, frames, fields, DMI accesses and per-frame timing come from WireSkein;
this script only checks and summarises them. Decode first, in the wireskein checkout (f21ee42 or later),
with the lower layers the timing needs (SWIO's pulses, RVSWD's sampling edges):

    uv run wireskein decode <fixture>/wire-v003.sr \
        --spec '{"buses":[{"protocol":"swio","pins":{"dio":"CH0"},"layers":["pulses"]}]}' --out <dir>/wire-v003.json
    uv run wireskein decode <fixture>/wire-x035.sr \
        --spec '{"buses":[{"protocol":"rvswd","pins":{"clk":"CH0","dio":"CH1"},"layers":["bits"]}]}' --out <dir>/wire-x035.json

then:

    python3 <fixture>/analyze.py <dir>/wire-v003.json <dir>/wire-x035.json
"""

from __future__ import annotations

import bisect
import json
import statistics
import sys
from collections import Counter
from pathlib import Path


ROOT = Path(__file__).resolve().parent
FLASH = 0x08000000


def pattern_words(count: int) -> list[int]:
    return [
        int.from_bytes(bytes(((4 * index + byte) % 256 for byte in range(4))), "little")
        for index in range(count)
    ]


def find_subsequence(values: list[int], expected: list[int], start: int = 0) -> int:
    for index in range(start, len(values) - len(expected) + 1):
        if values[index : index + len(expected)] == expected:
            return index
    raise AssertionError("known pattern was not found")


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[round((len(ordered) - 1) * fraction)]


def format_ns_stats(values: list[float]) -> str:
    """min / median / p99 / max / mean of values in ns."""
    return (
        f"min/median/p99/max="
        f"{percentile(values, 0):.0f}/{percentile(values, 0.5):.0f}/"
        f"{percentile(values, 0.99):.0f}/{percentile(values, 1):.0f} ns, "
        f"mean={statistics.mean(values):.1f} ns (n={len(values)})"
    )


def load_decode(path: str) -> tuple[dict, dict, float]:
    """The one bus of a WireSkein decode of a fixture capture, its part, and ns per tick."""
    document = json.loads(Path(path).read_text())
    assert document["format"] == "wireskein-analysis/1", path
    capture = document["capture"]
    sums = dict(
        reversed(line.split()) for line in (ROOT / "SHA256SUMS").read_text().splitlines() if line
    )
    assert sums.get(capture["file"]) == capture["sha256"], f"{path}: not a decode of this fixture"
    assert capture["complete"] and len(document["parts"]) == 1
    part = document["parts"][0]
    (bus,) = part["buses"]
    assert bus["decoded"], path
    numerator, denominator = capture["tick_hz"]
    return part, bus, 1e9 * denominator / numerator


def check_dm_reads(bus: dict, tick: float) -> str:
    """The Debug Module layer: the readback is 1024 memory reads of flash, the known pattern."""
    reads = [
        item for item in bus["result"]["dm"]
        if item["op"] == "MEMR" and item["addr"] >> 12 == FLASH >> 12
    ]
    assert [item["addr"] for item in reads] == [FLASH + 4 * index for index in range(1024)]
    assert [item["value"] for item in reads] == pattern_words(1024)
    return (
        f"DM layer: MEMR 0x{FLASH:08x}..+0x1000 x{len(reads)} = pattern, "
        f"{reads[0]['s'] * tick / 1e9:.8f}-{reads[-1]['e'] * tick / 1e9:.8f} s"
    )


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


def analyze_v003(path: str) -> None:
    part, bus, tick = load_decode(path)
    assert bus["protocol"] == "swio"
    dmi = bus["result"]["dmi"]
    pulses = bus["result"]["pulses"]
    full = [item for item in dmi if not item.get("fast")]
    fast = [item for item in dmi if item.get("fast")]

    # LOW widths of the pulses inside the 41-pulse frames, by WireSkein's short (1) / long (0).
    short: list[float] = []
    long: list[float] = []
    for item in full:
        first = bisect.bisect_left(pulses["s"], item["s"])
        assert pulses["s"][first] == item["s"]
        for index in range(first, first + 41):
            (short if pulses["short"][index] else long).append(pulses["width"][index] * tick)

    # Four 1 KiB pages written word by word to DATA0.
    writes = [item for item in full if item["op"] == "W" and item["addr"] == 0x04]
    write_values = [item["data"] for item in writes]
    expected_page = pattern_words(256)
    write_runs = []
    search_at = 0
    while True:
        try:
            start = find_subsequence(write_values, expected_page, search_at)
        except AssertionError:
            break
        write_runs.append(writes[start : start + 256])
        search_at = start + 256
    assert len(write_runs) == 4

    # Readback: reads of DATA0, a full 41-pulse frame or a 33-pulse fast-read continuation.
    reads = [item for item in dmi if item["op"] == "R" and item["addr"] == 0x04]
    read_start = find_subsequence([item["data"] for item in reads], pattern_words(1024))
    reads = reads[read_start : read_start + 1024]
    layouts = {
        "".join("s" if item.get("fast") else "F" for item in reads[index : index + 16])
        for index in range(0, 1024, 16)
    }
    assert layouts == {"FssssssssssssssF"}

    transfers, usb_dmi = analyze_usb("usb-v003.ndjson")
    print("V003 / SWIO")
    print(
        f"  ticks={part['ticks']}, duration={part['ticks'] * tick / 1e9:.8f} s, "
        f"short/long split={bus['params']['split_ns']} ns"
    )
    print(
        f"  pulses={len(pulses['s'])}, 41-pulse frames={len(full)}, "
        f"33-pulse fast reads={len(fast)}, failed checks={sum(not item['ok'] for item in dmi)}"
    )
    print(f"  bit 1 LOW: {format_ns_stats(short)}")
    print(f"  bit 0 LOW: {format_ns_stats(long)}")
    print(f"  read R/W-to-data turnaround: {format_ns_stats([item['rw_gap_ns'] for item in full if item['op'] == 'R'])}")
    print(f"  write R/W-to-data boundary: {format_ns_stats([item['rw_gap_ns'] for item in full if item['op'] == 'W'])}")
    print(
        "  per-frame median space / pitch after 1 / pitch after 0: "
        + " | ".join(
            f"{statistics.median(item[key] for item in full):.0f}"
            for key in ("space_ns", "pitch_1_ns", "pitch_0_ns")
        )
        + " ns"
    )
    for number, run in enumerate(write_runs, 1):
        start, end = run[0]["s"] * tick / 1e9, run[-1]["e"] * tick / 1e9
        print(
            f"  write page {number}: {start:.8f}-{end:.8f} s, "
            f"1 KiB exact, {1 / (end - start):.2f} KiB/s"
        )
    start, end = reads[0]["s"] * tick / 1e9, reads[-1]["e"] * tick / 1e9
    print(
        f"  readback: {start:.8f}-{end:.8f} s, 4096 bytes exact, "
        f"full/short={Counter('short' if item.get('fast') else 'full' for item in reads)}, "
        f"layout={next(iter(layouts))}, {4 / (end - start):.2f} KiB/s"
    )
    print(f"  {check_dm_reads(bus, tick)}")
    print(f"  USB transfers={transfers}, DMI={usb_dmi}")


def intervals(edges: list[int], item: dict, tick: float) -> list[float]:
    """The SWCLK intervals between a frame's sampling edges (WireSkein's bits.t from s to e), in ns."""
    first = bisect.bisect_left(edges, item["s"])
    last = bisect.bisect_right(edges, item["e"])
    return [(edges[index + 1] - edges[index]) * tick for index in range(first, last - 1)]


def analyze_x035(path: str) -> None:
    part, bus, tick = load_decode(path)
    assert bus["protocol"] == "rvswd"
    dmi = bus["result"]["dmi"]
    edges = bus["result"]["bits"]["t"]
    frames = [item for item in dmi if item["form"] == "short" and item["op"] != "BURST"]
    bursts = [item for item in dmi if item["op"] == "BURST"]
    long_frames = [item for item in dmi if item["form"] == "long"]
    assert {len(item["bits"]) for item in frames} == {53}
    assert {len(item["bits"]) for item in bursts} == {585}
    assert all(item["ok"] for item in frames + bursts)

    def tail(fields: dict) -> str:
        return fields["data_parity"] + fields["park2"] + fields["status"] + fields["pad"] + fields["stop"]

    writes = [item for item in frames if item["op"] == "W" and item["addr"] == 0x04]
    expected_image = pattern_words(1024)
    write_start = find_subsequence([item["data"] for item in writes], expected_image)
    writes = writes[write_start : write_start + 1024]
    aux = Counter(item["fields"]["header_parity"] + item["fields"]["park"] + item["fields"]["ctl"] for item in writes)
    trailer = Counter(tail(item["fields"]) for item in writes)
    assert aux == {"000000": 1024}
    assert trailer == {"0000110": 1024}

    fields = [item["fields"] for item in frames]
    header_parity_errors = sum(not field["header_parity_ok"] for field in fields)
    data_parity_errors = sum(not field["data_parity_ok"] for field in fields)
    header_park = sum(field["park"] != field["header_parity"] for field in fields)
    data_park = sum(field["park2"] != field["data_parity"] for field in fields)
    host_control = Counter(field["ctl"] for field in fields)
    status = Counter(field["status"] for field in fields)
    target_padding = Counter(field["pad"] for field in fields)
    stop_sample = Counter(field["stop"] for field in fields)
    assert header_parity_errors == 0
    assert data_parity_errors == 0
    assert target_padding == {"11": len(frames)}
    assert stop_sample == {"0": len(frames)}

    # Readback: 64 x (a 585-clock burst of 15 DATA0 reads + one DATA0 read frame).
    burst_headers = Counter(item["fields"]["header"] for item in bursts)
    burst_trailers = Counter(item["fields"]["trailer"] for item in bursts)
    separators = Counter(word for item in bursts for word in item["fields"]["aux"])
    read_words: list[int] = []
    final_frames = []
    for index, burst in enumerate(bursts):
        assert len(burst["data"]) == 15
        read_words.extend(burst["data"])
        following = next(
            item for item in frames
            if item["s"] > burst["e"] and item["op"] == "R" and item["addr"] == 0x04
        )
        assert index + 1 == len(bursts) or following["s"] < bursts[index + 1]["s"]
        final_frames.append(following)
        read_words.append(following["data"])
    assert len(bursts) == 64
    assert read_words == expected_image

    # SWCLK intervals. In a write frame, interval k runs from sampled bit k to k + 1.
    write_intervals = [intervals(edges, item, tick) for item in writes]
    assert {len(values) for values in write_intervals} == {52}
    regions = {
        "addr + R/W (0-7)": range(0, 8),
        "aux6 (8-13)": range(8, 14),
        "data32 (14-45)": range(14, 46),
        "trailer (46-51)": range(46, 52),
    }
    burst_intervals = [value for item in bursts for value in intervals(edges, item, tick)]
    final_intervals = [value for item in final_frames for value in intervals(edges, item, tick)]
    assert len(burst_intervals) == 64 * 584

    write_begin, write_end = writes[0]["s"] * tick / 1e9, writes[-1]["e"] * tick / 1e9
    read_begin, read_end = bursts[0]["s"] * tick / 1e9, final_frames[-1]["e"] * tick / 1e9
    transfers, usb_dmi = analyze_usb("usb-x035.ndjson")
    print("X035 / RVSWD")
    print(f"  ticks={part['ticks']}, duration={part['ticks'] * tick / 1e9:.8f} s")
    print(
        f"  sampling edges={len(edges)}, 53-clock frames={len(frames)}, 585-clock bursts={len(bursts)}, "
        f"85-clock long frames={len(long_frames)} "
        f"(answered={sum(item.get('answered', True) for item in long_frames)})"
    )
    print("  53 sampled bits: 52-bit short packet + the clock before STOP (MSB first)")
    print(
        "  short packet: addr7 + R/W + header parity1 + park1 + padding4 + "
        "data32 + data parity1 + park1 + status2 + padding2"
    )
    print(
        f"  parity checks over all frames: header errors={header_parity_errors}, "
        f"observed header park!=parity={header_park}, "
        f"data errors={data_parity_errors}, observed data park!=parity={data_park}"
    )
    print(
        f"  control4={host_control}, status2={status}, "
        f"padding2={target_padding}, STOP sample={stop_sample}"
    )
    print(f"  exact pattern writes: aux={aux}, trailer={trailer}")
    print(f"  write clock period: {format_ns_stats([value for values in write_intervals for value in values])}")
    for name, positions in regions.items():
        values = [values[k] for values in write_intervals for k in positions]
        print(f"    from bits {name}: {format_ns_stats(values)}")
    print(f"  bulk-read clock period: {format_ns_stats(burst_intervals)}")
    print(f"  bulk-read 1-word frame clock period: {format_ns_stats(final_intervals)}")
    print(
        f"  write: {write_begin:.8f}-{write_end:.8f} s, 4096 bytes exact, "
        f"{4 / (write_end - write_begin):.2f} KiB/s"
    )
    print(
        f"  read: {read_begin:.8f}-{read_end:.8f} s, 4096 bytes exact, "
        f"64 x (15-word burst + 1-word frame), {4 / (read_end - read_begin):.2f} KiB/s"
    )
    print(f"  burst headers={burst_headers}")
    print(f"  inter-word aux6={separators}")
    print(f"  burst trailers={burst_trailers}")
    print(f"  {check_dm_reads(bus, tick)}")
    print(f"  USB transfers={transfers}, DMI={usb_dmi}")


def main() -> None:
    if len(sys.argv) != 3:
        sys.exit(f"usage: {sys.argv[0]} WIRE-V003-DECODE.json WIRE-X035-DECODE.json")
    payload = (ROOT / "pattern-4k.bin").read_bytes()
    assert payload == bytes(range(256)) * 16
    analyze_v003(sys.argv[1])
    print()
    analyze_x035(sys.argv[2])


if __name__ == "__main__":
    main()
