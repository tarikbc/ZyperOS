#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""make check's runner itself (tools/check-run.py), with stand-in checks:

- --shard I/N: the N parts together are the list, each check in one part
  but the SPLIT ones, in every part; the same parts from the list in any
  order; no part more than the longest check over another; a check the
  table has not met counted as NEW_CHECK
- check-i18n-shots' own parts (--part I/N): every language on every
  handheld in one part at least, a part's languages with English on the
  same handheld, the first part with English on every handheld, and the
  parts within one Brick run of each other
- the heavy slots: never more heavy checks at once than slots, in one run or
  across two; the rest go on while the heavy ones wait for one; a slot held
  by another process is waited for and taken once it ends, however it ends
  (a lock, not a folder left behind); and the time a check waited said on
  its line
- the slots' folder: the user's own cache, not /tmp; one that another user
  could write (mode 777) refused, and the run with it; a slot that is a link
  refused, the file it points to left as it was

make check-checkrun. No make, no launcher: the stand-in check is this file.
"""
import importlib.util
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parent.parent
fails = []


def check(ok, what):
    print(('ok   ' if ok else 'FAIL ') + what)
    if not ok:
        fails.append(what)


def load():
    spec = importlib.util.spec_from_file_location('check_run', ROOT / 'tools' / 'check-run.py')
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def shards(cr):
    rnd = random.Random(7)
    times = {f'check-{k}': rnd.choice([0.2, 1, 3, 8, 20, 45, 110, 570]) for k in range(240)}
    checks = list(times) + ['check-new-a', 'check-new-b', 'check-i18n-shots']
    times['check-i18n-shots'] = 1546
    for n in (1, 2, 3, 4, 7):
        parts = [cr.shard(checks, i, n, times) for i in range(1, n + 1)]
        seen = [c for p in parts for c in p if c not in cr.SPLIT]
        check(sorted(seen) == sorted(c for c in checks if c not in cr.SPLIT),
              f'{n} parts: every check in exactly one')
        check(all('check-i18n-shots' in p for p in parts), f'{n} parts: check-i18n-shots in each')
        check(all(p == [c for c in checks if c in p] for p in parts), f'{n} parts: each in the list\'s order')
        shuffled = checks[:]
        rnd.shuffle(shuffled)
        again = [sorted(cr.shard(shuffled, i, n, times)) for i in range(1, n + 1)]
        check(again == [sorted(p) for p in parts], f'{n} parts: the same parts from the list in another order')
        load = [sum(times.get(c, cr.NEW_CHECK) for c in p if c not in cr.SPLIT) for p in parts]
        longest = max(t for c, t in times.items() if c not in cr.SPLIT)
        check(max(load) - min(load) <= longest, f'{n} parts: within the longest check of each other '
              f'({min(load):.0f} to {max(load):.0f} s)')
    one = cr.shard(['check-a', 'check-b'], 1, 2, {})
    two = cr.shard(['check-a', 'check-b'], 2, 2, {})
    check(len(one) == 1 and len(two) == 1, 'two checks the table has not met: one a part')


# A stand-in check: "start NAME" in the trace, then held until what the run
# is to show has happened (CHECKRUN_HEAVY_WAIT for a heavy one,
# CHECKRUN_LIGHT_WAIT for the rest), then "end NAME". The trace's lines are
# in the order they were written (each a single append), so what ran beside
# what is read from their order, not from clocks: the stand-ins used to sleep
# 0.15 and 0.4 s and the check to read overlap from their times, which a
# loaded Mac running make check four at a time could stretch past it
# (2026-11-24). What is to be shown, waited for: "together:N", N checks
# running at once at some point; "heavies-together:N", N heavy ones;
# "lights-ended:N", N light ones ended. 30 s at most, then "gave-up NAME":
# what was to be shown never happened.
STAND_IN = r'''
import os, sys, time
name = sys.argv[1]
trace = os.environ['CHECKRUN_TRACE']


def say(kind):
    with open(trace, 'a') as f:
        f.write('%s %s\n' % (kind, name))


def most_at_once(ev, which):
    now = top = 0
    for kind, n in ev:
        if which(n) and kind in ('start', 'end'):
            now += 1 if kind == 'start' else -1
            top = max(top, now)
    return top


def shown(rule):
    if not rule:
        return True
    what, n = rule.split(':')
    with open(trace) as f:
        ev = [line.split() for line in f.read().splitlines()]
    if what == 'together':
        return most_at_once(ev, lambda c: True) >= int(n)
    if what == 'heavies-together':
        return most_at_once(ev, lambda c: c.startswith('heavy')) >= int(n)
    if what == 'lights-ended':
        return sum(1 for k, c in ev if k == 'end' and c.startswith('light')) >= int(n)
    raise SystemExit('stand-in: no such wait ' + rule)


say('start')
rule = os.environ.get('CHECKRUN_HEAVY_WAIT' if name.startswith('heavy') else 'CHECKRUN_LIGHT_WAIT', '')
until = time.monotonic() + 30
while not shown(rule):
    if time.monotonic() > until:
        say('gave-up')
        break
    time.sleep(0.005)
say('end')
sys.exit(1 if name == 'light-fails' else 0)
'''

HOLD = r'''
import fcntl, sys, time
f = open(sys.argv[1], 'a+')
fcntl.flock(f, fcntl.LOCK_EX)
f.seek(0); f.truncate(); f.write('the other run\n'); f.flush()
print('held', flush=True)
time.sleep(float(sys.argv[2]))
'''


def events(trace):
    try:
        return [line.split() for line in trace.read_text().splitlines()]
    except FileNotFoundError:
        return []


def spans(trace):
    """Each check's start and end, as the places of its lines in the trace"""
    start, end = {}, {}
    for i, (kind, name) in enumerate(events(trace)):
        if kind in ('start', 'end'):
            (start if kind == 'start' else end)[name] = i
    return {n: (start[n], end.get(n, float('inf'))) for n in start}


