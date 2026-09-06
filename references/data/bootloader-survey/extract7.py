#!/usr/bin/env python3
"""bootloader 横断調査 — reg_ops の全 series 展開(U5)

EVT IAP が呼ぶ flash 関数の実体は SDK(`EVT/EXAM/SRC/Peripheral/src/ch32*_flash.c`)にある。
その関数本体から MMIO 操作列(FLASH->XXX への読み書きと待ち)を取り出して
`reg_ops.csv` と同じ座標系に落とす。

比較の単位は「操作」であって関数でも命令でもない(調査設計 §4.1)。
"""
import csv
import os
import re
from pathlib import Path

ROOT = Path(os.environ.get("WCH_ROOT", Path(__file__).resolve().parents[4]))
OUT = Path(__file__).resolve().parent
TAIL = ["#", "confidence", "basis"]

SERIES = [
    ("evt-v003-iap", "CH32V003", "CH32V003"), ("evt-v00x-iap", "CH32V006", "CH32V00X"),
    ("evt-v103-iap", "CH32V103", "CH32V103"), ("evt-v205-iap", "CH32V205", "CH32V205"),
    ("evt-v20x-iap", "CH32V20x", "CH32V20x"), ("evt-v30x-iap", "CH32V307", "CH32V30x"),
    ("evt-v407-iap", "CH32V407", "CH32V407"), ("evt-x035-iap", "CH32X035", "CH32X035"),
    ("evt-x315-iap", "CH32X315", "CH32X315"), ("evt-l103-iap", "CH32L103", "CH32L103"),
    ("evt-m030-iap", "CH32M030", "CH32M030"), ("evt-h417-v3f-iap", "CH32H417", "CH32H417"),
]
# IAP が呼ぶ入口(flash_ops.csv の api 列に出たもの)
ENTRIES = ["FLASH_Unlock_Fast", "FLASH_Lock_Fast", "FLASH_ProgramPage_Fast",
           "FLASH_ErasePage_Fast", "FLASH_ROM_WRITE", "FLASH_ROM_ERASE",
           "FLASH_BufReset", "FLASH_BufLoad", "FLASH_Unlock", "FLASH_Lock"]

# FLASH レジスタの番地(V003 以外は共通の 0x40022000 ブロック)
REGADDR = {"ACTLR": 0x40022000, "KEYR": 0x40022004, "OBKEYR": 0x40022008,
           "STATR": 0x4002200C, "CTLR": 0x40022010, "ADDR": 0x40022014,
           "OBR": 0x4002201C, "WPR": 0x40022020, "MODEKEYR": 0x40022024,
           "BOOT_MODEKEYR": 0x40022028}


def read(p):
    b = p.read_bytes()
    for enc in ("utf-8", "gb18030", "latin-1"):
        try:
            return b.decode(enc)
        except UnicodeDecodeError:
            continue
    return b.decode("latin-1", "replace")


def rel(p): return str(p.relative_to(ROOT)).replace(os.sep, "/")


def find_sdk(repo):
    hits = sorted((ROOT / repo / "EVT/EXAM/SRC/Peripheral/src").glob("ch32*_flash.c"))
    return hits[0] if hits else None


def body_of(text, name):
    """関数 `name` の本体(最初の { から対応する } まで)を返す。"""
    m = re.search(rf"\b(?:void|FLASH_Status|uint\d+_t|u\d+)\s+{re.escape(name)}\s*\([^)]*\)\s*\{{", text)
    if not m:
        return None, None
    i = m.end() - 1
    depth, j = 0, i
    while j < len(text):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return text[i + 1:j], text[:m.start()].count("\n") + 1


