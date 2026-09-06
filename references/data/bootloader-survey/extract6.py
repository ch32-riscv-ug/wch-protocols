#!/usr/bin/env python3
"""bootloader 横断調査 — HOST_IAP 13 project の横断抽出(U6 残)

target 自身が USB host になって USB メモリ上の image を読み、自分の flash へ書く経路。
BL 側(`HOST_IAP/`)と APP 側(`APP/`)の対で置かれている。
"""
import csv
import os
import re
from pathlib import Path

ROOT = Path(os.environ.get("WCH_ROOT", Path(__file__).resolve().parents[4]))
OUT = Path(__file__).resolve().parent
TAIL = ["#", "confidence", "basis"]

PROJECTS = [
    ("hostiap-h417-fs", "CH32H417", "USBFS", "EVT/EXAM/USBFS/HOST/HOST_IAP"),
    ("hostiap-h417-hs", "CH32H417", "USBHS", "EVT/EXAM/USBHS/HOST/HOST_IAP"),
    ("hostiap-l103-fs", "CH32L103", "USBFS", "EVT/EXAM/USB/USBFS/HOST_IAP"),
    ("hostiap-m030-fs", "CH32M030", "USBFS", "EVT/EXAM/USB/USBFS/HOST_IAP"),
    ("hostiap-v103-fs", "CH32V103", "USBFS", "EVT/EXAM/USB/USBFS/HOST_IAP"),
    ("hostiap-v205-fs", "CH32V205", "USBFS", "EVT/EXAM/USBFS/Host/HOST_IAP"),
    ("hostiap-v205-hs", "CH32V205", "USBHS", "EVT/EXAM/USBHS/Host/HOST_IAP"),
    ("hostiap-v20x-fs", "CH32V20x", "USBFS", "EVT/EXAM/USB/USBFS/HOST_IAP"),
    ("hostiap-v30x-fs", "CH32V307", "USBFS", "EVT/EXAM/USB/USBFS/HOST_IAP"),
    ("hostiap-v30x-hs", "CH32V307", "USBHS", "EVT/EXAM/USB/USBHS/HOST_IAP"),
    ("hostiap-v407-hs", "CH32V407", "USBHS", "EVT/EXAM/USBHS/Host/HOST_IAP"),
    ("hostiap-x035-fs", "CH32X035", "USBFS", "EVT/EXAM/USB/USBFS/HOST_IAP"),
    ("hostiap-x315-hs", "CH32X315", "USBHS", "EVT/EXAM/USBHS/Host/HOST_IAP"),
]


def read(p):
    """EVT には GB18030 のファイルが混ざる(F34)。UTF-8 で駄目なら GB18030 で読む。"""
    b = p.read_bytes()
    for enc in ("utf-8", "gb18030", "latin-1"):
        try:
            return b.decode(enc)
        except UnicodeDecodeError:
            continue
    return b.decode("latin-1", "replace")


def rel(p): return str(p.relative_to(ROOT)).replace(os.sep, "/")


rows, consts = [], []
for pid, repo, ctrl, prel in PROJECTS:
    base = ROOT / repo / prel
    bl, app = base / "HOST_IAP", base / "APP"
    if not bl.is_dir():
        print(f"  skip {pid}")
        continue
    srcs = [f for f in sorted(bl.rglob("*")) if f.suffix in {".c", ".h"}]
    blob = {f: read(f) for f in srcs}
    joined = "\n".join(blob.values())

    def grep(pat, flags=0):
        m = re.search(pat, joined, flags)
        return m.group(1) if m else ""

    fname = grep(r'"(/?[A-Z0-9_]{1,8}\.BIN)"') or grep(r"'(/?[A-Z0-9_]{1,8}\.BIN)'")
    # 書込先: APP 側 Link.ld の ORIGIN、無ければソース中の 0x0800xxxx
    appld = sorted(app.rglob("*.ld")) if app.is_dir() else []
    origin = ""
    if appld:
        t = read(appld[0])
        m = re.search(r"FLASH\s*\(rx\)\s*:\s*ORIGIN\s*=\s*(\S+?)\s*,\s*LENGTH\s*=\s*([^\n\r/}]+)", t)
        if m:
            origin = f"{m.group(1)} (+{m.group(2).strip()})"
    addr = grep(r"(0x0800[0-9A-Fa-f]{4})")
    # 1 回に読む単位
    blk = grep(r"#define\s+\w*(?:BLOCK|SECTOR|BUF|PAGE)\w*_?SIZE\s+(\S+)")
    files = len(srcs)
    total = sum(f.stat().st_size for f in srcs)
    rows.append([pid, repo, ctrl, prel, fname or "(不明)", addr, origin, blk,
                 files, total, "#", "single-source", f"evt:{rel(bl)}"])
    # 全 #define を constants 形式で
    for f, t in blob.items():
        for i, line in enumerate(t.splitlines(), 1):
            m = re.match(r"^\s*#define\s+([A-Za-z_]\w*)\s+(.+?)\s*(?:/[/*].*)?$", line)
            if m and "(" not in m.group(1):
                consts.append([pid, rel(f), i, m.group(1), m.group(2).strip()[:120],
                               "#", "single-source", f"evt:{rel(f)}(L{i})"])

with open(OUT / "host_iap.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(["project_id", "repo", "usb_controller", "path", "image_file",
                 "flash_addr_seen", "app_ld_origin", "block_size",
                 "src_file_count", "src_bytes"] + TAIL)
    cw.writerows(rows)
print(f"host_iap.csv: {len(rows)} rows")

with open(OUT / "host_iap_constants.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(["project_id", "file", "line", "name", "value_expr"] + TAIL)
    cw.writerows(consts)
print(f"host_iap_constants.csv: {len(consts)} rows")
