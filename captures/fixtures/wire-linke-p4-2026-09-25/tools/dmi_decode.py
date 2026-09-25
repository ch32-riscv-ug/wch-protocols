"""Decode the P4 wire captures into DMI transactions (frame layout from the 2026-09-11 fixture's analyze.py):
53 SWCLK rising edges = addr7, R/W(1 = write), hdr parity, park, ctl4, data32, data parity, park, status2, pad2, stop.
SWCLK glitches (reflections of the tap) are removed first: a level must hold >= 3 samples (60 ns)."""
import sys, zipfile
from collections import Counter
from pathlib import Path
import numpy as np

RATE = 50e6
GAP = 400        # samples of idle SWCLK that end a frame (the slow attach phase clocks at ~470 kHz, ~106 samples)
NAMES = {0x04: "data0", 0x05: "data1", 0x10: "dmcontrol", 0x11: "dmstatus", 0x12: "hartinfo", 0x16: "abstractcs",
         0x17: "command", 0x18: "abstractauto", 0x38: "sbcs", 0x39: "sbaddress0", 0x3c: "sbdata0", 0x40: "haltsum0",
         **{0x20 + i: f"progbuf{i}" for i in range(8)}}

class Slow(list):
    slow = True

def clean_edges(clk, k=None):
    k = k or int(__import__("os").environ.get("DEGLITCH", "3"))
    e = list(np.flatnonzero(np.diff(clk.astype(np.int8))) + 1)
    out = []
    for x in e:   # an edge within k samples of the previous kept edge cancels it (a glitch pulse)
        if out and x - out[-1] < k:
            out.pop()
        else:
            out.append(x)
    return out

def decode(path):
    d = np.frombuffer(zipfile.ZipFile(path).read("logic-1-1"), dtype=np.uint8)
    clk, dio = d & 1, (d >> 1) & 1
    edges = clean_edges(clk)
    level0 = int(clk[0])
    rising = [x for i, x in enumerate(edges) if (level0 + i + 1) % 2 == 1]   # edges alternate from the start level
    # Fast frames (~2.7 MHz) are separated by > 100 samples of idle SWCLK. The attach phase clocks slowly (~470 kHz,
    # ~106 samples a period), so each of its clocks falls into a group of its own: runs of such tiny groups closer
    # than 400 samples are joined back into one slow frame.
    fast, cur = [], []
    for x in rising:
        if cur and x - cur[-1] > 100:
            fast.append(cur); cur = []
        cur.append(x)
    if cur: fast.append(cur)
    groups = []
    for g in fast:
        if groups and len(g) <= 2 and len(groups[-1]) >= 1 and groups[-1][-1] - groups[-1][-2 if len(groups[-1]) > 1 else -1] >= 0 \
                and g[0] - groups[-1][-1] < 400 and getattr(groups[-1], "slow", False):
            groups[-1].extend(g); continue
        if len(g) <= 2:
            ng = Slow(g); groups.append(ng); continue
        groups.append(g)
    tx = []
    for g in groups:
        bits = [int(dio[i]) for i in g]
        if len(g) == 53:
            addr = int("".join(map(str, bits[:7])), 2); rw = bits[7]
            data = int("".join(map(str, bits[14:46])), 2)
            ok = bits[8] == (sum(bits[:8]) & 1) and bits[46] == (sum(bits[14:46]) & 1)
            tx.append((g[0] / RATE * 1e3, "W" if rw else "R", addr, data, "".join(map(str, bits[48:50])), ok, ""))
        elif len(g) == 54:   # a read: the target drives data0, one clock more (turnaround); data at bits 15..46
            addr = int("".join(map(str, bits[1:8])), 2)
            data = int("".join(map(str, bits[15:47])), 2)
            ok = bits[47] == (sum(bits[15:47]) & 1)
            tx.append((g[0] / RATE * 1e3, "R", addr, data, "".join(map(str, bits[49:51])), ok, ""))
        elif len(g) == 585:
            words = [int("".join(map(str, bits[14 + 38 * i:14 + 38 * i + 32])), 2) for i in range(15)]
            tx.append((g[0] / RATE * 1e3, "BURST15", None, words, "", True, "".join(map(str, bits[:14]))))
        elif len(g) > 3:
            tx.append((g[0] / RATE * 1e3, f"RAW{len(g)}", None, None, "", True, "".join(map(str, bits))))
    return tx, Counter(len(g) for g in groups)

def memory_log(tx):
    """Pair abstract commands with data0/data1: memory and register accesses in the order they ran."""
    regs = {}; out = []
    for t, kind, addr, data, st, ok, extra in tx:
        if kind == "W" and addr in (0x04, 0x05):
            regs[addr] = data
        elif kind == "R" and addr == 0x04:
            if out and out[-1][1] in ("MEMR", "REGR") and out[-1][3] is None:
                out[-1] = out[-1][:3] + (data,)
        elif kind == "W" and addr == 0x17:
            cmdtype = data >> 24
            if cmdtype == 2:
                write = (data >> 16) & 1
                a = regs.get(0x05)
                out.append((t, "MEMW" if write else "MEMR", a, regs.get(0x04) if write else None))
                if (data >> 19) & 1 and a is not None:   # aampostincrement
                    regs[0x05] = a + (1 << ((data >> 20) & 7))
            elif cmdtype == 0:
                write = (data >> 16) & 1
                out.append((t, "REGW" if write else "REGR", data & 0xFFFF, regs.get(0x04) if write else None,
                            "postexec" if (data >> 18) & 1 else ""))
        elif kind == "BURST15":
            out.append((t, "BURST15", regs.get(0x05), data))
    return out

if __name__ == "__main__":
    for path in sorted(Path(sys.argv[1]).glob("*.sr")):
        tx, sizes = decode(path)
        with open(path.with_suffix(".dmi.txt"), "w") as f:
            f.write(f"# {path.name}: DMI transactions decoded from the wire (t in ms from capture start)\n")
            for t, kind, addr, data, st, ok, extra in tx:
                if kind in ("W", "R"):
                    f.write(f"{t:10.4f} {kind} {NAMES.get(addr, hex(addr)):12s} {data:08x} st={st}{'' if ok else ' PARITY'}\n")
                elif kind == "BURST15":
                    f.write(f"{t:10.4f} BURST15 hdr={extra} " + " ".join(f"{w:08x}" for w in data) + "\n")
                else:
                    f.write(f"{t:10.4f} {kind} {extra}\n")
        with open(path.with_suffix(".mem.txt"), "w") as f:
            f.write(f"# {path.name}: abstract-command memory / register accesses (addr from data1, value via data0)\n")
            for rec in memory_log(tx):
                t, kind, a = rec[0], rec[1], rec[2]
                if kind == "BURST15":
                    f.write(f"{t:10.4f} BURST15 (data1 {a if a is None else hex(a)}) " + " ".join(f"{w:08x}" for w in rec[3]) + "\n")
                else:
                    v = rec[3]
                    f.write(f"{t:10.4f} {kind} {('%08x' % a) if isinstance(a, int) else a} = {('%08x' % v) if v is not None else '?'} {rec[4] if len(rec) > 4 else ''}\n")
        bad = sum(1 for x in tx if x[1] in ("W", "R") and not x[5])
        print(f"{path.stem:18s} tx {len(tx):5d} parity-bad {bad:3d} kinds {dict(Counter(x[1] for x in tx).most_common(6))}")
