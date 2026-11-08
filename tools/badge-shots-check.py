#!/usr/bin/env python3
"""The achievement notice and list as the launcher draws them: the badge or
the plain one, and Zyper.

    make check-badge-shots

tools/rabadge-check.c holds the badges' cache, fetch and fallback as numbers
and pictures; this holds what src/main.c and src/notice.c do with them, which
is where an unlock over a game loses its Zyper, a progress count gets him
when he is for unlocks, or a list shows a hole where a badge has not come:

  - an unlock's notice (--notice-kind unlock) has Zyper at its right end, his
    pink tongue, and at its left the plain badge, a trophy in the theme's
    accent-ink, when the card has no badge; with a badge (--notice-badge) it
    has that picture and no trophy; in both themes;
  - a count's notice (--notice-kind progress) has no Zyper; the set's arrival
    (the default kind) has neither Zyper nor a badge;
  - the list (--cheevos-screen) has a plain badge in every row of a set the
    card has no art for, and the card's own badge in every row of one it has,
    and the rows do not move between the two: the text to the right of the
    badges is the same pixels.

Host shots, headless, read back with Python's standard library. The card is a
scratch tree under build-native/badge-shots-check. Needs the launcher built,
which needs SDL2, and skips cleanly without it, as check-zyper does.

Exits non-zero and says what failed.
"""
import importlib.util
import os
import shutil
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = os.path.join(ROOT, "build-native", "badge-shots-check")
BIN = os.path.join(ROOT, "build-native", "zyperos")

spec = importlib.util.spec_from_file_location("zc", os.path.join(ROOT, "tools", "zyper-check.py"))
zc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(zc)   # its PNG reader; its main() is not run

TONGUE = (214, 118, 146)            # tools/zyperdef.py's TONGUE
STAND_IN = (200, 0, 200)            # the stand-in badge, a solid magenta
# accent-ink in each theme (src/design.h): the plain badge's trophy
TROPHY = {"light": (71, 107, 0), "dark": (190, 242, 60)}

ROWS = 750
# Where the notice lies: the bottom middle. Its left end is the badge, its
# right end Zyper, divided at the middle of the screen
NOTICE_Y = (620, 745)
BADGE_X = (90, 300)
ZYPER_X = (700, 960)
# The list's badge column, and the text to its right, under the first row, which
# is the selected one: its highlight is accent, the color of Salt & Pepper's
# trophy. Rows 2 and 3 are earned (in color), 4 and 5 are not (greyed)
LIST_ICONS = (118, 178, 365, 650)
LIST_TEXT = (192, 780, 365, 650)

fails = 0


def ck(cond, what):
    global fails
    if not cond:
        print("  FAIL", what)
        fails += 1


def png(path, w, h, rgb):
    """A solid-colored PNG, for a stand-in badge"""
    row = b"\x00" + bytes(rgb) * w
    raw = row * h

    def chunk(t, body):
        c = struct.pack(">I", len(body)) + t + body
        return c + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def count(img, col, x, y):
    """Pixels of exactly `col` in x = (x0, x1), y = (y0, y1)"""
    return sum(1 for yy in range(y[0], y[1]) for xx in range(x[0], x[1])
               if zc.px(img, xx, yy) == col)


def shot(name, *args):
    path = os.path.join(D, name + ".png")
    env = dict(os.environ, ZYPEROS_HEADLESS="1", SDL_VIDEODRIVER="dummy",
               SDL_RENDER_DRIVER="software", ZYPEROS_ROOT=os.path.join(D, "root"),
               ZYPEROS_CARD=D, ZYPEROS_ROMS=os.path.join(D, "Roms"),
               ZYPEROS_USERDATA=os.path.join(D, "userdata"),
               ZYPEROS_SHARED=os.path.join(D, "shared"), ZYPEROS_WEB=D,
               ZYPEROS_FONT=os.path.join(ROOT, "res", "fonts", "menu.ttf"))
    with open(os.path.join(D, "log"), "a") as log:
        st = subprocess.call([BIN, "--shot", path, "--screen", "systems", "--status",
                              "10:08 24h wifi=off batt=82"] + list(args), env=env, cwd=ROOT,
                             stdout=log, stderr=log)
    if st != 0 or not os.path.isfile(path):
        ck(False, "%s: the shot failed (exit %d), see %s" % (name, st, os.path.join(D, "log")))
        return None
    return zc.load(path, ROWS)


def same(a, b, x, y):
    return all(a[1][yy][x[0] * a[0]:x[1] * a[0]] == b[1][yy][x[0] * b[0]:x[1] * b[0]]
               for yy in range(y[0], y[1]))


