#!/usr/bin/env python3
"""The first start's line and bar, over the boot animation's loop.

    python3 tools/genfirststart.py              res/boot/first-start/
    python3 tools/genfirststart.py --check      whether it is what this draws
                                                (make check-genfirststart)
    python3 tools/genfirststart.py --preview F [light]  F: the three over a loop frame

A card with no ZyperOS on it gets the copy the system image carries on its
first start, and a newer system image puts its own in the same way on its
first start after a system update. That is the ZyperOS folder, cores, ScummVM
and all: 468 MB, about 54 s on a Pixel 2 (2026-11-06). The boot animation's
loop (tools/genboot.py) plays all that time, Zyper panting over and over, and
nothing said what was happening. My pick of three (2026-11-06): the
loop goes on, and under the lockup a line says what is happening, over a bar
that fills as the card fills (DESIGN.md 11.20).

plastron's splash draws them: its scripts/make-splash-frames.py --first-start
turns these onto the panel, and the splash puts them over the loop's frames
once the intro has written the word, when the image's init tells it the copy
has begun. The bar's fill is the splash's own, from how much of the copy is
on the card.

Everything is in PANEL pixels, the GKD Pixel 2's 640x480 landscape, as
tools/genloading.py's frames are: the splash draws straight to the panel.
Drawn at the launcher's 1024x768 and brought down with LANCZOS, as the boot
frames are brought down from it, so the line is the size and weight the
launcher's meta is on the panel.

    setup.png    "Setting up ZyperOS on this card" and the empty bar
    update.png   "Updating ZyperOS on this card" and the empty bar
    bar.png      the bar full, nothing else
    first-start.txt   where the bar is, for the splash's fill
    CODE/        setup.png and update.png in each other language of
                 res/strings (boot.setup, boot.update; src/i18n.h), as
                 English's are: the splash shows the language MENU > Language
                 chose (DESIGN.md 11.20). Chinese in the Noto Sans SC cut
                 (tools/cjkfont.py), the rest in Lexend. bar.png is every
                 language's.

Transparent around them: the splash lays them on the animation's ground,
which is Zyper's #111310 under the whole loop (tools/zyperdef.BG).
"""
import os
import shutil
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import designdef as D   # noqa: E402
import genboot as B     # noqa: E402
import gendrift         # noqa: E402
import i18n             # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "boot", "first-start")
FONT = os.path.join(ROOT, "res", "fonts", "menu.ttf")   # Lexend Regular

PANEL_W, PANEL_H = 640, 480
W, H = B.W, B.H                  # the launcher's drawing space, the boot frames'

# Each theme's colors (DESIGN.md 3.1), as the boot animation is in each
# (tools/genboot.py): Salt & Pepper's in this folder, Beard's in light/, as
# res/loading has them
def _rgb(h): return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
COLORS = {"dark": {name: _rgb(dark) for name, _, dark in D.COLORS},
          "light": {name: _rgb(light) for name, light, _ in D.COLORS}}
THEME_DIRS = {"dark": "", "light": "light"}    # under OUT

META = 37                        # DESIGN.md 5: meta, 23 px on the panel
LINE_Y = 706                     # its baseline, 49 under the word's lowest pixel (657)
BAR_W, BAR_H = 420, 6            # DESIGN.md 4: bar, the progress bar's height
BAR_Y = 728                      # its top: 16 under the line's descenders

# The widest a line may be, in the launcher's units: 64 in from each side of
# the 1024 (40 of the panel's 640)
LINE_MAX = W - 2 * 64


def lines(code):
    """The two lines in a language: res/strings' boot.setup and boot.update"""
    errors = []
    by_id = {x.id: x for x in i18n.parse(errors)}
    return {"setup": i18n.get(by_id["boot.setup"], code), "update": i18n.get(by_id["boot.update"], code)}


def font_for(code):
    """The face a language's line is drawn in: the UI's own for it"""
    name = i18n.FONTS.get(code, i18n.LATIN_FONTS)[0]
    return os.path.join(ROOT, "res", "fonts", name)


LINES = lines("en")


def bar_box():
    """The bar in the launcher's drawing space, centered under the line"""
    x = (W - BAR_W) // 2
    return (x, BAR_Y, x + BAR_W, BAR_Y + BAR_H)


