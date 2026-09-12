"""E077 check: read a .sr sigrok wrote and apply E074's period test to it.

Plan and report: README.ja.md

srzip splits the samples into numbered chunks, and "logic-1-10" sorts before
"logic-1-2" as text, so the chunks are ordered by their number here -- getting
that wrong fabricates edges at every chunk boundary.
"""

import argparse
import configparser
import zipfile

MULTIPLIERS = {"hz": 1, "khz": 1_000, "mhz": 1_000_000, "ghz": 1_000_000_000}


def read_sr(path: str) -> tuple[bytes, int, int, list[str]]:
    archive = zipfile.ZipFile(path)
    meta = configparser.ConfigParser()
    meta.read_string(archive.read("metadata").decode())
    device = meta["device 1"]
    unitsize = int(device.get("unitsize", "1"))
    value, _, unit = device["samplerate"].strip().partition(" ")
    rate = int(float(value) * MULTIPLIERS[unit.strip().lower() or "hz"])
    names = [device[key] for key in device if key.startswith("probe")]
    chunks = sorted(
        (name for name in archive.namelist() if name.startswith("logic-1-")),
        key=lambda name: int(name.rsplit("-", 1)[1]),
    )
    data = b"".join(archive.read(name) for name in chunks)
    return (data[::unitsize] if unitsize > 1 else data), rate, unitsize, names


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--channels", type=int, default=2)
    parser.add_argument("--source-hz", type=int, default=100_000)
    args = parser.parse_args()

    samples, rate, unitsize, names = read_sr(args.path)
    expected = rate / args.source_hz
    print(f"{args.path}: rate={rate} unitsize={unitsize} samples={len(samples)} probes={names[:args.channels]}")
    exact = True
    for bit in range(args.channels):
        edges = []
        previous = (samples[0] >> bit) & 1
        for index in range(1, len(samples)):
            current = (samples[index] >> bit) & 1
            if current and not previous:
                edges.append(index)
            previous = current
        gaps = [b - a for a, b in zip(edges, edges[1:])]
        duty = sum((value >> bit) & 1 for value in samples) / len(samples) * 100
        ok = bool(gaps) and min(gaps) == max(gaps) == round(expected)
        exact = exact and ok
        low, high = (min(gaps), max(gaps)) if gaps else (0, 0)
        mean = sum(gaps) / len(gaps) if gaps else 0
        print(f"  D{bit}: duty {duty:6.2f}%  edges {len(edges):6d}  period {low}/{mean:.2f}/{high} "
              f"(expected {expected:.0f}) {'OK' if ok else 'MISMATCH'}")
    print("=>", "sample-accurate" if exact else "SAMPLES LOST")
    return 0 if exact else 1


if __name__ == "__main__":
    raise SystemExit(main())
