"""Compact decoded text of an SDI-monitor wire capture (WCH-LinkE polling DMDATA0 for SDI printf output).

Streams the capture with rvswd_stream.py (default hold filter: none on the bit clock, 3 samples for START / STOP,
as for V203 at 100 MHz: at 25 MHz the LinkE's poll clock is high for only 3-7 samples) and writes:

- counts of frames by clock count;
- the polling phase (the longest run of `R data0` / `R data1` / `W data0` frames; frames of no known length do
  not end it): number of DMDATA0 reads and how many returned 0, the interval between consecutive DMDATA0 reads;
- the mailbox sequence: for each nonzero DMDATA0 read in the polling phase (EVT SDI_Printf: low byte = character
  count n, upper 3 bytes = the first 3 characters, DMDATA1 = characters 3-6), the ops up to the next DMDATA0 read,
  counted by n and pattern;
- frames that are no known frame: 106-116 clocks are two 53-clock frames whose STOP was not seen (each half is
  decoded and checked); 46-52 clocks are 53-clock frames that lost clocks to the hold filter (only with --k 2 or
  more): their first 9 bits (address, R/W, header parity) are checked, and 52-clock ones are repaired by trying
  every single-bit insertion that gives a parity-correct frame;
- the text the mailbox carried, reassembled;
- every frame before and after the polling phase in full, one line per DMI op with its time; in the polling phase
  one line per nonzero DMDATA0 read with the ops that follow it, and every other frame except DMDATA0 reads of 0.

    cd captures && uv run python tools/rvswd_sdi_monitor.py <file.sr> > <file>.dmi.txt
"""
import argparse, statistics, sys
from collections import Counter
from pathlib import Path

import rvswd
import rvswd_stream

DATA0, DATA1 = 0x04, 0x05
POLL_OPS = {("R", DATA0), ("R", DATA1), ("W", DATA0)}


def chars(data0, data1):
    """The characters of one mailbox frame: count in data0[7:0], chars 0-2 in data0[31:8], 3-6 in data1 (LE)."""
    n = data0 & 0xFF
    raw = bytes([(data0 >> 8) & 0xFF, (data0 >> 16) & 0xFF, (data0 >> 24) & 0xFF]) + (data1 or 0).to_bytes(4, "little")
    return raw[:min(n, 7)]


def short(bits):
    """(kind, addr, data) of a parity-correct 53-clock frame, else None."""
    f = rvswd.decode(bits)
    if f and f["kind"] in ("R", "W") and f["hdr_ok"] and f["data_ok"]:
        return f["kind"], f["addr"], f["data"]
    return None


def repair52(bits):
    """{(kind, addr, data)} of the parity-correct frames one inserted bit away from a 52-clock frame."""
    return {x for p in range(53) for b in "01" if (x := short(bits[:p] + b + bits[p:]))}