def draw(line, full, theme="dark", code="en"):
    """The bar fills in accent-ink on track, as DESIGN.md's progress strip:
    lime on Salt & Pepper, the dark green on Beard, where lime is too pale"""
    c = COLORS[theme]
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if line:
        font = ImageFont.truetype(font_for(code), META)
        if font.getlength(line) > LINE_MAX:
            sys.exit("genfirststart: %s's line is %d wide, past %d: %s" % (code, font.getlength(line), LINE_MAX, line))
        d.text((W / 2, LINE_Y), line, font=font, fill=c["text-soft"] + (255,), anchor="ms")
    d.rounded_rectangle(bar_box(), radius=BAR_H // 2,
                        fill=(c["accent-ink"] if full else c["track"]) + (255,))
    return im.resize((PANEL_W, PANEL_H), Image.LANCZOS)


def write(out):
    """Both themes' pictures and first-start.txt, into `out`: OUT, or a temp
    folder for --check. Returns the bar's box."""
    for theme, under in THEME_DIRS.items():
        folder = os.path.join(out, under)
        os.makedirs(folder, exist_ok=True)
        for code in i18n.CODES:
            to = folder if code == "en" else os.path.join(folder, code)
            os.makedirs(to, exist_ok=True)
            for name, line in lines(code).items():
                draw(line, False, theme, code).save(os.path.join(to, name + ".png"))
        bar = draw(None, True, theme)
        bar.save(os.path.join(folder, "bar.png"))
    x0, y0, x1, y1 = bar.getchannel("A").getbbox()
    with open(os.path.join(out, "first-start.txt"), "w") as f:
        f.write("# The first start's line and bar. Written by tools/genfirststart.py; edit that.\n")
        f.write("# Panel pixels, 640x480 landscape: the bar's box, every pixel bar.png\n")
        f.write("# draws. It fills left to right: bar.png's pixels from x to the fill,\n")
        f.write("# in its rows only; setup.png's or update.png's everywhere else.\n")
        f.write("# Salt & Pepper's pictures here, Beard's in light/, on each theme's ground.\n")
        f.write("bar %d %d %d %d\n" % (x0, y0, x1 - x0, y1 - y0))
        f.write("ground.dark %d %d %d\n" % COLORS["dark"]["ground"])
        f.write("ground.light %d %d %d\n" % COLORS["light"]["ground"])
    return x0, y0, x1, y1


def check():
    """OUT against a fresh drawing of it. The line is text, and how a Pillow
    lays text out is its own: these were drawn by one with Raqm, which is the
    one the Makefile's PYIMG picks first."""
    tmp = tempfile.mkdtemp(prefix="genfirststart-")
    try:
        write(tmp)
        bad = gendrift.drift(tmp, OUT, lambda rel: True)
    finally:
        shutil.rmtree(tmp)
    for b in bad:
        print("check-genfirststart: res/boot/first-start/" + b, file=sys.stderr)
    if bad:
        print("check-genfirststart: run python3 tools/genfirststart.py", file=sys.stderr)
        return 1
    print("check-genfirststart: res/boot/first-start is what tools/genfirststart.py draws")
    return 0


def main():
    if sys.argv[1:] == ["--check"]:
        sys.exit(check())
    if len(sys.argv) in (3, 4) and sys.argv[1] == "--preview":
        preview(sys.argv[2], sys.argv[3] if len(sys.argv) == 4 else "dark")
        return
    x0, y0, x1, y1 = write(OUT)
    print("res/boot/first-start (and light/): setup.png update.png bar.png first-start.txt, bar %d %d %d %d"
          % (x0, y0, x1 - x0, y1 - y0))


def preview(path, theme="dark"):
    """The three over a frame of the loop, as the panel shows them: empty,
    half, and the update's line, side by side"""
    frame = B.render(B.loop(0), theme).resize((PANEL_W, PANEL_H), Image.LANCZOS).convert("RGBA")
    bar = draw(None, True, theme)
    x0, y0, x1, y1 = bar.getchannel("A").getbbox()
    shots = []
    for line, frac in (("setup", 0.0), ("setup", 0.5), ("update", 0.8)):
        im = Image.alpha_composite(frame, draw(LINES[line], False, theme))
        full = Image.alpha_composite(frame, bar)
        split = x0 + round((x1 - x0) * frac)
        if split > x0:
            im.paste(full.crop((x0, y0, split, y1)), (x0, y0))
        shots.append(im)
    sheet = Image.new("RGBA", (PANEL_W * 3 + 16, PANEL_H), (60, 62, 60, 255))
    for i, s in enumerate(shots):
        sheet.paste(s, (i * (PANEL_W + 8), 0))
    sheet.convert("RGB").save(path)
    print(path)


if __name__ == "__main__":
    main()
