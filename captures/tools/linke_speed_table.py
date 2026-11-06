"""SWCLK / SWIO timing per LinkE speed setting (wire-linke-p4-2026-09-25/extra/<target>/speed_*), from WireSkein's
decode of each capture (tools/wireskein_decode.sh): the timing is WireSkein's, measured per frame.

For RVSWD it reports the median of the frames' SWCLK period (`period_ns`), split by frame kind (short / long / burst);
short frames at attach run at the slow attach rate, so both clusters are shown. For SWIO, over the host's 41-pulse
frames (not the target's fast-read ones), the median of the frames' LOW widths of 1 / 0 bits (`short_median_ns` /
`long_median_ns`), of the high between pulses (`space_ns`) and of the period after a 1 / a 0 (`pitch_1_ns` /
`pitch_0_ns`).

    cd captures && WIRESKEIN=<wireskein checkout> sh tools/wireskein_decode.sh <dir> \\
        fixtures/wire-linke-p4-2026-09-25/extra/*/speed_*.sr
    uv run python tools/linke_speed_table.py <dir>
"""
import json, sys
from collections import defaultdict
from pathlib import Path
from statistics import median

FIX = "fixtures/wire-linke-p4-2026-09-25"
OPS = ("target_info", "read_ram_256", "read_flash_4k")


def frames(decoded: Path, target: str, speed: str, op: str) -> list[dict]:
    d = json.loads((decoded / FIX / f"extra/{target}/speed_{speed}_{op}.json").read_text())
    return [f for part in d["parts"] for bus in part["buses"] for f in bus.get("result", {}).get("dmi", [])]


def main(decoded: Path) -> None:
    for target in ("l103", "v203"):
        for speed in ("high", "medium", "low"):
            per = defaultdict(list)
            for op in OPS:
                for f in frames(decoded, target, speed, op):
                    if f.get("period_ns") is not None:
                        per["burst" if f["op"] == "BURST" else f.get("form", "short")].append(f["period_ns"])
            cells = []
            for kind in ("long", "short", "burst"):
                v = per.get(kind, [])
                if v:
                    parts = [f"{median(x):.0f} ns x{len(x)}" for x in ([p for p in v if p < 1500], [p for p in v if p >= 1500]) if x]
                    cells.append(f"{kind}: " + " / ".join(parts))
            print(f"{target} {speed:6}  " + "  |  ".join(cells))
    for speed in ("high", "medium", "low"):
        keys = ("short_median_ns", "long_median_ns", "space_ns", "pitch_1_ns", "pitch_0_ns")
        got = {k: [] for k in keys}
        for op in OPS:
            for f in frames(decoded, "v003", speed, op):
                if f.get("fast"):
                    continue
                for k in keys:
                    if f.get(k) is not None:
                        got[k].append(f[k])
        m = {k: median(v) for k, v in got.items()}
        print(f"v003 {speed:6}  1: {m['short_median_ns']:.0f} ns  0: {m['long_median_ns']:.0f} ns  "
              f"high between: {m['space_ns']:.0f} ns  period after 1: {m['pitch_1_ns']:.0f} ns  "
              f"after 0: {m['pitch_0_ns']:.0f} ns")

if __name__ == "__main__":
    main(Path(sys.argv[1]))
