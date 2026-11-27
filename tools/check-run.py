#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""make check's runner: every check at once, a few at a time, each into a log.

    tools/check-run.py [-j N] [--shard I/N] CHECK...
    tools/check-run.py --times-from LOG...

The Makefile's `check` builds what more than one check needs first (the host
launcher, the ScreenScraper header: check-prep), then hands the list here.
Each check runs as `make --no-print-directory CHECK`, its output in
build-native/check-logs/CHECK.log; a line is printed as each one ends, the
whole log of each that failed at the end, and the slowest few. Exit status 1
when any failed.

N is the number of cores unless -j or CHECK_JOBS says otherwise; -j 1 is the
old one-after-another run. The longest checks of the last run start first (its
times are kept in build-native/check-logs/times.tsv), so the wall time is the
longest single check rather than the last one to be reached.

--shard I/N (make check CHECK_SHARD=I/N) runs the I-th of N parts of the list,
as CI's macOS job does on four runners at once: the checks dealt out by their
times in tools/check-times.tsv, the longest first, each to the part with the
least so far, so every part's own copy of this script deals the same hand
whatever its runner's last run took. A check in SPLIT runs in every part and
does its own I-th of N (CHECK_SHARD is in its environment): check-i18n-shots,
one launcher a language and a handheld, was 1546 of a 1941 s run on CI's
three cores (2026-11-21), longer than every other check together.
--times-from reads the "ok  check-x  12.3 s" lines of CI's job logs (gh run
view RUN --log, one log a part or all in one) into tools/check-times.tsv.

A check in HEAVY (a launcher a language, Diatom's own suite and build, Docker,
desk mode's timed presses) takes one of CHECK_HEAVY_SLOTS (2) slots first,
the same slots for every make check of this user on this computer, in any
worktree: five sessions' runs at once once put the load past 30 and desk
mode's presses were lost (2026-11-20). A slot is a lock on a file in the
user's own cache (~/Library/Caches/ZyperOS/heavy-slots on a Mac,
~/.cache/zyperos/heavy-slots elsewhere; CHECK_HEAVY_DIR), so a run that dies
lets go of it; the file says who holds it. The folder must be the user's
own and written by nobody else, and a slot is never a link: anything else is
refused. While the heavy ones wait, the rest go on. Not on CI (CI is set): a
runner is one run. CHECK_HEAVY_SLOTS=0 takes none.

