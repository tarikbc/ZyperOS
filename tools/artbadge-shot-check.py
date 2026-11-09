#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The art badge on the real launcher's shelves (src/artbadge.h, DESIGN.md 11.1).

    make check-artbadge-shots

2026-11-09: a heart on a favorite game's card, and on MP4's All a badge on
each card saying what it is: an album, a book or a video. The real launcher
draws, in both themes:

  - a console's shelf on a favorite: the heart at the focused cover's top
    left, none on the cover beside it, which is not a favorite;
  - the same shelf on that neighbor: no heart on it, and the favorite's,
    smaller, on the cover now to its left;
  - Favorites, where every cover has one;
  - MP4's shelf on All, an album, a book and a video in sight: each card's
    badge at its art's top left, in **accent-ink** (the album's, the book's
    and the video's glyphs told apart by where their ink is);
  - MP4's shelf on Music alone: no badge.

Writes the shots to build-native/artbadge-shots/."""
import os
import shutil
import sqlite3
import subprocess
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build-native/artbadge-shots'
BIN = ROOT / 'build-native/zyperos'
STATUS = '10:08 24h wifi=3 batt=82'

# The tokens, from the one place they are typed (tools/designdef.py)
import sys
sys.path.insert(0, str(ROOT / 'tools'))
import designdef  # noqa: E402

def role(name, theme):
    for r, light, dark, *_ in designdef.COLORS:
        if r == name:
            v = light if theme == 'light' else dark
            return tuple(int(v.lstrip('#')[i:i + 2], 16) for i in (0, 2, 4))
    raise KeyError(name)

# SNES's games, in the order the shelf sorts them; the second is the favorite
SNES = ['Alpha Quest (USA).sfc', 'Beta Racer (USA).sfc', 'Gamma Force (USA).sfc']
FAV = SNES[1]

# A cover with no art is the generated card, 512 x 656, fitted into the
# 384 x 328 box (src/games.c): 256 x 328 when focused, at (384, 212); its badge
# AB_D 40 across, AB_INSET 12 in, so its center is at (416, 244). A side card
# is 0.72 of that, centered 390 either side.
def cover_badge(offset):
    s = .72 + .28 * max(0, 1 - abs(offset))
    w, h = 256 * s, 328 * s
    x, y = 512 + offset * 390 - w / 2, 376 - h / 2
    c = (12 + 20) * s
    return x + c, y + c, 20 * s


def run(env, *args):
    r = subprocess.run([str(BIN), *args], env=env, cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, (args, r.stderr[-1500:])
    return r.stdout


def card(name):
    base = OUT / name
    if base.exists():
        shutil.rmtree(base)
    for d in ('root', 'Roms', 'userdata', 'shared'):
        (base / d).mkdir(parents=True)
    rows = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
            if l.startswith('sys|') and l.split('|')[4] == 'SFC']
    (base / 'root/systems.cfg').write_text('\n'.join(rows) + '\n')
    (base / 'root/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    folder = rows[0].split('|')[2]
    (base / 'Roms' / folder).mkdir(parents=True)
    for g in SNES:
        (base / 'Roms' / folder / g).write_bytes(b'x')
    # MP4: an album, a book and a film, which All sorts by name: the album
    # (Brass Lanterns) first, then the film (Cloud Harbor) and the book
    # (Distant Shores)
    album = base / 'Music/The Stand-ins/Brass Lanterns'
    album.mkdir(parents=True)
    (album / '01 Opening.mp3').write_bytes(b'')
    book = base / 'Audiobooks/Distant Shores'
    book.mkdir(parents=True)
    (book / '01 Chapter One.mp3').write_bytes(b'')
    (base / 'Videos').mkdir()
    (base / 'Videos/Cloud Harbor.mp4').write_bytes(b'')
    env = os.environ | {'ZYPEROS_HEADLESS': '1', 'SDL_VIDEODRIVER': 'dummy',
                        'SDL_RENDER_DRIVER': 'software', 'ZYPEROS_ROOT': str(base / 'root'),
                        'ZYPEROS_CARD': str(base), 'ZYPEROS_ROMS': str(base / 'Roms'),
                        'ZYPEROS_USERDATA': str(base / 'userdata'), 'ZYPEROS_SHARED': str(base / 'shared'),
                        'ZYPEROS_WEB': str(base), 'ZYPEROS_FONT': str(ROOT / 'res/fonts/menu.ttf')}
    run(env, '--shot', str(base / 'first.png'), '--screen', 'systems', '--status', STATUS)
    lib = sqlite3.connect(base / 'shared/.zyperos/library.db')
    with lib:
        lib.execute('INSERT OR REPLACE INTO settings(key, value) VALUES (?, ?)', (f'fav.SFC\t{FAV}', '1'))
    lib.close()
    return base, env


def shot(base, env, name, shelf, theme, game='', show=None):
    (base / 'root/.last').write_text(f'{shelf}\n{game}\n')
    if show:
        run(env, '--set', f'muse.show={show}')
    file = OUT / f'{name}-{theme}.png'
    run(env, '--shot', str(file), '--screen', 'games', '--phase', '3200', '--status', STATUS,
        '--theme', theme)
    return Image.open(file).convert('RGB')


def near(px, col, tol=40):
    return sum(abs(a - b) for a, b in zip(px, col)) <= tol


def count(im, box, col, tol=40):
    x0, y0, x1, y1 = (int(round(v)) for v in box)
    p = im.load()
    return sum(1 for y in range(y0, y1) for x in range(x0, x1) if near(p[x, y], col, tol))


def disc_box(cx, cy, r):
    return (cx - r, cy - r, cx + r, cy + r)


def heart_at(im, theme, cx, cy, r):
    """Pixels of **favorite** inside the disc, and of **surface** round it"""
    box = disc_box(cx, cy, r)
    return count(im, box, role('favorite', theme)), count(im, box, role('surface', theme), 12)


def pink(im, box):
    """Pixels pink at any opacity: a side card's heart is faded with its
    cover, so not **favorite** itself but red over green and over blue, which
    neither theme's greys nor SNES's purple are"""
    x0, y0, x1, y1 = (int(round(v)) for v in box)
    p = im.load()
    return sum(1 for y in range(y0, y1) for x in range(x0, x1)
               if p[x, y][0] - p[x, y][1] > 20 and p[x, y][0] - p[x, y][2] > 8)


# MP4's focused card: an album's generated card fills the square frame, 461
# across (CF_LAYOUT_ALBUMS: 0.60 of 768, centered at 0.54 down), so its art's
# top left is at (281, 184) and its badge's center 32 in from that
MP4_BADGE = (281.5 + 32, 414.7 - 230.4 + 32, 20)


def mp4_kind(im, ink):
    """What the focused MP4 card's badge shows, from where its ink is: None
    for no ink at all"""
    cx, cy, r = MP4_BADGE
    x0, y0 = int(cx - r), int(cy - r)
    p = im.load()
    pts = [(x, y) for y in range(y0, y0 + 2 * r) for x in range(x0, x0 + 2 * r) if near(p[x, y], ink, 60)]
    if len(pts) < 20:
        return None
    xs = [x for x, _ in pts]
    ys = [y for _, y in pts]
    w, h = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    fill = len(pts) / (w * h)
    # The column down the middle: the book's spine is empty, the screen's is
    # full but for the play mark's cut, the notes' is empty but for the beam
    mid = min(xs) + w // 2
    spine = sum(1 for x, _ in pts if x == mid)
    if w > h and spine <= 2:
        return 'book'
    if w > h * 1.15 and fill > 0.6:
        return 'video'
    if fill < 0.55:
        return 'album'
    return f'? {w}x{h} fill {fill:.2f} spine {spine}'


OUT.mkdir(parents=True, exist_ok=True)
base, env = card('card')
for theme in ('light', 'dark'):
    fav = role('favorite', theme)
    # On the favorite: a heart on it, on a disc, and none on either neighbor
    im0 = im = shot(base, env, 'shelf-favorite', 'SFC', theme, FAV)
    hearts, disc = heart_at(im, theme, *cover_badge(0))
    assert hearts > 150 and disc > 200, f'{theme}: no heart on the focused favorite ({hearts} pink, {disc} disc)'
    for off in (-1, 1):
        cx, cy, r = cover_badge(off)
        got = pink(im, disc_box(cx, cy, r * 1.5))
        assert got < 5, f'{theme}: a heart on the cover at {off}, which is no favorite ({got})'
    # Nothing else on the screen in **favorite** but the heart
    whole = count(im, (0, 0, 1024, 768), fav)
    assert whole == hearts, f'{theme}: favorite pink outside the heart ({whole} against {hearts})'
    # On the neighbor after it: none on the focused cover, the favorite's on
    # the side card to its left, smaller
    im = shot(base, env, 'shelf-plain', 'SFC', theme, SNES[2])
    cx, cy, r = cover_badge(0)
    got = pink(im, disc_box(cx, cy, r * 1.5))
    assert got < 5, f'{theme}: a heart on a focused cover that is no favorite ({got})'
    side = pink(im, disc_box(*cover_badge(-1)))
    focused = pink(im0, disc_box(*cover_badge(0)))
    assert 60 < side < focused, f'{theme}: the side favorite\'s heart is {side} pixels, against {focused} focused'
    # Favorites: the one game, with its heart
    im = shot(base, env, 'favorites', 'FAV', theme, FAV)
    got, _ = heart_at(im, theme, *cover_badge(0))
    assert got > 150, f'{theme}: no heart on Favorites ({got})'

    # MP4 on All: each card's badge, at its art's top left, focused in turn
    # (a side card's is faded with it): the album (Brass Lanterns), the film
    # (Cloud Harbor, a card of its own in Videos/) and the book (Distant
    # Shores), told apart by the shape of their **accent-ink**.
    ink = role('accent-ink', theme)
    for name, folder, want in (('album', 'Music/The Stand-ins/Brass Lanterns', 'album'),
                               ('video', '', 'video'),
                               ('book', 'Audiobooks/Distant Shores', 'book')):
        im = shot(base, env, f'mp4-all-{name}', 'MUSE', theme, str(folder), show='all')
        got = mp4_kind(im, ink)
        assert got == want, f'{theme}: the {name} on All reads as {got}'
    # On Music alone the shelf says the kind, and no card carries one
    im = shot(base, env, 'mp4-music', 'MUSE', theme, show='music')
    got = mp4_kind(im, ink)
    assert got is None, f'{theme}: a badge on Music alone ({got})'
run(env, '--set', 'muse.show=all')
print('ok: a heart on a favorite\'s cover and on no other, smaller on a side card; '
      'on MP4\'s All an album, a book and a video each badged, and none on Music alone')