def gave_up(trace):
    return [name for kind, name in events(trace) if kind == 'gave-up']


def at(trace, kind, name):
    return next((i for i, e in enumerate(events(trace)) if e == [kind, name]), None)


def most_at_once(sp, names):
    edges = sorted([(sp[n][0], 1) for n in names] + [(sp[n][1], -1) for n in names])
    now = top = 0
    for _, d in edges:
        now += d
        top = max(top, now)
    return top


def slots(cr):
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-checkrun-'))
    stand_in = tmp / 'stand-in.py'
    stand_in.write_text(STAND_IN)
    trace = tmp / 'trace'
    cr.LOGS, cr.TIMES = tmp / 'logs', tmp / 'logs' / 'times.tsv'
    cr.SLOTS_DIR = tmp / 'slots'
    cr.MAKE = [sys.executable, str(stand_in)]
    cr.HEAVY = {'heavy-1', 'heavy-2', 'heavy-3', 'heavy-4'}
    cr.DOCKER = {}
    os.environ.pop('CI', None)
    os.environ['CHECKRUN_TRACE'] = str(trace)
    heavy = sorted(cr.HEAVY)
    light = ['light-1', 'light-2', 'light-3', 'light-4', 'light-5', 'light-6']

    # One slot, four at a time: one heavy one at once, the light ones beside.
    # The light ones held until three ran at once, the heavy ones until every
    # light one has ended: so the first heavy one holds the slot while all six
    # light ones go, or gives up
    os.environ['CHECK_HEAVY_SLOTS'] = '1'
    os.environ['CHECKRUN_LIGHT_WAIT'] = 'together:3'
    os.environ['CHECKRUN_HEAVY_WAIT'] = f'lights-ended:{len(light)}'
    rc = cr.main(['-j', '4'] + heavy + light)
    sp = spans(trace)
    check(rc == 0 and len(sp) == 10, f'one slot: every check ran and passed ({len(sp)} of 10)')
    check(most_at_once(sp, heavy) == 1, f'one slot: one heavy check at a time ({most_at_once(sp, heavy)})')
    check(most_at_once(sp, heavy + light) >= 3 and not gave_up(trace),
          f'one slot: the light ones run beside it (gave up: {gave_up(trace)})')
    check(max(sp[n][1] for n in light) < min(sp[n][1] for n in heavy),
          'one slot: the light ones done before the heavy ones, which waited')

    # Two slots: two at once, never three. The heavy ones held until two ran
    # at once
    trace.unlink()
    os.environ['CHECK_HEAVY_SLOTS'] = '2'
    os.environ['CHECKRUN_LIGHT_WAIT'] = ''
    os.environ['CHECKRUN_HEAVY_WAIT'] = 'heavies-together:2'
    rc = cr.main(['-j', '6'] + heavy + light)
    sp = spans(trace)
    check(rc == 0 and most_at_once(sp, heavy) == 2 and not gave_up(trace),
          f'two slots: two heavy checks at a time ({most_at_once(sp, heavy)}, gave up: {gave_up(trace)})')

    # Another run holds the only slot: the heavy ones wait for it, the light
    # ones do not, and the slot is taken once the other run is killed. Killed
    # once the light ones have all ended, and 1.5 s at least (the wait is said
    # on its line from 1 s), "kill other" in the trace as it is: the order of
    # the lines says the rest
    trace.unlink()
    trace.write_text('')
    os.environ['CHECK_HEAVY_SLOTS'] = '1'
    os.environ['CHECKRUN_LIGHT_WAIT'] = os.environ['CHECKRUN_HEAVY_WAIT'] = ''
    cr.SLOTS_DIR.mkdir(exist_ok=True)
    other = subprocess.Popen([sys.executable, '-c', HOLD, str(cr.SLOTS_DIR / 'slot-1'), '120'],
                             stdout=subprocess.PIPE, text=True)
    check(other.stdout.readline().strip() == 'held', 'the other run holds the slot')
    held = cr.Slots(1).take('mine')
    check(held is None, 'a slot another process holds is not taken')
    check(cr.Slots(1).holders() == ['the other run'], 'the slot says who holds it')

    def killer():
        soonest, until = time.monotonic() + 1.5, time.monotonic() + 30
        while time.monotonic() < until:
            ended = {n for k, n in events(trace) if k == 'end'}
            if {'light-1', 'light-2', 'light-fails'} <= ended and time.monotonic() >= soonest:
                break
            time.sleep(0.005)
        else:
            with open(trace, 'a') as f:
                f.write('gave-up killer\n')
        with open(trace, 'a') as f:
            f.write('kill other\n')
        other.kill()
    import threading
    threading.Thread(target=killer, daemon=True).start()
    import contextlib
    import io
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = cr.main(['-j', '3', 'heavy-1', 'light-1', 'light-2', 'light-fails'])
    other.wait()
    said = [line for line in out.getvalue().splitlines() if ' heavy-1 ' in line]
    check(any('(waited ' in line for line in said), f'its line says how long it waited: {said}')
    check(any('waiting for a heavy slot' in line and 'the other run' in line for line in said),
          'and while it waits, for whom')
    sp = spans(trace)
    killed = at(trace, 'kill', 'other')
    check(rc == 1, 'a failing check still fails the run')
    check(killed is not None and sp['heavy-1'][0] > killed, 'the heavy one started after the other run ended')
    check(not gave_up(trace) and all(sp[n][1] < killed for n in ('light-1', 'light-2', 'light-fails')),
          f'the light ones did not wait for it (gave up: {gave_up(trace)})')
    times = cr.read_times(cr.TIMES)
    check({'heavy-1', 'light-6'} <= set(times), 'times.tsv keeps the checks a smaller run did not run')
    got = cr.Slots(1).take('mine')
    check(got not in (None, True), 'the slot is free again after the run')
    cr.Slots(1).give(got)
    check(cr.Slots(1).holders() == [], 'a slot let go says it is free')

    # Planted: a folder anyone writes, a slot that is a link
    where = str(load().SLOTS_DIR)
    check(not where.startswith(('/tmp', '/private/tmp', '/var/tmp')),
          f'the slots are in the user\'s own cache, not a shared temp folder ({where})')
    open_dir = tmp / 'open'
    open_dir.mkdir()
    os.chmod(open_dir, 0o777)
    try:
        cr.Slots(1, open_dir).take('mine')
        check(False, 'a slots folder anyone writes is refused')
    except cr.Refused as e:
        check('mode' in str(e), f'a slots folder anyone writes is refused ({e})')
    cr.SLOTS_DIR = open_dir
    os.environ.pop('CHECKRUN_LIGHT_WAIT', None)
    os.environ.pop('CHECKRUN_HEAVY_WAIT', None)
    trace.write_text('')
    rc = cr.main(['-j', '2', 'heavy-1', 'light-1'])
    check(rc == 2 and spans(trace) == {}, f'and the run with it, nothing run ({rc})')
    linked = tmp / 'linked'
    linked.mkdir(mode=0o700)
    victim = tmp / 'victim.txt'
    victim.write_text('the user\'s own file\n')
    (linked / 'slot-1').symlink_to(victim)
    try:
        cr.Slots(1, linked).take('mine')
        check(False, 'a slot that is a link is refused')
    except cr.Refused as e:
        check(True, f'a slot that is a link is refused ({e})')
    check(victim.read_text() == 'the user\'s own file\n', 'the file the link points to is as it was')
    cr.SLOTS_DIR = linked
    trace.write_text('')
    rc = cr.main(['-j', '2', 'heavy-1', 'light-1'])
    sp = spans(trace)
    check(rc == 1 and 'heavy-1' not in sp and 'light-1' in sp,
          f'in a run, its check fails and is not run, the rest are ({rc}, {sorted(sp)})')
    check(victim.read_text() == 'the user\'s own file\n', 'and the file is still as it was')
    subprocess.call(['rm', '-rf', str(tmp)])


