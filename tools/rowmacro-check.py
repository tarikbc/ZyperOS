#!/usr/bin/env python3
"""No row macro is handed a side effect, and ROW_H reads its row once.

src/menu.h's ROW_IS_* macros and menu_panel's ROW_H read their row more than
once. ROW_H(rows[--lo]) in the list's give stepped lo three rows a pass, to
rows[-2], and crashed the launcher whenever DOWN was held in MENU while the
list still eased (found on the Pixel 2, 2026-11-05). ROW_H is a function
under the macro now; this holds that, and that no ROW_* macro is handed an
argument with ++ or -- in it, which the ROW_IS_* ones would still repeat.
"""
from pathlib import Path
import re
import sys

failures = []
for path in sorted(Path("src").glob("*.c")):
    # Comments out, their line breaks kept, so a line number is the file's
    text = re.sub(r'/\*.*?\*/|//[^\n]*', lambda c: '\n' * c.group(0).count('\n'),
                  path.read_text(), flags=re.S)
    for m in re.finditer(r'\bROW_[A-Z_]+\s*\(', text):
        depth, i = 1, m.end()
        while i < len(text) and depth:
            depth += {'(': 1, ')': -1}.get(text[i], 0)
            i += 1
        arg = text[m.end():i - 1]
        if '++' in arg or '--' in arg:
            line = text.count('\n', 0, m.start()) + 1
            failures.append(f"{path}:{line}: {m.group(0)}{arg}): a side effect in a row macro")

panel = Path("src/menu_panel.c").read_text()
row_h = re.search(r'#define ROW_H\(r\)\s*(.*)', panel)
if not row_h or not re.match(r'\w+\(\(r\),', row_h.group(1).strip()):
    failures.append("src/menu_panel.c: ROW_H must be one function call on (r), so its row is read once")

for f in failures:
    print("FAIL", f)
if failures:
    sys.exit(1)
print("ok: no row macro is handed a side effect, and ROW_H reads its row once")
