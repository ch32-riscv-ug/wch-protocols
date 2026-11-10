"""E163: SWCLK period of the DMI frames sent after AttachChip, per SetSpeed experiment (stdlib only).

The frames and every sampling edge's time come from WireSkein's decode of out/<name>.sr (WireSkein
0.0.27 or later from PyPI, with the `bits` layer); the USB side from out/<name>.json. Decode first:

    for f in <e163>/out/*.sr; do
      uvx --from 'wireskein>=0.0.27' wireskein decode "$f" \
        --spec '{"buses":[{"protocol":"rvswd","pins":{"clk":"SWCLK","dio":"SWDIO"},"layers":["bits"]}]}' \
        --out <dir>/"$(basename "$f" .sr)".json
    done

then:

    python3 <e163>/analyze.py <dir>/*.json
"""
import bisect
import json
import statistics
import sys
from pathlib import Path

OUT = Path(__file__).resolve().parent / "out"


def percentile(values, q):
    """numpy's default (linear) percentile."""
    ordered = sorted(values)
    position = (len(ordered) - 1) * q / 100
    low = int(position)
    high = min(low + 1, len(ordered) - 1)
    return ordered[low] + (ordered[high] - ordered[low]) * (position - low)


def summarize(document):
    """The frames of raw_speed.py's DMI ops: 20 x R dmstatus, then W data0 = 0x11111111 * k, then memory reads."""
    (part,) = document["parts"]
    (bus,) = part["buses"]
    assert bus["protocol"] == "rvswd"
    numerator, denominator = document["capture"]["tick_hz"]
    tick_ns = 1e9 * denominator / numerator
    rows = bus["result"]["dmi"] if bus["decoded"] else []
    edges = bus["result"]["bits"]["t"] if bus["decoded"] else []

    def intervals(f):
        """The SWCLK intervals between the frame's sampling edges (s to e), in ns."""
        a, b = bisect.bisect_left(edges, f["s"]), bisect.bisect_right(edges, f["e"])
        return [(edges[i + 1] - edges[i]) * tick_ns for i in range(a, b - 1)]

    marks = [i for i, f in enumerate(rows) if f["op"] == "W" and f["addr"] == 0x04
             and f["data"] in (0x11111111, 0x22222222, 0x33333333, 0x44444444)]
    out = {"frames": len(rows), "not_ok": sum(not f["ok"] for f in rows if f["form"] == "short"),
           "repaired": sum(bool(f.get("repaired")) for f in rows)}
    if marks:
        first = marks[0]
        stat = [rows[i] for i in range(max(0, first - 20), first) if rows[i]["addr"] == 0x11]
        tail = rows[first:first + 16]

        def per(sel):
            v = [x for f in sel for x in intervals(f)]
            return {"n": len(sel), "p10": percentile(v, 10) if v else None,
                    "median_ns": statistics.median(v) if v else None}
        out["dmstatus_reads"] = per(stat)
        out["writes_and_memreads"] = per(tail)
        out["dmstatus_values"] = sorted({f"{f['data']:08x}" for f in stat if f["data"] is not None})
        out["data0_writes"] = [f"{rows[i]['data']:08x}" for i in marks]
    return out


def main(paths):
    documents = sorted((json.loads(Path(p).read_text()) for p in paths), key=lambda d: Path(d["capture"]["file"]).stem)
    for document in documents:
        assert document["format"] == "wireskein-analysis/1"
        name = Path(document["capture"]["file"]).stem
        meta = json.loads((OUT / f"{name}.json").read_text())
        usb = [json.loads(l) for l in meta.get("stdout", "").splitlines() if l.startswith("{")]
        speed = [u for u in usb if u["out"].startswith("810c")]
        s = summarize(document)
        a, b = s.get("dmstatus_reads", {}), s.get("writes_and_memreads", {})
        print(f"{name:14} setspeed {[(u['out'][6:], u['in']) for u in speed]}  "
              f"dmstatus x{a.get('n')}: median {a.get('median_ns')} ns | writes/memreads: median {b.get('median_ns')} ns "
              f"p10 {b.get('p10')}  values {s.get('dmstatus_values')} {s.get('data0_writes')}"
              + (f"  short frames failing checks: {s['not_ok']}" if s["not_ok"] else "")
              + (f"  frames repaired: {s['repaired']}" if s["repaired"] else ""))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(f"usage: {sys.argv[0]} DECODE.json...")
    main(sys.argv[1:])
