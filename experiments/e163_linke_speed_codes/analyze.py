"""E163: SWCLK period of the DMI frames sent after AttachChip, per SetSpeed experiment (out/<name>.sr + .json).

    cd captures && uv run python ../experiments/e163_linke_speed_codes/analyze.py ../experiments/e163_linke_speed_codes/out
"""
import json, sys
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "captures/tools"))
import rvswd


def summarize(sr):
    fr, rate = rvswd.frames(sr)
    rows = []
    for s, e, r, b, end in fr:
        f = rvswd.decode(b) if end.startswith("stop") else None
        if not f or len(r) < 2:
            continue
        rows.append((s / rate * 1e3, f, np.diff(r) / rate * 1e9, len(r)))
    # the probe DMI ops of raw_speed.py: 20 x R dmstatus, then W data0 = 0x11111111 * k, then memory reads
    marks = [i for i, (t, f, p, n) in enumerate(rows) if f["kind"] == "W" and f["addr"] == 0x04
             and f["data"] in (0x11111111, 0x22222222, 0x33333333, 0x44444444)]
    out = {"frames": len(rows)}
    if marks:
        first = marks[0]
        stat = [rows[i] for i in range(max(0, first - 20), first) if rows[i][1]["addr"] == 0x11]
        tail = rows[first:first + 16]
        def per(sel):
            v = np.concatenate([p for _, _, p, _ in sel]) if sel else np.array([])
            return {"n": len(sel), "p10": float(np.percentile(v, 10)) if len(v) else None,
                    "median_ns": float(np.median(v)) if len(v) else None,
                    "frame_median_ns": float(np.median([np.median(p) for _, _, p, _ in sel])) if sel else None}
        out["dmstatus_reads"] = per(stat)
        out["writes_and_memreads"] = per(tail)
        out["dmstatus_values"] = sorted({f"{r[1]['data']:08x}" for r in stat})
        out["data0_writes"] = [f"{rows[i][1]['data']:08x}" for i in marks]
    return out


def main(d):
    for sr in sorted(Path(d).glob("*.sr")):
        meta = json.loads(sr.with_suffix(".json").read_text())
        usb = [json.loads(l) for l in meta.get("stdout", "").splitlines() if l.startswith("{")]
        speed = [u for u in usb if u["out"].startswith("810c")]
        s = summarize(sr)
        a, b = s.get("dmstatus_reads", {}), s.get("writes_and_memreads", {})
        print(f"{sr.stem:14} setspeed {[(u['out'][6:], u['in']) for u in speed]}  "
              f"dmstatus x{a.get('n')}: median {a.get('median_ns')} ns | writes/memreads: median {b.get('median_ns')} ns "
              f"p10 {b.get('p10')}  values {s.get('dmstatus_values')} {s.get('data0_writes')}")


if __name__ == "__main__":
    main(sys.argv[1])
