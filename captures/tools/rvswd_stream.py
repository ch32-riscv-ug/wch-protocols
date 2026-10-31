"""Streaming RVSWD decoder: rvswd.py's frame cutting and decoding, one window of samples at a time.

rvswd.py loads the whole sigrok session into memory (a 25 MHz, 38 s capture is 950 MB of samples before any
intermediate array). This reads the session's logic-1-<n> chunks sequentially through zipfile, decodes a window of
WINDOW samples with rvswd.frames_from_samples(), keeps the frames that end at least MARGIN before the window's end
and starts the next window where the last kept frame ended (between frames, where both lines idle high). Memory
stays at a few hundred MB whatever the capture length. The frame cutting, hold filter and stop repair are rvswd.py's.

    cd captures && uv run python tools/rvswd_stream.py <file.sr> [--k 3] [--k-frame N]   # same lines as rvswd.py
"""
import argparse, sys, zipfile
from collections import Counter

import numpy as np

import rvswd

WINDOW = 1 << 23      # samples per window (8 Mi = 0.34 s at 25 MHz)
MARGIN_S = 2e-3       # a frame must end this long before the window's end to be kept (longest frame ~0.3 ms)


def session(path):
    """(zipfile, chunk names in order, samplerate, unitsize) of a sigrok session file."""
    z = zipfile.ZipFile(path)
    meta = z.read("metadata").decode()
    value, *unit_name = meta.split("samplerate=")[1].splitlines()[0].split()
    rate = float(value) * {"hz": 1, "khz": 1e3, "mhz": 1e6, "ghz": 1e9}[(unit_name or ["hz"])[0].lower()]
    unit = int(meta.split("unitsize=")[1].split()[0]) if "unitsize=" in meta else 1
    chunks = sorted((n for n in z.namelist() if n.startswith("logic-1-")), key=lambda n: int(n.rsplit("-", 1)[1]))
    return z, chunks, rate, unit


def samples(z, chunks, unit, size):
    """Yield arrays of up to `size` samples, reading the chunks in order without holding more than that."""
    dtype = np.uint8 if unit == 1 else np.dtype("<u2")
    want = size * unit
    pending = b""
    for name in chunks:
        with z.open(name) as f:
            while True:
                block = f.read(want - len(pending))
                if not block:
                    break
                pending += block
                if len(pending) >= want:
                    yield np.frombuffer(pending[:want], dtype=dtype)
                    pending = pending[want:]
    if pending:
        yield np.frombuffer(pending[:len(pending) // unit * unit], dtype=dtype)


def stream_frames(path, k=3, clk_bit=0, dio_bit=1, k_frame=None, window=WINDOW):
    """Yield (start_sample, end_sample, clocks, bits, end) with absolute sample indices; also returns the rate via
    the first yielded item being ('rate', rate). `end` is rvswd's 'stop' / 'stop-repaired' / 'open'."""
    z, chunks, rate, unit = session(path)
    yield ("rate", rate)
    margin = int(MARGIN_S * rate)
    src = samples(z, chunks, unit, window)
    buf = np.empty(0, dtype=np.uint8 if unit == 1 else np.dtype("<u2"))
    base = 0          # absolute index of buf[0]
    eof = False
    while True:
        while not eof and len(buf) < window:
            try:
                buf = np.concatenate((buf, next(src)))
            except StopIteration:
                eof = True
        if len(buf) == 0:
            return
        limit = len(buf) if eof else len(buf) - margin
        kept_end = 0
        for s, e, r, bits, end in rvswd.frames_from_samples(buf, rate, k, clk_bit, dio_bit, k_frame):
            if e > limit:
                break
            yield (base + s, base + e, len(r), bits, end)
            kept_end = e
        if eof:
            return
        cut = kept_end if kept_end else limit
        buf = buf[cut:].copy()
        base += cut


def line(t_ms, clocks, bits, end):
    """One output line in rvswd.py's format."""
    f = rvswd.decode(bits) if end.startswith("stop") else None
    if f is None:
        return f"{t_ms:10.4f} RAW{clocks} {end} {bits}"
    if f["kind"] in ("W", "R"):
        ok = "" if f["hdr_ok"] and f["data_ok"] else " PARITY"
        return f"{t_ms:10.4f} {f['kind']} {rvswd.NAMES.get(f['addr'], hex(f['addr'])):12} {f['data']:08x}{ok}"
    if f["kind"] == "LONG":
        return (f"{t_ms:10.4f} LONG {rvswd.NAMES.get(f['addr'], hex(f['addr']))} data {f['data']:08x} op {f['op']} "
                f"| target {f['t_addr']} {f['t_data']:08x} {f['t_status']}")
    return f"{t_ms:10.4f} BURST{len(f['words'])} {' '.join(f'{w:08x}' for w in f['words'])}"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sr"); ap.add_argument("--k", type=int, default=3)
    ap.add_argument("--k-frame", type=int, default=None, help="hold filter for START/STOP only (default: --k)")
    ap.add_argument("--clk", type=int, default=0); ap.add_argument("--dio", type=int, default=1)
    ap.add_argument("--window", type=int, default=WINDOW, help="samples per window")
    a = ap.parse_args()
    it = stream_frames(a.sr, a.k, a.clk, a.dio, a.k_frame, a.window)
    _, rate = next(it)
    lengths = Counter()
    for s, e, clocks, bits, end in it:
        lengths[(clocks, end)] += 1
        print(line(s / rate * 1e3, clocks, bits, end))
    print(f"# {a.sr}: {sum(lengths.values())} frames, lengths {sorted(lengths.items())}")


if __name__ == "__main__":
    sys.exit(main())
