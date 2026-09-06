#!/usr/bin/env python3
"""bootloader 横断調査 — stub 抽出と逆アセンブル(P3)

拡張可能な stub(host が target に送り込む機械語)を全部集める:
  1. ch32fun/minichlink/pgm-b003fun.c   の inline C 配列(有効 + コメントアウト)
  2. ch32fun/minichlink/pgm-wch-linke.c の WCH 純正 flash loader blob(文字列連結)
  3. ch32fun/minichlink/stubs/b003/*.h  の生成 header(元は同ディレクトリの *.S)

各 blob を stubs_hex/<stub_id>.hex に保存 → 逆アセンブル → stub_disasm/<stub_id>.asm、
使用レジスタ集合から rv32ec_safe(x0-x15 のみか)を判定する。
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
HEXDIR.mkdir(exist_ok=True)
ASMDIR.mkdir(exist_ok=True)
TAIL = ["#", "confidence", "basis"]

OBJDUMP = os.environ.get(
    "RISCV_OBJDUMP",
    str(ROOT / "tools/xpack-riscv-none-elf-gcc/14.3.0-1/bin/riscv-none-elf-objdump"))


def rel(p): return str(Path(p).relative_to(ROOT)).replace(os.sep, "/")
def read(p): return Path(p).read_text(encoding="utf-8", errors="replace")
def lineno(t, i): return t[:i].count("\n") + 1


def w(name, header, rows):
    with open(OUT / name, "w", newline="", encoding="utf-8") as f:
        cw = csv.writer(f, lineterminator="\n")
        cw.writerow(header)
        cw.writerows(rows)
    print(f"{name}: {len(rows)} rows")


# ------------------------------------------------------------ 逆アセンブル
REG = re.compile(r"\bx(\d+)\b")


def disasm(data: bytes):
    """(逆アセンブル文字列, 使用レジスタ集合) を返す。"""
    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as tf:
        tf.write(data)
        path = tf.name
    try:
        r = subprocess.run([OBJDUMP, "-D", "-b", "binary", "-m", "riscv:rv32",
                            "-M", "numeric", path],
                           capture_output=True, text=True, timeout=30)
        txt = r.stdout
    except Exception as e:
        return f"(objdump 失敗: {e})", set()
    finally:
        os.unlink(path)
    body = txt.split("<.data>:", 1)[-1].strip("\n")
    regs = set()
    for line in body.splitlines():
        # "   0:\t4681      \tli\tx13,0" の第 3 カラム以降だけ見る
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        for m in REG.finditer("\t".join(parts[2:])):
            regs.add(int(m.group(1)))
    return body, regs


def save(stub_id, data: bytes):
    hexs = " ".join(f"{b:02x}" for b in data)
    (HEXDIR / f"{stub_id}.hex").write_text(hexs + "\n", encoding="utf-8")
    body, regs = disasm(data)
    (ASMDIR / f"{stub_id}.asm").write_text(
        f"# {stub_id}  ({len(data)} bytes)\n"
        f"# riscv-none-elf-objdump -D -b binary -m riscv:rv32 -M numeric\n{body}\n",
        encoding="utf-8")
    return regs


stubs, stub_args = [], []

# ============================================================ 1. inline C 配列
B003 = ROOT / "ch32fun/minichlink/pgm-b003fun.c"
t = read(B003)

ARR = re.compile(r"(?P<pre>(?://[^\n]*\n\s*)*)"
                 r"(?P<decl>(?:static\s+)?(?:const\s+)?unsigned\s+char\s+(?P<name>\w+)\s*\[\s*\]\s*=\s*\{)"
                 r"(?P<body>.*?)\};", re.S)
# コメントアウトされた配列(行頭 // が続くブロック)
COMMENTED = re.compile(r"(?:^//\s*(?:static\s+)?(?:const\s+)?unsigned char (?P<name>\w+)\[\] = \{"
                       r"(?P<body>(?:\n//[^\n]*)*?)\n//\s*\};)", re.M)


def bytes_from(body: str):
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return bytes(int(x, 16) for x in re.findall(r"0[xX]([0-9a-fA-F]{1,2})", body))


for m in ARR.finditer(t):
    name = m.group("name")
    data = bytes_from(m.group("body"))
    if not data:
        continue
    sid = f"b003-{name.replace('_blob','').replace('_bin','')}"
    regs = save(sid, data)
    ln = lineno(t, m.start("decl"))
    note = ""
    if "/*" in m.group("body"):
        note = "同じ配列内にコメントアウトされた代替版あり"
    stubs.append([sid, "minichlink/pgm-b003fun", "ch32fun", rel(B003), ln, name,
                  "V003;V00X;X035;V20x;V30x;CH5xx", len(data), 1,
                  "hex-array", 0, "", f"stub_disasm/{sid}.asm", "",
                  " ".join(f"x{r}" for r in sorted(regs)),
                  1 if regs and max(regs) <= 15 else (0 if regs else ""),
                  note, "#", "attested", f"oss:{rel(B003)}(L{ln})"])

for m in COMMENTED.finditer(t):
    name = m.group("name")
    data = bytes_from(m.group("body").replace("//", ""))
    if not data:
        continue
    sid = f"b003-{name.replace('_blob','').replace('_bin','')}-commented"
    regs = save(sid, data)
    ln = lineno(t, m.start())
    stubs.append([sid, "minichlink/pgm-b003fun", "ch32fun", rel(B003), ln, name,
                  "V003", len(data), 0, "hex-array", 0, "", f"stub_disasm/{sid}.asm", "",
                  " ".join(f"x{r}" for r in sorted(regs)),
                  1 if regs and max(regs) <= 15 else (0 if regs else ""),
                  "コメントアウトされた旧版", "#", "attested", f"oss:{rel(B003)}(L{ln})"])

# ============================================================ 2. WCH 純正 blob
LINKE = ROOT / "ch32fun/minichlink/pgm-wch-linke.c"
tl = read(LINKE)
BL = re.compile(r"struct BootloaderBlob (?P<name>\w+)\s*=\s*\{(?P<body>.*?)\.len\s*=\s*(?P<len>\d+)", re.S)
for m in BL.finditer(tl):
    name = m.group("name")
    data = bytes(int(x, 16) for x in re.findall(r"\\x([0-9a-fA-F]{2})", m.group("body")))
    sid = f"linke-{name.replace('bootloader_','flashloader-')}"
    regs = save(sid, data)
    ln = lineno(tl, m.start())
    tgt = re.search(r"//\s*Flash(?:loader|\s*Bootloader)\s*for\s*([^\n]*)", tl[max(0, m.start() - 200):m.start()])
    stubs.append([sid, "minichlink/pgm-wch-linke", "ch32fun", rel(LINKE), ln, name,
                  (tgt.group(1).strip() if tgt else ""), len(data), 1,
                  "raw-byte-string", 0, "", f"stub_disasm/{sid}.asm", "",
                  " ".join(f"x{r}" for r in sorted(regs)),
                  1 if regs and max(regs) <= 15 else (0 if regs else ""),
                  f"宣言 .len={m.group('len')}(0xff padding 込み)", "#", "attested",
                  f"oss:{rel(LINKE)}(L{ln})"])

# ============================================================ 3. 生成 header + .S
SDIR = ROOT / "ch32fun/minichlink/stubs/b003"
for hf in sorted(SDIR.glob("*.h")):
    th = read(hf)
    mm = re.search(r"unsigned char (\w+)\[\]\s*=\s*\{(.*?)\};", th, re.S)
    if not mm:
        continue
    name = mm.group(1)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", mm.group(2)))
    sfile = SDIR / (hf.stem + ".S")
    sid = f"b003stub-{hf.stem}"
    regs = save(sid, data)
    stubs.append([sid, "minichlink/stubs/b003", "ch32fun", rel(hf), 1, name,
                  "CH5xx" if hf.stem.startswith("ch5xx") else "V003;V00X;X035;V20x;V30x",
                  len(data), 1, "generated-header", 1,
                  rel(sfile) if sfile.exists() else "", f"stub_disasm/{sid}.asm", "",
                  " ".join(f"x{r}" for r in sorted(regs)),
                  1 if regs and max(regs) <= 15 else (0 if regs else ""),
                  "", "#", "attested", f"oss:{rel(hf)}"])

# .S ソース側(asm)を別行で。使用レジスタはソース表記から拾う
ABI = {"zero": 0, "ra": 1, "sp": 2, "gp": 3, "tp": 4, "t0": 5, "t1": 6, "t2": 7,
       "s0": 8, "fp": 8, "s1": 9, "a0": 10, "a1": 11, "a2": 12, "a3": 13, "a4": 14, "a5": 15,
       "a6": 16, "a7": 17, "s2": 18, "s3": 19, "s4": 20, "s5": 21, "s6": 22, "s7": 23,
       "s8": 24, "s9": 25, "s10": 26, "s11": 27, "t3": 28, "t4": 29, "t5": 30, "t6": 31}
ABI_RE = re.compile(r"\b(" + "|".join(sorted(ABI, key=len, reverse=True)) + r"|x(?:[12]?\d|3[01]))\b")

asm_files = sorted(SDIR.glob("*.S")) + sorted(
    (ROOT / "ch32fun/misc/attic/rv003usb_bootloader_stubs_for_minichlink").glob("*.asm"))
for af in asm_files:
    ta = read(af)
    code = re.sub(r"//[^\n]*|/\*.*?\*/|#[^\n]*", "", ta, flags=re.S)
    regs = set()
    for m in ABI_RE.finditer(code):
        tok = m.group(1)
        regs.add(ABI[tok] if tok in ABI else int(tok[1:]))
    sid = f"asm-{af.stem}" + ("" if af.suffix == ".S" else "-attic")
    stubs.append([sid, "minichlink/stubs" if af.suffix == ".S" else "misc/attic",
                  "ch32fun", rel(af), 1, af.stem,
                  "CH5xx" if af.stem.startswith("ch5xx") else "V003;V00X;X035;V20x;V30x",
                  "", 1, "asm", 0, "", "", "",
                  " ".join(f"x{r}" for r in sorted(regs)),
                  1 if regs and max(regs) <= 15 else (0 if regs else ""),
                  "サイズはビルド条件依存のため空(§4.1)", "#", "attested", f"oss:{rel(af)}"])

# ============================================================ scratchpad 引数
# minichlink が scratchpad の何 byte 目に何を置くかを pgm-b003fun.c から拾う
ARG = re.compile(r"(?:rbuff|buffer|scratch\w*)\s*\[\s*(\d+)\s*\]")
for m in re.finditer(r"//\s*(@(\d+)\s+[^\n]*)", t):
    stub_args.append(["b003-scratchpad", m.group(2), "", "", m.group(1).strip(),
                      "#", "attested", f"oss:{rel(B003)}(L{lineno(t, m.start())})"])

w("stubs.csv",
  ["stub_id", "host_tool", "repo", "path", "line", "name", "target_family", "blob_bytes",
   "active", "source_form", "is_generated", "generated_from", "disasm_path", "equiv_group",
   "reg_set", "rv32ec_safe", "notes"] + TAIL, stubs)
w("stub_args.csv", ["stub_id", "scratchpad_offset", "width_bytes", "direction", "meaning"] + TAIL,
  stub_args)
