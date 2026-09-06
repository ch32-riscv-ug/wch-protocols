#!/usr/bin/env python3
"""harness の配線表を ch32-device-data の evidence から生成する。

入力(すべて ch32-device-data/evidence/):
  debug_wiring.csv   series ごとの SWDIO/SWCLK pad と 1/2 線両対応(confidence: confirmed)
  remap_routes.csv   selector/value ごとの signal → pad(confidence: reference)

出力(このディレクトリ):
  debug_pins.csv     series × debug 線(wire_modes を導出)
  bus_routes.csv     series × USART/UART/SPI/I2C/PIOC の全 route
  pin_conflicts.csv  同一 pad に 2 つ以上の役割が来る組(harness の配線判断に使う)
  coverage.csv       series ごとに、どの入力が存在したか(穴の可視化)

使い方:
  CH32_DEVICE_DATA=../../../../ch32-device-data python3 extract.py
既定は ../../../../ch32-device-data(この repo の隣に clone されている前提)。
"""
import csv, os, sys
from collections import defaultdict

SRC = os.environ.get("CH32_DEVICE_DATA",
                     os.path.join(os.path.dirname(__file__), "../../../../ch32-device-data"))
EV = os.path.join(SRC, "evidence")
OUT = os.path.dirname(os.path.abspath(__file__))

BUS_PREFIXES = ("USART", "UART", "SPI", "I2C", "PIOC")


def read(name):
    path = os.path.join(EV, name)
    if not os.path.exists(path):
        sys.exit(f"not found: {path}\nCH32_DEVICE_DATA を指定してください")
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def peripheral_of(signal):
    """USART1_TX -> USART1 / SPI_MOSI -> SPI / PIOC_IO0 -> PIOC"""
    return signal.rsplit("_", 1)[0] if "_" in signal else signal


def main():
    debug = read("debug_wiring.csv")
    routes = read("remap_routes.csv")

    # ---- debug_pins.csv -------------------------------------------------
    dbg = {}
    rows = []
    for r in debug:
        s, dio, clk = r["series"], r["swdio_pad"].strip(), r["swclk_pad"].strip()
        dual = r["dual_support"].strip().lower() == "yes"
        if dual:
            modes = "1-wire+2-wire"
        elif clk:
            modes = "2-wire"
        else:
            modes = "1-wire"
        dbg[s] = {"swdio": dio, "swclk": clk, "modes": modes}
        rows.append({"series": s, "swdio_pad": dio, "swclk_pad": clk,
                     "wire_modes": modes, "confidence": r["confidence"],
                     "basis": r["basis"]})
    rows.sort(key=lambda x: x["series"])
    with open(os.path.join(OUT, "debug_pins.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, ["series", "swdio_pad", "swclk_pad", "wire_modes",
                               "confidence", "basis"])
        w.writeheader(); w.writerows(rows)

    # ---- bus_routes.csv ------------------------------------------------
    bus = [r for r in routes if r["signal"].startswith(BUS_PREFIXES)]
    brows = [{"series": r["series"], "peripheral": peripheral_of(r["signal"]),
              "signal": r["signal"], "pad": r["pad"], "selector": r["selector"],
              "value": r["value"], "confidence": r["confidence"]} for r in bus]
    brows.sort(key=lambda x: (x["series"], x["peripheral"], x["signal"], x["value"]))
    with open(os.path.join(OUT, "bus_routes.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, ["series", "peripheral", "signal", "pad",
                               "selector", "value", "confidence"])
        w.writeheader(); w.writerows(brows)

    # ---- pin_conflicts.csv ---------------------------------------------
    # 同一 pad に来る「異なる signal」を衝突とみなす。
    # 同じ signal が複数 remap value で同じ pad に出るのは衝突ではない。
    roles = defaultdict(set)          # (series, pad) -> {role}
    for r in bus:
        roles[(r["series"], r["pad"])].add(r["signal"])
    for s, d in dbg.items():
        if d["swdio"]:
            roles[(s, d["swdio"])].add("DEBUG_SWDIO" if d["swclk"] else "DEBUG_SWIO")
        if d["swclk"]:
            roles[(s, d["swclk"])].add("DEBUG_SWCLK")

    crows = []
    for (s, pad), rs in roles.items():
        if len(rs) < 2:
            continue
        has_debug = any(x.startswith("DEBUG_") for x in rs)
        crows.append({
            "series": s, "pad": pad, "n_roles": len(rs),
            "involves_debug": "yes" if has_debug else "no",
            "roles": ";".join(sorted(rs)),
            # debug が絡む行だけ confirmed 側の証拠を含む
            "confidence": "confirmed+reference" if has_debug else "reference",
        })
    crows.sort(key=lambda x: (x["involves_debug"] == "no", x["series"], x["pad"]))
    with open(os.path.join(OUT, "pin_conflicts.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, ["series", "pad", "n_roles", "involves_debug",
                               "roles", "confidence"])
        w.writeheader(); w.writerows(crows)

    # ---- coverage.csv --------------------------------------------------
    all_series = sorted(set(dbg) | {r["series"] for r in routes})
    bus_series = {r["series"] for r in bus}
    route_series = {r["series"] for r in routes}
    cov = []
    for s in all_series:
        cov.append({
            "series": s,
            "has_debug_pins": "yes" if s in dbg else "no",
            "has_any_remap": "yes" if s in route_series else "no",
            "has_bus_routes": "yes" if s in bus_series else "no",
            "bus_route_rows": sum(1 for r in bus if r["series"] == s),
        })
    with open(os.path.join(OUT, "coverage.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, ["series", "has_debug_pins", "has_any_remap",
                               "has_bus_routes", "bus_route_rows"])
        w.writeheader(); w.writerows(cov)

    print(f"debug_pins.csv    {len(rows)} series")
    print(f"bus_routes.csv    {len(brows)} rows / {len(bus_series)} series")
    print(f"pin_conflicts.csv {len(crows)} rows "
          f"({sum(1 for c in crows if c['involves_debug']=='yes')} が debug 絡み)")
    print(f"coverage.csv      {len(cov)} series "
          f"({sum(1 for c in cov if c['has_bus_routes']=='no')} が bus route 無し)")


if __name__ == "__main__":
    main()
