#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-shelfkeys: a shelf's d-pad held down, as a thumb holds it, in each
View, in desk mode (src/device/desk.h) in SDL's dummy window, driven over
its control socket, its `state` read for where the cursor landed.

The List view (DESIGN.md 11.39) and the grid's rows (11.47) step as every
list does (11.3): a held direction walks to the end and stops there, and
the next press goes round (my pick in the List View lab, 2026-11-23, as
the home's Grid has it). Before, a held Down on the list ran from the last
game to the first and on without a stop. The cover row and the Video view's
band go round held or not, as the home's row does.

L1 and R1 page every list by one rule (gg_page, 11.3): a screenful on, or
to the end, then from the end a press goes round and a held one stops
there. The shelf's list and grid, the home's Grid and the achievements
list (a menu list that pages) alike; before, the list went round on a hold,
the grids went to the end and round, and the achievements list never went
round.
build-native/zyperos must be built (make native)."""
import importlib.util
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
GAMES = ['Alpha', 'Bravo', 'Charlie', 'Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel', 'India', 'Juliet',
         'Kilo', 'Lima', 'Mike', 'November', 'Oscar', 'Papa', 'Quebec', 'Romeo', 'Sierra', 'Tango']
N = len(GAMES)
CHEEVOS = 20      # the achievements list's rows; it pages 8 at a time

# The home's check's socket talk, hold and settle: one way of holding a
# button for both
_spec = importlib.util.spec_from_file_location('homekeys', ROOT / 'tools/homekeys-check.py')
H = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(H)
ask, tap, until, hold, settle = H.ask, H.tap, H.until, H.hold, H.settle


def check(ok, what):
    H.check(ok, what)


def cursor(sock):
    return ask(sock, 'state')['cursor']


def run(tmp, view, name=None):
    """A shelf of N games in `view`, opened from the home; (sock, process, log)"""
    name = name or view
    base = tmp / name
    for d in ('ZyperOS', 'Roms', 'userdata', 'shared', 'web'):
        (base / d).mkdir(parents=True)
    sfc = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
           if l.startswith('sys|') and l.split('|')[4] == 'SFC']
    (base / 'ZyperOS/systems.cfg').write_text(sfc[0] + '\n')
    (base / 'ZyperOS/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    folder = base / 'Roms' / sfc[0].split('|')[2]
    folder.mkdir(parents=True)
    for g in GAMES:
        (folder / f'{g}.sfc').touch()
    # Alpha's achievements (src/cheevos.h's set file), so its details page
    # has an Achievements row that opens their list
    (folder / '.cheevos').mkdir()
    (folder / '.cheevos/Alpha.set').write_text(
        '#! zyperos-cheevos 2\tgame=1\tconsole=3\ttitle=Alpha\n' +
        ''.join(f'#:\t{i + 1}\t5\tStep {i + 1}\tA step.\n' for i in range(CHEEVOS)))
    env = dict(os.environ, ZYPEROS_ROOT=str(base / 'ZyperOS'), ZYPEROS_CARD=str(base),
               ZYPEROS_ROMS=str(base / 'Roms'), ZYPEROS_USERDATA=str(base / 'userdata'),
               ZYPEROS_SHARED=str(base / 'shared'), ZYPEROS_WEB=str(base / 'web'),
               ZYPEROS_FONT=str(ROOT / 'res/fonts/menu.ttf'))
    subprocess.run([str(BINARY), '--set', 'tour.done=1'], env=env, check=True, stdout=subprocess.DEVNULL)
    if view != 'covers':
        with sqlite3.connect(base / 'userdata/zyperos.db') as db:
            db.execute("insert or replace into settings(key,value) values('view.SFC',?)", (view,))
    (base / 'ZyperOS/.last').write_text('SFC\nAlpha.sfc\n\n')
    sock = Path(tempfile.gettempdir()) / f'zyperos-shelfkeys-{os.getpid()}-{name}.sock'
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(tmp / f'{name}-flags'),
               ZYPEROS_CTL_DIR=str(tmp / f'{name}-ctl'))
    (tmp / f'{name}-flags').mkdir()
    env.pop('ZYPEROS_HEADLESS', None)
    log = open(tmp / f'{name}.log', 'w')
    p = subprocess.Popen([str(BINARY), '--device', 'pixel2'], env=env, stdout=log, stderr=subprocess.STDOUT)
    for _ in range(200):
        if sock.exists() or p.poll() is not None:
            break
        time.sleep(0.05)
    if not sock.exists():
        check(False, f'{name}: the control socket is up')
        print((tmp / f'{name}.log').read_text()[-2000:])
        p.kill()
        return None
    time.sleep(0.5)
    tap(sock, 'a')
    until(lambda: ask(sock, 'state').get('screen'), lambda s: s == 'games', 10.0)
    settle(sock)
    return sock, p, log


def stop(sock, p, log):
    try:
        ask(sock, 'quit')
        p.wait(timeout=10)
    except Exception:
        pass
    if p.poll() is None:
        p.kill()
        p.wait()
    log.close()
    if sock.exists():
        sock.unlink()


def stops(tmp, view, fwd, back):
    """A held `fwd` walks to the last game and stops there; a press then
    goes round to the first; a held `back` from the first goes round once
    (the press) and walks back to the first, stopping there"""
    got = run(tmp, view)
    if not got:
        return
    sock, p, log = got
    try:
        start = cursor(sock)
        seen, at = hold(sock, fwd, lambda v: v[-1] == N - 1)
        check(at > 0 and seen[0] == start and all(b - a == 1 for a, b in zip(seen, seen[1:])),
              f'{view}: a held {fwd} walks one game at a time to the last ({seen})')
        check(at > 0 and len(seen) == at and seen[-1] == N - 1,
              f'{view}: held on at the last game, {fwd} stops there ({seen})')
        tap(sock, fwd)
        c = until(lambda: cursor(sock), lambda c: c != N - 1, 3.0)
        check(c == 0, f'{view}: a press of {fwd} on the last game goes round to the first ({c})')
        seen, at = hold(sock, back, lambda v: len(v) > 1 and v[-1] == 0)
        check(at > 0 and seen[:2] == [0, N - 1] and all(b - a == -1 for a, b in zip(seen[1:], seen[2:])),
              f'{view}: a held {back} on the first goes round once, then walks back to the first ({seen})')
        check(at > 0 and len(seen) == at and seen[-1] == 0,
              f'{view}: held on at the first game, {back} stops there ({seen})')
    finally:
        stop(sock, p, log)


def pages(sock, what, n, last_is, look=cursor):
    """L1 and R1 on a list of `n` at its first entry: a held R1 pages on
    to the last and stops there; a press there goes round to the first; a
    held L1 on the first goes round once (the press) and pages back to the
    first, stopping there"""
    seen, at = hold(sock, 'r1', lambda v: v[-1] == n - 1)
    check(at > 0 and seen[0] == 0 and all(b > a for a, b in zip(seen, seen[1:])) and len(seen) == at,
          f'{what}: a held R1 pages on to the last and stops there ({seen})')
    tap(sock, 'r1')
    c = until(lambda: look(sock), lambda c: c != n - 1, 3.0)
    check(c == 0, f'{what}: a press of R1 on the last goes round to the first ({c})')
    seen, at = hold(sock, 'l1', lambda v: len(v) > 1 and v[-1] == 0)
    check(at > 0 and seen[:2] == [0, n - 1] and all(b < a for a, b in zip(seen[1:], seen[2:])) and len(seen) == at,
          f'{what}: a held L1 on the first goes round once, then pages back to the first and stops ({seen})')


def shelf_pages(tmp, view):
    got = run(tmp, view, view + '-pages')
    if not got:
        return
    sock, p, log = got
    try:
        pages(sock, view, N, N - 1)
    finally:
        stop(sock, p, log)


def cheevo_pages(tmp):
    """The achievements list, from the shelf: X, its Achievements row, A"""
    got = run(tmp, 'covers', 'covers-cheevos')
    if not got:
        return
    sock, p, log = got
    try:
        tap(sock, 'x')
        until(lambda: ask(sock, 'state').get('focus'), lambda f: bool(f) and f != 'Alpha', 5.0)
        for _ in range(12):
            if ask(sock, 'state').get('focus') == 'Achievements':
                break
            tap(sock, 'down')
            ask(sock, 'wait-frames 3', timeout=30)
        check(ask(sock, 'state').get('focus') == 'Achievements', 'the details page has an Achievements row')
        tap(sock, 'a')
        st = until(lambda: ask(sock, 'state'), lambda s: s.get('focus') == 'Step 1', 5.0)
        check(st.get('focus') == 'Step 1', f'the achievements list opens on its first ({st.get("focus")})')
        settle(sock)
        pages(sock, 'the achievements list', CHEEVOS, CHEEVOS - 1)
    finally:
        stop(sock, p, log)


def home_pages(tmp):
    """The home's Grid on 24 systems, a game each"""
    base = tmp / 'home'
    env = H.card(base, 24)
    sock = Path(tempfile.gettempdir()) / f'zyperos-shelfkeys-{os.getpid()}-home.sock'
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(tmp / 'home-flags'),
               ZYPEROS_CTL_DIR=str(tmp / 'home-ctl'))
    (tmp / 'home-flags').mkdir()
    env.pop('ZYPEROS_HEADLESS', None)
    log = open(tmp / 'home.log', 'w')
    p = subprocess.Popen([str(BINARY), '--device', 'pixel2'], env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(200):
            if sock.exists() or p.poll() is not None:
                break
            time.sleep(0.05)
        check(sock.exists(), 'home: the control socket is up')
        if not sock.exists():
            return
        until(lambda: ask(sock, 'state').get('screen'), lambda s: s == 'systems', 10.0)
        settle(sock)
        hold(sock, 'left', lambda v: v[-1] == 0, on_frames=0, on_ms=0)    # to the first system
        settle(sock)
        pages(sock, "the home's Grid", 24, 23)
    finally:
        stop(sock, p, log)


def goes_round(tmp, view):
    """The cover row: a held Right goes on past the last game to the first"""
    got = run(tmp, view)
    if not got:
        return
    sock, p, log = got
    try:
        seen, at = hold(sock, 'right', lambda v: len(v) > N, on_frames=0, on_ms=0)
        check(at > 0 and N - 1 in seen and seen[seen.index(N - 1) + 1] == 0,
              f'{view}: a held Right goes round past the last game, as the home\'s row does ({seen})')
    finally:
        stop(sock, p, log)


def main():
    if not BINARY.is_file():
        print('check-shelfkeys: build-native/zyperos missing (make native)')
        return 1
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-shelfkeys-'))
    try:
        stops(tmp, 'list', 'down', 'up')
        stops(tmp, 'grid', 'right', 'left')
        goes_round(tmp, 'covers')
        shelf_pages(tmp, 'list')
        shelf_pages(tmp, 'grid')
        home_pages(tmp)
        cheevo_pages(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'shelfkeys: {"all passed" if not H.fails else f"{len(H.fails)} failed"}')
    return 1 if H.fails else 0


if __name__ == '__main__':
    sys.exit(main())
