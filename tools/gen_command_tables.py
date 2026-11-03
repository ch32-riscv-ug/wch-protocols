#!/usr/bin/env python3
"""Generate the WCH-Link command tables in protocols/pc-to-link.ja.md from protocols/data/wchlink-commands.csv,
and the AttachChip family-byte table (§9) from protocols/data/families.csv (between
`<!-- BEGIN generated:families -->` and `<!-- END generated:families -->`).

The CSV is the source. Each table in the markdown sits between

    <!-- BEGIN generated:wchlink-commands section=<S> -->
    <!-- END generated:wchlink-commands section=<S> -->

and holds the CSV rows whose `section` column is <S>, in CSV order. Rows that share an `id` are the same command
measured in different scopes; the first of them carries the anchor `<a id="<id>"></a>`.

    python3 tools/gen_command_tables.py          # rewrite the tables in place
    python3 tools/gen_command_tables.py --check  # exit 1 if the markdown is out of date

Only the standard library is used.
"""
import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CSV = ROOT / "protocols/data/wchlink-commands.csv"
FAMILIES = ROOT / "protocols/data/families.csv"
FAMILY_COLUMNS = ["byte", "family", "core", "status", "note"]
FAMILY_BLOCK = re.compile(r"(<!-- BEGIN generated:families -->\n)(.*?)(<!-- END generated:families -->)", re.S)
DOC = ROOT / "protocols/pc-to-link.ja.md"
COLUMNS = ["id", "section", "name", "request_frame", "cmd", "sub", "payload", "response", "status", "evidence",
           "scope_probe", "scope_fw", "scope_chip", "accepted", "effect", "aliases", "source", "note"]
STATUS = {"verified", "refuted", "attested", "single-source", "conflict", "todo", "draft"}
YESNO = {"yes", "no", "unknown", "n/a"}
PROBE = {"linke": "LinkE", "ch549": "CH549 Link", "linkw": "LinkW", "any": "全 probe"}
BLOCK = re.compile(r"(<!-- BEGIN generated:wchlink-commands section=(?P<s>[0-9a-z]+) -->\n)(?P<body>.*?)"
                   r"(<!-- END generated:wchlink-commands section=(?P=s) -->)", re.S)


def load():
    with CSV.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != COLUMNS:
            sys.exit(f"{CSV}: columns {reader.fieldnames} != {COLUMNS}")
        rows = list(reader)
    for n, r in enumerate(rows, 2):
        if r["status"] not in STATUS:
            sys.exit(f"{CSV}:{n}: status {r['status']!r} is not in the README vocabulary")
        for c in ("accepted", "effect"):
            if r[c] not in YESNO:
                sys.exit(f"{CSV}:{n}: {c} {r[c]!r} is not one of {sorted(YESNO)}")
        if r["request_frame"] and not r["request_frame"].startswith("81 "):
            sys.exit(f"{CSV}:{n}: request_frame must be the whole frame starting with 81")
    return rows


def cell(text):
    return text.replace("|", "\\|").replace("\n", " ")


def code(text):
    return f"`{text}`" if text and not text.startswith("(") else (text or "—")


def scope(r):
    probe = PROBE.get(r["scope_probe"], r["scope_probe"])
    head = " ".join(x for x in (probe, r["scope_fw"]) if x)
    chips = r["scope_chip"] if r["scope_chip"] not in ("", "-") else ""
    return " · ".join(x for x in (head, chips) if x) or "—"


def table(rows):
    out = ["| request frame(host → probe) | 名前 | 応答 | status | 受理 / 効果 | 範囲(probe fw · chip) | 根拠 | 説明 |",
           "|---|---|---|---|---|---|---|---|"]
    seen = set()
    for r in rows:
        name = r["name"]
        if r["id"] not in seen:
            seen.add(r["id"])
            name = f'<a id="{r["id"]}"></a>**{name}**'
        evidence = "; ".join(x for x in (r["evidence"], r["source"]) if x)
        note = r["note"]
        if r["aliases"]:
            note = (note + " " if note else "") + f"別名: {r['aliases']}"
        out.append("| " + " | ".join(cell(x) for x in (
            code(r["request_frame"]), name, code(r["response"]), f"`{r['status']}`",
            f"{r['accepted']} / {r['effect']}", scope(r), evidence or "—", note or "")) + " |")
    return "\n".join(out) + "\n"


def render(doc, rows):
    def sub(m):
        sel = [r for r in rows if r["section"] == m["s"]]
        if not sel:
            sys.exit(f"{DOC}: no CSV rows for section {m['s']}")
        return m.group(1) + table(sel) + m.group(4)
    return BLOCK.sub(sub, doc)


def families(doc):
    with FAMILIES.open(newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != FAMILY_COLUMNS:
            sys.exit(f"{FAMILIES}: columns {reader.fieldnames} != {FAMILY_COLUMNS}")
        rows = list(reader)
    seen = set()
    for n, r in enumerate(rows, 2):
        if not re.fullmatch(r"0x[0-9A-F]{2}", r["byte"]):
            sys.exit(f"{FAMILIES}:{n}: byte {r['byte']!r} is not 0xHH (upper case)")
        if r["byte"] in seen:
            sys.exit(f"{FAMILIES}:{n}: byte {r['byte']} twice")
        seen.add(r["byte"])
        if r["status"] not in STATUS:
            sys.exit(f"{FAMILIES}:{n}: status {r['status']!r} is not in the README vocabulary")
    out = ["| byte | family | core | status |", "|---|---|---|---|"]
    for r in rows:
        status = f"`{r['status']}`" + (f"({r['note']})" if r["note"] else "")
        out.append("| " + " | ".join(cell(x) for x in (f"`{r['byte']}`", r["family"], r["core"], status)) + " |")
    body = "\n".join(out) + "\n"
    if not FAMILY_BLOCK.search(doc):
        sys.exit(f"{DOC}: no generated:families block")
    return FAMILY_BLOCK.sub(lambda m: m.group(1) + body + m.group(3), doc)


def main():
    rows = load()
    doc = DOC.read_text(encoding="utf-8")
    new = families(render(doc, rows))
    used = {m["s"] for m in BLOCK.finditer(doc)}
    missing = sorted({r["section"] for r in rows} - used)
    if missing:
        sys.exit(f"{DOC}: no generated block for section(s) {missing}")
    if "--check" in sys.argv[1:]:
        if new != doc:
            sys.exit(f"{DOC} is out of date with {CSV} / {FAMILIES}; run tools/gen_command_tables.py")
        return
    DOC.write_text(new, encoding="utf-8")


if __name__ == "__main__":
    main()