STMT = re.compile(
    r"(?P<wait>while\s*\(\s*FLASH->(?P<wreg>\w+)\s*&\s*(?P<wbit>[\w|() ]+?)\s*\)\s*;?)"
    r"|(?P<asn>FLASH->(?P<areg>\w+)\s*(?P<op>\|=|&=|=)\s*(?P<val>[^;]+);)"
    r"|(?P<call>\bFLASH_\w+\s*\([^;]*\);)"
    r"|(?P<loop>while\s*\(\s*(?P<lv>\w+)\s*\))"
    r"|(?P<store>\*\(\s*(?:__IO\s+)?uint32_t\s*\*\s*\)\s*[^;=]+=\s*[^;]+;)")


def ops_for(pid, sdk_path, text, entry):
    b, ln = body_of(text, entry)
    if b is None:
        return []
    rows, seq = [], 0
    for m in STMT.finditer(b):
        line = ln + b[:m.start()].count("\n")
        if m.group("wait"):
            reg = m.group("wreg")
            rows.append([pid, entry, seq, "wait", f"FLASH_{reg}",
                         hex(REGADDR.get(reg, 0)) if reg in REGADDR else "",
                         "", "", "", f"FLASH_{reg}", m.group("wbit").strip(), "",
                         "c-sdk", rel(sdk_path), line])
        elif m.group("asn"):
            reg, op, val = m.group("areg"), m.group("op"), m.group("val").strip()
            kind = {"=": "write", "|=": "set-bits", "&=": "clear-bits"}[op]
            rows.append([pid, entry, seq, kind, f"FLASH_{reg}",
                         hex(REGADDR.get(reg, 0)) if reg in REGADDR else "",
                         val[:60], "", "", "", "", "", "c-sdk", rel(sdk_path), line])
        elif m.group("store"):
            rows.append([pid, entry, seq, "write", "FLASH_BUF", "",
                         re.sub(r"\s+", " ", m.group("store"))[:60], "", "", "", "", "",
                         "c-sdk", rel(sdk_path), line])
        elif m.group("loop"):
            rows.append([pid, entry, seq, "loop-begin", "", "", m.group("lv"), "", "", "", "",
                         m.group("lv"), "c-sdk", rel(sdk_path), line])
        elif m.group("call"):
            c = re.sub(r"\s+", "", m.group("call"))
            rows.append([pid, entry, seq, "call", "", "", c[:60], "", "", "", "", "",
                         "c-sdk", rel(sdk_path), line])
        else:
            continue
        seq += 1
    return rows


allrows = []
for pid, repo, label in SERIES:
    sdk = find_sdk(repo)
    if not sdk:
        print(f"  SDK 無し: {repo}")
        continue
    t = read(sdk)
    for e in ENTRIES:
        allrows += ops_for(pid, sdk, t, e)

hdr = ["impl_id", "function", "seq", "op", "reg_name", "reg_addr", "value_expr", "value_num",
       "mask", "wait_on", "wait_bit", "loop_count", "source_form", "source_file", "source_line"]
with open(OUT / "reg_ops_sdk.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(hdr + TAIL)
    for r in allrows:
        cw.writerow(r + ["#", "single-source", f"evt:{r[13]}(L{r[14]})"])
print(f"reg_ops_sdk.csv: {len(allrows)} rows")

# --- 関数ごとの操作列を署名化して series 間で比較 ---
import collections
sig = collections.defaultdict(dict)
for r in allrows:
    sig[r[1]].setdefault(r[0], []).append(f"{r[3]}:{r[4] or r[6][:18]}")
cmp_rows = []
for fn, per in sorted(sig.items()):
    groups = collections.defaultdict(list)
    for pid, ops in per.items():
        groups["|".join(ops)].append(pid)
    cmp_rows.append([fn, len(per), len(groups),
                     " // ".join(",".join(sorted(v)) for v in groups.values())[:300]])
with open(OUT / "reg_ops_signature.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(["function", "series_count", "distinct_sequences", "groups"] + TAIL)
    for r in cmp_rows:
        cw.writerow(r + ["#", "attested", "computed:extract7.py"])
print(f"reg_ops_signature.csv: {len(cmp_rows)} rows")
for r in cmp_rows:
    print(f"  {r[0]:24} {r[1]:2} series / {r[2]} 通り")
