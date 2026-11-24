#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MENU > Display > Game Names on the real launcher (DESIGN.md 11.71).

    make check-gamenames-shots

2026-11-23, DoctorIssa on the Discord: "is there any chance there is a way
we can have a setting to show them": the brackets of ~900 files, a version,
a region, a disc. A SNES shelf in the List view, as the Pixel 2 and as the
Brick, in both themes: under Short (as it ships) a row is the name less its
brackets; under File Name the same row runs on with them, and a downloaded
name gives way to the file's. MENU's row turns it over at once, kept, and
names every shelf again (its log line)."""
import os
import shutil
import sqlite3
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build-native/gamenames-shots'
BIN = ROOT / 'build-native/zyperos'
STATUS = '10:08 24h wifi=3 bt batt=82'
GAMES = ['Chrono Trigger (USA).sfc', 'Donkey Kong Country (USA) (Rev 2).sfc', 'EarthBound (USA).sfc',
         'F-Zero (Japan).sfc', 'Mario Bros (v1.1).sfc', 'Super Metroid (Japan, USA) (En,Ja).sfc']
LAST = 'F-Zero (Japan).sfc'
# The List view's rows on the 1024 x 768 canvas: the name from x 121, a
# row 56 high from y 200 (the first, Chrono Trigger, the same game in both
# orders), the fifth Mario Bros
ROW1 = (110, 205, 640, 252)
ROW5 = (110, 429, 640, 476)


def card():
    base = OUT / 'card'
    if base.exists():
        shutil.rmtree(base)
    for d in ('root', 'Roms/SNES', 'userdata', 'shared'):
        (base / d).mkdir(parents=True)
    rows = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
            if l.startswith('sys|') and l.split('|')[4] == 'SFC']
    (base / 'root/systems.cfg').write_text('\n'.join(rows) + '\n')
    (base / 'root/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    for g in GAMES:
        (base / 'Roms/SNES' / g).write_bytes(b'x')
    env = os.environ | {'ZYPEROS_HEADLESS': '1', 'SDL_VIDEODRIVER': 'dummy',
                        'SDL_RENDER_DRIVER': 'software', 'ZYPEROS_ROOT': str(base / 'root'),
                        'ZYPEROS_CARD': str(base), 'ZYPEROS_ROMS': str(base / 'Roms'),
                        'ZYPEROS_USERDATA': str(base / 'userdata'), 'ZYPEROS_SHARED': str(base / 'shared'),
                        'ZYPEROS_WEB': str(base), 'ZYPEROS_FONT': str(ROOT / 'res/fonts/menu.ttf')}
    run(env, '--shot', str(base / 'first.png'), '--screen', 'systems', '--status', STATUS)
    # A downloaded name, as MENU > Downloads' Names keeps it
    lib = sqlite3.connect(base / 'shared/.zyperos/library.db')
    with lib:
        lib.execute("INSERT OR REPLACE INTO games(folder, file, title) VALUES ('SNES', ?, ?)",
                    ('Mario Bros (v1.1).sfc', 'Mario Bros. Arcade Classic'))
    lib.close()
    return base, env


def run(env, *args):
    r = subprocess.run([str(BIN), *args], env=env, cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, (args, r.stderr[-1500:])
    return r


def names(base, value=None):
    d = sqlite3.connect(base / 'userdata/zyperos.db')
    with d:
        if value is not None:
            d.execute("INSERT OR REPLACE INTO settings(key, value) VALUES ('names', ?)", (value,))
        got = d.execute("SELECT value FROM settings WHERE key = 'names'").fetchone()
    d.close()
    return got[0] if got else None


def shot(base, env, name, device, theme):
    (base / 'root/.last').write_text(f'SFC\n{LAST}\n')
    file = OUT / f'{name}.png'
    run(env, '--shot', str(file), '--screen', 'games', '--view', 'list', '--phase', '3200',
        '--status', STATUS, '--theme', theme, '--device', device)
    return Image.open(file).convert('RGB')


def right_ink(im, box):
    """The rightmost column in `box` that differs from the row's background"""
    part = im.crop(box)
    px = part.load()
    w, h = part.size
    bg = px[w - 1, 0]
    for x in range(w - 1, -1, -1):
        for y in range(h):
            if sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 90:
                return x + box[0]
    return box[0]


OUT.mkdir(parents=True, exist_ok=True)
base, env = card()
for device in ('pixel2', 'brick'):
    for theme in ('light', 'dark'):
        names(base, 'short')
        short = shot(base, env, f'{device}-{theme}-short', device, theme)
        names(base, 'file')
        full = shot(base, env, f'{device}-{theme}-file', device, theme)
        s1, f1 = right_ink(short, ROW1), right_ink(full, ROW1)
        # "Chrono Trigger" against "Chrono Trigger (USA)": the bracket's width
        assert f1 >= s1 + 70, f'{device} {theme}: File Name\'s first row ends at {f1}, Short\'s at {s1}'
        s5, f5 = right_ink(short, ROW5), right_ink(full, ROW5)
        # "Mario Bros. Arcade Classic", the downloaded name, against the file's
        # "Mario Bros (v1.1)", which is shorter: the download gave way
        assert f5 < s5 - 20, f'{device} {theme}: the fifth row ends at {f5} under File Name, {s5} under Short'
        assert short.crop(ROW1) != full.crop(ROW1)
# MENU's row, Display's sixth: Right turns it over, kept, and names every
# shelf again; again, back to Short
names(base, 'short')
r = run(env, '--shot', str(OUT / 'menu-press.png'), '--screen', 'systems', '--menu', '5', '--menu-tab', '1',
        '--press', 'right', '--status', STATUS)
assert 'names: File Name, every shelf named again' in r.stderr, r.stderr[-800:]
assert names(base) == 'file', names(base)
r = run(env, '--shot', str(OUT / 'menu-back.png'), '--screen', 'systems', '--menu', '5', '--menu-tab', '1',
        '--press', 'left', '--status', STATUS)
assert 'names: Short, every shelf named again' in r.stderr, r.stderr[-800:]
assert names(base) == 'short', names(base)
print('ok: Game Names: Short cuts the brackets, File Name shows them over a downloaded name, '
      'and MENU turns it over at once')
