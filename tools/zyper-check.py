#!/usr/bin/env python3
"""Zyper beside the top menu's heading, and his shadow on Beard, as the
launcher draws them.

    make check-zyper

tools/zyperrig-check.c holds his moves as numbers, and tools/checkmark.py
holds his triangles to their definition; neither sees what src/main.c does
with them, which is where a menu that should not have him gets him, a shadow
falls on Salt & Pepper, a shadow outlives the light going out, or a failure
is answered with a hop. So this shoots the host build headless and reads the
pixels back:

  - the top ZyperOS menu (--menu on the systems row) has him, in both
    themes, and a system's menu (--menu on a games shelf) does not: his
    tongue and his ears in the heading band, or none of either;
  - a failure (--zyper fails --shot-at 1200, 700 ms after it) has his
    tongue in and his ears where they are, in both themes;
  - a blink (--zyper --shot-at 2681, the first blink of the shot's seed, 50
    ms in) has
    his eyes shut: fewer of his ink pixels than at 5081, the same moment of
    the breath with no blink;
  - on Beard he casts a shadow under his chin in the menu, and on Salt &
    Pepper the panel there is its plain surface;
  - on the power-off screen at 560 ms Beard's ground under his chin is
    darker and Salt & Pepper's is its ground; at 800 ms, the light nearly
    out, Beard's shadow is a quarter of that or less, and at 900, the last
    frame, it is gone.

His colors are zyperdef's, drawn by SDL_RenderGeometry with no blending
over anything, so they are exact in a shot from SDL's software renderer. The
card is a scratch tree under build-native/zyper-check: systems.cfg as
shipped and a Game Boy shelf of one empty file. It needs the launcher built,
which needs SDL2, and skips cleanly without it, as check-flip does. The PNGs
are read with the standard library alone (zlib and the five filters), so
nothing is to be installed.

Exits non-zero and says what failed.
"""
import os
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = os.path.join(ROOT, "build-native", "zyper-check")
BIN = os.path.join(ROOT, "build-native", "zyperos")

TONGUE = (214, 118, 146)    # tools/zyperdef.py's TONGUE
EAR = (222, 146, 158)       # its PINK, the ears' insides
INK = (17, 19, 16)          # its INK: eyes, nose, mouth

# The menu panel's top, MENU_TOP in src/main.c: 96, under the home's header
# row (the clock, the song and the status marks); it was 64 before that
# row, and every menu point below moves down with it.
MENU_DOWN = 96 - 64
# The heading band's right end, inside the panel's frame: where he stands,
# and as high as his hop takes him
BAND = (800, 69 + MENU_DOWN, 952, 173 + MENU_DOWN)
# Under his chin, in the menu and on the power-off screen
MENU_CHIN = [(875, 160 + MENU_DOWN), (875, 162 + MENU_DOWN)]
# The panel's plain surface, level with his chin and clear of the heading
MENU_SURFACE = (700, 130 + MENU_DOWN)
OFF_CHIN = [(512, 500), (512, 506), (512, 512)]

fails = 0


def ck(cond, what):
    global fails
    if not cond:
        print("  FAIL", what)
        fails += 1


