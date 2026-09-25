"""RVSWD decoder for the WCH-LinkE wire captures (.sr, D0 = SWCLK, D1 = SWDIO by default).

Frames are cut by START / STOP, not by idle time:

- outside a frame: SWDIO falling while SWCLK is high = START
- inside a frame:  SWDIO rising while SWCLK is high, with SWCLK then staying high >= STOP_AFTER_NS = STOP.
                   While the target drives read data it also changes SWDIO right after a rising edge, but SWCLK
                   falls again within ~1 us; a STOP is followed by >= ~2 us of high (LinkE 2.22, L103).
- inside a frame:  SWCLK not clocking for >= IDLE_US closes the frame without STOP ('open').

Bits are SWDIO sampled at each SWCLK rising edge, MSB first. Both lines get a hold-time filter first (a level
counts once it holds `k` samples), because the tap at the LinkE end reflects short glitches onto SWCLK.

Frame kinds by rising-edge count (LinkE 2.22 + L103, captures/fixtures/wire-linke-p4-2026-09-25):
  53        short packet: addr7 R/W parity park padding4 data32 parity tail5 + termination clock
  85        long packet:  host addr7 data32 op2 parity1 | target addr7 data32 status2 parity1 + termination clock
  15 + 38N  bulk read of N words: header14 data32 (aux6 data32)*(N-1) trailer7 (N = 15 gives 585)

    cd captures && uv run python tools/rvswd.py <file.sr> [--k 3] [--k-frame N]   # one line per frame
"""
import argparse, sys, zipfile
from collections import Counter
import numpy as np

STOP_AFTER_NS = 1600
IDLE_US = 20
NAMES = {0x04: "data0", 0x05: "data1", 0x10: "dmcontrol", 0x11: "dmstatus", 0x12: "hartinfo", 0x16: "abstractcs",
         0x17: "command", 0x18: "abstractauto", 0x40: "haltsum0", **{0x20 + i: f"progbuf{i}" for i in range(8)}}


def load(path):
    z = zipfile.ZipFile(path)
    d = np.frombuffer(z.read("logic-1-1"), dtype=np.uint8)
    rate = float(z.read("metadata").decode().split("samplerate=")[1].split()[0])
    return d, rate


def hold(x, k):
    """A level change counts only once the new level holds k samples; shorter runs keep the old level."""
    if k <= 1:
        return x
    e = np.flatnonzero(np.diff(x.astype(np.int8))) + 1
    bounds = np.concatenate(([0], e, [len(x)]))
    out = np.empty_like(x)
    level = int(x[0]); pos = 0
    for a, b in zip(bounds[:-1], bounds[1:]):
        if int(x[a]) != level and b - a >= k:
            out[pos:a] = level; pos = a; level = int(x[a])
    out[pos:] = level
    return out


def frames(path, k=3, clk_bit=0, dio_bit=1, k_frame=None):
    """[(start_sample, end_sample, rising_edges, bits, 'stop' | 'open')], samplerate.

    `k` is the hold filter for the bit clock. `k_frame` (default = k) is a stronger filter used only to find
    START / STOP, whose SWCLK-high intervals are long: with it a 1-sample glitch on an idle-high SWCLK does not
    hide a STOP even when the bit clock has to be read unfiltered (V203 at 100 MHz: SWCLK high is 2-3 samples)."""
    d, rate = load(path)
    clk, dio = hold((d >> clk_bit) & 1, k), hold((d >> dio_bit) & 1, k)
    clkf = clk if not k_frame or k_frame == k else hold((d >> clk_bit) & 1, k_frame)
    dc = np.diff(clk.astype(np.int8))
    rise = np.flatnonzero(dc == 1) + 1
    fall = np.flatnonzero(np.diff(clkf.astype(np.int8)) == -1) + 1
    de = np.flatnonzero(np.diff(dio.astype(np.int8))) + 1
    de = de[clkf[de] == 1]
    j = np.searchsorted(fall, de)
    after = np.where(j < len(fall), fall[np.minimum(j, len(fall) - 1)] - de, 1 << 40)
    stop_ok = after >= STOP_AFTER_NS * 1e-9 * rate
    idle = IDLE_US * 1e-6 * rate
    out, start = [], None

    fall_bit = np.flatnonzero(dc == -1) + 1

    def emit(a, b, end):
        r = rise[np.searchsorted(rise, a, "right"):np.searchsorted(rise, b, "left")]
        if end == "stop" and decode("0" * len(r)) is None:
            # Too many clocks for a known frame: drop the clocks whose SWCLK high lasted only one sample (a
            # reflection glitch read unfiltered), if that leaves a known length. Parity then checks the result.
            high = fall_bit[np.minimum(np.searchsorted(fall_bit, r), len(fall_bit) - 1)] - r
            for m in range(1, 4):
                if decode("0" * (len(r) - m)) is not None and (high <= 1).sum() >= m:
                    drop = np.argsort(high, kind="stable")[:m]
                    r = np.delete(r, drop); end = "stop-repaired"
                    break
        out.append((a, b, r, "".join(map(str, dio[r])), end))

    big = np.flatnonzero(np.diff(rise) > idle)          # rising-edge gaps that end a frame without STOP
    for pos, v, sok in zip(de, dio[de], stop_ok):
        if start is not None:
            lo = np.searchsorted(rise, start, "right"); hi = np.searchsorted(rise, pos, "left")
            if hi > lo:
                g = big[np.searchsorted(big, lo):]
                cut = g[0] if len(g) and g[0] < hi - 1 else (hi - 1 if pos - rise[hi - 1] > idle else None)
                if cut is not None:
                    emit(start, rise[cut] + 1, "open"); start = None
        if start is None:
            if v == 0:
                start = pos
        elif v == 1 and sok:
            emit(start, pos, "stop"); start = None
    if start is not None:
        emit(start, len(clk), "open")
    return out, rate


