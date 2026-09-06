#!/usr/bin/env python3
"""bootloader 横断調査 — 半自動抽出(P2): protocol / entry_exit / flash_ops / usb / clock_uart

extract.py と同じ規約(repo 相対パス・末尾 3 列 #,confidence,basis)。
regex で候補を出し、判定できないものは空欄にして confidence を落とす。
"""
import csv
import os
import re
from pathlib import Path

ROOT = Path(os.environ.get("WCH_ROOT", Path(__file__).resolve().parents[4]))
OUT = Path(__file__).resolve().parent
TAIL = ["#", "confidence", "basis"]


def rel(p): return str(p.relative_to(ROOT)).replace(os.sep, "/")
def read(p): return p.read_text(encoding="utf-8", errors="replace")
def basis(k, p, line=None):
    s = f"{k}:{rel(p)}"
    return f"{s}(L{line})" if line else s


def w(name, header, rows):
    with open(OUT / name, "w", newline="", encoding="utf-8") as f:
        cw = csv.writer(f, lineterminator="\n")
        cw.writerow(header)
        cw.writerows(rows)
    print(f"{name}: {len(rows)} rows")


# project_id -> project ディレクトリ(bl のみ)
PROJ = {}
for row in csv.DictReader(open(OUT / "projects.csv", encoding="utf-8")):
    if row["role"] != "bl":
        continue
    PROJ[row["project_id"]] = (ROOT / row["repo"] / row["path"], row["series"], row["repo"])


def find(pdir: Path, *names):
    for n in names:
        hits = sorted(pdir.rglob(n))
        if hits:
            return hits[0]
    # H417 は Common/ が親側にある
    for n in names:
        hits = sorted(pdir.parent.rglob(n))
        if hits:
            return hits[0]
    return None


def grep1(text, pat, group=1, flags=0):
    m = re.search(pat, text, flags)
    return m.group(group) if m else ""


def lineno(text, idx):
    return text[:idx].count("\n") + 1


def grep_line(text, pat, flags=0):
    m = re.search(pat, text, flags)
    return (m, lineno(text, m.start())) if m else (None, "")


# ============================================================ protocol.csv
protocol = []
CMDS = ["CMD_IAP_PROM", "CMD_IAP_ERASE", "CMD_IAP_VERIFY", "CMD_IAP_END", "CMD_JUMP_IAP"]
ERRS = ["ERR_SUCCESS", "ERR_SCUESS", "ERR_ERROR", "ERR_End"]

for pid, (pdir, series, repo) in PROJ.items():
    h = find(pdir, "iap.h")
    if not h:
        continue
    t = read(h)
    s1 = grep1(t, r"#define\s+Uart_Sync_Head1\s+(\S+)")
    s2 = grep1(t, r"#define\s+Uart_Sync_Head2\s+(\S+)")
    # 構造体レイアウト(UART メンバ or struct 本体)をそのまま文字列化
    mstruct = re.search(r"typedef\s+(union|struct)[^{]*\{(.*?)\}\s*isp_cmd", t, re.S)
    layout = ""
    if mstruct:
        body = re.sub(r"\s+", " ", mstruct.group(2)).strip()
        layout = body[:300]
    for c in CMDS + ERRS:
        m = re.search(rf"#define\s+{c}\s+(\S+)", t)
        if not m:
            continue
        protocol.append([pid, series, "uart+usb", s1, s2, layout if c == CMDS[0] else "",
                         c, m.group(1),
                         "host->target" if c.startswith("CMD") else "target->host",
                         rel(h), lineno(t, m.start()), "#", "single-source", basis("evt", h, lineno(t, m.start()))])

