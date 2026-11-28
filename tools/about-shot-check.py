#!/usr/bin/env python3
"""About keeps page information inside its panel, in both themes, on all five pages.

The page count belongs to the heading. The last page's two QR codes read
back (zbarimg, where it is installed) as the Discord's and the tip page's
addresses, on every handheld's screen. The shelf below the panels must
contain no undimmed page text. Panel shadows may differ there between the
four-row and six-row pages, so compare text roles instead of whole strips.
"""
import importlib.util
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("tour_shots", os.path.join(ROOT, "tools", "tour-shot-check.py"))
t = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t)
t.D = os.path.join(ROOT, "build-native", "about-shot-check")

if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_ttf", "SDL2_image"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
    print("skipped: no SDL2 for About shots")
    sys.exit(0)
shutil.rmtree(t.D, ignore_errors=True)
for sub in ("root", "Roms/Game Boy", "userdata", "shared"):
    os.makedirs(os.path.join(t.D, sub))
shutil.copy(os.path.join(ROOT, "config", "systems.cfg"), os.path.join(t.D, "root"))
shutil.copy(os.path.join(ROOT, "res", "zyper.png"), os.path.join(t.D, "root"))
open(os.path.join(t.D, "Roms/Game Boy/Tetris.gb"), "w").close()
for theme in ("light", "dark"):
    # The five pages: the system, its details, the name, the thanks and the
    # thank-you with its codes
    pages = [t.shot("about-%d-%s" % (p, theme), "--theme", theme, "--about", str(p),
                    "--status", "10:08 wifi=off batt=100 chg") for p in range(5)]
    if None in pages:
        continue
    for p, img in enumerate(pages):
        t.ck(t.zc.count(img, t.TEXT_DIM[theme], (0, 720, 1024, 768)) == 0,
             "%s page %d: no page counter floats over the shelf" % (theme, p + 1))
        if p > 1:
            continue
        band = (750, 180, 948, 250) if p == 0 else (750, 80, 948, 170)
        t.ck(t.zc.count(img, t.TEXT_DIM[theme], band) > 25,
             "%s page %d: the count appears inside the heading" % (theme, p + 1))
    # The name's page holds still: the same pixels whatever the clock says, at
    # the top where it opens and scrolled halfway where Up and Down leave it
    story = ("--theme", theme, "--about", "2", "--status", "10:08 wifi=off batt=100 chg")
    top = [t.shot("story-top-%d-%s" % (ph, theme), *story, "--phase", str(ph)) for ph in (0, 4000, 31000)]
    half = [t.shot("story-half-%d-%s" % (ph, theme), *story, "--about-scroll", "0.5", "--phase", str(ph))
            for ph in (0, 4000, 31000)]
    end = t.shot("story-end-%s" % theme, *story, "--about-scroll", "1")
    if None in top + half + [end]:
        continue
    t.ck(top[0] == top[1] == top[2], "%s story: it does not scroll by itself from the top" % theme)
    t.ck(half[0] == half[1] == half[2], "%s story: scrolled halfway it stays where it is" % theme)
    t.ck(half[0] != top[0], "%s story: scrolling moves the text" % theme)
    t.ck(end != half[0] and end != top[0], "%s story: the bottom is not the middle or the top" % theme)
    # The thanks, one page walked by hand as the story is: still at the top
    # and halfway whatever the clock says, and three different pictures
    thanks = ("--theme", theme, "--about", "3", "--status", "10:08 wifi=off batt=100 chg")
    ttop = [t.shot("thanks-top-%d-%s" % (ph, theme), *thanks, "--phase", str(ph)) for ph in (0, 4000, 31000)]
    thalf = [t.shot("thanks-half-%d-%s" % (ph, theme), *thanks, "--about-scroll", "0.5", "--phase", str(ph))
             for ph in (0, 4000, 31000)]
    tend = t.shot("thanks-end-%s" % theme, *thanks, "--about-scroll", "1")
    if None in ttop + thalf + [tend]:
        continue
    t.ck(ttop[0] == ttop[1] == ttop[2], "%s thanks: it does not scroll by itself from the top" % theme)
    t.ck(thalf[0] == thalf[1] == thalf[2], "%s thanks: scrolled halfway it stays where it is" % theme)
    t.ck(thalf[0] != ttop[0] and tend != thalf[0] and tend != ttop[0],
         "%s thanks: the top, the middle and the end are three pictures" % theme)
# The last page's codes, as a phone's camera reads them, on every handheld's
# screen and in both themes (the code is ink on paper in either)
WANT = {"https://discord.gg/Ust8sE22Tm", "https://zyperos.com/tip/"}
zbar = shutil.which("zbarimg")
if not zbar:
    print("note: no zbarimg here, the last page's codes are not read back")
for device in ("pixel2", "brick", "r36s", "smartpro"):
    for theme in ("light", "dark"):
        name = "support-%s-%s" % (device, theme)
        if t.shot(name, "--device", device, "--theme", theme, "--about", "4",
                  "--status", "10:08 wifi=off batt=100 chg") is None or not zbar:
            continue
        got = subprocess.run([zbar, "-q", "--raw", os.path.join(t.D, name + ".png")],
                             stdout=subprocess.PIPE, text=True).stdout.split()
        t.ck(set(got) == WANT, "%s: the codes read %s, not the Discord's and the tip's" % (name, got))
if not t.fails:
    print("ok: About page information stays inside its panel in both themes, the last page's codes read back")
sys.exit(1 if t.fails else 0)
