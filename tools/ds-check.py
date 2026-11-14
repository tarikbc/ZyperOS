#!/usr/bin/env python3
"""The design system's rules the compiler cannot hold (DESIGN.md section 15).

    python3 tools/ds-check.py        (make check-ds)

A screen is built from src/ds/'s parts by name and copies none of their
numbers, and a part decides nothing by reading a name. These are the rules
that went wrong before, read off the source:

- The components in src/ds/ know nothing of src/main.c: no app.h, no
  panel.h. Then a check can link them, and a screen in any file can use them.
- A page's foot is DS_PAGE_FOOT_Y, not 704 typed again at a page_hint.
- Zyper beside a page's heading is DS_PAGE_ZYPER_*, not (123, 168, 124).
- A tile's mark and the tiles' style are the caller's word (tiles_show):
  tile_glyph read the mark off the name, and tiles_are_list the style off the
  first tile's mark, which cut "ScreenScraper". Neither comes back.

Fails with each place, file:line.
"""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"

RULES = [
    (re.compile(r"\bpage_hint(?:_fade)?\s*\([^;]*?,\s*704\s*,"),
     "a page's foot typed as 704: DS_PAGE_FOOT_Y (src/ds/layout.h)"),
    (re.compile(r"\b(?:draw_zyper(?:_pose)?|zyper)\s*\([^;]*?,\s*123(?:\.0f?)?\s*,\s*16\d(?:\.0f?)?\s*,"),
     "Zyper beside a page's heading typed again: DS_PAGE_ZYPER_X, _Y and _H"),
    (re.compile(r"\b(?:tile_glyph|tiles_are_list)\s*\("),
     "a tile's mark or the tiles' style read off a name: the caller says them (tiles_show)"),
]
DS_INCLUDES = re.compile(r'#\s*include\s*"(app\.h|panel\.h)"')


def code_lines(text):
    """Each line with its comments blanked, so a rule's words in a comment
    (as here) are not the code it forbids"""
    out, in_block = [], False
    for line in text.splitlines():
        kept, i = [], 0
        while i < len(line):
            if in_block:
                end = line.find("*/", i)
                if end < 0:
                    i = len(line)
                    continue
                in_block, i = False, end + 2
                continue
            if line.startswith("/*", i):
                in_block, i = True, i + 2
                continue
            if line.startswith("//", i):
                break
            kept.append(line[i])
            i += 1
        out.append("".join(kept))
    return out


def main():
    fails = []
    for path in sorted(SRC.rglob("*.[ch]")):
        rel = path.relative_to(ROOT)
        text = path.read_text(errors="replace")
        lines = code_lines(text)
        in_ds = path.parent == SRC / "ds"
        for n, line in enumerate(lines, 1):
            if in_ds and DS_INCLUDES.search(line):
                fails.append("%s:%d: src/ds/ includes %s: a component knows nothing of src/main.c"
                             % (rel, n, DS_INCLUDES.search(line).group(1)))
            if in_ds:
                continue
            for rule, why in RULES:
                if rule.search(line):
                    fails.append("%s:%d: %s" % (rel, n, why))
    for f in fails:
        print("FAIL", f)
    if fails:
        return 1
    print("ok: src/ds/ stands alone; no page foot, page Zyper or tile mark typed or read off a name outside it")
    return 0


if __name__ == "__main__":
    sys.exit(main())