# ============================================================ entry_exit.csv
entry = []
for pid, (pdir, series, repo) in PROJ.items():
    m = find(pdir, "hardware.c") if "h417" in pid else find(pdir, "main.c")
    hh = find(pdir, "iap.h")
    if not m:
        continue
    t = read(m)
    th = read(hh) if hh else ""
    # blank pattern: if(*(...)FLASH_Base != 0x....)
    mb, lb = grep_line(t, r"FLASH_Base\s*!=\s*(0x[0-9a-fA-F]+)")
    blank = mb.group(1) if mb else ""
    # CalAddr の極性
    mc, lc = grep_line(t, r"CalAddr\s*(==|!=)\s*CheckNum")
    pol = mc.group(1) if mc else ""
    # GPIO check 関数
    mg, lg = grep_line(t, r"\b(P[A-F]\d+)_Check\s*\(\)")
    pin = mg.group(1) if mg else ""
    # exit 方式
    if "SystemReset_StartMode" in t:
        exitm = "boot-mode-register+reset"
        mex, lex_ = grep_line(t, r"SystemReset_StartMode\([^)]*\)")
    elif "Software_IRQn" in t:
        exitm = "software-irq"
        mex, lex_ = grep_line(t, r"NVIC_SetPendingIRQ\(Software_IRQn\)")
    else:
        exitm, lex_ = "", ""
    # jump 前に落とす周辺
    deinit = ";".join(re.findall(r"(GPIO_DeInit\([A-Z0-9]+\)|USART_DeInit\([A-Z0-9]+\)|"
                                 r"RCC_\w+ClockCmd\(\s*\w+\s*,\s*DISABLE\s*\)|"
                                 r"USBFS_Device_Init\(\s*DISABLE[^)]*\)|USBHS_Device_Init\(\s*DISABLE\s*\)|"
                                 r"USB_Init\(DISABLE\)|USB_Port_Set\(DISABLE[^)]*\)|"
                                 r"NVIC_DisableIRQ\(\s*\w+\s*\))", t))
    caladdr = grep1(th, r"#define\s+CalAddr\s+(.+?)\s*(?://|$)", flags=re.M)
    checknum = grep1(th, r"#define\s+CheckNum\s+(.+?)\s*(?://|$)", flags=re.M)
    fbase = grep1(th, r"#define\s+FLASH_Base\s+(\S+)")
    wdt = "IWDG_ReloadCounter" in t
    entry.append([pid, series, "app-present-marker", blank, pol, caladdr, checknum, fbase,
                  pin, exitm, deinit[:400], 1 if wdt else 0,
                  rel(m), lb or lc or "", "#", "single-source", basis("evt", m, lb or lc or None)])

# ============================================================ flash_ops.csv
flash = []
CALL = re.compile(r"\b(FLASH_ProgramPage_Fast|FLASH_ROM_WRITE|FLASH_BufLoad|FLASH_BufReset|"
                  r"FLASH_ErasePage_Fast|FLASH_ROM_ERASE|CH32_IAP_ERASE)\s*\(([^;]*?)\)\s*;")
MASK = re.compile(r"&\s*(0x[0-9a-fA-F]{8})")
SIZEARG = re.compile(r"Size_(\d+)(KB|B)\b|,\s*(\d+)\s*\)?\s*$")


def gran_from(call_text, fname):
    m = MASK.search(call_text)
    if m:
        v = int(m.group(1), 16)
        return ((~v + (1 << 32)) & 0xFFFFFFFF) + 1   # マスクの補数+1 = 粒度
    m = re.search(r"Size_(\d+)(KB|B)\b", call_text)
    if m:
        return int(m.group(1)) * (1024 if m.group(2) == "KB" else 1)
    m = re.search(r",\s*(\d+)\s*$", call_text.strip())
    if m:
        return int(m.group(1))
    return ""


for pid, (pdir, series, repo) in PROJ.items():
    seen = set()
    srcs = [x for x in (find(pdir, "flash.c"), find(pdir, "iap.c"), find(pdir, "hardware.c"),
                        find(pdir, "bootloader.c")) if x]
    for f in srcs:
        t = read(f)
        for mm in CALL.finditer(t):
            fn, args = mm.group(1), re.sub(r"\s+", " ", mm.group(2)).strip()
            op = ("erase" if "ERASE" in fn.upper() or "Erase" in fn else
                  "program" if "WRITE" in fn.upper() or "Program" in fn else "buffer")
            key = (fn, args)
            if key in seen:
                continue
            seen.add(key)
            flash.append([pid, series, op, fn, args[:160], gran_from(args, fn),
                          rel(f), lineno(t, mm.start()),
                          "#", "single-source", basis("evt", f, lineno(t, mm.start()))])

# ============================================================ usb.csv
usb = []
HEXTOK = re.compile(r"0[xX][0-9a-fA-F]+")


