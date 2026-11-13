#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The song's name in the top bar slides when it does not fit, and Now Playing's
foot is a row of button hints (src/home.c's home_header, src/main.c's np_hints).

    make check-np-footer

The real launcher draws, in both themes where the README does: a long song
name at two moments of its travel (the name's box changes, nothing else in the
bar does), and a name that fits at two moments (the same picture: it stands
still). Now Playing, and a video's two views, each draw their hints as the
shelves' hints are, a glyph and a word, in a row between the margins 64 and
960, and nothing in the foot is left over from the old line of keys."""
import subprocess
import sys
from pathlib import Path
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build-native/np-footer-shots'
NAMES = ['topbar-still', 'topbar', 'topbar-long', 'topbar-long-moving', 'nowplaying',
         'video-controls', 'video-full']
SONG = (272, 40, 800, 90)   # the bar's row between the clock and the marks
BAR = (0, 40, 1024, 90)


def ink_cols(im, box, tol=60):
    part = im.crop(box)
    bg = part.getpixel((0, 0))
    px = part.load()
    w, h = part.size
    return [x + box[0] for x in range(w)
            if any(sum(abs(a - b) for a, b in zip(px[x, y], bg)) > tol for y in range(h))]


def main():
    r = subprocess.run([sys.executable, str(ROOT / 'tools/readme-shots.py'), '--no-page', '--out', str(OUT), *NAMES],
                       cwd=ROOT, capture_output=True, text=True)
    assert r.returncode == 0, r.stderr[-1500:] + r.stdout[-1500:]
    def img(theme, name):
        return Image.open(OUT / theme / f'{name}.png').convert('RGB')

    # The bar's row only: the focused system's own motion moves below it
    # between the two moments (src/home.c)
    a, b = img('light', 'topbar').crop(BAR), img('light', 'topbar-still').crop(BAR)
    assert ImageChops.difference(a, b).getbbox() is None, 'a name that fits moved'
    a, b = img('light', 'topbar-long'), img('light', 'topbar-long-moving')
    diff = ImageChops.difference(a.crop(BAR), b.crop(BAR)).getbbox()
    assert diff, 'a long name did not move between two moments'
    assert diff[0] + BAR[0] >= SONG[0] and diff[2] + BAR[0] <= SONG[2], f'more than the name moved: {diff}'
    cols = ink_cols(a, SONG)
    assert cols and max(cols) - min(cols) <= 256 + 24 + 12 + 4, 'the long name was not held to its room'

    for theme in ('light', 'dark'):
        for name, rows in (('nowplaying', (702, 740)), ('video-controls', (710, 748)),
                           ('video-full', (710, 748))):
            f = OUT / theme / f'{name}.png'
            if not f.is_file():
                continue
            im = Image.open(f).convert('RGB')
            cols = ink_cols(im, (0, rows[0], 1024, rows[1]), tol=40)
            assert cols, f'{theme}/{name}: no hints at the foot'
            assert min(cols) >= 60 and max(cols) <= 964, f'{theme}/{name}: hints run {min(cols)}..{max(cols)}'
            assert min(cols) <= 80 and max(cols) >= 940, f'{theme}/{name}: the row does not span the margins'
    print('ok: a long song name slides in the top bar, a short one stands; Now Playing and a video show their hints at the foot')


main()
