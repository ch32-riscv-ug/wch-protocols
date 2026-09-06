#!/usr/bin/env python3
"""bootloader 横断調査 — WCH 純正 OpenOCD から flash loader を全数抽出(U11)

WCH が配布する Linux 版 OpenOCD(`tools/OpenOCD/OpenOCD/bin/openocd`)の .rodata に、
target RAM で走らせる flash loader blob が **0xff padding 区切りで並んでいる**。
wlink / minichlink が持っているのはこの一部。

`--strict` を付けなければ、既知 blob との照合結果も出す。
"""
import csv
import hashlib
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(os.environ.get("WCH_ROOT", Path(__file__).resolve().parents[4]))
OUT = Path(__file__).resolve().parent
HEXDIR, ASMDIR = OUT / "stubs_hex", OUT / "stub_disasm"
OD = ROOT / "tools/OpenOCD/OpenOCD/bin/openocd"
OBJDUMP = os.environ.get(
    "RISCV_OBJDUMP",
    str(ROOT / "tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-objdump"))
TAIL = ["#", "confidence", "basis"]
PROV = "wch:tools/OpenOCD/OpenOCD/bin/openocd (WCH 配布の Linux OpenOCD, .rodata)"

# blob の先頭に現れる prologue。RV32 の関数入口(スタックフレーム確保)
# 先頭 4 byte が RV32 の関数プロローグになっているもの(実際に観測した種類)
PROLOGUE_HEX = ["011102ce", "11112 2cc".replace(" ", ""), "011122ce",
                "797122d4", "797126d2", "797122d2", "797126d4", "397106de"]
PAT = re.compile(b"|".join(re.escape(bytes.fromhex(h)) for h in PROLOGUE_HEX))

REG = re.compile(r"\bx(\d+)\b")


def disasm(data: bytes):
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
        tf.write(data)
        p = tf.name
    try:
        r = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-m", "riscv:rv32",
                            "-M", "numeric", p], capture_output=True, text=True, timeout=60)
        txt = r.stdout
    finally:
        os.unlink(p)
    body = txt.split("<.data>:", 1)[-1].strip("\n")
    regs = set()
    for line in body.splitlines():
        parts = line.split("\t")
        if len(parts) >= 3:
            for m in REG.finditer("\t".join(parts[2:])):
                regs.add(int(m.group(1)))
    return body, regs


def fnv1a64(d: bytes) -> str:
    h = 0xcbf29ce484222325
    for b in d:
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return f"{h:016x}"


fw = OD.read_bytes()
# 既知 blob
known = {}
for h in sorted(HEXDIR.glob("*.hex")):
    if h.stem.startswith("wchocd-"):   # 自分の出力は既知扱いにしない
        continue
    known[h.stem] = bytes(int(x, 16) for x in h.read_text().split())

# --- blob の切り出し ---
# 先頭候補を集め、「次の 0xff が 8 個以上続くところ」までを 1 本とする
starts = sorted({m.start() for m in PAT.finditer(fw)})
blobs = []
for s in starts:
    # .rodata らしい範囲だけ(先頭候補が密集している領域)
    if not (0x400000 <= s <= 0x480000):
        continue
    # 終端は「4 個以上の 0xff の連続」の手前。ただし次の prologue 開始を超えない
    e = s + 4
    while e < len(fw):
        if fw[e] == 0xFF and fw[e:e + 4] == b"\xff" * 4:
            break
        e += 1
    nxt = next((o for o in starts if o > s), len(fw))
    e = min(e, nxt)
    n = e - s
    if 32 <= n <= 4096:
        blobs.append((s, fw[s:e]))

# 同一開始位置の重複を除去し、包含関係にあるものは長いほうを残す
blobs.sort(key=lambda x: (x[0], -len(x[1])))
uniq, seen_off = [], set()
for off, b in blobs:
    if off in seen_off:
        continue
    seen_off.add(off)
    uniq.append((off, b))

rows, new_count = [], 0
for off, b in uniq:
    dg = hashlib.sha256(b).hexdigest()[:16]
    match = [k for k, v in known.items() if v == b or (len(v) > len(b) and v.startswith(b))
             or (len(b) > len(v) and b.startswith(v))]
    sid = f"wchocd-{off:06X}"
    hexs = " ".join(f"{x:02x}" for x in b)
    (HEXDIR / f"{sid}.hex").write_text(hexs + "\n", encoding="utf-8")
    asm, regs = disasm(b)
    (ASMDIR / f"{sid}.asm").write_text(
        f"# {sid}  ({len(b)} bytes)  @0x{off:X}  fnv1a64={fnv1a64(b)}\n"
        f"# source: {PROV}\n"
        f"# 既知 blob との一致: {','.join(match) if match else '(新規)'}\n"
        f"# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric\n{asm}\n",
        encoding="utf-8")
    if not match:
        new_count += 1
    rows.append([sid, f"0x{off:X}", len(b), fnv1a64(b), dg,
                 " ".join(f"x{r}" for r in sorted(regs)),
                 1 if regs and max(regs) <= 15 else (0 if regs else ""),
                 ";".join(match) if match else "", "新規" if not match else "既知",
                 "#", "verified", PROV])

with open(OUT / "wch_openocd_loaders.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(["stub_id", "file_offset", "bytes", "fnv1a64", "sha256_16",
                 "reg_set", "rv32ec_safe", "matches_known", "status"] + TAIL)
    cw.writerows(rows)
print(f"wch_openocd_loaders.csv: {len(rows)} rows  (新規 {new_count} / 既知 {len(rows)-new_count})")
for r in rows:
    print(f"  {r[0]:18}{r[1]:>10}{r[2]:>6} B  ec={r[6]}  {r[8]:4} {r[7]}")
