#!/usr/bin/env python3
"""bootloader 横断調査 — wlink 系 flash loader stub の取り込み(依頼 0005)

ch32rv `crates/flash/src/stub.rs` の byte 配列(wlink `src/flash_op.rs` からの逐語転記、
元は WCH EVT の flash ルーチン)を読み、既存の stub 目録と同じ形で
stubs_hex/ + stub_disasm/ + stubs.csv 追記行を作る。

**ch32rv には書き込まない**(読むだけ)。
"""
import csv
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(os.environ.get("WCH_ROOT", Path(__file__).resolve().parents[4]))
OUT = Path(__file__).resolve().parent
HEXDIR, ASMDIR = OUT / "stubs_hex", OUT / "stub_disasm"
SRC = ROOT / "ch32rv/crates/flash/src/stub.rs"
OBJDUMP = os.environ.get(
    "RISCV_OBJDUMP",
    str(ROOT / "tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-objdump"))
TAIL = ["#", "confidence", "basis"]
PROV = "oss:ch32-rs/wlink/src/flash_op.rs (via ch32rv/crates/flash/src/stub.rs)"

# AttachChip family byte(依頼 0005 の表 + pc-to-link.ja.md §5)
FAMILY = {
    "CH32V307": "V20x(0x05) / V30x(0x06)",
    "CH32V103": "V103(0x01)",
    "CH32V003": "V003(0x09) / CH641(0x49)",
    "CH643":    "X035(0x0d) / CH643(0x0c)",
    "CH32L103": "L103(0x0e)",
}

REG = re.compile(r"\bx(\d+)\b")


def disasm(data: bytes):
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
        tf.write(data)
        path = tf.name
    try:
        r = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-m", "riscv:rv32",
                            "-M", "numeric", path], capture_output=True, text=True, timeout=60)
        txt = r.stdout
    finally:
        os.unlink(path)
    body = txt.split("<.data>:", 1)[-1].strip("\n")
    regs = set()
    for line in body.splitlines():
        parts = line.split("\t")
        if len(parts) >= 3:
            for m in REG.finditer("\t".join(parts[2:])):
                regs.add(int(m.group(1)))
    return body, regs


def fnv1a64(data: bytes) -> str:
    h = 0xcbf29ce484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


text = SRC.read_text(encoding="utf-8", errors="replace")
ARR = re.compile(r"pub const (?P<name>\w+):\s*\[u8;\s*(?P<len>\d+)\]\s*=\s*\[(?P<body>.*?)\];", re.S)

blobs = {}
rows = []
for m in ARR.finditer(text):
    name, declared = m.group("name"), int(m.group("len"))
    body = re.sub(r"//[^\n]*", "", m.group("body"))
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", body))
    assert len(data) == declared, f"{name}: {len(data)} != {declared}"
    sid = f"wlink-{name}"
    blobs[name] = data
    hexs = " ".join(f"{b:02x}" for b in data)
    (HEXDIR / f"{sid}.hex").write_text(hexs + "\n", encoding="utf-8")
    asm, regs = disasm(data)
    (ASMDIR / f"{sid}.asm").write_text(
        f"# {sid}  ({len(data)} bytes)  fnv1a64={fnv1a64(data)}\n"
        f"# source: {PROV}\n"
        f"# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric\n{asm}\n",
        encoding="utf-8")
    line = text[:m.start()].count("\n") + 1
    rows.append([sid, "wlink/flash_op", "ch32rv", "crates/flash/src/stub.rs", line, name,
                 FAMILY.get(name, ""), len(data), 1, "raw-byte-array", 0, "",
                 f"stub_disasm/{sid}.asm", "",
                 " ".join(f"x{r}" for r in sorted(regs)),
                 1 if regs and max(regs) <= 15 else (0 if regs else ""),
                 f"fnv1a64={fnv1a64(data)}; 先頭4B={' '.join(f'{b:02x}' for b in data[:4])}",
                 "#", "attested", PROV])

# ---- 依頼 0005 の 4 つの問いに答えるための突き合わせ ----
report = []
existing = {}
for f in sorted(HEXDIR.glob("*.hex")):
    existing[f.stem] = bytes(int(x, 16) for x in f.read_text().split())

# Q1: CH32L103(512) vs linke-flashloader-v1 / v2(ともに 512)
for other in ("linke-flashloader-v1", "linke-flashloader-v2"):
    a, b = blobs.get("CH32L103"), existing.get(other)
    if a and b:
        same = a == b
        # 0xff padding を除いた実体で比較
        ta, tb = a.rstrip(b"\xff"), b.rstrip(b"\xff")
        report.append(("Q1", f"wlink-CH32L103 vs {other}",
                       f"完全一致={same} / 0xff padding 除去後の長さ={len(ta)} vs {len(tb)} / "
                       f"padding 除去後一致={ta == tb} / 先頭16B一致={a[:16] == b[:16]}"))

# Q2: 先頭 4 byte と rv32ec 安全性
for n, d in blobs.items():
    _, regs = disasm(d)
    report.append(("Q2", n, f"先頭4B={' '.join(f'{x:02x}' for x in d[:4])} / "
                            f"x16以上={'あり' if regs and max(regs) > 15 else 'なし'} / "
                            f"最大レジスタ=x{max(regs) if regs else '-'}"))

# Q3: 5 本の相互差分(共通接頭辞・接尾辞の長さ)
names = list(blobs)
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        a, b = blobs[names[i]], blobs[names[j]]
        pre = 0
        while pre < min(len(a), len(b)) and a[pre] == b[pre]:
            pre += 1
        suf = 0
        while suf < min(len(a), len(b)) - pre and a[-1 - suf] == b[-1 - suf]:
            suf += 1
        report.append(("Q3", f"{names[i]} vs {names[j]}",
                       f"共通接頭辞={pre}B / 共通接尾辞={suf}B / サイズ={len(a)} vs {len(b)}"))

with open(OUT / "wlink_stub_comparison.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(["question", "pair_or_stub", "result"] + TAIL)
    for q, k, v in report:
        cw.writerow([q, k, v, "#", "verified", "computed:extract5.py"])
print(f"wlink_stub_comparison.csv: {len(report)} rows")

# ---- stubs.csv へ追記(既存の wlink- 行は入れ替え)----
path = OUT / "stubs.csv"
old = list(csv.DictReader(open(path, encoding="utf-8")))
hdr = list(old[0].keys())
keep = [r for r in old if not r["stub_id"].startswith("wlink-")]
with open(path, "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(hdr)
    for r in keep:
        cw.writerow([r[k] for k in hdr])
    cw.writerows(rows)
print(f"stubs.csv: {len(keep)} + {len(rows)} = {len(keep) + len(rows)} rows")
