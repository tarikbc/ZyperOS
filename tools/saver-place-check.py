#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-saver-place: the screensaver's cover never goes under its two slabs,
and its snap plays over the whole screen (DESIGN.md 11.37, src/saver.h's
saver_art_place and saver_fit), read off the real launcher's frames.

2026-11-23: a tall cover ran under "Play / Any other button goes back" at
the top right. A snap keeps the whole screen, the slabs over it, as it did.

Each picture is one flat color the launcher draws nowhere else. A cover
nothing covers is a whole rectangle of it, so every pixel inside the box
round that color's pixels (less a pixel at each edge, where scaling blends
it) is that color; a slab over it leaves its own pixels in the box. A snap's
box is the whole screen's width or height, in its own shape, in the middle,
within a pixel. Tall, wide and square covers (as the screensaver picks one,
from gamelist.xml's image) and 4:3, 16:9 and tall snaps (--saver-pic), on
the Pixel 2, the Brick and the Smart Pro, in English; the tall cover and the
4:3 snap in every language and both themes on each, with a long name in the
game's slab. build-native/zyperos must be built (make native)."""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
ART = (255, 0, 255)
NAME = 'The Legend of Zelda - A Link to the Past'
DEVICES = ('pixel2', 'brick', 'smartpro')
LANGS = ('en', 'zh-Hans', 'es', 'pt-BR', 'fr', 'de', 'ja', 'it')
COVERS = {'tall': (300, 420), 'wide': (420, 236), 'square': (320, 320)}
SNAPS = {'snap43': (640, 480), 'snap169': (640, 360), 'snapds': (256, 384)}
fails = []


def card(tmp, shape, size):
    base = tmp / shape
    for d in ('ZyperOS', 'Roms', 'userdata', 'shared', 'web'):
        (base / d).mkdir(parents=True)
    sfc = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
           if l.startswith('sys|') and l.split('|')[4] == 'SFC']
    (base / 'ZyperOS/systems.cfg').write_text(sfc[0] + '\n')
    (base / 'ZyperOS/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    folder = base / 'Roms' / sfc[0].split('|')[2]
    (folder / 'images').mkdir(parents=True)
    (folder / f'{NAME} (USA).sfc').write_bytes(b'\0' * 16)
    Image.new('RGB', size, ART).save(folder / 'images' / f'{NAME} (USA)-image.png')
    (folder / 'gamelist.xml').write_text(
        '<?xml version="1.0"?>\n<gameList>\n\t<game>\n'
        f'\t\t<path>./{NAME} (USA).sfc</path>\n\t\t<name>{NAME}</name>\n'
        f'\t\t<image>./images/{NAME} (USA)-image.png</image>\n\t</game>\n</gameList>\n')
    return base


def shoot(base, out, args):
    env = dict(os.environ, ZYPEROS_HEADLESS='1', SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
               ZYPEROS_ROOT=str(base / 'ZyperOS'), ZYPEROS_CARD=str(base), ZYPEROS_ROMS=str(base / 'Roms'),
               ZYPEROS_USERDATA=str(base / 'userdata'), ZYPEROS_SHARED=str(base / 'shared'),
               ZYPEROS_WEB=str(base / 'web'), ZYPEROS_FONT=str(ROOT / 'res/fonts/menu.ttf'))
    r = subprocess.run([str(BINARY), '--shot', str(out), *args], env=env, capture_output=True, text=True)
    return r.returncode == 0 and out.is_file()


def art_box(png):
    """The frame, its pixels off the picture's color, and the box round them"""
    im = Image.open(png).convert('RGB')
    off = ImageChops.difference(im, Image.new('RGB', im.size, ART)).convert('L')
    return im, off, off.point(lambda v: 255 if v == 0 else 0).getbbox()


def clear(png):
    """True when the picture's color is a whole rectangle: nothing over it"""
    _, off, box = art_box(png)
    if not box:
        return False, 'no picture drawn'
    x0, y0, x1, y1 = box[0] + 1, box[1] + 1, box[2] - 1, box[3] - 1
    inside = off.crop((x0, y0, x1, y1)).point(lambda v: 255 if v else 0)
    covered = inside.histogram()[255]
    if covered:
        return False, f'{covered} pixels of its box ({x0},{y0})-({x1},{y1}) under something'
    return True, f'({x0},{y0})-({x1},{y1})'


def whole(png, size):
    """True when a `size` snap fills the frame's width or height, in its own
    shape, in the middle: saver_fit on the whole screen"""
    im, _, box = art_box(png)
    if not box:
        return False, 'no picture drawn'
    fw, fh = im.size
    k = min(fw / size[0], fh / size[1])  # never near saver_fit's 8 times here
    w, h = size[0] * k, size[1] * k
    want = ((fw - w) / 2, (fh - h) / 2, (fw + w) / 2, (fh + h) / 2)
    if any(abs(a - b) > 1.5 for a, b in zip(box, want)):
        return False, ('its box ({},{})-({},{}), the whole screen\'s ({:.0f},{:.0f})-({:.0f},{:.0f})'
                       .format(*box, *want))
    return True, '({},{})-({},{})'.format(*box)


def main():
    if not BINARY.is_file():
        print('check-saver-place: build-native/zyperos is missing (make native)')
        return 1
    n = 0
    with tempfile.TemporaryDirectory(prefix='zyperos-saverplace-') as t:
        tmp = Path(t)
        cards = {s: card(tmp, s, z) for s, z in COVERS.items()}
        pics = {}
        for s, z in SNAPS.items():
            pics[s] = tmp / f'{s}.png'
            Image.new('RGB', z, ART).save(pics[s])
        snapcard = cards['square']
        runs = []
        for dev in DEVICES:
            for shape in COVERS:
                runs.append((dev, 'en', 'light', shape))
            for shape in SNAPS:
                runs.append((dev, 'en', 'light', shape))
            for lang in LANGS:
                for theme in ('light', 'dark'):
                    for shape in ('tall', 'snap43'):
                        if (lang, theme) != ('en', 'light'):
                            runs.append((dev, lang, theme, shape))
        for dev, lang, theme, shape in runs:
            out = tmp / f'{dev}-{lang}-{theme}-{shape}.png'
            if shape in COVERS:
                args = ['--saver', 'cover']
                base = cards[shape]
            else:
                args = ['--saver', '--saver-pic', str(pics[shape])]
                base = snapcard
            args += ['--device', dev, '--lang', lang, '--theme', theme]
            what = f'{dev} {lang} {theme} {shape}'
            if not shoot(base, out, args):
                fails.append(what + ': no shot')
                print(f'  FAIL {what}: no shot')
                continue
            ok, why = clear(out) if shape in COVERS else whole(out, SNAPS[shape])
            n += 1
            if not ok:
                fails.append(f'{what}: {why}')
                print(f'  FAIL {what}: {why}')
    if fails:
        print(f'check-saver-place: {len(fails)} of {n} frames with a cover under a slab '
              'or a snap off the whole screen')
        return 1
    print(f'ok: the screensaver\'s covers clear of both slabs and its snaps over the whole screen '
          f'in {n} frames (tall, wide, square covers; 4:3, 16:9, tall snaps; three handhelds, '
          '8 languages, both themes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