def parity_ok(bits):
    """Even parity: the parity bit equals the XOR of the covered bits."""
    return bits[-1] == str(bits[:-1].count("1") & 1)


def decode(bits):
    """dict describing one frame, or None for an unknown length."""
    n = len(bits)
    if n == 53:
        return dict(kind="W" if bits[7] == "1" else "R", addr=int(bits[:7], 2), data=int(bits[14:46], 2),
                    hdr_ok=parity_ok(bits[:9]), data_ok=parity_ok(bits[14:47]), dontcare=bits[9:14] + "|" + bits[47:])
    if n == 85:
        return dict(kind="LONG", addr=int(bits[:7], 2), data=int(bits[7:39], 2), op=bits[39:41], parity=bits[41],
                    t_addr=bits[42:49], t_data=int(bits[49:81], 2), t_status=bits[81:83], t_parity=bits[83],
                    last=bits[84])
    if n >= 53 and (n - 15) % 38 == 0:
        words = (n - 15) // 38
        data = [int(bits[14 + 38 * i:14 + 38 * i + 32], 2) for i in range(words)]
        return dict(kind="BURST", addr=int(bits[:7], 2), words=data, header=bits[:14], trailer=bits[-7:])
    return None


def memory_log(decoded):
    """Pair abstract commands with data0/data1 into memory / register accesses, in the order they ran.
    `decoded` is a list of (time_ms, decode() dict). Returns (time_ms, 'MEMR'|'MEMW'|'REGR'|'REGW', addr, value)."""
    regs, out = {}, []
    for t, f in decoded:
        if f["kind"] == "W" and f["addr"] in (0x04, 0x05):
            regs[f["addr"]] = f["data"]
        elif f["kind"] == "R" and f["addr"] == 0x04 and out and out[-1][1] in ("MEMR", "REGR") and out[-1][3] is None:
            out[-1] = out[-1][:3] + (f["data"],)
        elif f["kind"] == "W" and f["addr"] == 0x17:
            cmd = f["data"]; write = (cmd >> 16) & 1
            if cmd >> 24 == 2:
                a = regs.get(0x05)
                out.append((t, "MEMW" if write else "MEMR", a, regs.get(0x04) if write else None))
                if (cmd >> 19) & 1 and a is not None:
                    regs[0x05] = a + (1 << ((cmd >> 20) & 7))
            elif cmd >> 24 == 0:
                out.append((t, "REGW" if write else "REGR", cmd & 0xFFFF, regs.get(0x04) if write else None))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sr"); ap.add_argument("--k", type=int, default=3)
    ap.add_argument("--k-frame", type=int, default=None, help="hold filter for START/STOP only (default: --k)")
    ap.add_argument("--clk", type=int, default=0); ap.add_argument("--dio", type=int, default=1)
    a = ap.parse_args()
    fr, rate = frames(a.sr, a.k, a.clk, a.dio, a.k_frame)
    lengths = Counter((len(r), end) for *_, r, b, end in fr)
    print(f"# {a.sr}: {len(fr)} frames, lengths {sorted(lengths.items())}")
    for s, e, r, b, end in fr:
        t = s / rate * 1e3
        f = decode(b) if end.startswith("stop") else None
        if f is None:
            print(f"{t:10.4f} RAW{len(r)} {end} {b}")
        elif f["kind"] in ("W", "R"):
            ok = "" if f["hdr_ok"] and f["data_ok"] else " PARITY"
            print(f"{t:10.4f} {f['kind']} {NAMES.get(f['addr'], hex(f['addr'])):12} {f['data']:08x}{ok}")
        elif f["kind"] == "LONG":
            print(f"{t:10.4f} LONG {NAMES.get(f['addr'], hex(f['addr']))} data {f['data']:08x} op {f['op']} "
                  f"| target {f['t_addr']} {f['t_data']:08x} {f['t_status']}")
        else:
            print(f"{t:10.4f} BURST{len(f['words'])} {' '.join(f'{w:08x}' for w in f['words'])}")


if __name__ == "__main__":
    sys.exit(main())