def main():
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    if not os.access(BIN, os.X_OK):
        print("FAIL no build-native/zyperos: make native first", file=sys.stderr)
        return 1

    shutil.rmtree(D, ignore_errors=True)
    for sub in ("root", "Roms/Game Boy", "userdata", "shared", "set/badges"):
        os.makedirs(os.path.join(D, sub))
    shutil.copy(os.path.join(ROOT, "config", "systems.cfg"), os.path.join(D, "root"))
    open(os.path.join(D, "Roms", "Game Boy", "Tetris.gb"), "w").close()
    badge = os.path.join(D, "badge.png")
    png(badge, 64, 64, STAND_IN)
    for i in range(1, 9):
        png(os.path.join(D, "set", "badges", "b%d.png" % i), 64, 64, STAND_IN)
    unlocked = ["--notice", "Violated Heavens", "Unlocked · 10 points"]

    print("an unlock's notice has Zyper and a badge, the plain one where the card has none; "
          "a count's has the plain one; the set's arrival neither")
    for theme in ("light", "dark"):
        t = ("--theme", theme)
        trophy = TROPHY[theme]
        bare = shot("bare-" + theme, *t)
        plain = shot("unlock-plain-" + theme, *t, "--notice-kind", "unlock", *unlocked)
        art = shot("unlock-art-" + theme, *t, "--notice-kind", "unlock", *unlocked,
                   "--notice-badge", badge)
        prog = shot("progress-" + theme, *t, "--notice-kind", "progress", "--notice-progress",
                    "74", "100", *unlocked)
        arrival = shot("set-" + theme, *t, *unlocked)
        if None in (bare, plain, art, prog, arrival):
            continue
        base_tongue = count(bare, TONGUE, ZYPER_X, NOTICE_Y)
        base_trophy = count(bare, trophy, BADGE_X, NOTICE_Y)
        n = count(plain, TONGUE, ZYPER_X, NOTICE_Y) - base_tongue
        ck(n >= 40, "%s: an unlock has Zyper at its right end (%d tongue pixels)" % (theme, n))
        n = count(plain, trophy, BADGE_X, NOTICE_Y) - base_trophy
        ck(n >= 100, "%s: an unlock with no badge on the card has the plain one (%d trophy pixels)"
           % (theme, n))
        ck(count(plain, STAND_IN, BADGE_X, NOTICE_Y) == 0, "%s: ...and no picture of a badge" % theme)
        ck(count(art, STAND_IN, BADGE_X, NOTICE_Y) >= 2500,
           "%s: with the card's badge it has that picture (%d pixels)"
           % (theme, count(art, STAND_IN, BADGE_X, NOTICE_Y)))
        ck(count(art, trophy, BADGE_X, NOTICE_Y) - base_trophy <= 5,
           "%s: ...and not the plain one under it" % theme)
        ck(count(art, TONGUE, ZYPER_X, NOTICE_Y) - base_tongue >= 40,
           "%s: ...and Zyper still" % theme)
        ck(count(prog, TONGUE, ZYPER_X, NOTICE_Y) - base_tongue <= 0,
           "%s: a count moving has no Zyper: he is for the unlock" % theme)
        ck(count(arrival, TONGUE, ZYPER_X, NOTICE_Y) - base_tongue <= 0,
           "%s: the set's arrival has no Zyper" % theme)
        ck(count(arrival, trophy, BADGE_X, NOTICE_Y) - base_trophy <= 0,
           "%s: ...and no badge" % theme)

    print("the list has a badge in every row, the plain one until the card has the art, "
          "and the rows do not move when it comes")
    for theme in ("light", "dark"):
        t = ("--theme", theme)
        trophy = TROPHY[theme]
        plain = shot("list-plain-" + theme, *t, "--cheevos-screen", "0")
        art = shot("list-art-" + theme, *t, "--cheevos-screen", "0", "--cheevos-badge",
                   os.path.join(D, "set", "x.set"), "b#")
        if None in (plain, art):
            continue
        n = count(plain, trophy, LIST_ICONS[:2], LIST_ICONS[2:])
        ck(n >= 300, "%s: a set with no art on the card has the plain badge in its rows (%d trophy "
           "pixels)" % (theme, n))
        n = count(art, STAND_IN, LIST_ICONS[:2], LIST_ICONS[2:])
        ck(n >= 2000, "%s: a set with art has the card's badges (%d pixels)" % (theme, n))
        ck(count(art, trophy, LIST_ICONS[:2], LIST_ICONS[2:]) == 0,
           "%s: ...and no plain one" % theme)
        ck(same(plain, art, LIST_TEXT[:2], LIST_TEXT[2:]),
           "%s: the rows' words do not move when the badges come" % theme)

    if fails:
        print("badges: %d FAILED" % fails)
        return 1
    print("ok: an unlock has Zyper and its badge or the plain one, a count only the plain one, "
          "the set's arrival neither; every row of the list has its badge, plain until the "
          "art is there")
    return 0


if __name__ == "__main__":
    sys.exit(main())
