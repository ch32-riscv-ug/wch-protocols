"""Structural check of the P4 wire captures, with the decoding of the 2026-09-11 fixture's analyze.py (X035 RVSWD):
sample SWDIO on each SWCLK rising edge; 53-clock short frames = addr7 + R/W + parity + park + pad4 + data32 + ..."""
import array, sys, zipfile
from collections import Counter
from pathlib import Path

def read_sr(path):
    with zipfile.ZipFile(path) as z:
        s = array.array("B"); s.frombytes(z.read("logic-1-1")); return s

def rising(s):
    out, prev = [], s[0] & 1
    for i in range(1, len(s)):
        c = s[i] & 1
        if c and not prev: out.append(i)
        prev = c
    return out

def groups(edges, gap):
    out, cur = [], []
    for e in edges:
        if cur and e - cur[-1] > gap: out.append(cur); cur = []
        cur.append(e)
    if cur: out.append(cur)
    return out

def bits_int(bits):
    v = 0
    for b in bits: v = v << 1 | b
    return v

def frame(s, g):
    b = [(s[i] >> 1) & 1 for i in g]
    return b, bits_int(b[:7]), b[7], bits_int(b[14:46])

pattern = [int.from_bytes(bytes(range(256))[i:i+4] * 1, "little") for i in range(0, 256, 4)] * 16
for path in sorted(Path(sys.argv[1]).glob("*.sr")):
    s = read_sr(path); r = rising(s); gs = groups(r, 100)
    sizes = Counter(len(g) for g in gs)
    frames = [frame(s, g) for g in gs if len(g) == 53]
    hp = sum(b[8] != (sum(b[:8]) & 1) for b, *_ in frames)
    dp = sum(b[46] != (sum(b[14:46]) & 1) for b, *_ in frames)
    line = f"{path.stem:18s} clocks {len(r):7d} groups {len(gs):5d} sizes {dict(sizes.most_common(4))} frames53 {len(frames)} parity errors hdr {hp} data {dp}"
    if "pattern4k" in path.stem and "flash" in path.stem:
        writes = [d for b, a, rw, d in frames if a == 4 and rw == 1]
        found = next((i for i in range(len(writes) - 1023) if writes[i:i + 1024] == pattern), None)
        line += f" | data0 writes {len(writes)}, 4 KiB pattern in order at {found}"
    print(line)
