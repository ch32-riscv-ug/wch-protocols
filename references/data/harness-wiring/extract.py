#!/usr/bin/env python3
"""harness の配線表を ch32-device-data の **index/** から生成する。

なぜ index/ か: ch32-device-data は 3 層に分かれている(`docs/data-layout.ja.md`)。
  evidence/ = 資料の綴りのまま   index/ = 利用者が引くための表   catalog/ = 名前
`index/` は `tools/build_index.py` が evidence から組み直したもので、
`tools/check_tables.py` が「索引の行は証拠に戻せる」ことを毎回検証している。
**引くのは index/**。初版は evidence/ を読んでいて、confidence と網羅の両方で劣っていた。

入力(ch32-device-data/index/):
  debug_interfaces.csv  series × debug_if(1-wire/2-wire/both)+ SWDIO/SWCLK pad
  pinout.csv            part_number × pad × (peripheral, role, route, selector, value)

出力(このディレクトリ):
  debug_pins.csv    series × debug 線(index の debug_if をそのまま使う)
  routes.csv        series × pad × 役割(harness が関心を持つ class に絞る)
  pin_conflicts.csv 同一 pad に 2 つ以上の役割が来る組
  coverage.csv      series ごとの網羅状況

使い方:
  CH32_DEVICE_DATA=../../../../ch32-device-data python3 extract.py
"""
import csv, os, sys
from collections import defaultdict

SRC = os.environ.get("CH32_DEVICE_DATA",
                     os.path.join(os.path.dirname(__file__), "../../../../ch32-device-data"))
IDX = os.path.join(SRC, "index")
OUT = os.path.dirname(os.path.abspath(__file__))

# harness が関心を持つ役割の class。これ以外(power/nc/USB/CAN/…)は落とす。
def classify(periph, signal):
    p = periph.upper()
    if p.startswith(("USART", "UART")):
        return "uart"
    if p.startswith("SPI"):
        return "spi"
    if p.startswith("I2C"):
        return "i2c"
    if p.startswith("PIOC"):
        return "pioc"
    if signal.upper() == "MCO" or p == "RCC" and signal.upper() == "MCO":
        return "clock"      # 比率測定に使える基準クロック出力
    if p.startswith("ADC") or p.startswith("DAC") or p.startswith("OPA") or p.startswith("CMP"):
        return "analog"
    if p.startswith("TIM"):
        return "timer"      # PWM 観測
    return None


def read(name):
    path = os.path.join(IDX, name)
    if not os.path.exists(path):
        sys.exit(f"not found: {path}\nCH32_DEVICE_DATA を指定してください")
    with open(path, newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def main():
    dbg_rows = read("debug_interfaces.csv")
    pinout = read("pinout.csv")

    # ---- debug_pins.csv (index の debug_if をそのまま。導出しない) -------
    dbg = {}
    out = []
    for r in dbg_rows:
        s = r["series"]
        dbg[s] = {"if": r["debug_if"], "swdio": r["swdio_pads"], "swclk": r["swclk_pads"]}
        out.append({k: r[k] for k in
                    ("series", "family", "debug_if", "swdio_pads", "swclk_pads",
                     "wording", "section", "confidence", "basis")})
    out.sort(key=lambda x: x["series"])
    with open(os.path.join(OUT, "debug_pins.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, list(out[0].keys())); w.writeheader(); w.writerows(out)

    # ---- routes.csv -----------------------------------------------------
    # pinout は part_number 単位。series ごとに (pad, role) を畳み、
    # どの型番でその pad が出ているかを数える(小パッケージで欠ける pad が見える)。
    agg = defaultdict(lambda: {"parts": set(), "routes": set(), "class": "",
                               "confidence": set()})
    parts_of_series = defaultdict(set)
    for r in pinout:
        parts_of_series[r["series"]].add(r["part_number"])
        cls = classify(r["peripheral"], r["signal"])
        if not cls:
            continue
        role = f"{r['peripheral']}.{r['role']}" if r["role"] else r["peripheral"]
        key = (r["series"], r["pad"], role)
        a = agg[key]
        a["class"] = cls
        a["parts"].add(r["part_number"])
        if r["route"]:
            a["routes"].add(r["route"])
        a["confidence"].add(r["confidence"])

    rrows = []
    for (s, pad, role), a in agg.items():
        rrows.append({"series": s, "pad": pad, "role": role, "class": a["class"],
                      "routes": ";".join(sorted(a["routes"])),
                      "parts_with_pad": len(a["parts"]),
                      "parts_in_series": len(parts_of_series[s]),
                      "confidence": ";".join(sorted(a["confidence"]))})
    rrows.sort(key=lambda x: (x["series"], x["pad"], x["role"]))
    with open(os.path.join(OUT, "routes.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, list(rrows[0].keys())); w.writeheader(); w.writerows(rrows)

    # ---- pin_conflicts.csv ---------------------------------------------
    roles = defaultdict(set)
    for r in rrows:
        roles[(r["series"], r["pad"])].add(r["role"])
    for s, d in dbg.items():
        for pad in [p.strip() for p in d["swdio"].split(";") if p.strip()]:
            roles[(s, pad)].add("DEBUG.SWDIO" if d["if"] != "1-wire" else "DEBUG.SWIO")
        for pad in [p.strip() for p in d["swclk"].split(";") if p.strip()]:
            roles[(s, pad)].add("DEBUG.SWCLK")

    crows = []
    for (s, pad), rs in sorted(roles.items()):
        if len(rs) < 2:
            continue
        has_dbg = any(x.startswith("DEBUG.") for x in rs)
        crows.append({"series": s, "pad": pad, "n_roles": len(rs),
                      "involves_debug": "yes" if has_dbg else "no",
                      "roles": ";".join(sorted(rs))})
    crows.sort(key=lambda x: (x["involves_debug"] == "no", x["series"], x["pad"]))
    with open(os.path.join(OUT, "pin_conflicts.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, list(crows[0].keys())); w.writeheader(); w.writerows(crows)

    # ---- coverage.csv ---------------------------------------------------
    have = defaultdict(set)
    for r in rrows:
        have[r["series"]].add(r["class"])
    cov = []
    for s in sorted(set(dbg) | set(parts_of_series)):
        cls = have.get(s, set())
        cov.append({"series": s,
                    "parts": len(parts_of_series.get(s, ())),
                    "debug_if": dbg.get(s, {}).get("if", "-"),
                    **{c: ("yes" if c in cls else "no")
                       for c in ("uart", "spi", "i2c", "pioc", "clock", "analog", "timer")}})
    with open(os.path.join(OUT, "coverage.csv"), "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, list(cov[0].keys())); w.writeheader(); w.writerows(cov)

    print(f"debug_pins.csv    {len(out)} series")
    print(f"routes.csv        {len(rrows)} rows / {len(have)} series / "
          f"{sum(len(v) for v in parts_of_series.values())} part 参照")
    print(f"pin_conflicts.csv {len(crows)} rows "
          f"({sum(1 for c in crows if c['involves_debug']=='yes')} が debug 絡み)")
    print(f"coverage.csv      {len(cov)} series")


if __name__ == "__main__":
    main()
