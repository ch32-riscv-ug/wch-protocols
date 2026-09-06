#!/usr/bin/env python3
"""bootloader 横断調査 — 機械抽出(P1)

WCH_ROOT 配下に各 repo が clone されている前提で、repo 相対パスの CSV を出す。
絶対パスは出力に含めない(references/bootloader-survey-plan.ja.md §4)。

usage: WCH_ROOT=<repos の親> python3 extract.py
"""
import csv
import hashlib
import os
import re
import sys
from pathlib import Path

ROOT = Path(os.environ.get("WCH_ROOT", Path(__file__).resolve().parents[4]))
OUT = Path(__file__).resolve().parent

EVT_SERIES = [
    # (repo, series ラベル, IAP project 相対パス, APP project 相対パス)
    ("CH32V003", "CH32V003", "EVT/EXAM/USART_IAP/CH32V003_IAP", "EVT/EXAM/USART_IAP/CH32V003_APP"),
    ("CH32V006", "CH32V00X", "EVT/EXAM/USART_IAP/CH32V00X_IAP", "EVT/EXAM/USART_IAP/CH32V00X_APP"),
    ("CH32V103", "CH32V103", "EVT/EXAM/IAP/UART_USB_IAP/CH32V103_IAP", "EVT/EXAM/IAP/UART_USB_IAP/CH32V103_APP"),
    ("CH32V205", "CH32V205", "EVT/EXAM/IAP/USB_UART/CH32V205_IAP", "EVT/EXAM/IAP/USB_UART/CH32V205_APP"),
    ("CH32V20x", "CH32V20x", "EVT/EXAM/IAP/USB_UART/CH32V20x_IAP", "EVT/EXAM/IAP/USB_UART/CH32V20x_APP"),
    ("CH32V307", "CH32V30x", "EVT/EXAM/IAP/USB_UART/CHV30x_IAP", "EVT/EXAM/IAP/USB_UART/CHV30x_APP"),
    ("CH32V407", "CH32V407", "EVT/EXAM/IAP/USB_UART/CH32V407_IAP", "EVT/EXAM/IAP/USB_UART/CH32V407_APP"),
    ("CH32X035", "CH32X035", "EVT/EXAM/IAP/USB_UART/CH32X035_IAP", "EVT/EXAM/IAP/USB_UART/CH32X035_APP"),
    ("CH32X315", "CH32X315", "EVT/EXAM/IAP/USB_UART/CH32X315_IAP", "EVT/EXAM/IAP/USB_UART/CH32X315_APP"),
    ("CH32L103", "CH32L103", "EVT/EXAM/IAP/USB_UART/CH32L103_IAP", "EVT/EXAM/IAP/USB_UART/CH32L103_APP"),
    ("CH32M030", "CH32M030", "EVT/EXAM/IAP/UART_USB_IAP/CH32M030_IAP", "EVT/EXAM/IAP/UART_USB_IAP/CH32M030_APP"),
]

# H417 は core ごとに project が分かれる(Common/ + V3F|V5F/)
H417 = [
    ("evt-h417-v3f", "EVT/EXAM/IAP/USB_UART/CH32H417_IAP", "V3F", "QingKeV3F/RV32IMAFC"),
    ("evt-h417-v5f", "EVT/EXAM/IAP/USB_UART/CH32H417_IAP", "V5F", "QingKeV5F/RV32IMAFCB"),
]

PROJECT_ID = {
    "CH32V003": "evt-v003", "CH32V00X": "evt-v00x", "CH32V103": "evt-v103",
    "CH32V205": "evt-v205", "CH32V20x": "evt-v20x", "CH32V30x": "evt-v30x",
    "CH32V407": "evt-v407", "CH32X035": "evt-x035", "CH32X315": "evt-x315",
    "CH32L103": "evt-l103", "CH32M030": "evt-m030", "CH32H417": "evt-h417",
}

# 副対象・OSS・stub 保持 repo
OSS = [
    ("oss-rv003usb-bl", "oss-bl", "rv003usb", "bootloader", "CH32V003", "bl",
     "software-usb-hid", "", "boot-region", "QingKeV2A/RV32EC"),
    ("oss-ch32fun-bl", "oss-bl", "ch32fun", "examples_usb/bootloader", "CH32X035;CH5xx", "bl",
     "hardware-usb-hid", "", "boot-region", "QingKeV4C/RV32IMAC"),
    ("oss-uiap-flasher", "oss-bl", "ch32_user_bootloader_flasher", ".", "CH32V003", "app",
     "swio", "", "application", "QingKeV2A/RV32EC"),
]

TAIL = ["#", "confidence", "basis"]