def i18n_parts():
    spec = importlib.util.spec_from_file_location('i18n_shots', ROOT / 'tools' / 'i18n-shots.py')
    s = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, ['i18n-shots']
    try:
        spec.loader.exec_module(s)
    finally:
        sys.argv = argv
    langs = s.i18n.CODES + [s.i18n.PSEUDO]
    runs = [(d, c) for d in s.DEVICES for c in langs]
    for n in (1, 2, 3, 4, 6, 18, 30):
        parts = [s.part_of(runs, s.DEVICES, i, n) for i in range(1, n + 1)]
        check(set().union(*map(set, parts)) == set(runs), f'i18n, {n} parts: every language on every handheld')
        check(all((d, 'en') in p for p in parts for d, _ in p), f'i18n, {n} parts: each with its handhelds\' English')
        check(all((d, 'en') in parts[0] for d in s.DEVICES), f'i18n, {n} parts: the first with English on each')
        cost = [sum(s.COST.get(d, 1.0) for d, _ in p) for p in parts]
        if n <= len(runs) // 2:
            check(max(cost) - min(cost) <= max(s.COST.values()),
                  f'i18n, {n} parts: within one Brick run of each other ({min(cost)} to {max(cost)})')
        again = [s.part_of(list(runs), s.DEVICES, i, n) for i in range(1, n + 1)]
        check(again == parts, f'i18n, {n} parts: the same deal again')


def main():
    cr = load()
    shards(cr)
    i18n_parts()
    slots(cr)
    print(f'check-checkrun: {"all passed" if not fails else f"{len(fails)} FAILED"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
