"""Compare repeated LinkE wire captures bit by bit (wire-linke-p4-2026-09-25/more/<target>/repeat<N>_<op>.sr).

Uses the fixture's own decoder (tools/dmi_decode.py) for frame grouping, and takes the raw SWDIO bits of each frame
so that the don't-care fields (park/padding, tail) can be compared, not only addr/data.

    cd captures && SPLIT=two DEBOUNCE=3 uv run python tools/linke_repeat_compare.py l103 target_info read_ram_256
"""
import difflib, inspect, sys
from collections import Counter
from pathlib import Path

FIXTURE = Path(__file__).resolve().parent.parent / "fixtures/wire-linke-p4-2026-09-25"
sys.path.insert(0, str(FIXTURE / "tools"))
import dmi_decode  # noqa: E402

# Same decode(), but return (clock count, SWDIO bits) per frame instead of the DMI transactions.
_ANCHOR = "    tx = []\n"
_src = inspect.getsource(dmi_decode.decode)
assert _src.count(_ANCHOR) == 1, "dmi_decode.decode() changed; update the anchor"
_ns = dict(vars(dmi_decode))
exec(_src.replace(_ANCHOR, "    return [(len(g), ''.join(str(int(dio[i])) for i in g)) for g in groups if len(g) > 3]\n"), _ns)
frames = _ns["decode"]


def fields(n, b):
    """(kind, addr/RW/parity + data + data parity, don't-care bits) for the known frame lengths."""
    if n == 53:
        return ("W" if b[7] == "1" else "R53", b[:9] + "|" + b[14:47], b[9:14] + "|" + b[47:])
    if n == 54:
        return ("R54", b[:10] + "|" + b[15:48], b[10:15] + "|" + b[48:])
    if n == 585:
        return ("B", b[:8], b[8:14])
    return (f"L{n}", b, "")


def main(target, *ops):
    for op in ops:
        runs = [frames(FIXTURE / f"more/{target}/repeat{i}_{op}.sr") for i in range(1, 6)]
        print(f"== {target} {op}: frames per run {[len(r) for r in runs]}")
        print("   lengths (run1):", sorted(Counter(n for n, _ in runs[0]).items()))
        for i, r in enumerate(runs[1:], 2):
            sm = difflib.SequenceMatcher(a=[fields(*f)[1] for f in runs[0]], b=[fields(*f)[1] for f in r], autojunk=False)
            diff = [(o[0], o[1], [runs[0][k][0] for k in range(o[1], o[2])], [r[k][0] for k in range(o[3], o[4])])
                    for o in sm.get_opcodes() if o[0] != "equal"]
            print(f"   run{i} vs run1: {len(diff)} differing blocks {diff[:10]}")
        long = Counter(b for r in runs for n, b in r if n == 85)
        print(f"   85-clock frames: {sum(long.values())} in 5 runs, {len(long)} distinct bit pattern(s)")
        for b in long:
            print(f"     addr {int(b[:7], 2):#04x} data {int(b[7:39], 2):#010x} op {b[39:41]} par {b[41]} | "
                  f"target {b[42:49]} {int(b[49:81], 2):#010x} {b[81:83]} {b[83]} | last {b[84]}")
        dc = Counter((fields(n, b)[0], fields(n, b)[2]) for r in runs for n, b in r if n in (53, 54))
        kinds = Counter()
        for (k, d), c in dc.items():
            kinds[k] += 1
        print(f"   distinct don't-care patterns per kind: {dict(kinds)}")
        park = Counter((b[8] == b[9]) for r in runs for n, b in r if n == 53)
        print(f"   53-clock frames with bit9 == bit8 (parity): {park[True]} / {park[True] + park[False]}")


if __name__ == "__main__":
    main(sys.argv[1], *sys.argv[2:])
