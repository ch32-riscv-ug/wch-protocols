"""SWIO (1-wire) decoder for the WCH-LinkE wire captures (.sr, SWIO on bit `--bit`, default 0).

A bit is one LOW pulse: short (< SPLIT_NS) = 1, long = 0. A pause of more than GAP_US between pulse starts ends a
frame. Frame kinds (LinkE 2.22 + V003, captures/fixtures/wire-flash-v003-x035-2026-09-11 and wire-linke-p4-2026-09-25):

  41 pulses  start(1) addr7 R/W(1 = write) data32      (write data from the host, read data from the target)
  33 pulses  0 data32                                   fast-read continuation (inside a 16-word bulk read)

SWIO carries no parity, so check decoded values against known ones (DMSTATUS, the 4 KiB pattern).

    cd captures && uv run python tools/swio.py <file.sr>
"""
import argparse, sys
from collections import Counter
import numpy as np
from rvswd import NAMES, load, memory_log

SPLIT_NS = 500
GAP_US = 4


def pulses(path, bit=0):
    """LOW pulses as (start_sample, width_samples), samplerate."""
    d, rate = load(path)
    x = ((d >> bit) & 1).astype(np.int8)
    e = np.flatnonzero(np.diff(x)) + 1
    falls = e[x[e] == 0]; rises = e[x[e] == 1]
    rises = rises[np.searchsorted(rises, falls[0]):] if len(falls) else rises
    n = min(len(falls), len(rises))
    return falls[:n], rises[:n] - falls[:n], rate


def frames(path, bit=0):
    """[(start_sample, bits, widths_ns)], samplerate."""
    starts, widths, rate = pulses(path, bit)
    split = SPLIT_NS * 1e-9 * rate
    cut = np.flatnonzero(np.diff(starts) > GAP_US * 1e-6 * rate) + 1
    out = []
    for a, b in zip(np.concatenate(([0], cut)), np.concatenate((cut, [len(starts)]))):
        w = widths[a:b]
        out.append((starts[a], "".join("1" if v < split else "0" for v in w), w / rate * 1e9))
    return out, rate


def decode(bits):
    if len(bits) == 41 and bits[0] == "1":
        return dict(kind="W" if bits[8] == "1" else "R", addr=int(bits[1:8], 2), data=int(bits[9:], 2))
    if len(bits) == 33 and bits[0] == "0":
        return dict(kind="R", addr=0x04, data=int(bits[1:], 2), fast=True)
    return None


def main():
    ap = argparse.ArgumentParser(); ap.add_argument("sr"); ap.add_argument("--bit", type=int, default=0)
    a = ap.parse_args()
    fr, rate = frames(a.sr, a.bit)
    print(f"# {a.sr}: {len(fr)} frames, pulse counts {sorted(Counter(len(b) for _, b, _ in fr).items())}")
    for s, b, w in fr:
        f = decode(b)
        t = s / rate * 1e3
        if f is None:
            print(f"{t:10.4f} RAW{len(b)} {b}")
        else:
            print(f"{t:10.4f} {f['kind']}{'s' if f.get('fast') else ''} {NAMES.get(f['addr'], hex(f['addr'])):12} {f['data']:08x}")


if __name__ == "__main__":
    sys.exit(main())
