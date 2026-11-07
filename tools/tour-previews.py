#!/usr/bin/env python3
"""The tutorial's home screen previews (DESIGN.md 11.10, "The choices").

    tools/tour-previews.py           write res/tour/home-LAYOUT-THEME.png
    tools/tour-previews.py --check   hold the shipped ones to the launcher

The Theme and Home Screen steps show the home screen as the launcher draws
it, in Spotlight and Gallery, in Light and Dark. These are its own frames:
the host build's --shot of a card holding the little shelf's five systems
(src/tour.c's TOUR_LITTLE: stand-ins, never the player's library, as on the
stage), the SNES focused, the status corner fixed. Each is cut to 380 x 285
units with the row radius's corners left clear, as the step draws it, so a
step is one texture copy a frame.

--check shoots them again and fails when a shipped one is not the launcher's
any more (the home screen changed and they were not made again): `make
tour-previews` makes them again. The comparison is of the pictures, not their
bytes, so a FreeType or Pillow version's antialiasing does not trip it.
Needs Pillow and the host launcher (make native).
"""
import os
import shutil
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageStat

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "build-native" / "zyperos"
OUT = ROOT / "res" / "tour"
WORK = ROOT / "build-native" / "tour-previews"
STAND_INS = ("Game Boy", "NES", "SNES", "Game Boy Color", "Neo Geo Pocket Color")
FOCUS = "SNES"
STATUS = "10:08 24h wifi=3 batt=82"
SIZE = (380, 285)
RADIUS = 14        # DS_RADIUS_ROW, in the preview's own units
# Mean difference, of 255, above which a shipped preview is not the launcher's
TOLERANCE = 2.0


def card(base):
    if base.exists():
        shutil.rmtree(base)
    for d in ("root", "Roms", "userdata", "shared"):
        (base / d).mkdir(parents=True)
    rows = []
    for line in (ROOT / "config" / "systems.cfg").read_text().splitlines():
        f = line.split("|")
        if line.startswith("sys|") and f[1] in STAND_INS:
            rows.append(line)
            folder = base / "Roms" / f[2]
            folder.mkdir(parents=True, exist_ok=True)
            (folder / ("Example." + f[7].split(",")[0])).touch()
    if len(rows) != len(STAND_INS):
        raise SystemExit("tour-previews: systems.cfg lacks one of " + ", ".join(STAND_INS))
    (base / "root" / "systems.cfg").write_text("\n".join(rows) + "\n")
    (base / "root" / "system-icons").symlink_to(ROOT / "res" / "system-icons", target_is_directory=True)
    return os.environ | {
        "ZYPEROS_HEADLESS": "1", "SDL_VIDEODRIVER": "dummy", "SDL_RENDER_DRIVER": "software",
        "ZYPEROS_ROOT": str(base / "root"), "ZYPEROS_CARD": str(base),
        "ZYPEROS_ROMS": str(base / "Roms"), "ZYPEROS_USERDATA": str(base / "userdata"),
        "ZYPEROS_SHARED": str(base / "shared"), "ZYPEROS_WEB": str(base),
        "ZYPEROS_FONT": str(ROOT / "res" / "fonts" / "menu.ttf")}


def run(env, *args):
    r = subprocess.run([str(BIN), *args], env=env, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        raise SystemExit("tour-previews: %s failed: %s" % (" ".join(args), r.stderr[-800:]))


def previews(into):
    """Shoot the four and cut them to the step's size, into `into`."""
    env = card(WORK / "card")
    run(env, "--set", "startup_system=" + FOCUS)
    into.mkdir(parents=True, exist_ok=True)
    mask = Image.new("L", SIZE, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, SIZE[0] - 1, SIZE[1] - 1), RADIUS, fill=255)
    made = []
    for layout in ("spotlight", "gallery"):
        for theme in ("light", "dark"):
            full = WORK / ("full-%s-%s.png" % (layout, theme))
            run(env, "--shot", str(full), "--screen", "systems", "--status", STATUS,
                "--home", layout, "--theme", theme)
            im = Image.open(full).convert("RGB").resize(SIZE, Image.LANCZOS).convert("RGBA")
            im.putalpha(mask)
            path = into / ("home-%s-%s.png" % (layout, theme))
            im.save(path, optimize=True)
            made.append(path)
    return made


def main():
    if not BIN.exists():
        raise SystemExit("tour-previews: no build-native/zyperos: make native first")
    if "--check" not in sys.argv:
        for p in previews(OUT):
            print("wrote", p.relative_to(ROOT), p.stat().st_size, "bytes")
        return 0
    fails = 0
    for fresh in previews(WORK / "fresh"):
        shipped = OUT / fresh.name
        if not shipped.exists():
            print("  FAIL res/tour/%s is missing" % fresh.name)
            fails += 1
            continue
        a, b = Image.open(shipped).convert("RGBA"), Image.open(fresh).convert("RGBA")
        if a.size != SIZE:
            print("  FAIL res/tour/%s is %dx%d, not %dx%d" % ((fresh.name,) + a.size + SIZE))
            fails += 1
            continue
        off = sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 4.0
        if off > TOLERANCE:
            print("  FAIL res/tour/%s is not the launcher's home screen any more (%.1f of 255 "
                  "apart): make tour-previews" % (fresh.name, off))
            fails += 1
    if fails:
        print("%d tutorial preview check(s) FAILED" % fails)
        return 1
    print("ok: the tutorial's previews are the launcher's home screen")
    return 0


if __name__ == "__main__":
    sys.exit(main())