Two checks never share a file: each writes its own build-native/ folder or a
temp path with its process id in it (Makefile, check-* rules), and Hare's
listen on ports the kernel picks. A check that needs the network or a device
is not in the list.
"""
import argparse
import fcntl
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / 'build-native' / 'check-logs'
TIMES = LOGS / 'times.tsv'
# CI's own times, for the parts: a runner has no last run
CI_TIMES = ROOT / 'tools' / 'check-times.tsv'
# A check the table has not met yet: as long as a middling one
NEW_CHECK = 15.0

# In every part, each doing its own share (CHECK_SHARD=I/N)
SPLIT = {'check-i18n-shots'}

# The ones that load the whole computer, or fail when it is loaded: a
# launcher a language and a handheld; Diatom's parallel build and its 43
# checks; Docker's (only with their flag); desk mode's presses and frames
HEAVY = {'check-i18n-shots', 'check-diatom',
         'check-desk', 'check-homekeys', 'check-videoview', 'check-menukeys', 'check-saver-art', 'check-saver-place', 'check-mp4views', 'check-mp4resume',
         'check-ingame-colors', 'check-pico8-display', 'check-pico8-keys', 'check-pico8-mp4',
         'check-p8fx', 'check-xferopen'}
DOCKER = {'check-gpu-upright': 'UPRIGHT_DOCKER', 'check-p8fx-gl': 'P8FX_DOCKER'}
SLOTS_DIR = Path(os.environ.get('CHECK_HEAVY_DIR') or
                 (Path.home() / 'Library/Caches/ZyperOS/heavy-slots' if sys.platform == 'darwin' else
                  Path(os.environ.get('XDG_CACHE_HOME') or Path.home() / '.cache') / 'zyperos/heavy-slots'))
# How a check is run (tools/check-run-check.py runs a stand-in)
MAKE = ['make', '--no-print-directory']


def read_times(path):
    try:
        rows = (line.split('\t') for line in path.read_text().splitlines())
        return {r[0]: float(r[1]) for r in rows if len(r) == 2}
    except (OSError, ValueError):
        return {}


def flag_set(name):
    """A make variable given on the command line or in the environment"""
    if os.environ.get(name):
        return True
    return bool(re.search(r'(^|\s)%s=\S' % name, os.environ.get('MAKEFLAGS', '')))


def heavy_set():
    return HEAVY | {c for c, flag in DOCKER.items() if flag_set(flag)}


def parse_shard(s):
    m = re.fullmatch(r'(\d+)/(\d+)', s or '')
    if not m or not 1 <= int(m.group(1)) <= int(m.group(2)):
        raise argparse.ArgumentTypeError(f'a shard is I/N with 1 <= I <= N, not {s!r}')
    return int(m.group(1)), int(m.group(2))


def shard(checks, i, n, times):
    """The I-th of N parts of checks, in their own order: the SPLIT ones and
    those dealt to it, longest first, each to the part with the least time so
    far (the lowest part on a tie). Depends on nothing but the list and the
    table, so every part deals the same."""
    rest = sorted((c for c in checks if c not in SPLIT), key=lambda c: (-times.get(c, NEW_CHECK), c))
    loads = [0.0] * n
    mine = {c for c in checks if c in SPLIT}
    for c in rest:
        k = min(range(n), key=lambda k: (loads[k], k))
        loads[k] += times.get(c, NEW_CHECK)
        if k == i - 1:
            mine.add(c)
    return [c for c in checks if c in mine]


class Refused(Exception):
    pass


class Slots:
    """CHECK_HEAVY_SLOTS locks in SLOTS_DIR, one heavy check each, shared by
    every run of this user on the computer. flock: let go when the holder
    ends, however. The folder is made 0700 and must be the user's own and
    written by nobody else, a slot a plain file of the user's opened with
    O_NOFOLLOW: a folder or link another user planted is refused (Refused),
    never written through."""

    def __init__(self, count, where=None):
        self.count, self.where = count, Path(where or SLOTS_DIR)

    def folder(self):
        self.where.mkdir(mode=0o700, parents=True, exist_ok=True)
        st = os.lstat(self.where)
        if not stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o022:
            raise Refused(f'{self.where} is not a folder of this user\'s that only it writes '
                          f'(owner {st.st_uid}, mode {stat.filemode(st.st_mode)}): remove it, or set '
                          f'CHECK_HEAVY_DIR')
        return self.where

    def open_slot(self, k):
        path = self.folder() / f'slot-{k}'
        try:
            fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW, 0o600)
        except OSError as e:
            raise Refused(f'{path}: {e.strerror} (a link, or not this user\'s)') from e
        st = os.fstat(fd)
        if not stat.S_ISREG(st.st_mode) or st.st_uid != os.getuid():
            os.close(fd)
            raise Refused(f'{path} is not a plain file of this user\'s')
        return os.fdopen(fd, 'r+')

    def take(self, check):
        """A held slot (an open file) or None when all are taken"""
        if self.count <= 0:
            return True
        for k in range(1, self.count + 1):
            f = self.open_slot(k)
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError:
                f.close()
                continue
            f.seek(0)
            f.truncate()
            f.write(f'{check} pid {os.getpid()} in {ROOT} since {time.strftime("%H:%M:%S")}\n')
            f.flush()
            return f
        return None

    def give(self, f):
        if f is True or f is None:
            return
        f.seek(0)
        f.truncate()
        f.write('free\n')
        f.flush()
        fcntl.flock(f, fcntl.LOCK_UN)
        f.close()

    def holders(self):
        said = []
        for k in range(1, self.count + 1):
            try:
                with self.open_slot(k) as f:
                    said.append(f.read().strip())
            except (OSError, Refused):
                pass
        return [s for s in said if s and s != 'free']


def times_from(logs):
    """CI's per-check times from its job logs into tools/check-times.tsv; a
    check in more than one log (a SPLIT one) at its longest"""
    got = {}
    for name in logs:
        text = sys.stdin.read() if name == '-' else Path(name).read_text(errors='replace')
        for m in re.finditer(r'(?:ok  |FAIL) (\S+)\s+([\d.]+) s(?:  \(waited .*\))?\s*$', text, re.M):
            got[m.group(1)] = max(got.get(m.group(1), 0.0), float(m.group(2)))
    if not got:
        print('check-run: no "ok  check-x  12.3 s" lines in ' + ', '.join(logs), file=sys.stderr)
        return 1
    CI_TIMES.write_text(''.join(f'{c}\t{got[c]:.1f}\n' for c in sorted(got)))
    print(f'check-run: wrote {CI_TIMES.relative_to(ROOT)}: {len(got)} checks, '
          f'{sum(got.values()):.0f} s in all')
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=int(os.environ.get('CHECK_JOBS') or 0) or os.cpu_count() or 4)
    ap.add_argument('--shard', type=parse_shard, help='I/N: the I-th of N parts (CI)')
    ap.add_argument('--times-from', nargs='+', metavar='LOG', help="write tools/check-times.tsv from CI's logs")
    ap.add_argument('checks', nargs='*')
    a = ap.parse_args(argv)
    if a.times_from:
        return times_from(a.times_from)
    if not a.checks:
        ap.error('no checks')

    checks = a.checks
    env = dict(os.environ)
    if a.shard:
        i, n = a.shard
        checks = shard(a.checks, i, n, read_times(CI_TIMES))
        split = [c for c in checks if c in SPLIT]
        env['CHECK_SHARD'] = f'{i}/{n}'
        print(f'check-run: part {i} of {n}: {len(checks)} of {len(a.checks)} checks'
              + (f' ({", ".join(split)} its own part {i}/{n})' if split else ''), flush=True)

    LOGS.mkdir(parents=True, exist_ok=True)
    for old in LOGS.glob('*.log'):
        old.unlink()
    before = read_times(TIMES)
    # Unknown checks first (a new one may be slow), then the slowest last time
    pending = sorted(checks, key=lambda c: -before.get(c, float('inf')))
    on_ci = bool(os.environ.get('CI'))
    slots = Slots(0 if on_ci else int(os.environ.get('CHECK_HEAVY_SLOTS') or 2))
    heavy = heavy_set() if slots.count > 0 else set()
    if heavy:
        try:
            slots.folder()
        except (OSError, Refused) as e:
            print(f'check-run: the heavy checks\' slots: {e}', file=sys.stderr)
            return 2
    lock = threading.Condition()
    took, failed, waited, said = {}, [], {}, [-60.0]
    width = max(len(c) for c in checks)
    start = time.monotonic()

    def next_check():
        """The first pending check that can start: any but a heavy one, which
        needs a slot; None when none are left"""
        with lock:
            while pending:
                refused = False
                for c in pending:
                    if c not in heavy:
                        pending.remove(c)
                        return c, None
                    try:
                        held = slots.take(c)
                    except (OSError, Refused) as e:
                        # Never run past a planted slot: the check fails, said why
                        pending.remove(c)
                        (LOGS / f'{c}.log').write_text(f'check-run: its heavy slot: {e}\n')
                        took[c] = 0.0
                        failed.append(c)
                        print(f'FAIL {c:<{width}}    0.0 s  (its heavy slot: {e})', flush=True)
                        refused = True
                        break
                    if held:
                        pending.remove(c)
                        return c, held
                if refused:
                    continue
                # Only heavy ones left, every slot taken
                t = time.monotonic()
                if t - said[0] >= 60:
                    said[0] = t
                    print(f'check-run: {", ".join(pending)} waiting for a heavy slot ({slots.where}: '
                          + ('; '.join(slots.holders()) or 'taken') + ')', flush=True)
                for c in pending:
                    waited.setdefault(c, t)
                lock.wait(2)
            return None, None

    def run(check, held):
        t0 = time.monotonic()
        try:
            with open(LOGS / f'{check}.log', 'wb') as log:
                rc = subprocess.call(MAKE + [check], cwd=ROOT, env=env,
                                     stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        finally:
            slots.give(held)
        dt = time.monotonic() - t0
        with lock:
            took[check] = dt
            if rc:
                failed.append(check)
            wait = t0 - waited.get(check, t0)
            wait = f'  (waited {wait:.0f} s for a slot)' if wait >= 1 else ''
            print(f'{"ok  " if rc == 0 else "FAIL"} {check:<{width}} {dt:6.1f} s{wait}', flush=True)
            lock.notify_all()

    def worker():
        while True:
            check, held = next_check()
            if check is None:
                return
            run(check, held)

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(max(1, min(a.j, len(checks))))]
    for t in threads:
        t.start()
    for t in threads:
        while t.is_alive():
            t.join(0.5)
    wall = time.monotonic() - start

    # This run's times over the last ones: a run of a few checks keeps the
    # rest's
    before.update(took)
    TIMES.write_text(''.join(f'{c}\t{before[c]:.2f}\n' for c in sorted(before)))
    for check in sorted(failed, key=checks.index):
        print(f'\n=== {check} (build-native/check-logs/{check}.log) ===')
        sys.stdout.write((LOGS / f'{check}.log').read_text(errors='replace'))
    slow = sorted(took, key=took.get, reverse=True)[:5]
    print(f'\n{len(checks)} checks in {wall:.1f} s with {a.j} at a time; '
          f'slowest: ' + ', '.join(f'{c} {took[c]:.1f} s' for c in slow))
    if failed:
        print(f'some checks FAILED: {" ".join(sorted(failed, key=checks.index))}', file=sys.stderr)
        return 1
    print('ok: every check passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