def rel(p: Path) -> str:
    return str(p.relative_to(ROOT)).replace(os.sep, "/")


def basis(kind: str, p: Path, line=None) -> str:
    s = f"{kind}:{rel(p)}"
    return f"{s}(L{line})" if line else s


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    h.update(p.read_bytes())
    return h.hexdigest()


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace")


def w(name, header, rows):
    with open(OUT / name, "w", newline="", encoding="utf-8") as f:
        cw = csv.writer(f, lineterminator="\n")
        cw.writerow(header)
        cw.writerows(rows)
    print(f"{name}: {len(rows)} rows")


# ---------------------------------------------------------------- size 式評価
SIZE_RE = re.compile(r"^\s*([0-9a-fA-FxX+\-*/() ]+?)\s*([KMkm])?\s*$")


def eval_size(expr: str):
    """`64K-24K` `0x20000000+1024` `(448K-256)` を 10 進に。失敗は None。"""
    if expr is None:
        return None
    e = expr.strip()
    if not e:
        return None
    # 単位付き数値を展開
    e2 = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)\s*([KkMm])\b",
                lambda m: str(int(m.group(1), 0) * (1024 if m.group(2) in "Kk" else 1024 * 1024)), e)
    if not re.fullmatch(r"[0-9a-fA-FxX+\-*/() ]+", e2):
        return None
    try:
        return int(eval(e2, {"__builtins__": {}}, {}))
    except Exception:
        return None


# ---------------------------------------------------------------- MEMORY 解析
MEM_BLOCK = re.compile(r"MEMORY\s*\{(.*?)\}", re.S)
MEM_LINE = re.compile(
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*\((?P<attr>[^)]*)\)\s*:\s*"
    r"ORIGIN\s*=\s*(?P<origin>[^,]+?)\s*,\s*LENGTH\s*=\s*(?P<length>[^\n\r/}]+)")

COMMENT = re.compile(r"/\*(.*?)\*/", re.S)


def parse_memory(text: str, keep_commented=False):
    """MEMORY{} 内の region を返す。keep_commented=True ならコメント内の region も
    active=0 として拾う(series 既定 ld の品種別 variant 用)。"""
    m = MEM_BLOCK.search(text)
    if not m:
        return []
    body = m.group(1)
    out = []
    if keep_commented:
        # コメントごとに variant ラベル(直前のコメント)を追う
        pos, label = 0, ""
        for c in COMMENT.finditer(body):
            active_part = body[pos:c.start()]
            for r in MEM_LINE.finditer(active_part):
                out.append((1, "", r))
            inner = c.group(1)
            regs = list(MEM_LINE.finditer(inner))
            if regs:
                for r in regs:
                    out.append((0, label.strip(), r))
                label = ""
            else:
                label = inner  # region を含まないコメント = 次の variant のラベル
            pos = c.end()
        for r in MEM_LINE.finditer(body[pos:]):
            out.append((1, label.strip(), r))
    else:
        clean = COMMENT.sub(" ", body)
        for r in MEM_LINE.finditer(clean):
            out.append((1, "", r))
    return out


# ---------------------------------------------------------------- .template
def parse_template(p: Path):
    d = {}
    if not p.exists():
        return d
    for line in read(p).splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


# ================================================================ 収集
projects, files_rows, memmap, seriesmem, constants, clockuart = [], [], [], [], [], []

SRC_EXT = {".c", ".h", ".S", ".s", ".asm", ".ld"}


def collect_files(pid, repo, projdir: Path, role_of):
    n = b = l = 0
    for f in sorted(projdir.rglob("*")):
        if not f.is_file() or f.suffix not in SRC_EXT:
            continue
        txt = read(f)
        lines = txt.count("\n") + 1
        files_rows.append([pid, repo, rel(f), f.stat().st_size, lines, sha256(f),
                           role_of(f), "#", "single-source", basis("evt" if repo.startswith("CH32") else "oss", f)])
        n += 1
        b += f.stat().st_size
        l += lines
    return n, b, l


def role_of_file(f: Path):
    s = f.name.lower()
    if s.endswith(".ld"):
        return "linker"
    if "iap" in s:
        return "protocol"
    if "flash" in s:
        return "flash"
    if s.startswith("main"):
        return "entry"
    if "usb" in s:
        return "usb"
    if s.startswith("system_") or s.startswith("startup"):
        return "startup"
    return "support"


