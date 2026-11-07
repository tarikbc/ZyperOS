#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
"""make check's runner: every check at once, a few at a time, each into a log.

    tools/check-run.py [-j N] CHECK...

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

Two checks never share a file: each writes its own build-native/ folder or a
temp path with its process id in it (Makefile, check-* rules), and Hare's
listen on ports the kernel picks. A check that needs the network or a device
is not in the list.
"""
import argparse
import concurrent.futures as cf
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
LOGS = ROOT / 'build-native' / 'check-logs'
TIMES = LOGS / 'times.tsv'


def last_times():
    try:
        rows = (line.split('\t') for line in TIMES.read_text().splitlines())
        return {r[0]: float(r[1]) for r in rows if len(r) == 2}
    except (OSError, ValueError):
        return {}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-j', type=int, default=int(os.environ.get('CHECK_JOBS') or 0) or os.cpu_count() or 4)
    ap.add_argument('checks', nargs='+')
    a = ap.parse_args()

    LOGS.mkdir(parents=True, exist_ok=True)
    for old in LOGS.glob('*.log'):
        old.unlink()
    before = last_times()
    # Unknown checks first (a new one may be slow), then the slowest last time
    order = sorted(a.checks, key=lambda c: -before.get(c, float('inf')))
    lock = threading.Lock()
    took, failed = {}, []
    width = max(len(c) for c in a.checks)
    start = time.monotonic()

    def run(check):
        t0 = time.monotonic()
        with open(LOGS / f'{check}.log', 'wb') as log:
            rc = subprocess.call(['make', '--no-print-directory', check], cwd=ROOT,
                                 stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        dt = time.monotonic() - t0
        with lock:
            took[check] = dt
            if rc:
                failed.append(check)
            print(f'{"ok  " if rc == 0 else "FAIL"} {check:<{width}} {dt:6.1f} s', flush=True)

    with cf.ThreadPoolExecutor(max_workers=max(1, a.j)) as pool:
        for f in [pool.submit(run, c) for c in order]:
            f.result()
    wall = time.monotonic() - start

    TIMES.write_text(''.join(f'{c}\t{took[c]:.2f}\n' for c in a.checks))
    for check in sorted(failed, key=a.checks.index):
        print(f'\n=== {check} (build-native/check-logs/{check}.log) ===')
        sys.stdout.write((LOGS / f'{check}.log').read_text(errors='replace'))
    slow = sorted(took, key=took.get, reverse=True)[:5]
    print(f'\n{len(a.checks)} checks in {wall:.1f} s with {a.j} at a time; '
          f'slowest: ' + ', '.join(f'{c} {took[c]:.1f} s' for c in slow))
    if failed:
        print(f'some checks FAILED: {" ".join(sorted(failed, key=a.checks.index))}', file=sys.stderr)
        return 1
    print('ok: every check passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