def load(path, rows):
    """The first `rows` rows of an 8-bit RGB or RGBA PNG, as bytearrays."""
    d = open(path, "rb").read()
    p, idat = 8, b""
    while p < len(d):
        n, = struct.unpack(">I", d[p:p + 4])
        typ, body = d[p + 4:p + 8], d[p + 8:p + 8 + n]
        p += 12 + n
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, lace = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT":
            idat += body
    if depth != 8 or lace or ctype not in (2, 6):
        raise ValueError("%s: not an 8-bit RGB(A) PNG" % path)
    bpp = 4 if ctype == 6 else 3
    raw, st = zlib.decompress(idat), w * bpp
    prev, out = bytearray(st), []
    for y in range(min(h, rows)):
        f = raw[y * (st + 1)]
        cur = bytearray(raw[y * (st + 1) + 1:(y + 1) * (st + 1)])
        if f == 1:
            for i in range(bpp, st):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif f == 2:
            for i in range(st):
                cur[i] = (cur[i] + prev[i]) & 255
        elif f == 3:
            for i in range(st):
                cur[i] = (cur[i] + ((cur[i - bpp] if i >= bpp else 0) + prev[i]) // 2) & 255
        elif f == 4:
            for i in range(st):
                a = cur[i - bpp] if i >= bpp else 0
                b, c = prev[i], prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                cur[i] = (cur[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(cur)
        prev = cur
    return bpp, out


def px(img, x, y):
    bpp, rows = img
    return tuple(rows[y][x * bpp:x * bpp + 3])


def count(img, col, box=BAND):
    return sum(1 for y in range(box[1], box[3]) for x in range(box[0], box[2])
               if px(img, x, y) == col)


def darker(img, at, plain):
    """How much darker than `plain` the points are, summed over red."""
    return sum(plain[0] - px(img, x, y)[0] for x, y in at)


def shot(name, rows, *args):
    path = os.path.join(D, name + ".png")
    env = dict(os.environ, ZYPEROS_HEADLESS="1", SDL_VIDEODRIVER="dummy",
               SDL_RENDER_DRIVER="software", ZYPEROS_ROOT=os.path.join(D, "root"),
               ZYPEROS_CARD=D, ZYPEROS_ROMS=os.path.join(D, "Roms"),
               ZYPEROS_USERDATA=os.path.join(D, "userdata"),
               ZYPEROS_SHARED=os.path.join(D, "shared"), ZYPEROS_WEB=D,
               ZYPEROS_FONT=os.path.join(ROOT, "res", "fonts", "menu.ttf"))
    with open(os.path.join(D, "log"), "a") as log:
        st = subprocess.call([BIN, "--shot", path] + list(args), env=env, cwd=ROOT,
                             stdout=log, stderr=log)
    if st != 0 or not os.path.isfile(path):
        ck(False, "%s: the shot failed (exit %d), see %s" % (name, st, os.path.join(D, "log")))
        return None
    return load(path, rows)


def main():
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    if not os.access(BIN, os.X_OK):
        print("FAIL no build-native/zyperos: make native first", file=sys.stderr)
        return 1

    shutil.rmtree(D, ignore_errors=True)
    for sub in ("root", "Roms/Game Boy", "userdata", "shared"):
        os.makedirs(os.path.join(D, sub))
    shutil.copy(os.path.join(ROOT, "config", "systems.cfg"), os.path.join(D, "root"))
    open(os.path.join(D, "Roms", "Game Boy", "Tetris.gb"), "w").close()

    print("the top menu has him and a system's menu does not; a failure, a blink "
          "and his shadow on Beard")
    rows = BAND[3]
    for theme in ("light", "dark"):
        top = shot("top-" + theme, rows, "--theme", theme, "--menu", "--zyper",
                   "--shot-at", "1000")
        sysm = shot("games-" + theme, rows, "--theme", theme, "--screen", "games", "--menu")
        failed = shot("fails-" + theme, rows, "--theme", theme, "--menu", "--zyper", "fails",
                      "--shot-at", "1200")
        shut = shot("blink-" + theme, rows, "--theme", theme, "--menu", "--zyper",
                    "--shot-at", "2681")
        open_ = shot("noblink-" + theme, rows, "--theme", theme, "--menu", "--zyper",
                     "--shot-at", "5081")
        if None in (top, sysm, failed, shut, open_):
            continue
        ck(count(top, TONGUE) > 40 and count(top, EAR) > 40,
           "%s: the top menu has Zyper beside its heading" % theme)
        ck(count(sysm, TONGUE) == 0 and count(sysm, EAR) == 0,
           "%s: a system's menu has no Zyper" % theme)
        ck(count(failed, TONGUE) == 0 and count(failed, EAR) > 40,
           "%s: a failure has his tongue in (%d of its pixels)" % (theme, count(failed, TONGUE)))
        ck(count(shut, INK) <= count(open_, INK) - 20,
           "%s: a blink shuts his eyes (%d ink pixels against %d)"
           % (theme, count(shut, INK), count(open_, INK)))
        surface = px(top, *MENU_SURFACE)
        d = darker(top, MENU_CHIN, surface)
        if theme == "light":
            ck(d >= 20, "light: his shadow on the panel under his chin (%d darker)" % d)
        else:
            ck(d == 0, "dark: no shadow on Salt & Pepper's panel (%d darker)" % d)

    rows = OFF_CHIN[-1][1] + 1
    lit = shot("off-560-light", rows, "--theme", "light", "--poweroff", "--shot-at", "560")
    unlit = shot("off-560-dark", rows, "--theme", "dark", "--poweroff", "--shot-at", "560")
    dimmed = shot("off-800-light", rows, "--theme", "light", "--poweroff", "--shot-at", "800")
    gone = shot("off-900-light", rows, "--theme", "light", "--poweroff", "--shot-at", "900")
    if None not in (lit, unlit, dimmed, gone):
        d560 = darker(lit, OFF_CHIN, px(lit, 100, 100))
        ck(d560 >= 20, "power off, light: his shadow under his chin (%d darker)" % d560)
        ck(darker(unlit, OFF_CHIN, px(unlit, 100, 100)) == 0 and
           all(px(unlit, x, y) == px(unlit, 100, 100) for x, y in OFF_CHIN),
           "power off, dark: Salt & Pepper's ground under his chin, no shadow")
        d800 = darker(dimmed, OFF_CHIN, px(dimmed, 100, 100))
        ck(d800 <= d560 // 4, "power off at 800: the shadow dims with him (%d darker against "
           "%d at 560)" % (d800, d560))
        ck(all(px(gone, x, y) == px(gone, 100, 100) for x, y in OFF_CHIN),
           "power off at 900, the last frame: no shadow left")

    if fails:
        print("zyper: %d FAILED" % fails)
        return 1
    print("ok: Zyper is in the top menu and no other, answers a failure, blinks, and "
          "casts his shadow on Beard only, going out with him")
    return 0


if __name__ == "__main__":
    sys.exit(main())
