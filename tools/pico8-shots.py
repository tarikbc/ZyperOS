#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Your PICO-8 on the host (src/pico8.h, DESIGN.md 11.55): the real launcher
draws the PICO-8 shelf with Splore first, before and after your PICO-8 is on
the card, a cart with Splore beside it, the shelf's menu with its Core row,
and the page A opens on Splore while PICO-8 is not on the card, in both
themes. And the in-game menu over your PICO-8 (1.1.0-rc.8: a game's tabs),
its Game, Display and Controls tabs beside a Game Boy Advance game's, in
both themes (menu-sheet.png: a row a tab and theme, PICO-8 at the left).
The card is made here: carts that are a line of text each, and in
Bios/PICO-8 two files of made-up bytes standing in for the player's own
(never Lexaloffle's).

    make pico8-shots     build-native/pico8-shots/*.png, sheet.png and menu-sheet.png
    python3 tools/pico8-shots.py --out DIR    the same into DIR"""
import sys
import shutil
import sqlite3
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(sys.argv[sys.argv.index('--out') + 1]).resolve() if '--out' in sys.argv else ROOT / 'build-native/pico8-shots'
BIN = ROOT / 'build-native/zyperos'
STATUS = '10:08 24h wifi=3 bt batt=82'
CARTS = ['Cloud Hopper.p8', 'Ember Keep.p8.png', 'Moss Garden.p8']


def fixture(name, installed):
    base = OUT / name
    if base.exists():
        shutil.rmtree(base)
    for d in ('ZyperOS/pico8', 'Roms/PICO-8', 'userdata', 'shared'):
        (base / d).mkdir(parents=True)
    rows = []
    for line in (ROOT / 'config/systems.cfg').read_text().splitlines():
        f = line.split('|')
        if (line.startswith('sys|') and f[4] in ('PICO8', 'GBA')) or line.startswith('brings|PICO8|'):
            rows.append(line)
    (base / 'ZyperOS/systems.cfg').write_text('\n'.join(rows) + '\n')
    (base / 'ZyperOS/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    # The payload's script and Splore's art, as mk/payload.sh puts them
    shutil.copy(ROOT / 'sd/pixel2/PICO-8.sh', base / 'ZyperOS/pico8/PICO-8.sh')
    for art in ('cover-light.png', 'cover-dark.png', 'icon.png'):
        if (ROOT / 'res/pico8' / art).exists():
            shutil.copy(ROOT / 'res/pico8' / art, base / 'ZyperOS/pico8' / art)
    # fake08, the shelf's other core, as the payload has it (a stand-in file)
    (base / 'ZyperOS/cores').mkdir()
    (base / 'ZyperOS/cores/fake08_libretro.so').write_bytes(b'')
    for cart in CARTS:
        (base / 'Roms/PICO-8' / cart).write_text('pico-8 cartridge // stand-in\n')
    (base / 'Roms/Game Boy Advance').mkdir(parents=True)
    (base / 'Roms/Game Boy Advance/Pocket Quest.gba').write_bytes(b'x')
    if installed:
        (base / 'Bios/PICO-8').mkdir(parents=True)
        (base / 'Bios/PICO-8/pico8_64').write_bytes(b'\x7fELF\x02\x01\x01' + b'\0' * 11 + b'\xb7\0' + b'\0' * 64)
        (base / 'Bios/PICO-8/pico8.dat').write_bytes(b'stand-in data\n')
        (base / 'Bios/PICO-8/version.txt').write_text('0.2.7\n')
    env = {'PATH': '/usr/bin:/bin', 'HOME': str(base), 'ZYPEROS_HEADLESS': '1', 'SDL_VIDEODRIVER': 'dummy',
           'SDL_RENDER_DRIVER': 'software', 'ZYPEROS_ROOT': str(base / 'ZyperOS'),
           'ZYPEROS_CARD': str(base), 'ZYPEROS_ROMS': str(base / 'Roms'),
           'ZYPEROS_USERDATA': str(base / 'userdata'), 'ZYPEROS_SHARED': str(base / 'shared'),
           'ZYPEROS_WEB': str(base), 'ZYPEROS_FONT': str(ROOT / 'res/fonts/menu.ttf')}
    run(env, '--shot', str(base / 'first.png'), '--screen', 'systems', '--status', STATUS)
    run(env, '--set', 'startup_system=PICO-8')
    return base, env


def run(env, *args):
    r = subprocess.run([str(BIN), *args], env=env, cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, (args, r.stderr[-1500:])
    return r.stderr


def shot(base, env, name, theme, game=None, *more, tag='PICO8'):
    last = base / 'ZyperOS/.last'
    if game:
        last.write_text(f'{tag}\n{game}\n')
    elif last.exists():
        last.unlink()
    file = OUT / f'{name}.png'
    log = run(env, '--shot', str(file), '--screen', 'games', '--phase', '3200', '--status', STATUS,
              '--theme', theme, *more)
    return file, log


OUT.mkdir(parents=True, exist_ok=True)
shots = []
base, env = fixture('without', installed=False)
f, log = shot(base, env, 'splore-not-installed-light', 'light', 'Splore.splore')
assert 'core: PICO-8 runs on fake08' in log, log[-800:]
shots.append(f)
shots.append(shot(base, env, 'core-row-without-light', 'light', 'Cloud Hopper.p8', '--menu', 'Core')[0])
shots.append(shot(base, env, 'page-light', 'light', None, '--pico8-page')[0])
shots.append(shot(base, env, 'page-dark', 'dark', None, '--pico8-page')[0])
base, env = fixture('with', installed=True)
f, log = shot(base, env, 'splore-dark', 'dark', 'Splore.splore')
assert 'core: PICO-8 runs on run:ZyperOS/pico8/PICO-8.sh' in log, log[-800:]
shots.append(f)
shots.append(shot(base, env, 'cart-light', 'light', 'Cloud Hopper.p8')[0])
shots.append(shot(base, env, 'core-row-dark', 'dark', 'Cloud Hopper.p8', '--menu', 'Core')[0])


def sheet_of(files, name, cols=2):
    ims = [Image.open(s).convert('RGB') for s in files]
    w, h = ims[0].size
    sheet = Image.new('RGB', (w * cols, h * ((len(ims) + cols - 1) // cols)), 'white')
    for i, im in enumerate(ims):
        sheet.paste(im, ((i % cols) * w, (i // cols) * h))
    sheet.save(OUT / name)
    return OUT / name


print('pico8-shots:', ', '.join(s.name for s in shots), '->', sheet_of(shots, 'sheet.png'))

# The in-game menu over your PICO-8 (--gamemenu ROW port: the menu portkeys
# starts, src/portmenu.h) and over a GBA game, each tab, in both themes, at
# Sharp Pixels with CRT 70% and Blur, so every Display row a game would show
# is there; the cursor on a row each tab has
with sqlite3.connect(base / 'userdata/zyperos.db') as db:
    for tag in ('PICO8', 'GBA'):
        for key, value in (('display', 'integer'), ('shader', 'crt'), ('shader_strength', '70'),
                           ('border', 'blur')):
            db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)', (f'{key}.{tag}', value))
menus = []
for theme in ('light', 'dark'):
    for tab, row, what in (('0', '0', 'game'), ('1', '0', 'display'), ('2', '1', 'controls')):
        menus.append(shot(base, env, f'menu-pico8-{what}-{theme}', theme, 'Cloud Hopper.p8',
                          '--gamemenu', row, 'port', '--gm-tab', tab, '--device', 'pixel2')[0])
        menus.append(shot(base, env, f'menu-gba-{what}-{theme}', theme, 'Pocket Quest.gba',
                          '--gamemenu', row, '--gm-tab', tab, '--device', 'pixel2', tag='GBA')[0])
print('pico8-shots:', ', '.join(s.name for s in menus), '->', sheet_of(menus, 'menu-sheet.png'))
