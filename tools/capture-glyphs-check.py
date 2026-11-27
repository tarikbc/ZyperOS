#!/usr/bin/env python3
"""The capture screen's glyphs are whole after a long session (src/ui.c,
button_font; DESIGN.md 11.61).

    python3 tools/capture-glyphs-check.py [--keep DIR]   (make check-capture-glyphs)

On the Pixel 2 test of 1.1.1-test.2 the screen that waits for the keys
(MENU > System > Hotkeys, A on a row) drew L1, R1, L2 and R2 as empty
rings. Their word is Regular at 112 (a pill's, 11.9), and the table of
button faces held 8: a session had filled it with SemiBold sizes, so the
Regular one found nothing to stand in and no word was drawn. A first
shot never shows it, the table being empty.

So each handheld's capture, SELECT's with each shoulder and MENU's with
each shoulder (the keys as they go down, 112 high), is shot twice: as
the first frame of a start, and after the faces a session opens have been
asked for (--glyph-warm). The two must be the same picture, and the
shoulder's pill must carry its word: ink of the text's color inside it,
not only its ring. --keep DIR keeps both of every pair.
"""
import argparse
import importlib.util
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rs", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)

DEVICES = ["pixel2", "brick"] + [d for d in ("smartpro", "r36s")
                                 if (ROOT / "src/device" / (d + "_profile.c")).exists()]
# src/hotkeys.h's keys: L1, R1, L2, R2
SHOULDERS = {4: "L1", 5: "R1", 6: "L2", 7: "R2"}
# The rows whose capture is shot: Screenshot (SELECT and a key) and MENU's
# Screenshot (MENU and a key), src/hotkeys_screen.c's ids
ROWS = {"select": 4, "menu": 107}


def shoot(device, card, out, row, key, warm):
    cmd = [str(ROOT / "build-native/zyperos"), "--shot", str(out), "--lang", "en", "--theme", "light",
           "--screen", "systems", "--status", "10:08 24h wifi=off batt=82", "--device", device,
           "--hotkeys", str(row), "--hk-capture", "2", "--hk-key", str(key)]
    if warm:
        cmd.append("--glyph-warm")
    return subprocess.run(cmd, cwd=ROOT, env=R.env_for(card), text=True, errors="replace",
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)


def word_ink(img, box):
    """How many pixels inside the pill's middle are near the glyph's text
    (dark in the light theme): its word, where the ring and the face are
    light"""
    x0, y0, x1, y1 = box
    px = img.load()
    n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y][:3]
            if r + g + b < 200:
                n += 1
    return n


def pill_box(img):
    """The rightmost glyph on the capture's row of keys (the shoulder, after
    SELECT or MENU and the plus): its lit ring's columns on the row through
    the keys' middle, and the inside of it"""
    w, h = img.size
    px = img.load()
    # The keys' row: the middle of the band of rows the lit rings' lime is
    # on (the accent: green over red and blue), the capture panel's header
    def lime(x, y):
        r, g, b = px[x, y][:3]
        return g > 200 and b < 120 and r > 120

    band = [y for y in range(h // 5, h * 3 // 4) if sum(1 for x in range(0, w, 2) if lime(x, y)) > 6]
    if not band:
        return None
    top = band[0]
    bottom = top
    for y in band[1:]:
        if y > bottom + 2:
            break
        bottom = y
    best_y = (top + bottom) // 2
    xs = [x for x in range(w) if lime(x, best_y)]
    if not xs:
        return None
    # The rightmost run of lime is the shoulder's right edge; walk left to
    # its left edge (the next lime going left after a gap of face)
    right = xs[-1]
    runs, start, prev = [], xs[0], xs[0]
    for x in xs[1:]:
        if x != prev + 1:
            runs.append((start, prev))
            start = x
        prev = x
    runs.append((start, prev))
    if len(runs) < 2:
        return None
    left = runs[-2][0]
    return (left + 12, best_y - 30, right - 12, best_y + 30)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep", type=Path)
    args = ap.parse_args()
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    if not (ROOT / "build-native/zyperos").is_file():
        print("FAIL no build-native/zyperos: make native first")
        return 1
    try:
        from PIL import Image, ImageChops
    except ImportError:
        print("skipped: no PIL here to read the shots with")
        return 0
    fails, shots = [], 0
    with tempfile.TemporaryDirectory(prefix="zyperos-capglyph-") as tmp:
        card = Path(tmp) / "card"
        R.demo_card(card, None, {"artwork": {}})
        (card / "ZyperOS/.last").write_text("PS1\n" + R.FOCUS + ".chd\n")
        for device in DEVICES:
            for how, row in ROWS.items():
                for key, name in SHOULDERS.items():
                    pair = []
                    for warm in (False, True):
                        out = Path(tmp) / ("%s-%s-%s-%s.png" % (device, how, name, "warm" if warm else "first"))
                        r = shoot(device, card, out, row, key, warm)
                        if r.returncode or not out.is_file():
                            fails.append("%s %s+%s: the launcher ended with %d: %s"
                                         % (device, how, name, r.returncode, r.stdout[-300:]))
                            break
                        pair.append(out)
                        shots += 1
                        if args.keep:
                            args.keep.mkdir(parents=True, exist_ok=True)
                            shutil.copy(out, args.keep / out.name)
                    if len(pair) != 2:
                        continue
                    first, warm = (Image.open(p).convert("RGB") for p in pair)
                    what = "%s %s+%s" % (device, "MENU" if how == "menu" else "SELECT", name)
                    box = pill_box(first)
                    if not box:
                        fails.append("%s: no lit keys found on the capture screen" % what)
                        continue
                    ink = word_ink(first, box), word_ink(warm, box)
                    if ink[0] < 40:
                        fails.append("%s: the shoulder's pill has no word in the first frame (%d ink)" % (what, ink[0]))
                    if ink[1] < 40:
                        fails.append("%s: after a session's faces the shoulder's pill is an empty ring (%d ink)"
                                     % (what, ink[1]))
                    diff = ImageChops.difference(first, warm).getbbox()
                    if diff:
                        fails.append("%s: after a session's faces the capture differs from the first frame, at %s"
                                     % (what, diff))
    for f in fails:
        print("FAIL " + f)
    print("capture-glyphs: %d shots, %s" % (shots, "the shoulders whole on %s" % ", ".join(DEVICES)
                                             if not fails else "%d failed" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
