#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Two layout rules, held on the launcher's own shots (src/games.c, src/main.c).

    make check-layout-shots

1. The play time on a game's page sits where the system sits on Favorites: the
   second line's place under a one-line name (cap 628), one shared constant
   (GAMES_SUBLINE_Y), not 80 down at cap 664.
2. Over a paused game the status corner is the home screen's header: the clock
   at the top left (x 64), the marks and the battery's text at the top right
   (x 960), so the longest Time Left text never meets the clock. Shot in every
   language, for a Diatom game's menu and for the port menu.

The shots go to build-native/layout-shots (LAYOUT_SHOTS_OUT to move them)."""
import os
import shutil
import sqlite3
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get('LAYOUT_SHOTS_OUT', ROOT / 'build-native/layout-shots'))
BIN = ROOT / 'build-native/zyperos'
STATUS = '10:08 24h wifi=3 bt batt=82'
LONG = '10:08 24h wifi=3 bt batt=100 chg xfer=on eta=595 icon=time'
GAME = 'Chrono Trigger (USA).sfc'
GBA = 'Metroid Fusion (USA).gba'
UNDER = (40, 612, 780, 650)   # ink of cap 628
BELOW = (40, 652, 1024, 692)  # ink of cap 664


SOFT = os.environ.get('LAYOUT_SHOTS_SOFT')   # take the shots, report instead of failing
FAILED = []


def need(cond, why=''):
    if cond:
        return
    if not SOFT:
        raise AssertionError(why)
    FAILED.append(str(why))


def fixture(name):
    base = OUT / name
    if base.exists():
        shutil.rmtree(base)
    for d in ('root', 'Roms/SNES', 'Roms/Game Boy Advance', 'userdata', 'shared'):
        (base / d).mkdir(parents=True)
    (base / 'Roms/SNES' / GAME).write_bytes(b'x')
    (base / 'Roms/Game Boy Advance' / GBA).write_bytes(b'x')
    rows = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
            if l.startswith('sys|') and l.split('|')[4] in ('SFC', 'GBA')]
    (base / 'root/systems.cfg').write_text('\n'.join(rows) + '\n')
    (base / 'root/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    env = os.environ | {'ZYPEROS_HEADLESS': '1', 'SDL_VIDEODRIVER': 'dummy',
                        'SDL_RENDER_DRIVER': 'software', 'SDL_AUDIODRIVER': 'dummy',
                        'ZYPEROS_ROOT': str(base / 'root'), 'ZYPEROS_CARD': str(base),
                        'ZYPEROS_ROMS': str(base / 'Roms'), 'ZYPEROS_USERDATA': str(base / 'userdata'),
                        'ZYPEROS_SHARED': str(base / 'shared'), 'ZYPEROS_WEB': str(base),
                        'ZYPEROS_FONT': str(ROOT / 'res/fonts/menu.ttf')}
    run(env, '--shot', str(base / 'first.png'), '--screen', 'systems', '--status', STATUS)
    lib = sqlite3.connect(base / 'shared/.zyperos/library.db')
    with lib:
        lib.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)',
                    (f'sess.1760000000.1.SFC\t{GAME}', '8040\tend'))
        lib.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)',
                    (f'fav.GBA\t{GBA}', '1'))
        lib.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)',
                    (f'sess.1760000000.2.GBA\t{GBA}', '3600\tend'))
    lib.close()
    return base, env


def run(env, *args, check=True):
    r = subprocess.run([str(BIN), *args], env=env, cwd=ROOT, capture_output=True, text=True)
    if check:
        assert r.returncode == 0, (args, r.stdout[-500:], r.stderr[-1500:])
    return r


def ink(im, box):
    part = im.crop(box)
    bg = part.getpixel((0, 0))
    cols, rows = set(), set()
    px = part.load()
    for x in range(part.size[0]):
        for y in range(part.size[1]):
            if sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 60:
                cols.add(x + box[0])
                rows.add(y + box[1])
    return cols, rows


def games(base, env, name, shelf, theme):
    run(env, '--set', f'startup_system={shelf}')
    f = base / f'{name}.png'
    run(env, '--shot', str(f), '--screen', 'games', '--phase', '3200', '--status', STATUS, '--theme', theme)
    return Image.open(f).convert('RGB')


OUT.mkdir(parents=True, exist_ok=True)
base, env = fixture('card')
for theme in ('light', 'dark'):
    play = games(base, env, f'playtime-{theme}', 'SNES', theme)
    fav = games(base, env, f'favorites-{theme}', 'Favorites', theme)
    pc, pr = ink(play, UNDER)
    need(pc, f'{theme}: no play time right under the name')
    pc = pc or {0}
    pr = pr or {0}
    need(not ink(play, BELOW)[0], f'{theme}: the play time is still on the low line')
    need(min(pc) >= 64 and max(pc) <= 760, f'{theme}: play time runs {min(pc)}..{max(pc)}')
    fc, fr = ink(fav, UNDER)
    need(fc, f'{theme}: no system line on Favorites')
    fc = fc or {0}
    fr = fr or {0}
    # the same line on both pages: the same rows, to the pixel, at the same x
    need(min(fc) == min(pc) == 64 or abs(min(fc) - min(pc)) <= 2, (min(fc), min(pc)))
    need(abs(min(fr) - min(pr)) <= 3 and abs(max(fr) - max(pr)) <= 3,
         f'{theme}: the play time rows {min(pr)}..{max(pr)}, the system {min(fr)}..{max(fr)}')

LANGS = ('en', 'zh-Hans', 'es', 'pt-BR', 'fr', 'de', 'ja', 'it')
CLOCK = (0, 16, 200, 56)     # top left: the clock only
RIGHT = (200, 16, 1024, 56)  # top right: marks and battery text
for lang in LANGS:
    for kind, extra in (('diatom', ['dual']), ('port', ['port'])):
        for theme in (('light', 'dark') if lang == 'en' else ('light',)):
            f = base / f'ingame-{kind}-{lang}-{theme}.png'
            r = run(env, '--shot', str(f), '--theme', theme, '--lang', lang, '--screen', 'games',
                    '--gamemenu', '0', *extra, '--status', LONG, check=False)
            need(r.returncode == 0 and 'CORNER:' not in r.stderr, (lang, kind, r.stderr[-400:]))
            im = Image.open(f).convert('RGB')
            cc, _ = ink(im, CLOCK)
            rc, _ = ink(im, RIGHT)
            need(cc and min(cc) >= 60 and max(cc) < 200, f'{lang} {kind}: no clock at the top left')
            need(rc and cc and max(rc) <= 1000 and min(rc) > max(cc) + 16,
                 f'{lang} {kind}: right marks start at {min(rc, default=0)}, clock ends at {max(cc, default=0)}')
for why in FAILED:
    print('FAIL (soft):', why)
print('ok: play time shares the system line\'s place; in-game clock top left, battery top right, in 8 languages')
