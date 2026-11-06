"""Compare repeated LinkE wire captures bit by bit (wire-linke-p4-2026-09-25/more/<target>/repeat<N>_<op>.sr), from
WireSkein's decode of each (tools/wireskein_decode.sh): each frame's `bits` are the bits as sampled, the don't-care
fields (park/padding, tail) included, so they can be compared, not only addr/data. The bit positions below are
link-to-target's (#short-frame, #long-frame).

    cd captures && WIRESKEIN=<wireskein checkout> sh tools/wireskein_decode.sh <dir> \\
        fixtures/wire-linke-p4-2026-09-25/more/l103/repeat*_{target_info,read_ram_256}.sr
    uv run python tools/linke_repeat_compare.py <dir> l103 target_info read_ram_256
"""
import difflib, json, sys
from collections import Counter
from pathlib import Path

FIX = "fixtures/wire-linke-p4-2026-09-25"


def run(decoded: Path, target: str, op: str, i: int) -> list[dict]:
    d = json.loads((decoded / FIX / f"more/{target}/repeat{i}_{op}.json").read_text())
    return [f for part in d["parts"] for bus in part["buses"] for f in bus.get("result", {}).get("dmi", [])]


def key(f: dict) -> str:
    """Everything except the don't-care bits (a burst: its words)."""
    b = f["bits"]
    if f["op"] == "BURST":
        return "B" + "".join(f"{w:08x}" for w in f["data"])
    if len(b) == 53:
        return b[:9] + "|" + b[14:47]
    return b


def long_fields(b: str) -> str:
    n = lambda s: int(s, 2)
    return (f"addr {n(b[0:7]):#04x} data {n(b[7:39]):#010x} op {b[39:41]} bit41 {b[41]} | "
            f"target {b[42:49]} {n(b[49:81]):#010x} {b[81:83]} bit83 {b[83]} | last {b[84]}")


def main(decoded: Path, target: str, *ops: str) -> None:
    for op in ops:
        runs = [run(decoded, target, op, i) for i in range(1, 6)]
        print(f"== {target} {op}: frames per run {[len(r) for r in runs]}")
        print("   lengths (run1):", sorted(Counter(len(f["bits"]) for f in runs[0]).items()))
        for i, r in enumerate(runs[1:], 2):
            sm = difflib.SequenceMatcher(a=[key(f) for f in runs[0]], b=[key(f) for f in r], autojunk=False)
            diff = [(o[0], o[1], o[2] - o[1], o[4] - o[3]) for o in sm.get_opcodes() if o[0] != "equal"]
            print(f"   run{i} vs run1: {len(diff)} differing blocks {diff[:10]}")
        long = Counter(f["bits"] for r in runs for f in r if len(f["bits"]) == 85)
        print(f"   85-clock frames: {sum(long.values())} in 5 runs, {len(long)} distinct bit pattern(s)")
        for b, n in long.most_common():
            print(f"     x{n}: {long_fields(b)}")
        short = [f["bits"] for r in runs for f in r if len(f["bits"]) == 53 and f["op"] != "BURST"]
        dc = Counter(("W" if b[7] == "1" else "R", b[9:14] + b[47:53]) for b in short)
        print(f"   53-clock frames {len(short)}; distinct don't-care patterns: "
              f"W {sum(1 for k in dc if k[0] == 'W')}, R {sum(1 for k in dc if k[0] == 'R')}")
        print(f"   bit9 == bit8 (parity): {sum(b[8] == b[9] for b in short)} / {len(short)}")


if __name__ == "__main__":
    main(Path(sys.argv[1]), *sys.argv[2:])