def name(addr):
    return rvswd.NAMES.get(addr, hex(addr)) if addr is not None else "?"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("sr")
    ap.add_argument("--k", type=int, default=1, help="hold filter for the bit clock (samples)")
    ap.add_argument("--k-frame", type=int, default=3, help="hold filter for START / STOP (samples)")
    a = ap.parse_args()
    it = rvswd_stream.stream_frames(a.sr, a.k, 0, 1, a.k_frame)
    _, rate = next(it)
    ms = 1e3 / rate

    lengths = Counter()
    recs = []      # [t_ms, kind, addr, data, note, line]; kind R / W / OTHER / DROP; line only for non-poll-like
    unusual = Counter()
    for s, e, clocks, bits, end in it:
        lengths[(clocks, end)] += 1
        t = s * ms
        if end.startswith("stop") and 106 <= clocks <= 116:
            a1, a2 = short(bits[:53]), short(bits[-53:])
            t2 = (s + (e - s) * (clocks - 53) / clocks) * ms        # second frame: estimated from its share of clocks
            unusual[(clocks, "two frames, STOP not seen", f"{a1 and (a1[0], name(a1[1]))} + {a2 and (a2[0], name(a2[1]))}")] += 1
            if a1 and a2:
                recs.append([t, *a1, f"first half of a {clocks}-clock frame", None])
                recs.append([t2, *a2, f"second half of a {clocks}-clock frame (time estimated)", None])
                continue
        if end.startswith("stop") and 46 <= clocks <= 52:
            pre = bits[:7]; addr = int(pre, 2)
            hdr_ok = rvswd.parity_ok(bits[:9])
            rep = repair52(bits) if clocks == 52 else set()
            fixed = next(iter(rep)) if len(rep) == 1 else None
            unusual[(clocks, "clocks lost", f"header {('R' if bits[7] == '0' else 'W')} {name(addr)} parity {'ok' if hdr_ok else 'bad'}",
                     f"repair {sorted((k, name(ad), f'{d:08x}') for k, ad, d in rep) if clocks == 52 else '-'}")] += 1
            if fixed:
                recs.append([t, *fixed, f"{clocks} clocks, repaired", None])
            else:
                recs.append([t, "DROP", addr if hdr_ok else None, None, f"{clocks} clocks", rvswd_stream.line(t, clocks, bits, end)])
            continue
        f = short(bits) if end.startswith("stop") and clocks == 53 else None
        if f:
            recs.append([t, *f, "", None])
        else:
            known = end.startswith("stop") and rvswd.decode(bits) is not None
            recs.append([t, "KNOWN" if known else "OTHER", None, None, "", rvswd_stream.line(t, clocks, bits, end)])

    # polling phase = longest run of R data0 / R data1 / W data0 frames; frames of no known length (OTHER) do not
    # end it, any other decoded frame (another register, long or burst frame) does
    def pollish(r):
        return (r[1], r[2]) in POLL_OPS or r[1] == "OTHER" or (r[1] == "DROP" and r[2] == DATA0)
    best, cur = (0, 0), None
    for i, r in enumerate(recs + [[0, "END", None, None, "", None]]):
        if i < len(recs) and pollish(r):
            cur = i if cur is None else cur
        else:
            if cur is not None and i - cur > best[1] - best[0]:
                best = (cur, i)
            cur = None
    p0, p1 = best

    polls = [r for r in recs[p0:p1] if r[2] == DATA0 and r[1] in ("R", "DROP")]
    zero = sum(1 for r in polls if r[1] == "R" and r[3] == 0)
    gaps = [(b[0] - a[0]) * 1e3 for a, b in zip(polls, polls[1:])]
    text = bytearray()
    patterns = Counter()      # (n, ops between this nonzero DMDATA0 read and the next DMDATA0 read)
    mailbox = {}              # index of a nonzero DMDATA0 read -> one output line
    consumed = set()
    for i in range(p0, p1):
        t, kind, addr, data = recs[i][:4]
        if not (kind == "R" and addr == DATA0 and data):
            continue
        n = data & 0xFF
        j = i + 1
        while j < p1 and not (recs[j][1] in ("R", "DROP") and recs[j][2] == DATA0):
            j += 1
        nxt = recs[i + 1:j]
        consumed.update(range(i + 1, j))
        patterns[(n, " ; ".join(f"{r[1]} {name(r[2])}" + (f" {r[3]:08x}" if r[1] == "W" and r[3] is not None else "")
                                for r in nxt))] += 1
        d1 = next((r[3] for r in nxt if r[1] == "R" and r[2] == DATA1), None)
        c = chars(data, d1 if n > 3 else None)
        text += c
        mailbox[i] = (f"{t:10.4f} R data0 {data:08x} n={n} {c!r}" +
                      "".join(f" | +{(r[0] - t) * 1e3:.1f} us {r[1]} {name(r[2])} " + (f"{r[3]:08x}" if r[3] is not None else "?")
                              for r in nxt))

    def fmt(r):
        t, kind, addr, data, note, line = r
        if line:
            return line + (f"   ({note})" if note else "")
        return f"{t:10.4f} {kind} {name(addr):12} {data:08x}" + (f"   {note}" if note else "")

    out = sys.stdout
    w = lambda s="": print(s, file=out)
    w(f"# {Path(a.sr).name}: RVSWD decoded by captures/tools/rvswd_sdi_monitor.py "
      f"(captures/tools/rvswd_stream.py, hold filter {a.k} sample(s) on the bit clock, {a.k_frame} for START / STOP)")
    w(f"# samplerate {rate / 1e6:g} MHz, {sum(lengths.values())} frames. Times are ms from the start of the capture.")
    w("\n## frames by clock count (clocks end: count)")
    for (c, e), n in sorted(lengths.items()):
        w(f"{c:4} {e:13} {n}")
    w("\n## frames that are no known frame (clocks, what, decode: count)")
    for k, n in sorted(unusual.items(), key=str):
        w(f"{k}: {n}")
    w("\n## polling phase")
    if p1 > p0:
        w(f"from {recs[p0][0]:.4f} ms to {recs[p1 - 1][0]:.4f} ms: {p1 - p0} frames, all R data0 / R data1 / W data0 "
          f"except {sum(1 for r in recs[p0:p1] if r[1] == 'OTHER')} of no known length")
        w(f"DMDATA0 reads: {len(polls)}, of which data 0: {zero}, nonzero: {len(mailbox)}, "
          f"not decoded (clocks lost): {sum(1 for r in polls if r[1] == 'DROP')}")
    if len(gaps) > 100:
        q = statistics.quantiles(gaps, n=100)
        w(f"interval between consecutive DMDATA0 reads (us): median {statistics.median(gaps):.2f}, "
          f"p1 {q[0]:.2f}, p99 {q[98]:.2f}, min {min(gaps):.2f}, max {max(gaps):.2f}")
        hist = Counter(min(int(g), 100) for g in gaps)
        w("histogram, 1 us bins (100 = 100 or more): " + ", ".join(f"{b}:{n}" for b, n in sorted(hist.items())))
    w("\n## mailbox sequence: what follows each nonzero DMDATA0 read in the polling phase, up to the next DMDATA0 read")
    w("(n = DATA0 low byte = character count; EVT SDI_Printf puts characters 3-6 in DATA1)")
    for (n, pat), c in sorted(patterns.items()):
        w(f"n={n}: {pat}: {c}")
    w("\n## text carried by the mailbox (reassembled from the polling phase; \\r and \\n escaped)")
    parts = text.decode("latin-1").replace("\r", "\\r").split("\n")
    for i, l in enumerate(parts):
        if l or i < len(parts) - 1:
            w("  " + l + ("\\n" if i < len(parts) - 1 else ""))
    w(f"\n## before the polling phase: every frame ({p0})")
    for r in recs[:p0]:
        w(fmt(r))
    w("\n## polling phase: one line per nonzero DMDATA0 read with the ops that follow it (time from the read), and every")
    w("## other frame except DMDATA0 reads that returned 0 (two-frame halves that read 0 are only counted above)")
    for i in range(p0, p1):
        r = recs[i]
        if i in mailbox:
            w(mailbox[i])
        elif i in consumed or (r[1] == "R" and r[2] == DATA0 and r[3] == 0):
            continue
        else:
            w(fmt(r))
    w(f"\n## after the polling phase: every frame ({len(recs) - p1})")
    for r in recs[p1:]:
        w(fmt(r))


if __name__ == "__main__":
    sys.exit(main())