DEFINE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\(([^)]*)\))?\s+(.+?)\s*(?:/[/*].*)?$")


def categorize(name, val):
    n = name.upper()
    if n.startswith(("CMD_", "ERR_")) or "SYNC" in n:
        return "protocol"
    if "ADDR" in n or "BASE" in n or n in ("CALADDR", "CHECKNUM"):
        return "address"
    if "SIZE" in n or "LEN" in n:
        return "size"
    if "BAUD" in n or "CLK" in n or "CLOCK" in n:
        return "clock"
    if "VID" in n or "PID" in n or n.startswith("USB") or n.startswith("DEF_USB"):
        return "usb"
    if re.match(r"^P[A-F]\d", n) or "GPIO" in n or "PIN" in n:
        return "pin"
    if "DELAY" in n or "TIMEOUT" in n:
        return "timing"
    return "misc"


def collect_defines(pid, projdir: Path, only=None):
    for f in sorted(projdir.rglob("*")):
        if not f.is_file() or f.suffix not in {".c", ".h"}:
            continue
        if only and f.name not in only:
            continue
        for i, line in enumerate(read(f).splitlines(), 1):
            m = DEFINE.match(line)
            if not m:
                continue
            name, args, val = m.group(1), m.group(2), m.group(3).strip()
            if args is not None:
                continue  # 関数マクロは対象外(値が無い)
            val = val.rstrip("\\").strip()
            num = eval_size(val) if re.fullmatch(r"[\s0-9a-fA-FxX+\-*/()]+", val or "") else None
            constants.append([pid, rel(f), i, name, val, "" if num is None else num,
                              categorize(name, val), "#", "single-source", basis("evt", f, i)])


# ---------------------------------------------------------------- EVT projects
for repo, series, iap_rel, app_rel in EVT_SERIES:
    for role, prel in (("bl", iap_rel), ("app", app_rel)):
        pdir = ROOT / repo / prel
        if not pdir.is_dir():
            continue
        pid = PROJECT_ID[series] + ("-iap" if role == "bl" else "-app")
        tmpl = parse_template(pdir / ".template")
        cproj = pdir / ".cproject"
        ldref = ""
        if cproj.exists():
            m = re.search(r"([^\"&;<>]*Link[^\"&;<>]*\.ld)", read(cproj))
            ldref = m.group(1) if m else ""
        lds = sorted(pdir.rglob("*.ld"))
        n, b, l = collect_files(pid, repo, pdir, role_of_file)
        # doc version/date は iap.h か main.c のヘッダコメントから
        dv = dd = ""
        for cand in ("iap.h", "main.c"):
            hits = list(pdir.rglob(cand))
            if hits:
                t = read(hits[0])[:900]
                mv = re.search(r"Version\s*:\s*(\S+)", t)
                md = re.search(r"Date\s*:\s*(\S+)", t)
                dv, dd = (mv.group(1) if mv else dv), (md.group(1) if md else dd)
                if dv:
                    break
        tp = tmpl.get("Target Path", "")
        projects.append([
            pid, "evt-iap" if role == "bl" else "evt-app", "WCH", series, series, role,
            repo, prel, dv, dd,
            "", "", "", "",
            n, b, l,
            tmpl.get("Series", ""), tmpl.get("MCU", ""), tmpl.get("Mcu Type", ""),
            tmpl.get("Address", ""), tp,
            ("bin" if tp.lower().endswith(".bin") else "hex" if tp.lower().endswith(".hex") else ""),
            tmpl.get("SDIPrintf", ""),
            ldref, 1 if lds else 0,
            "#", "single-source", basis("evt", pdir / ".template"),
        ])
        # memory map
        for ld in lds:
            for active, label, r in parse_memory(read(ld)):
                memmap.append([pid, r.group("name"), r.group("origin").strip(),
                               eval_size(r.group("origin")) or "",
                               r.group("length").strip(), eval_size(r.group("length")) or "",
                               "",  # constrains_size は後段で埋める
                               rel(ld), read(ld)[:r.start()].count("\n") + 1,
                               "#", "single-source", basis("evt", ld)])
        collect_defines(pid, pdir, only={"iap.h", "iap.c", "flash.h", "flash.c", "main.c"})

# ---------------------------------------------------------------- H417(core 別)
for pid_base, prel, core, corename in H417:
    for role, suffix in (("bl", "_IAP"), ("app", "_APP")):
        base = ROOT / "CH32H417" / prel.replace("_IAP", suffix)
        cdir, kdir = base / "Common", base / core
        if not kdir.is_dir():
            continue
        pid = pid_base + ("-iap" if role == "bl" else "-app")
        tmpl = parse_template(kdir / ".template")
        n = b = l = 0
        for d in (cdir, kdir):
            if d.is_dir():
                dn, db, dl = collect_files(pid, "CH32H417", d, role_of_file)
                n, b, l = n + dn, b + db, l + dl
        lds = [x for x in sorted(base.rglob("*.ld")) if core.lower() in str(x).lower()]
        tp = tmpl.get("Target Path", "")
        projects.append([
            pid, "evt-iap" if role == "bl" else "evt-app", "WCH", "CH32H417", "CH32H417", role,
            "CH32H417", prel.replace("_IAP", suffix) + "/" + core, "", "",
            "", "", "", corename, n, b, l,
            tmpl.get("Series", ""), tmpl.get("MCU", ""), tmpl.get("Mcu Type", ""),
            tmpl.get("Address", ""), tp,
            ("bin" if tp.lower().endswith(".bin") else "hex" if tp.lower().endswith(".hex") else ""),
            tmpl.get("SDIPrintf", ""), "", 1 if lds else 0,
            "#", "single-source", basis("evt", kdir / ".template"),
        ])
        for ld in lds:
            for active, label, r in parse_memory(read(ld)):
                memmap.append([pid, r.group("name"), r.group("origin").strip(),
                               eval_size(r.group("origin")) or "",
                               r.group("length").strip(), eval_size(r.group("length")) or "", "",
                               rel(ld), read(ld)[:r.start()].count("\n") + 1,
                               "#", "single-source", basis("evt", ld)])
        collect_defines(pid, cdir, only={"iap.h", "iap.c", "flash.h", "flash.c", "hardware.c"})

# ---------------------------------------------------------------- series 既定 ld
for repo, series in [(r, sr) for r, sr, *_ in EVT_SERIES] + [("CH32H417", "CH32H417")]:
    cands = sorted((ROOT / repo / "EVT/EXAM/SRC/Ld").rglob("*.ld"))
    for ld in cands:
        core = "V3F" if "v3f" in ld.name.lower() else ("V5F" if "v5f" in ld.name.lower() else "")
        txt = read(ld)
        for active, label, r in parse_memory(txt, keep_commented=True):
            lab = re.sub(r"\s+", " ", label).strip()
            seriesmem.append([series, core, lab, lab, active,
                              r.group("origin").strip(), eval_size(r.group("origin")) or "",
                              r.group("name"),
                              r.group("length").strip(), eval_size(r.group("length")) or "",
                              rel(ld), txt[:r.start()].count("\n") + 1,
                              "#", "single-source", basis("evt", ld)])

# ---------------------------------------------------------------- OSS projects
for pid, kind, repo, prel, series, role, t1, t2, resides, core in OSS:
    pdir = ROOT / repo / prel if prel != "." else ROOT / repo
    if not pdir.is_dir():
        continue
    n, b, l = collect_files(pid, repo, pdir if prel != "." else pdir, role_of_file)
    projects.append([pid, kind, "OSS", series, series, role, repo, prel, "", "",
                     t1, t2, resides, core, n, b, l,
                     "", "", "", "", "", "", "", "", 1 if list(pdir.glob("*.ld")) else 0,
                     "#", "single-source", basis("oss", pdir)])
    for ld in sorted(pdir.glob("*.ld")):
        for active, label, r in parse_memory(read(ld)):
            memmap.append([pid, r.group("name"), r.group("origin").strip(),
                           eval_size(r.group("origin")) or "",
                           r.group("length").strip(), eval_size(r.group("length")) or "", "",
                           rel(ld), read(ld)[:r.start()].count("\n") + 1,
                           "#", "single-source", basis("oss", ld)])
    collect_defines(pid, pdir)

# ---------------------------------------------------------------- 出力
w("projects.csv",
  ["project_id", "kind", "vendor", "series", "family", "role", "repo", "path",
   "doc_version", "doc_date", "transport_primary", "transport_secondary", "resides_in", "core",
   "src_file_count", "src_bytes", "src_lines",
   "tmpl_series", "tmpl_mcu", "tmpl_mcu_type", "tmpl_address", "tmpl_target_path",
   "tmpl_out_format", "tmpl_sdi_printf", "ld_ref_path", "ld_present"] + TAIL,
  projects)

w("memory_map.csv",
  ["project_id", "region", "origin_expr", "origin_bytes", "length_expr", "length_bytes",
   "constrains_size", "source_file", "source_line"] + TAIL, memmap)

w("series_memory.csv",
  ["series", "core", "variant_label", "parts", "active", "origin_expr", "origin_bytes",
   "region", "length_expr", "length_bytes", "source_file", "source_line"] + TAIL, seriesmem)

w("constants.csv",
  ["project_id", "file", "line", "name", "value_expr", "value_num", "category"] + TAIL, constants)

w("files.csv",
  ["project_id", "repo", "path", "bytes", "lines", "sha256", "role"] + TAIL, files_rows)
