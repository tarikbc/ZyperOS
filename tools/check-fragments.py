#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turns the Makefile's lists of checks and docs/building.md's table into a
file a check (mk/checks/NAME.mk; tools/building-doc.py says what is in one).

    python3 tools/check-fragments.py          convert this tree
    python3 tools/check-fragments.py --dry    say what it would do
    python3 tools/check-fragments.py --merge-main [--main REF]
                                              main (origin/main) merged into
                                              this branch, its lists taken
                                              whole and converted again, one
                                              commit: how the pull request
                                              that brings this in is merged

From the Makefile's CHECKS (the names), docs/building.md's table (each one's
row) and CI's check-linux job (its CHECKS_SKIP), each check gets its file;
then the Makefile gets the include in CHECKS' place, its .PHONY list loses
the checks' names (`.PHONY: $(CHECKS)` has them), and gains `make
building-doc` and `make check-linux`; the table is written from the files,
in the names' order; and check-linux runs `make check-linux`.

Run again on a tree that is converted, it changes nothing; on one where a
pull request merged since added a name to the old lists, merged by taking
main's Makefile, docs/building.md and check.yml whole, it makes that check's
file and converts the rest again. So the pull request that brings this in is
merged as: main merged into it, those three files main's, this run, one
commit, instead of a conflict resolved by hand.
"""
from pathlib import Path
import re
import subprocess
import sys
import textwrap

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
MK = ROOT / 'Makefile'
DOC = ROOT / 'docs' / 'building.md'
CI = ROOT / '.github' / 'workflows' / 'check.yml'
DIR = ROOT / 'mk' / 'checks'
ROW = re.compile(r'^\| `([\w-]+)` \| (.*) \|$')


def logical(text, start):
    """The span (a, b) of the make line that starts with `start`, its
    continuations with it"""
    m = re.search(r'^%s' % re.escape(start), text, re.M)
    if not m:
        return None
    b = m.start()
    while True:
        e = text.index('\n', b)
        if not text[b:e].rstrip().endswith('\\'):
            return m.start(), e + 1
        b = e + 1


def names(text, span, start):
    return text[span[0]:span[1]].replace('\\\n', ' ')[len(start):].split()


def linux_skip(ci):
    m = re.search(r'make check CHECKS_SKIP="([^"]*)"', ci)
    return m.group(1).split() if m else []


# Rows that spoke of the old lists, as they were, and as they are now
ROWS = {
    'check-building-doc': ("this table has a row for every check in the Makefile's CHECKS, and none for a check "
                           "that is gone",
                           "this table is what `make building-doc` writes from `mk/checks/*.mk`, a row for every "
                           "check in the names' order; each file adds its own name to CHECKS and has a row, the "
                           "Makefile adds none itself, and every check has a rule"),
}


def fragment(name, row, skip):
    if name in ROWS and row == ROWS[name][0]:
        row = ROWS[name][1]
    out = ['CHECKS += ' + name]
    if skip:
        out.append('CHECKS_LINUX_SKIP += ' + name)
    lines = textwrap.wrap(row, width=96, break_long_words=False, break_on_hyphens=False)
    if ' '.join(lines) != row:
        lines = [row]
    out += ['#| ' + line for line in lines]
    return '\n'.join(out) + '\n'


INCLUDE = '''# Each check registers itself in a file of its own, mk/checks/NAME.mk: its
# name added to CHECKS, and to CHECKS_LINUX_SKIP while it does not pass on
# Linux yet, and its row of docs/building.md's table as #| lines, which make
# building-doc writes the table from (tools/building-doc.py). Its rule is
# here beside the ones like it, or in the file. Two pull requests that each
# add a check add a file each, and meet nowhere; the two lists here and the
# table, which each such PR added a line to the end of, conflicted every
# time (2026-11-20: #421 twice, #422 and #428 once).
CHECKS :=
CHECKS_LINUX_SKIP :=
include $(sort $(wildcard mk/checks/*.mk))
'''

CHECK_LINUX = '''
# CI's Linux job (.github/workflows/check.yml, check-linux): make check but
# for the checks that do not pass on Linux yet, which macOS still runs; each
# says so in its mk/checks file (CHECKS_LINUX_SKIP)
check-linux:
	@$(MAKE) --no-print-directory check CHECKS_SKIP="$(strip $(CHECKS_LINUX_SKIP) $(CHECKS_SKIP))"
'''

BUILDING_DOC = '''# docs/building.md's table of checks is what mk/checks/*.mk say: a row for
# every check, in the names' order, and every check a rule. It had 32 of 91
# rows when a hand kept it (2026-11-06) and said nothing of the rest.
check-building-doc:
	@python3 tools/building-doc.py --check

# The table written again from mk/checks/*.mk
building-doc:
	@python3 tools/building-doc.py --write
'''

OLD_BUILDING_DOC = '''# docs/building.md's table names every check in CHECKS and nothing else:
# the table had 32 of 91 rows (2026-11-06) and said nothing of the rest.
check-building-doc:
	@python3 tools/building-doc-check.py
'''

DOC_NOTE = '''Each check registers itself in a file of its own, `mk/checks/NAME.mk`:
`CHECKS += NAME`, `CHECKS_LINUX_SKIP += NAME` while it does not pass on
Linux yet (CI's check-linux job, `make check-linux`, leaves those out), and
its row of the table below as lines that start `#|`. `make building-doc`
writes the table from those files, in the names' order, and
`check-building-doc` fails while it is not current, or a check has no rule.
Two pull requests that each add a check add a file each and merge without a
conflict.

'''

# Prose that named the old lists, as it was, and as it is now
PROSE = [
    (DOC, "`HEAD`), `make native`, `make check` with check-linux's `CHECKS_SKIP` and\n",
     "`HEAD`), `make native`, `make check-linux` (check-linux's list) and\n"),
    # Not tools/ubuntu-setup.sh's comment: the cloud workspace keeps a copy of
    # that file, held to its hash, and tools/merge-train.py's image is tagged by it
]


def git(*args, check=True):
    r = subprocess.run(['git', *args], cwd=ROOT, text=True, capture_output=True)
    if check and r.returncode:
        sys.exit('check-fragments: git %s: %s' % (' '.join(args), r.stderr.strip()))
    return r


def listed_checks(makefile):
    span = logical(makefile, 'CHECKS = ')
    return set(names(makefile, span, 'CHECKS = ')) if span else None


def merge_main(main_ref):
    """main merged into this branch, the old lists and table taken from
    main whole and converted again: the merge of the pull request that
    brings the files in, with no conflict resolved by hand"""
    if git('status', '--porcelain').stdout.strip():
        sys.exit('check-fragments: the worktree has changes: commit or put them aside first')
    if main_ref == 'origin/main':
        git('fetch', '-q', 'origin', '+refs/heads/main:refs/remotes/origin/main')
    want = listed_checks(git('show', main_ref + ':Makefile').stdout)
    if want is None:
        sys.exit('check-fragments: main has no CHECKS list: it is converted already')
    r = git('-c', 'merge.conflictStyle=merge', 'merge', '--no-ff', '--no-commit', main_ref, check=False)
    conflicted = git('diff', '--name-only', '--diff-filter=U').stdout.split()
    mine = ['Makefile', 'docs/building.md', '.github/workflows/check.yml', 'tools/building-doc-check.py']
    other = [p for p in conflicted if p not in mine]
    if other:
        git('merge', '--abort', check=False)
        sys.exit('check-fragments: main conflicts with this branch in %s, which is not a list: by hand' %
                 ', '.join(other))
    if r.returncode and not conflicted:
        git('merge', '--abort', check=False)
        sys.exit('check-fragments: git merge: ' + (r.stderr.strip() or r.stdout.strip()))
    # The lists and the table as main has them, whole
    for p in mine[:3]:
        git('checkout', main_ref, '--', p)
    if git('cat-file', '-e', main_ref + ':tools/building-doc-check.py', check=False).returncode == 0:
        git('checkout', main_ref, '--', 'tools/building-doc-check.py')
    convert(False)
    git('add', '-A')
    import importlib.util
    spec = importlib.util.spec_from_file_location('building_doc', ROOT / 'tools' / 'building-doc.py')
    bd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bd)
    got = {n for n, *_ in bd.fragments()}
    if got != want:
        sys.exit('check-fragments: the files are not main\'s checks: more %s, fewer %s; the merge is left '
                 'as it is (git merge --abort)' % (sorted(got - want), sorted(want - got)))
    if subprocess.run([sys.executable, str(ROOT / 'tools' / 'building-doc.py'), '--check'], cwd=ROOT).returncode:
        sys.exit('check-fragments: make check-building-doc fails; the merge is left as it is (git merge --abort)')
    git('commit', '-q', '-m', 'Merge ' + main_ref.replace('origin/', '') + '; its checks into mk/checks again\n\npython3 tools/check-fragments.py '
        '--merge-main: main\'s Makefile, docs/building.md and check.yml taken whole and converted again.')
    print('check-fragments: main merged and converted: %d checks, as main has them; push it (git push)' % len(got))
    return 0


def main():
    args = sys.argv[1:]
    if '--merge-main' in args:
        return merge_main(args[args.index('--main') + 1] if '--main' in args else 'origin/main')
    return convert('--dry' in sys.argv[1:])


def convert(dry):
    mk, doc, ci = MK.read_text(), DOC.read_text(), CI.read_text()
    said = []

    # The checks: the Makefile's CHECKS, then any file already made
    span = logical(mk, 'CHECKS = ')
    listed = names(mk, span, 'CHECKS = ') if span else []
    rows = {}
    for line in doc.splitlines():
        m = ROW.match(line)
        if m:
            rows[m.group(1)] = m.group(2)
    skip = set(linux_skip(ci))
    made = 0
    # While the Makefile has its list, the list, the table and check-linux
    # are the truth: every file written from them again, and a file of a
    # check no longer listed gone (main merged in, its lists taken whole)
    for name in listed:
        if name not in rows:
            sys.exit(f'check-fragments: {name} is in CHECKS and has no row in docs/building.md: write one first')
        f = DIR / (name + '.mk')
        text = fragment(name, rows[name], name in skip)
        if not f.exists() or f.read_text() != text:
            made += 1
            if not dry:
                DIR.mkdir(parents=True, exist_ok=True)
                f.write_text(text)
    gone = [f for f in DIR.glob('*.mk') if listed and f.stem not in listed]
    for f in gone:
        if not dry:
            f.unlink()
    if made:
        said.append(f'{made} files in mk/checks written')
    if gone:
        said.append(f'{len(gone)} files of checks no longer listed gone ({", ".join(f.stem for f in gone)})')
    frag = {f.stem for f in DIR.glob('*.mk')} | (set(listed) if dry else set())

    # The Makefile: the include in CHECKS' place, .PHONY without the checks
    if span:
        mk = mk[:span[0]] + INCLUDE + mk[span[1]:]
        said.append('the Makefile\'s CHECKS: the include')
    pspan = logical(mk, '.PHONY: ')
    phony = names(mk, pspan, '.PHONY: ')
    keep = [n for n in phony if n not in frag]
    for extra in ('check-linux', 'building-doc'):
        if extra not in keep:
            keep.append(extra)
    if keep != phony:
        text = '.PHONY: ' + ' \\\n        '.join(' '.join(keep[i:i + 12]) for i in range(0, len(keep), 12)) + '\n'
        mk = mk[:pspan[0]] + text + mk[pspan[1]:]
        said.append(f'the Makefile\'s .PHONY: {len(phony) - len(keep) + 2} names fewer')
    if '\ncheck-linux:' not in mk:
        m = re.search(r'^check: check-prep\n(?:\t.*\n)+', mk, re.M)
        if not m:
            sys.exit('check-fragments: no `check: check-prep` rule to put check-linux after')
        mk = mk[:m.end()] + CHECK_LINUX + mk[m.end():]
        said.append('make check-linux')
    if OLD_BUILDING_DOC in mk:
        mk = mk.replace(OLD_BUILDING_DOC, BUILDING_DOC)
        said.append('make building-doc')

    # docs/building.md: the table between its marks, written from the files
    import importlib.util
    spec = importlib.util.spec_from_file_location('building_doc', ROOT / 'tools' / 'building-doc.py')
    bd = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bd)
    if bd.START not in doc:
        lines = doc.split('\n')
        idx = [i for i, line in enumerate(lines) if ROW.match(line)]
        if not idx or idx != list(range(idx[0], idx[-1] + 1)) or lines[idx[0] - 2:idx[0]] != ['| | |', '|---|---|']:
            sys.exit('check-fragments: docs/building.md\'s table of checks is not one block under "| | |"')
        lines[idx[0] - 2:idx[-1] + 1] = [bd.START, bd.END]
        doc = '\n'.join(lines)
        para = 'Each part can be run alone (`make check-NAME`)'
        if DOC_NOTE.strip() not in doc and para in doc:
            doc = doc.replace(para, DOC_NOTE + para, 1)
        said.append('docs/building.md: the table between its marks')
    if not dry:
        new = bd.written(doc, bd.fragments())
        if new != doc or new != DOC.read_text():
            said.append('docs/building.md\'s table written from mk/checks')
        doc = new

    # check.yml: check-linux runs make check-linux
    m = re.search(r'run: >\n(\s+)make check CHECKS_SKIP="[^"]*"\n', ci)
    if m:
        ci = ci[:m.start()] + 'run: make check-linux\n' + ci[m.end():]
        ci = ci.replace('''  # moving the suite there. Not trusted yet: the checks named in CHECKS_SKIP
  # do not pass on Ubuntu (their first run, 2026-11-18: PNGs the shader demo
  # writes otherwise, the portal and the play page in another Chrome, the
  # shot checks' fonts, timing on four cores, Diatom's desktop port), so they
  # run on macOS only until each is fixed and taken off the list.''', '''  # moving the suite there. Not trusted yet: the checks whose mk/checks file
  # adds them to CHECKS_LINUX_SKIP do not pass on Ubuntu (their first run,
  # 2026-11-18: PNGs the shader demo writes otherwise, the portal and the
  # play page in another Chrome, the shot checks' fonts, timing on four
  # cores, Diatom's desktop port), so make check-linux leaves them out and
  # they run on macOS only until each is fixed and taken off the list.''')
        ci = ci.replace('- name: make check, but for the checks that do not pass on Linux yet',
                        '- name: make check-linux, the checks that pass on Linux')
        said.append('check.yml: check-linux runs make check-linux')

    if not dry:
        MK.write_text(mk)
        DOC.write_text(doc)
        CI.write_text(ci)
        for path, old, new in PROSE:
            text = path.read_text()
            if old in text:
                path.write_text(text.replace(old, new))
                said.append(f'{path.relative_to(ROOT)}: the prose')
        old = ROOT / 'tools' / 'building-doc-check.py'
        if old.exists():
            subprocess.run(['git', 'rm', '-q', '-f', '--', str(old.relative_to(ROOT))], cwd=ROOT)
            said.append('tools/building-doc-check.py gone (tools/building-doc.py --check)')
    print('check-fragments: ' + ('; '.join(said) if said else 'nothing to do: converted already'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
