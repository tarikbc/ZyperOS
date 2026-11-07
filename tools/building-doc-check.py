#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
"""docs/building.md's table of checks against the Makefile's CHECKS: a row for
every check, and no row for one that is gone (make check-building-doc)."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
mk = (ROOT / 'Makefile').read_text().replace('\\\n', ' ')
checks = re.search(r'^CHECKS = (.*)$', mk, re.M).group(1).split()
doc = (ROOT / 'docs/building.md').read_text()
rows = re.findall(r'^\| `([\w-]+)` \|', doc, re.M)
missing = [c for c in checks if c not in rows]
gone = [r for r in rows if r.startswith('check') and r not in checks]
twice = sorted({r for r in rows if rows.count(r) > 1})
for c in missing:
    print(f'FAIL {c} is in CHECKS and has no row in docs/building.md')
for r in gone:
    print(f'FAIL docs/building.md has a row for {r}, which is not in CHECKS')
for r in twice:
    print(f'FAIL docs/building.md has {r} twice')
if missing or gone or twice:
    sys.exit(1)
print(f'ok: docs/building.md has a row for each of the {len(checks)} checks')
