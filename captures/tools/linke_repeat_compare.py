"""Compare repeated LinkE wire captures bit by bit (wire-linke-p4-2026-09-25/more/<target>/repeat<N>_<op>.sr).

Frames come from tools/rvswd.py (START / STOP framing), so the don't-care fields (park/padding, tail) can be compared,
not only addr/data.

    cd captures && uv run python tools/linke_repeat_compare.py l103 target_info read_ram_256
"""
import difflib, sys
from collections import Counter
from pathlib import Path
import rvswd

FIXTURE = Path(__file__).resolve().parent.parent / "fixtures/wire-linke-p4-2026-09-25"


def key(bits):
    """Everything except the don't-care bits."""
    if len(bits) == 53:
        return bits[:9] + "|" + bits[14:47]
    if len(bits) >= 53 and rvswd.decode(bits) and rvswd.decode(bits)["kind"] == "BURST":
        return "B" + "".join(f"{w:08x}" for w in rvswd.decode(bits)["words"])
    return bits


def main(target, *ops):
    for op in ops:
        runs = []
        for i in range(1, 6):
            fr, _ = rvswd.frames(FIXTURE / f"more/{target}/repeat{i}_{op}.sr")
            runs.append([b for *_, r, b, end in fr])
        print(f"== {target} {op}: frames per run {[len(r) for r in runs]}")
        print("   lengths (run1):", sorted(Counter(len(b) for b in runs[0]).items()))
        for i, r in enumerate(runs[1:], 2):
            sm = difflib.SequenceMatcher(a=[key(b) for b in runs[0]], b=[key(b) for b in r], autojunk=False)
            diff = [(o[0], o[1], o[2] - o[1], o[4] - o[3]) for o in sm.get_opcodes() if o[0] != "equal"]
            print(f"   run{i} vs run1: {len(diff)} differing blocks {diff[:10]}")
        long = Counter(b for r in runs for b in r if len(b) == 85)
        print(f"   85-clock frames: {sum(long.values())} in 5 runs, {len(long)} distinct bit pattern(s)")
        for b, n in long.most_common():
            f = rvswd.decode(b)
            print(f"     x{n}: addr {f['addr']:#04x} data {f['data']:#010x} op {f['op']} par {f['parity']} | "
                  f"target {f['t_addr']} {f['t_data']:#010x} {f['t_status']} {f['t_parity']} | last {f['last']}")
        short = [b for r in runs for b in r if len(b) == 53]
        dc = Counter(("W" if b[7] == "1" else "R", rvswd.decode(b)["dontcare"]) for b in short)
        print(f"   53-clock frames {len(short)}; distinct don't-care patterns: "
              f"W {sum(1 for k in dc if k[0] == 'W')}, R {sum(1 for k in dc if k[0] == 'R')}")
        park = Counter(b[8] == b[9] for b in short)
        print(f"   bit9 == bit8 (parity): {park[True]} / {len(short)}")


if __name__ == "__main__":
    main(sys.argv[1], *sys.argv[2:])