def descr_bytes(text, name):
    m = re.search(rf"{name}\s*(?:\[\s*\d*\s*\])?\s*=\s*\{{(.*?)\}}\s*;", text, re.S)
    if not m:
        return None, None
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    toks = [x.strip() for x in body.split(",")]
    return toks, lineno(text, m.start())


for pid, (pdir, series, repo) in PROJ.items():
    vid = pidv = pidh = mode = style = ""
    src = ln = ""
    dh = find(pdir, "usb_desc.h")
    if dh and "DEF_USB_VID" in read(dh):  # 世代 D: マクロ方式
        t = read(dh)
        vid = grep1(t, r"#define\s+DEF_USB_VID\s+(\S+)")
        pidv = grep1(t, r"#define\s+DEF_USB_PID_VENDOR\s+(\S+)")
        pidh = grep1(t, r"#define\s+DEF_USB_PID_HID\s+(\S+)")
        style = "macro"
        src, ln = rel(dh), 20
        inf = find(pdir, "usb_inf.h")
        if inf:
            mode = grep1(read(inf), r"#define\s+DEF_USB_IAP_MODE\s+(DEF_USB_IAP_MODE_\w+)")
    if not vid:  # 旧世代: 生バイト descriptor
        for cand in ("usb_desc.c", "*usbfs_device.c", "*usbhs_device.c", "main.c"):
            f = find(pdir, cand)
            if not f:
                continue
            t = read(f)
            for nm in ("MyDevDescr", "MyDevDescrHD", "USBD_DeviceDescriptor"):
                toks, l = descr_bytes(t, nm)
                if toks and len(toks) >= 12 and toks[0].lower() in ("0x12",):
                    def hx(i):
                        return toks[i] if HEXTOK.fullmatch(toks[i]) else ""
                    if hx(8) and hx(9):
                        vid = f"0x{int(toks[9],16):02X}{int(toks[8],16):02X}"
                    if hx(10) and hx(11):
                        pidv = f"0x{int(toks[11],16):02X}{int(toks[10],16):02X}"
                    style, src, ln = "raw-bytes", rel(f), l
                    break
            if style:
                break
    usb.append([pid, series, style, vid, pidv, pidh, mode, src, ln,
                "#", "single-source", basis("evt", ROOT / src, ln) if src else ""])

# ============================================================ clock_uart.csv
clock = []
for pid, (pdir, series, repo) in PROJ.items():
    m = find(pdir, "main.c")
    ic = find(pdir, "iap.c")
    port = baud = brr = printf_baud = ""
    t = read(m) if m else ""
    ti = read(ic) if ic else ""
    mm = re.search(r"USART(\d)_CFG\s*\(\s*(\d+)?\s*\)", t)
    if mm:
        port, baud = f"USART{mm.group(1)}", mm.group(2) or ""
    if not baud and ti:
        mb = re.search(r"USART\d_CFG.*?BRR\s*=\s*(0[xX][0-9a-fA-F]+|\d+)", ti, re.S)
        brr = mb.group(1) if mb else ""
        if re.search(r"baud rate\s*=\s*460800", read(m) if m else ""):
            baud = "460800"
    pb = re.search(r"USART_Printf_Init\(\s*(\d+)\s*\)", t)
    printf_baud = pb.group(1) if pb else ""
    clock.append([pid, series, port, baud, brr, printf_baud,
                  rel(m) if m else "", "", "#", "single-source", basis("evt", m) if m else ""])

# ============================================================ 出力
w("protocol.csv", ["project_id", "series", "transport", "sync1", "sync2", "header_layout",
                   "cmd_name", "cmd_byte", "direction", "source_file", "source_line"] + TAIL, protocol)
w("entry_exit.csv", ["project_id", "series", "mechanism", "blank_pattern", "polarity",
                     "marker_addr", "marker_value", "flash_base", "gpio_pin", "exit_method",
                     "deinit_steps", "watchdog", "source_file", "source_line"] + TAIL, entry)
w("flash_ops.csv", ["project_id", "series", "op", "api", "call_args", "granularity_bytes",
                    "source_file", "source_line"] + TAIL, flash)
w("usb.csv", ["project_id", "series", "descr_style", "vid", "pid_vendor", "pid_hid",
              "iap_mode", "source_file", "source_line"] + TAIL, usb)
w("clock_uart.csv", ["project_id", "series", "uart_port", "baud", "brr_value", "printf_baud",
                     "source_file", "source_line"] + TAIL, clock)
