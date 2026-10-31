#!/usr/bin/env python3
"""Check the explicit anchors of the spec documents.

- Every heading in protocols/*.ja.md has an explicit ASCII anchor `<a id="..."></a>` on the nearest non-empty line
  above it, and ids are unique within a file.
- Every markdown link in the repo whose target is protocols/*.ja.md, README.md or coverage.ja.md and carries a
  `#fragment` points at an explicit anchor that exists (auto-generated heading slugs change with the heading text).

    python3 tools/check_anchors.py   # exit 1 and list the problems

Only the standard library is used.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ANCHOR = re.compile(r'<a id="([A-Za-z0-9._-]+)"></a>')
HEADING = re.compile(r"#{1,6} ")
LINK = re.compile(r"\]\(([^)\s]+\.md)#([^)\s]+)\)|\]\(#([^)\s]+)\)")
SPEC = {"README.md", "coverage.ja.md"}


def anchors(path):
    return set(ANCHOR.findall(path.read_text(encoding="utf-8")))


def is_spec(path):
    rel = path.relative_to(ROOT).as_posix()
    return rel in SPEC or (rel.startswith("protocols/") and rel.endswith(".ja.md"))


def main():
    errors = []
    for p in sorted((ROOT / "protocols").glob("*.ja.md")):
        lines = p.read_text(encoding="utf-8").split("\n")
        ids = []
        in_code = False
        for i, line in enumerate(lines):
            if line.startswith("```"):
                in_code = not in_code
            if in_code or not HEADING.match(line):
                continue
            prev = next((lines[j] for j in range(i - 1, -1, -1) if lines[j].strip()), "")
            m = ANCHOR.fullmatch(prev.strip())
            if not m:
                errors.append(f"{p.relative_to(ROOT)}:{i + 1}: heading without explicit anchor: {line}")
        ids = ANCHOR.findall("\n".join(lines))
        dup = {x for x in ids if ids.count(x) > 1}
        if dup:
            errors.append(f"{p.relative_to(ROOT)}: duplicate anchors {sorted(dup)}")
    files = subprocess.run(["git", "-C", str(ROOT), "ls-files", "*.md"], capture_output=True, text=True).stdout.split()
    cache = {}
    for f in files:
        src = ROOT / f
        for n, line in enumerate(src.read_text(encoding="utf-8").split("\n"), 1):
            for m in LINK.finditer(line):
                target = src if m.group(3) else (src.parent / m.group(1)).resolve()
                frag = m.group(3) or m.group(2)
                if not target.is_relative_to(ROOT) or not target.exists() or not is_spec(target):
                    continue
                if target not in cache:
                    cache[target] = anchors(target)
                if frag not in cache[target]:
                    errors.append(f"{f}:{n}: #{frag} is not an explicit anchor of {target.relative_to(ROOT)}")
    for e in errors:
        print(e)
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
