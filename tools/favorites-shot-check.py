#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
"""A game's own system right under its name on the Favorites shelf (src/games.c).

    make check-favorites-shots

2026-11-07: "In the favorites screen, let's have the system written
below the game name", then "have the system right below the name". The real
launcher draws a card with favorites from three systems, in both themes: under
a one-line name the system takes the second line's place, clear of A's hint;
under a name of two lines it leads the line below; on that system's own shelf,
where the heading already says it, there is none; and a system name too long
for its place is left out rather than drawn past the right margin (64 from the
edge, as the left)."""
import os
import shutil
import sqlite3
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build-native/favorites-shots'
BIN = ROOT / 'build-native/zyperos'
STATUS = '10:08 24h wifi=3 bt batt=82'
# Under a one-line name (ink at rows 566-605): the system, cap 628, ink at
# rows 619-635, left of A's hint. The line below (Played, achievements, or the
# system under a two-line name): cap 664, ink at rows 655-671.
UNDER = (40, 612, 780, 650)
BELOW = (40, 652, 1024, 692)
ORIGIN_RIGHT = 760
RIGHT = 1024 - 64
TMNT = 'Teenage Mutant Ninja Turtles IV - Turtles in Time (USA).sfc'


GAMES = {'SFC': ('SNES', 'Chrono Trigger (USA).sfc'),
         'GBA': ('Game Boy Advance', 'Metroid Fusion (USA).gba'),
         'NGPC': ('Neo Geo Pocket Color', 'Metal Slug - 2nd Mission (World) (En,Ja).ngc')}


def fixture(name, rename=None):
    base = OUT / name
    if base.exists():
        shutil.rmtree(base)
    for d in ('root', 'Roms', 'userdata', 'shared'):
        (base / d).mkdir(parents=True)
    rows = []
    for line in (ROOT / 'config/systems.cfg').read_text().splitlines():
        f = line.split('|')
        if line.startswith('sys|') and f[4] in GAMES:
            if rename and f[4] in rename:
                f[1] = rename[f[4]]
            rows.append('|'.join(f))
            folder, game = GAMES[f[4]]
            (base / 'Roms' / folder).mkdir(parents=True, exist_ok=True)
            (base / 'Roms' / folder / game).write_bytes(b'x')
            if f[4] == 'SFC':
                (base / 'Roms' / folder / TMNT).write_bytes(b'x')
    (base / 'root/systems.cfg').write_text('\n'.join(rows) + '\n')
    (base / 'root/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    env = os.environ | {'ZYPEROS_HEADLESS': '1', 'SDL_VIDEODRIVER': 'dummy',
                        'SDL_RENDER_DRIVER': 'software', 'ZYPEROS_ROOT': str(base / 'root'),
                        'ZYPEROS_CARD': str(base), 'ZYPEROS_ROMS': str(base / 'Roms'),
                        'ZYPEROS_USERDATA': str(base / 'userdata'), 'ZYPEROS_SHARED': str(base / 'shared'),
                        'ZYPEROS_WEB': str(base), 'ZYPEROS_FONT': str(ROOT / 'res/fonts/menu.ttf')}
    run(env, '--shot', str(base / 'first.png'), '--screen', 'systems', '--status', STATUS)
    lib = sqlite3.connect(base / 'shared/.zyperos/library.db')
    with lib:
        for tag, (_, game) in list(GAMES.items()) + [('SFC', (None, TMNT))]:
            lib.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)', (f'fav.{tag}\t{game}', '1'))
    lib.close()
    return base, env


def run(env, *args):
    r = subprocess.run([str(BIN), *args], env=env, cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, (args, r.stderr[-1500:])
    return r.stdout


def shot(base, env, name, shelf, theme, game=None):
    """The shelf's first game, or `game` (its file) by the card's .last"""
    last = base / 'root/.last'
    if game:
        last.write_text(f'FAV\n{game}\n')
    elif last.exists():
        last.unlink()
    run(env, '--set', f'startup_system={shelf}')
    file = base / f'{name}.png'
    run(env, '--shot', str(file), '--screen', 'games', '--phase', '3200', '--status', STATUS, '--theme', theme)
    return Image.open(file).convert('RGB')


def ink(im, box):
    """The columns in `box` that differ from the line's own background"""
    part = im.crop(box)
    bg = part.getpixel((0, 0))
    cols = set()
    w, h = part.size
    px = part.load()
    for x in range(w):
        for y in range(h):
            if sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 60:
                cols.add(x + box[0])
                break
    return cols


OUT.mkdir(parents=True, exist_ok=True)
base, env = fixture('three')
for theme in ('light', 'dark'):
    one = shot(base, env, f'favorites-{theme}', 'Favorites', theme)          # Chrono Trigger
    under, below = ink(one, UNDER), ink(one, BELOW)
    assert under, f'{theme}: no system right under a one-line name'
    assert min(under) >= 64 and max(under) <= ORIGIN_RIGHT, f'{theme}: the system runs {min(under)}..{max(under)}'
    assert not below, f'{theme}: the system went to the line below a one-line name too ({min(below)}..)'
    two = ink(shot(base, env, f'favorites-two-{theme}', 'Favorites', theme, TMNT), BELOW)
    assert two, f'{theme}: no system under a two-line name'
    assert min(two) >= 64 and max(two) <= RIGHT, f'{theme}: the line below runs {min(two)}..{max(two)}'
    own = shot(base, env, f'snes-{theme}', 'SNES', theme)
    for box in (UNDER, BELOW):
        got = ink(own, box)
        assert not got, f'{theme}: the SNES shelf repeats its system under the name ({min(got)}..)'
# A system name too long for its place beside A: on the line below, and never
# past the right margin there (left out, here, as it is longer than that too)
base, env = fixture('long', rename={'SFC': 'Super Nintendo Entertainment System of a Length No Line Holds'})
long = shot(base, env, 'favorites-long', 'Favorites', 'light')
assert not ink(long, UNDER), 'a system name too long for its place beside A was drawn there'
got = ink(long, BELOW)
assert not got or max(got) <= RIGHT, f'a long system name runs to {max(got)}, past {RIGHT}'
print('ok: Favorites names each game\'s system right under it, its own shelf does not, and every line keeps its margin')
