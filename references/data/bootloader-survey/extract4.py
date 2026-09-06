#!/usr/bin/env python3
"""bootloader 横断調査 — stub の scratchpad 引数配置(U4)

minichlink `pgm-b003fun.c` の呼び出し側を読んで、各 stub が scratchpad の
何 byte 目に何を置かれるかを確定する。

framing(pgm-b003fun.c を読んで確定):
    ResetOp()          commandbuffer[0..3] = "AA 00 00 00"、commandplace = 4
    WriteOpArb(blob)   commandplace から blob をコピー(= stub 本体は offset 4)
    WriteOp4(v)        commandplace に 4 byte 追記
    memcpy(&cb[N], …)  データを絶対 offset N へ
    CommitOp()         pad_size へ切り上げ、末尾 4 byte に magic 0x1234abcd、
                       cb[0] = 0xAA + pad_size/1024

    pad_size は {128, 1152, 2176, 3200, 4096, 5248, 6272} のいずれか。
"""
import csv
import os
import re
from pathlib import Path

ROOT = Path(os.environ.get("WCH_ROOT", Path(__file__).resolve().parents[4]))
OUT = Path(__file__).resolve().parent
SRC = ROOT / "ch32fun/minichlink/pgm-b003fun.c"
TAIL = ["#", "confidence", "basis"]

text = SRC.read_text(encoding="utf-8", errors="replace")
lines = text.splitlines()
rel = "ch32fun/minichlink/pgm-b003fun.c"

# stub 名 -> blob サイズ(stubs.csv から引く)
blob_size = {}
for r in csv.DictReader(open(OUT / "stubs.csv", encoding="utf-8")):
    if r["blob_bytes"]:
        blob_size[r["name"]] = int(r["blob_bytes"])

rows = []
cur = None          # (stub_name, blob_len, start_line)
place = None
alts = []

RESET = re.compile(r"\bResetOp\(")
ARB = re.compile(r"WriteOpArb\(\s*eps,\s*(\w+),")
OP4 = re.compile(r"WriteOp4\(\s*eps,\s*(.+?)\s*\);\s*(?://\s*(.*))?$")
MEMCPY_W = re.compile(r"memcpy\(\s*&eps->commandbuffer\[(\d+|eps->commandplace)\]\s*,\s*([\w+ ]+),\s*(.+?)\s*\);\s*(?://\s*(.*))?$")
MEMCPY_R = re.compile(r"memcpy\(\s*(\w+),\s*&eps->respbuffer\[(\d+)\]\s*,\s*(.+?)\s*\)")
COMMIT = re.compile(r"CommitOp\(\s*eps,\s*([^,]+),\s*([^)]+)\)")

for i, line in enumerate(lines, 1):
    s = line.strip()
    if s.startswith("//") or s.startswith("*"):
        continue
    if RESET.search(s):
        cur, place, alts = None, 4, []
        continue
    m = ARB.search(s)
    if m and place is not None:
        name = m.group(1)
        n = blob_size.get(name)
        # 同じ Reset..Commit 区間に複数の WriteOpArb がある場合は if/else の
        # 代替実装(chip 別)なので、offset は進めず「別名の同じ位置」にする。
        if cur is None:
            place = place + (n if n else 0)
        alts.append(name)
        cur = (name, n, i)
        rows.append([name, "code", 4, n or "", "in", f"stub 本体({name})",
                     i, "#", "attested", f"oss:{rel}(L{i})"])
        continue
    m = OP4.search(s)
    if m and cur:
        for a in alts:
            rows.append([a, "arg", place, 4, "in",
                         (m.group(2) or m.group(1)).strip().rstrip(";"),
                         i, "#", "attested", f"oss:{rel}(L{i})"])
        place += 4
        continue
    m = MEMCPY_W.search(s)
    if m and cur:
        for a in alts:
            rows.append([a, "data", (place if m.group(1)=="eps->commandplace" else int(m.group(1))), m.group(3).strip(), "in",
                         (m.group(4) or f"host -> target データ ({m.group(2)})").strip(),
                         i, "#", "attested", f"oss:{rel}(L{i})"])
        continue
    m = MEMCPY_R.search(s)
    if m and cur:
        for a in alts:
            rows.append([a, "result", int(m.group(2)), m.group(3).strip(), "out",
                         "target -> host 読み出し結果", i, "#", "attested", f"oss:{rel}(L{i})"])
        continue
    m = COMMIT.search(s)
    if m and cur:
        for a in alts:
            rows.append([a, "commit", "pad_size-4", 4, "in",
                         f"magic 0x1234abcd(send_data_len={m.group(1).strip()}, "
                         f"receive_data_len={m.group(2).strip()})",
                         i, "#", "attested", f"oss:{rel}(L{i})"])
        cur, place, alts = None, None, []

# 重複行を潰す(同じ stub・同じ offset・同じ意味)
seen, uniq = set(), []
for r in rows:
    k = (r[0], r[1], r[2], r[5])
    if k in seen:
        continue
    seen.add(k)
    uniq.append(r)

with open(OUT / "stub_args.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(["stub_name", "kind", "scratchpad_offset", "width_bytes", "direction",
                 "meaning", "source_line"] + TAIL)
    cw.writerows(uniq)
print(f"stub_args.csv: {len(uniq)} rows")

# framing 定数も表にしておく
frames = []
for m in re.finditer(r"pad_size > (\d+) \) pad_size = (\d+);", text):
    frames.append([int(m.group(2)), 0xAA + int(m.group(2)) // 1024, f"> {m.group(1)}",
                   "#", "attested", f"oss:{rel}"])
frames.append([128, 0xAA, "<= 128", "#", "attested", f"oss:{rel}"])
frames.sort()
with open(OUT / "stub_framing.csv", "w", newline="", encoding="utf-8") as f:
    cw = csv.writer(f, lineterminator="\n")
    cw.writerow(["pad_size_bytes", "hid_report_id", "condition"] + TAIL)
    for r in frames:
        cw.writerow([r[0], f"0x{r[1]:02X}", r[2], r[3], r[4], r[5]])
print(f"stub_framing.csv: {len(frames)} rows")
