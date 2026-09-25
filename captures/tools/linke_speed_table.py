"""SWCLK / SWIO timing per LinkE speed setting (wire-linke-p4-2026-09-25/extra/<target>/speed_*).

For RVSWD it reports the median SWCLK rising-edge period inside frames, split by frame kind; for SWIO the median
LOW width of 1 / 0 bits and the median pulse-to-pulse period inside 41-pulse frames.

    cd captures && uv run python tools/linke_speed_table.py
"""
from collections import defaultdict
from pathlib import Path
import numpy as np
import rvswd, swio

FIX = Path(__file__).resolve().parent.parent / "fixtures/wire-linke-p4-2026-09-25"
OPS = ("target_info", "read_ram_256", "read_flash_4k")


def rvswd_row(p, k):
    fr, rate = rvswd.frames(p, k=k, k_frame=max(k, 3))
    per = defaultdict(list)
    for s, e, r, b, end in fr:
        if not end.startswith("stop") or len(r) < 2:
            continue
        f = rvswd.decode(b)
        if f is None:
            continue
        kind = {"W": "short", "R": "short"}.get(f["kind"], f["kind"].lower())
        per[kind].append(np.median(np.diff(r)) / rate * 1e9)
    return {k2: np.array(v) for k2, v in per.items()}


def main():
    for target, k in (("l103", 3), ("v203", 3)):
        for speed in ("high", "medium", "low"):
            per = defaultdict(list)
            for op in OPS:
                for kind, v in rvswd_row(FIX / f"extra/{target}/speed_{speed}_{op}.sr", k).items():
                    per[kind].extend(v)
            cells = []
            for kind in ("long", "short", "burst"):
                v = np.array(per.get(kind, []))
                if len(v):
                    # short frames at attach run at the slow attach rate; show both clusters
                    lo, hi = v[v < 1500], v[v >= 1500]
                    parts = [f"{np.median(x):.0f} ns x{len(x)}" for x in (lo, hi) if len(x)]
                    cells.append(f"{kind}: " + " / ".join(parts))
            print(f"{target} {speed:6}  " + "  |  ".join(cells))
    for speed in ("high", "medium", "low"):
        ones, zeros, period = [], [], []
        for op in OPS:
            fr, rate = swio.frames(FIX / f"extra/v003/speed_{speed}_{op}.sr")
            for s, b, w in fr:
                if len(b) == 41:
                    ones.extend(w[np.array([c == "1" for c in b])]); zeros.extend(w[np.array([c == "0" for c in b])])
            starts, widths, rate = swio.pulses(FIX / f"extra/v003/speed_{speed}_{OPS[0]}.sr")
            d = np.diff(starts) / rate * 1e9
            period.extend(d[d < 4000])
        print(f"v003 {speed:6}  1: {np.median(ones):.0f} ns  0: {np.median(zeros):.0f} ns  pulse period: {np.median(period):.0f} ns")


if __name__ == "__main__":
    main()
