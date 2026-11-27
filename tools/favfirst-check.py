#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A shelf's favorites ahead of its other games, MENU > Favorites First (DESIGN.md 11.70).

    make check-favfirst

Asked for on the Discord (2026-11-22): "it would be a nice option to be able
to get the favorites on top of their respective games lists." The real host
launcher, on a card of one NES shelf with three favorites, walked with the
shelf's own keys (--shelf-press, which logs the game each press lands on):

- Off, as it ships: the shelf is its Sort By order, favorites among the rest.
- On: the favorites first, in Sort By's order, then the others in it, by Name
  and by Recently Added, in Covers and in List.
- The letter jump stops where the favorites end even when the last favorite
  and the first of the others share an initial (Zelda II, then Zanac).
- Favorites, the home's group, is as it was: its games A to Z.
"""
import os
import shutil
import sqlite3
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build-native/favfirst-check'
BIN = ROOT / 'build-native/zyperos'
GAMES = ['Bomberman (USA).nes', 'Rygar (USA).nes', 'Zanac (USA).nes', 'Zelda II - The Adventure of Link (USA).nes',
         'Zombie Nation (USA).nes']
FAVS = ['Bomberman (USA).nes', 'Rygar (USA).nes', 'Zelda II - The Adventure of Link (USA).nes']
# Recently Added: each file's time, newest last here
AGE = {'Zanac (USA).nes': 5, 'Rygar (USA).nes': 4, 'Zombie Nation (USA).nes': 3,
       'Zelda II - The Adventure of Link (USA).nes': 2, 'Bomberman (USA).nes': 1}
fails = 0


def ck(cond, what):
    global fails
    if not cond:
        print('  FAIL', what)
        fails += 1


def fixture():
    if OUT.exists():
        shutil.rmtree(OUT)
    for d in ('root', 'Roms/NES', 'userdata', 'shared'):
        (OUT / d).mkdir(parents=True)
    rows = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines() if l.startswith('sys|NES|')]
    (OUT / 'root/systems.cfg').write_text('\n'.join(rows) + '\n')
    (OUT / 'root/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    now = time.time()
    for g in GAMES:
        p = OUT / 'Roms/NES' / g
        p.write_bytes(b'x')
        os.utime(p, (now - AGE[g] * 86400, now - AGE[g] * 86400))
    env = os.environ | {'ZYPEROS_HEADLESS': '1', 'SDL_VIDEODRIVER': 'dummy',
                        'SDL_RENDER_DRIVER': 'software', 'ZYPEROS_ROOT': str(OUT / 'root'),
                        'ZYPEROS_CARD': str(OUT), 'ZYPEROS_ROMS': str(OUT / 'Roms'),
                        'ZYPEROS_USERDATA': str(OUT / 'userdata'), 'ZYPEROS_SHARED': str(OUT / 'shared'),
                        'ZYPEROS_WEB': str(OUT), 'ZYPEROS_FONT': str(ROOT / 'res/fonts/menu.ttf')}
    run(env, '--shot', str(OUT / 'first.png'), '--screen', 'systems')
    with sqlite3.connect(OUT / 'shared/.zyperos/library.db') as lib:
        for g in FAVS:
            lib.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)', (f'fav.NES\t{g}', '1'))
    return env


def run(env, *args):
    r = subprocess.run([str(BIN), *args], env=env, cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, (args, r.stderr[-1500:])
    return r.stderr


def setting(key, value, db='userdata/zyperos.db'):
    with sqlite3.connect(OUT / db) as dev:
        if value is None:
            dev.execute('DELETE FROM settings WHERE key = ?', (key,))
        else:
            dev.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)', (key, value))


def walk(env, shelf, keys, view='covers'):
    """The game each press lands on, from the shelf's first"""
    last = OUT / 'root/.last'
    if last.exists():
        last.unlink()
    setting('startup_system', shelf, 'shared/.zyperos/library.db')
    err = run(env, '--shot', str(OUT / 'walk.png'), '--screen', 'games', '--view', view,
              '--shelf-press', ','.join(keys))
    got = []
    for line in err.splitlines():
        if line.startswith('shelf-press: '):
            got.append(line.split('[', 1)[1].split(']', 1)[0].removesuffix(' (USA).nes'))
    assert len(got) == len(keys), err[-1500:]
    # Which shelf it walked: its count, after the slash
    count = int(err.split('shelf-press: ')[-1].split('\n', 1)[0].rsplit('/', 1)[1])
    assert count == (len(FAVS) if shelf == 'Favorites' else len(GAMES)), (shelf, count)
    return got


if not BIN.is_file():
    raise SystemExit('run make native first')
env = fixture()

print('Off, as it ships:')
step = walk(env, 'NES', ['right'] * 4)
ck(step == ['Rygar', 'Zanac', 'Zelda II - The Adventure of Link', 'Zombie Nation'], f'by name, favorites among the rest: {step}')

print('On, by Name:')
setting('favfirst.NES', 'on')
step = walk(env, 'NES', ['right'] * 5)
ck(step == ['Rygar', 'Zelda II - The Adventure of Link', 'Zanac', 'Zombie Nation', 'Bomberman'],
   f'the favorites A to Z, then the others A to Z, round to the first: {step}')
step = walk(env, 'NES', ['down'] * 4)
ck(step == ['Rygar', 'Zelda II - The Adventure of Link', 'Zanac', 'Bomberman'],
   f'Down stops at the first of the others, though Zelda II and Zanac share a Z: {step}')
step = walk(env, 'NES', ['down', 'down', 'down', 'up', 'up'])
ck(step == ['Rygar', 'Zelda II - The Adventure of Link', 'Zanac', 'Zelda II - The Adventure of Link', 'Rygar'],
   f'and Up is its inverse: {step}')
lst = walk(env, 'NES', ['down'] * 4, 'list')
ck(lst == ['Rygar', 'Zelda II - The Adventure of Link', 'Zanac', 'Zombie Nation'], f'the List view steps the same order: {lst}')
lst = walk(env, 'NES', ['right'] * 3, 'list')
ck(lst == ['Rygar', 'Zelda II - The Adventure of Link', 'Zanac'], f'and its letter jump stops the same: {lst}')

print('On, by Recently Added:')
setting('sort.NES', 'added')
step = walk(env, 'NES', ['right'] * 4)
ck(step == ['Zelda II - The Adventure of Link', 'Rygar', 'Zombie Nation', 'Zanac'],
   f'the favorites newest first, then the others newest first: {step}')

print('Off again:')
setting('favfirst.NES', 'off')
step = walk(env, 'NES', ['right'] * 4)
ck(step == ['Zelda II - The Adventure of Link', 'Zombie Nation', 'Rygar', 'Zanac'], f'Recently Added as it was: {step}')

print('Favorites, the home\'s group:')
setting('favfirst.NES', 'on')
setting('sort.NES', None)
step = walk(env, 'Favorites', ['right'] * 2)
ck(step == ['Rygar', 'Zelda II - The Adventure of Link'], f'its games A to Z, as they were: {step}')

if fails:
    raise SystemExit(f'\n{fails} Favorites First check(s) failed')
print('\nok: Favorites First puts a shelf\'s favorites ahead of its other games, each part in its order')
