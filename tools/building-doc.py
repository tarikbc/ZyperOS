#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""docs/building.md's table of checks, from the checks' own files.

    python3 tools/building-doc.py --write    write the table (make building-doc)
    python3 tools/building-doc.py --check    fail while it is not current
                                             (make check-building-doc)

Each check registers itself in mk/checks/NAME.mk, which the Makefile
includes: `CHECKS += NAME`, `CHECKS_LINUX_SKIP += NAME` while it does not
pass on Linux yet (CI's check-linux job leaves those out), and its row of
the table as comment lines that start `#|`, joined with spaces. Its rule can
stay in the Makefile beside the ones like it, or sit in the file too.

Every pull request that added a check used to add a name at the end of two
lists in the Makefile and a row at the end of this table, so each conflicted
with the next as soon as one merged (2026-11-20: #421 twice, #422 and #428
once, a 35-minute CI run each). A file a check, and a table in the order of
the names, meet nowhere: two new checks touch the same lines only when
their names are next to each other in that order, and then `make
building-doc` writes the table again.

--check fails, saying which, on: a file whose CHECKS name is not its own, a
name added twice, a file with no row; a CHECKS added to in the Makefile
itself; a check with no rule; and a table that is not what --write writes.
"""
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
DIR = ROOT / 'mk' / 'checks'
DOC = ROOT / 'docs' / 'building.md'
START = '<!-- The checks: make building-doc writes this table from mk/checks/*.mk -->'
END = '<!-- The end of the checks -->'


def fragments():
    """Each check's (name, linux skip, row, problems), in name order"""
    got = []
    for f in sorted(DIR.glob('*.mk'), key=lambda f: f.stem):
        text = f.read_text()
        bad = []
        adds = re.findall(r'^CHECKS\s*\+=\s*(.*?)\s*$', text, re.M)
        skips = re.findall(r'^CHECKS_LINUX_SKIP\s*\+=\s*(.*?)\s*$', text, re.M)
        row = ' '.join(line[2:].strip() for line in text.splitlines() if line.startswith('#|'))
        if adds != [f.stem]:
            bad.append(f'{f.relative_to(ROOT)} adds {adds or "nothing"} to CHECKS, not its own name {f.stem}')
        if skips not in ([], [f.stem]):
            bad.append(f'{f.relative_to(ROOT)} adds {skips} to CHECKS_LINUX_SKIP, not its own name')
        if not row:
            bad.append(f'{f.relative_to(ROOT)} has no row for docs/building.md (#| lines)')
        got.append((f.stem, bool(skips), row, bad))
    return got


def table(frags):
    return [START, '| | |', '|---|---|'] + ['| `%s` | %s |' % (n, row) for n, _, row, _ in frags] + [END]


def written(doc, frags):
    """doc with its table written from frags; None when it has no table"""
    lines = doc.split('\n')
    try:
        a, b = lines.index(START), lines.index(END)
    except ValueError:
        return None
    return '\n'.join(lines[:a] + table(frags) + lines[b + 1:])


def rules():
    """The targets the Makefile has a rule for, a recipe or prerequisites
    (make's own database: one named only in .PHONY is listed there too,
    with neither)"""
    r = subprocess.run(['make', '-pqrR', '-f', 'Makefile', 'no-such-target-here'], cwd=ROOT, text=True,
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    have = set()
    for entry in r.stdout.split('\n\n'):
        lines = [line for line in entry.split('\n') if line and not line.startswith('#')]
        m = re.match(r'^([\w.-]+):(.*)$', lines[0]) if lines else None
        if m and (m.group(2).strip() or any(line.startswith('\t') for line in lines[1:])):
            have.add(m.group(1))
    return have


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else '--check'
    frags = fragments()
    doc = DOC.read_text()
    new = written(doc, frags)
    if new is None:
        print(f'FAIL docs/building.md has no table of checks between {START} and {END}')
        return 1
    if mode == '--write':
        if new != doc:
            DOC.write_text(new)
            print(f'building-doc: wrote docs/building.md\'s table: {len(frags)} checks')
        else:
            print(f'building-doc: docs/building.md\'s table is current ({len(frags)} checks)')
        return 0
    bad = [b for *_, problems in frags for b in problems]
    names = [n for n, *_ in frags]
    mk = (ROOT / 'Makefile').read_text().replace('\\\n', ' ')
    for m in re.finditer(r'^CHECKS\s*(\+=|=)\s*(\S.*)$', mk, re.M):
        bad.append(f'the Makefile adds {m.group(2).split()[:3]} to CHECKS itself: a check registers in '
                   f'mk/checks/NAME.mk')
    have = rules()
    bad += [f'{n} is in mk/checks and has no rule' for n in names if n not in have]
    if new != doc:
        bad.append('docs/building.md\'s table of checks is not what mk/checks/*.mk say: make building-doc')
    for b in bad:
        print('FAIL ' + b)
    if bad:
        return 1
    print(f'ok: docs/building.md has a row for each of the {len(frags)} checks, as mk/checks/*.mk have them')
    return 0


if __name__ == '__main__':
    sys.exit(main())
