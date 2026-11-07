"""Compact text of an SDI-monitor wire capture (a WCH-LinkE polling DMDATA0 for SDI printf output), from WireSkein's
decode of it: the RVSWD frames and DMI accesses are WireSkein's (`--items`, one JSON item per line); this only finds
the polling phase and the mailbox in them. It writes:

- counts of DMI frames by clock count, and how many WireSkein could not read;
- the polling phase (the longest run of `R data0` / `R data1` / `W data0` accesses; frames WireSkein could not read
  do not end it): number of DMDATA0 reads and how many returned 0, the interval between consecutive DMDATA0 reads;
- the mailbox sequence: for each nonzero DMDATA0 read in the polling phase (EVT SDI_Printf: low byte = character
  count n, upper 3 bytes = the first 3 characters, DMDATA1 = characters 3-6), the ops up to the next DMDATA0 read,
  counted by n and pattern;
- the text the mailbox carried, reassembled;
- every access before and after the polling phase, one line per DMI op with its time; in the polling phase one line
  per nonzero DMDATA0 read with the ops that follow it, and every other access except DMDATA0 reads of 0.

    WIRESKEIN=<wireskein checkout>   # d681cc5 or later (a long capture read in pieces, ringing at START / STOP)
    (cd $WIRESKEIN && uv run wireskein decode $OLDPWD/<file.sr> \\
        --spec '{"buses":[{"protocol":"rvswd","pins":{"clk":"SWCLK","dio":"SWDIO"}}]}' \\
        --split workers=4 --items <dir>/items.ndjson --out <dir>/doc.json)
    uv run python tools/sdi_monitor.py <dir>/doc.json <dir>/items.ndjson > <file>.dmi.txt
"""
import json, statistics, sys
from collections import Counter
from pathlib import Path

DATA0, DATA1 = 0x04, 0x05
POLL_OPS = {("R", DATA0), ("R", DATA1), ("W", DATA0)}
NAMES = {0x04: "data0", 0x05: "data1", 0x10: "dmcontrol", 0x11: "dmstatus", 0x12: "hartinfo", 0x16: "abstractcs",
         0x17: "command", 0x18: "abstractauto", 0x40: "haltsum0", **{0x20 + i: f"progbuf{i}" for i in range(8)}}


def chars(data0, data1):
    """The characters of one mailbox frame: count in data0[7:0], chars 0-2 in data0[31:8], 3-6 in data1 (LE)."""
    n = data0 & 0xFF
    raw = bytes([(data0 >> 8) & 0xFF, (data0 >> 16) & 0xFF, (data0 >> 24) & 0xFF]) + (data1 or 0).to_bytes(4, "little")
    return raw[:min(n, 7)]


def name(addr):
    return NAMES.get(addr, hex(addr)) if addr is not None else "?"


def main(doc_path: str, items_path: str) -> None:
    doc = json.loads(Path(doc_path).read_text())
    hz = doc["capture"]["tick_hz"]
    hz = hz[0] / hz[1] if isinstance(hz, list) else hz          # a fraction [num, den]
    ms = 1e3 / hz
    lengths = Counter()
    recs = []      # [t_ms, kind, addr, data, line]; kind R / W / other op / BAD (not read); line for a BAD one
    with open(items_path) as f:
        for raw in f:
            it = json.loads(raw)
            if it.get("layer") != "dmi":
                continue
            bits = it.get("bits") or ""
            lengths[(len(bits), it.get("form", "?"), "ok" if it.get("ok") else "not ok")] += 1
            t = it["s"] * ms
            if it.get("ok") and it.get("op") in ("R", "W"):
                recs.append([t, it["op"], it.get("addr"), it.get("data"), None])
            elif it.get("ok"):
                recs.append([t, it.get("op", "?"), it.get("addr"), None,
                             f"{t:10.4f} {it.get('op')} {name(it.get('addr'))} {json.dumps(it.get('data'))}"])
            else:
                recs.append([t, "BAD", it.get("addr"), None, f"{t:10.4f} not read: {len(bits)} clocks {bits}"])
    recs.sort(key=lambda r: r[0])

    # polling phase = longest run of R data0 / R data1 / W data0; frames not read do not end it
    def pollish(r):
        return (r[1], r[2]) in POLL_OPS or r[1] == "BAD"
    best, cur = (0, 0), None
    for i, r in enumerate(recs + [[0, "END", None, None, None]]):
        if i < len(recs) and pollish(r):
            cur = i if cur is None else cur
        else:
            if cur is not None and i - cur > best[1] - best[0]:
                best = (cur, i)
            cur = None
    p0, p1 = best

    polls = [r for r in recs[p0:p1] if r[1] == "R" and r[2] == DATA0]
    zero = sum(1 for r in polls if r[3] == 0)
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
        while j < p1 and not (recs[j][1] == "R" and recs[j][2] == DATA0):
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
        t, kind, addr, data, line = r
        return line or f"{t:10.4f} {kind} {name(addr):12} {data:08x}"

    w = print
    w(f"# {doc['capture'].get('file', Path(doc_path).name)}: RVSWD decoded by WireSkein {doc.get('wireskein', '?')}, "
      f"summarised by captures/tools/sdi_monitor.py")
    w(f"# tick {hz / 1e6:g} MHz, {len(recs)} DMI frames. Times are ms from the start of the capture.")
    w("\n## DMI frames by clock count (clocks form read: count)")
    for (c, form, ok), n in sorted(lengths.items()):
        w(f"{c:4} {form:6} {ok:7} {n}")
    for part in doc.get("parts", []):
        for bus in part.get("buses", []):
            u = bus.get("uninterpreted_summary")
            if u:
                w(f"uninterpreted activity (WireSkein): {u.get('count')} stretch(es), {u.get('edges')} edges")
    w("\n## polling phase")
    if p1 > p0:
        w(f"from {recs[p0][0]:.4f} ms to {recs[p1 - 1][0]:.4f} ms: {p1 - p0} frames, all R data0 / R data1 / W data0 "
          f"except {sum(1 for r in recs[p0:p1] if r[1] == 'BAD')} not read")
        w(f"DMDATA0 reads: {len(polls)}, of which data 0: {zero}, nonzero: {len(mailbox)}")
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
    w("## other frame except DMDATA0 reads that returned 0")
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
    main(sys.argv[1], sys.argv[2])
