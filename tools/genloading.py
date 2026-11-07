#!/usr/bin/env python3
"""Zyper waits: the loading animation, drawn from the mark's own shapes.

    python3 tools/genloading.py                 res/loading/ and res/loading/light/
    python3 tools/genloading.py --check         whether they are what it draws
                                                (make check-genloading)
    python3 tools/genloading.py --preview DIR   GIFs and contact sheets of every
                                                loop in both themes, for choosing
    python3 tools/genloading.py --layout LIGHT.png DARK.png
                                                res/loading/layout.png, composed
                                                from two 1024x768 shots of the
                                                launcher's own loading screen

A port or a standalone emulator can take most of a minute to start (Stardew
Valley's Mono). port_loading_screen() in src/main.c draws the game's cover on
its own blurred backdrop, with the system, the game's name and "Starting." in
a column beside it, and plastron's splash holds that still; this is the small
loop splash plays over it so the screen is seen to be alive. Zyper's frame
sits at the left edge of the column, under the words, where the launcher
leaves it free: (504, 528) in the 1024x768 drawing space, loading.txt's x 315
y 330.

The frames are PNGs with transparency in PANEL pixels, the GKD Pixel 2's
640x480 landscape, not the launcher's 1024x768 drawing space: splash draws
straight to the framebuffer. loading.txt beside them says how many, how fast
and where. Flat shapes at 1x: everything is drawn at 4x and downsampled, and
the loop is seamless, its last frame leading back into its first.

Two themes (DESIGN.md 3.1), two sets of frames, the same size, so a player
treats them alike. The launcher writes the word `light` or `dark` to
/tmp/zyperos-theme (src/themefile.h):

  dark   Salt & Pepper, res/loading/zyper-NN.png. Zyper as he was drawn, on
         the ground #111310; he needs nothing else (DESIGN.md 10).
  light  Beard, res/loading/light/zyper-NN.png. The same Zyper with the
         focus-small shadow of DESIGN.md 7 baked in: on Beard's ground
         #e6e4da his beard and brows are nearly the ground's own color, and
         the shadow is what gives him an edge (DESIGN.md 10).

The shadow is ink at 18%, 5 px down (8 units), blurred as 20 units are
blurred in src/blur.h, cast by his silhouette. It stays on the page: it is
the shadow of the head where it rests, and when a hop lifts him off the page
it does not go with him, it softens and lightens as a shadow does when what
casts it rises. At the bottom of each hop it is exactly the token; a shadow
that went up with him would be a drop shadow on a sticker that slides, where
this is a head that leaves the page and comes back to it.

Both sets are on one canvas, found by drawing both and keeping what they
touch: 120x116, where the first version's frames were 96x96, since the shadow
reaches 12 px past the head at the sides and 18 below it. He is on it where
he was, and Salt & Pepper's frames are the first version's pixel for pixel in
a larger transparent frame.

Beard's frames come down from the 4x drawing by BOX, each pixel its coverage,
and Salt & Pepper's by LANCZOS, as they were. LANCZOS rings at an edge: a rim
up to 17 levels over the beard's own white inside it and a white pixel of
alpha 4 outside, which is an edge light on a dark ground and a halo on a light
one, where the beard is the ground's color.

Nothing here is redrawn. tools/zyperdef.head() takes the tilt, the tongue,
the ear swing and the tongue's sideways offset as arguments; a loop is a
function of the phase that chooses them, plus a bob and a squash applied to
the finished shapes.
"""
import math
import os
import shutil
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zyperdef as Z   # noqa: E402
import genzyper as G   # noqa: E402
import gendrift        # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "loading")
FONTS = os.path.join(ROOT, "res", "fonts")      # the captions of layout.png

PANEL_W, PANEL_H = 640, 480
PANEL_PER_UNIT = 640 / 1024.0           # DESIGN.md 2: 0.625 panel px to a unit

# Where the launcher puts his frame, in its 1024x768 drawing space: the left
# edge of the text column, under the words (port_loading_screen, claude/
# lexend-cards 6cb4ef2). 504 and 528 units are whole panel pixels.
CORNER_UNITS = (504, 528)
CORNER = (int(CORNER_UNITS[0] * PANEL_PER_UNIT + 0.5), int(CORNER_UNITS[1] * PANEL_PER_UNIT + 0.5))

# ---- the two themes ------------------------------------------------------------
#
# DESIGN.md 3.2 (tools/designdef.py on claude/themes): the roles the loading
# screen is drawn from, per theme. `word` is what the launcher writes to
# /tmp/zyperos-theme, `dir` where the frames are, under res/loading/.

THEMES = [
    #  word     name             dir      ground           text           text-dim
    ("light", "Beard",         "light", (230, 228, 218), (26, 28, 26),  (90, 93, 90)),
    ("dark",  "Salt & Pepper", "",      (17, 19, 16),    (228, 226, 216), (150, 153, 152)),
]
THEME = dict((t[0], dict(zip(("word", "name", "dir", "ground", "text", "dim"), t)))
             for t in THEMES)

INK = (17, 19, 16)                       # the role `ink`, #111310, in both

# DESIGN.md 7, focus-small in Beard: 8 units down, 20 blur, ink at 18%.
SHADOW_Y_UNITS, SHADOW_BLUR_UNITS, SHADOW_A = 8, 20, 0.18
SHADOW_Y = SHADOW_Y_UNITS * PANEL_PER_UNIT          # 5.0 px
# src/blur.h makes a shadow of three boxes, each `blur` + 1 units wide, whose
# variance is 3 * (w*w - 1) / 12: a Gaussian of 10.49 units for 20, which is
# 6.56 px here (CSS would draw 10, and the launcher is what the eye compares).
_W = SHADOW_BLUR_UNITS + 1
SHADOW_SIGMA = math.sqrt(3 * (_W * _W - 1) / 12.0) * PANEL_PER_UNIT
# What a hop does to it, per px of lift off the page: wider, and lighter.
SHADOW_SOFTEN = 0.20 / 7.0               # sigma +20% at the top of a 7 px hop
SHADOW_FADE = 0.25 / 7.0                 # alpha -25% there

FRAME = 96                              # px, square, the frame the head was drawn in
HEAD_H = 76.0                           # his resting height inside it
FOOT = FRAME - 6                        # his chin, in that frame
SS = 4

# ---- the loops ---------------------------------------------------------------
#
# Each returns, for a phase t in [0,1): (tilt, tongue, ear_swing, tongue_dx,
# bob_px, squash). bob is up (negative y) in panel pixels; squash scales the
# height about his bottom edge, width the other way, volume kept.

def _s(t): return math.sin(2 * math.pi * t)
def _c(t): return math.cos(2 * math.pi * t)


def loop_bounce(t):
    """Two little hops a second, ears lagging a quarter behind, the tongue
    wagging once. The happy wait. Head straight, not cocked: My call,
    2026-11-03, so the hop reads as the motion rather than the tilt."""
    hop = abs(_s(t))                       # two hops per loop
    bob = -7.0 * hop
    squash = 1.0 - 0.06 * (1.0 - hop) ** 4   # flattens at the bottom of each
    ears = 10.0 * abs(_s(t - 0.12)) - 3.0  # lift as he rises, lag behind
    return 0.0, 1.0, ears, 5.0 * _s(t), bob, squash


def loop_tilt(t):
    """The head cocks from one side to the other and back, the brow lifting on
    the high side, the tongue swinging the other way. Slower, the listening
    wait."""
    tilt = Z.TILT * _c(t)
    ears = 2.0 * abs(_c(t)) - 1.0
    return tilt, 1.0, ears, -6.0 * _c(t), -1.5 * abs(_s(t)), 1.0


LOOPS = {
    "bounce": (loop_bounce, 12, 12),       # frames, fps: a one-second loop
    "tilt":   (loop_tilt, 16, 10),         # 1.6 s
}
CHOSEN = "bounce"


# ---- the canvas ---------------------------------------------------------------
#
# The frame the head was drawn in was 96 px square with the chin 6 px from its
# foot. Beard's shadow reaches farther than that, so both themes' frames are
# on one larger canvas, and the head is on it where it was: the old 96x96 frame
# sits in from the canvas's left and top edges by `left` and `top`.

class Canvas(object):
    def __init__(self, left, top, right, bottom):
        self.left, self.top, self.right, self.bottom = left, top, right, bottom
        self.w = FRAME + left + right
        self.h = FRAME + top + bottom


def _round_up(n, k):
    return int(math.ceil(n / float(k))) * k


_CANVAS = {}


def canvas(name):
    """The smallest canvas, to 4 px, that holds everything either theme draws
    in this loop with one transparent pixel to spare, found by drawing it on
    a big one. Wider the same each side, so he stays in the middle."""
    if name not in _CANVAS:
        big = Canvas(64, 64, 64, 64)
        fn, n, _ = LOOPS[name]
        x0 = y0 = 1 << 30
        x1 = y1 = -(1 << 30)
        for i in range(n):
            for theme in ("dark", "light"):
                bb = _layers(fn, i / float(n), theme, big)[0].getchannel("A").getbbox()
                x0, y0 = min(x0, bb[0]), min(y0, bb[1])
                x1, y1 = max(x1, bb[2]), max(y1, bb[3])
        # in the old frame's coordinates
        x0 -= big.left; y0 -= big.top
        x1 -= big.left + FRAME; y1 -= big.top + FRAME
        side = _round_up(max(-x0, x1, 0) + 1, 4)
        _CANVAS[name] = Canvas(side, _round_up(max(-y0, 0) + 1, 4),
                               side, _round_up(max(y1, 0) + 1, 4))
    return _CANVAS[name]


# ---- the head -----------------------------------------------------------------

def head_frame(loop_fn, t, cv, ss=SS, lift=None, resample=Image.LANCZOS):
    """Zyper alone, RGBA, on the canvas. `lift` replaces the loop's bob (in px
    up, zero for the head where it rests): the shadow is cast by the head at
    rest. `resample` is how the 4x drawing comes down: LANCZOS is what the
    frames were made with and what Salt & Pepper keeps, but it rings at an
    edge, a bright rim a few levels over the beard's own white and a white
    alpha-4 pixel outside it, which on a dark ground is an edge light and on
    Beard's is a halo; Beard's frames come down by BOX, the plain coverage
    of each pixel, which cannot overshoot."""
    tilt, tongue, ears, tdx, bob, squash = loop_fn(t)
    if lift is not None:
        bob = -lift
    S = Z.head(tilt=tilt, tongue=tongue, ear_swing=ears, tongue_dx=tdx)
    rx0, ry0, rx1, ry1 = Z.bbox(Z.head())        # the resting extent sizes him
    k = HEAD_H / (ry1 - ry0) * ss
    cx = (rx0 + rx1) / 2.0
    bottom = ry1                                   # squash is about his chin
    im = Image.new("RGBA", (cv.w * ss, cv.h * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # He sits on the old frame's lower edge with a little room, so the bob has
    # somewhere to go and the frame can be set down on a baseline.
    foot = (cv.top + FOOT) * ss
    mid = (cv.left + FRAME / 2.0) * ss

    def T(x, y):
        x = cx + (x - cx) / math.sqrt(squash)
        y = bottom + (y - bottom) * squash
        return ((x - cx) * k + mid, (y - bottom) * k + foot + bob * ss)
    G.draw_shapes(d, S, T)
    return im.resize((cv.w, cv.h), resample)


def shadow_layer(loop_fn, t, cv, ss=SS):
    """The focus-small shadow under Zyper at phase t, RGBA, ink with the
    shadow's alpha: his silhouette where he rests, lowered SHADOW_Y, blurred,
    at SHADOW_A, all softened and lightened by how far this frame has lifted
    him. PIL's GaussianBlur takes the standard deviation."""
    lift = max(0.0, -loop_fn(t)[4])
    sil = head_frame(loop_fn, t, cv, ss, lift=0.0, resample=Image.BOX).getchannel("A")
    moved = Image.new("L", sil.size, 0)
    moved.paste(sil, (0, int(round(SHADOW_Y))))
    blur = moved.filter(ImageFilter.GaussianBlur(SHADOW_SIGMA * (1.0 + SHADOW_SOFTEN * lift)))
    k = SHADOW_A * (1.0 - SHADOW_FADE * lift)
    layer = Image.new("RGBA", sil.size, INK + (0,))
    layer.putalpha(blur.point(lambda v: int(v * k + 0.5)))
    return layer


def _layers(loop_fn, t, theme, cv, ss=SS):
    """(frame, shadow) for the theme: the frame is RGBA; Salt & Pepper's has no
    shadow, and Beard's has it composed under the head."""
    if theme == "dark":
        return head_frame(loop_fn, t, cv, ss), None
    head = head_frame(loop_fn, t, cv, ss, resample=Image.BOX)
    sh = shadow_layer(loop_fn, t, cv, ss)
    out = Image.alpha_composite(sh, head)
    # where nothing is drawn, nothing: not ink at alpha 0
    out.paste((0, 0, 0, 0), mask=out.getchannel("A").point(lambda v: 255 if v == 0 else 0))
    return out, sh


def frames(name, theme):
    fn, n, fps = LOOPS[name]
    cv = canvas(name)
    return [_layers(fn, i / float(n), theme, cv)[0] for i in range(n)], fps


# ---- the screen, as the launcher draws it -----------------------------------

def compose_layout(light_png, dark_png, path):
    """layout.png: the launcher's own loading screens, one shot per theme, each
    1024x768 as it draws and brought to panel size by BOX, Beard on the left
    and Salt & Pepper on the right with a name under each. The shots are
    `--portload` of the launcher, which has Zyper's first frame at the corner."""
    gap, strip = 16, 28
    sheet = Image.new("RGB", (PANEL_W * 2 + gap, PANEL_H + strip), (128, 128, 128))
    d = ImageDraw.Draw(sheet)
    lf = ImageFont.truetype(os.path.join(FONTS, "menu.ttf"), 16)
    for i, (th, shot) in enumerate(zip(THEMES, (light_png, dark_png))):
        im = Image.open(shot).convert("RGB")
        if im.size != (1024, 768):
            sys.exit("%s is %dx%d, not 1024x768" % (shot, im.width, im.height))
        sheet.paste(im.resize((PANEL_W, PANEL_H), Image.BOX), (i * (PANEL_W + gap), 0))
        d.text((i * (PANEL_W + gap) + PANEL_W / 2, PANEL_H + strip / 2),
               "%s (%s)" % (th[1], th[0]), font=lf, fill=(24, 24, 24), anchor="mm")
    sheet.save(path)


def gif(name, theme, path, scale=2):
    fr, fps = frames(name, theme)
    g = THEME[theme]["ground"]
    out = []
    for f in fr:
        bg = Image.new("RGBA", f.size, g + (255,))
        bg.alpha_composite(f)
        out.append(bg.convert("RGB").resize((f.width * scale, f.height * scale), Image.NEAREST))
    out[0].save(path, save_all=True, append_images=out[1:], duration=int(1000 / fps), loop=0)


def sheet(name, theme, path, scale=1):
    fr, fps = frames(name, theme)
    g = THEME[theme]["ground"]
    w, h = fr[0].size
    s = Image.new("RGB", (w * len(fr), h), g)
    for i, f in enumerate(fr):
        cell = Image.new("RGBA", f.size, g + (255,))
        cell.alpha_composite(f)
        s.paste(cell.convert("RGB"), (i * w, 0))
    if scale != 1:
        s = s.resize((s.width * scale, s.height * scale), Image.NEAREST)
    s.save(path)


def arg(flag, n=1):
    if flag in sys.argv:
        i = sys.argv.index(flag)
        if i + n < len(sys.argv):
            return sys.argv[i + 1:i + 1 + n]
        sys.exit("%s needs %d argument%s" % (flag, n, "" if n == 1 else "s"))
    return None


def write(out):
    """The chosen loop's frames in both themes and loading.txt, into `out`:
    res/loading, or a temp folder for --check"""
    cv = canvas(CHOSEN)
    _, n, fps = LOOPS[CHOSEN]
    for th in THEMES:
        folder = os.path.join(out, th[2])
        os.makedirs(folder, exist_ok=True)
        for old in os.listdir(folder):
            if old.startswith("zyper-") and old.endswith(".png"):
                os.remove(os.path.join(folder, old))
        fr, _ = frames(CHOSEN, th[0])
        for i, f in enumerate(fr):
            f.save(os.path.join(folder, "zyper-%02d.png" % i), optimize=True)
    with open(os.path.join(out, "loading.txt"), "w") as f:
        f.write(
            "# Zyper's loading loop. Written by tools/genloading.py; edit that.\n"
            "loop %s\n"
            "frames %d\n"
            "fps %d\n"
            "# Every frame, in both themes: this size, RGBA, in panel pixels.\n"
            "size %dx%d\n"
            "#\n"
            "# Which set: the word in /tmp/zyperos-theme, `light` or `dark`\n"
            "# (src/themefile.h). A missing file, or any other word, is light,\n"
            "# as the launcher reads it. The frames of a theme are in its dir,\n"
            "# under this folder, as zyper-00.png to zyper-%02d.png.\n"
            "#   light  Beard          dir light  ground #e6e4da, Zyper's\n"
            "#          focus-small shadow (8 down, 20 blur, ink 18%%) is in\n"
            "#          the frames: composite them over the still, nothing else.\n"
            "#   dark   Salt & Pepper  dir .      ground #111310\n"
            "# The ground is the theme's, as the launcher's still has it under\n"
            "# its backdrop; it is here for a player that needs a color, and\n"
            "# the old 'ground 7 8 12' was the launcher's before the themes.\n"
            "# It is not what goes under a frame: see the corner.\n"
            "dir.light light\n"
            "dir.dark .\n"
            "ground.light %d %d %d\n"
            "ground.dark %d %d %d\n"
            "#\n"
            "# Panel pixels, 640x480 landscape, the frame's top-left corner:\n"
            "# (%d, %d) in the launcher's 1024x768 drawing space, the left edge\n"
            "# of the text column, under the words. port_loading_screen() in\n"
            "# the launcher (claude/lexend-cards) leaves this %dx%d area free\n"
            "# of text and of the cover.\n"
            "# The still under it is not flat: it has the cover blurred as a\n"
            "# backdrop and the cover lifted, so the splash must copy the\n"
            "# still back under the frame before each frame, not fill the\n"
            "# area with the ground, which would show as a box.\n"
            "x %d\ny %d\n"
            % (CHOSEN, n, fps, cv.w, cv.h, n - 1,
               THEME["light"]["ground"][0], THEME["light"]["ground"][1], THEME["light"]["ground"][2],
               THEME["dark"]["ground"][0], THEME["dark"]["ground"][1], THEME["dark"]["ground"][2],
               CORNER_UNITS[0], CORNER_UNITS[1], cv.w, cv.h, CORNER[0], CORNER[1]))
    return cv, n, fps


def check():
    """res/loading against a fresh drawing of it. layout.png is not this
    mode's: it is composed from shots of the launcher (--layout)."""
    tmp = tempfile.mkdtemp(prefix="genloading-")
    try:
        write(tmp)
        bad = gendrift.drift(tmp, OUT, lambda rel: rel == "loading.txt" or
                             os.path.basename(rel).startswith("zyper-"))
    finally:
        shutil.rmtree(tmp)
    for b in bad:
        print("check-genloading: res/loading/" + b, file=sys.stderr)
    if bad:
        print("check-genloading: run python3 tools/genloading.py", file=sys.stderr)
        return 1
    print("check-genloading: res/loading is what tools/genloading.py draws")
    return 0


def main():
    if "--check" in sys.argv:
        sys.exit(check())
    layout = arg("--layout", 2)
    pd = arg("--preview")
    if layout:
        compose_layout(layout[0], layout[1], os.path.join(OUT, "layout.png"))
        print("  res/loading/layout.png")
        return
    if pd:
        pd = pd[0]
        os.makedirs(pd, exist_ok=True)
        for name in LOOPS:
            cv = canvas(name)
            for word in THEME:
                gif(name, word, os.path.join(pd, "loop-%s-%s.gif" % (name, word)))
                sheet(name, word, os.path.join(pd, "sheet-%s-%s.png" % (name, word)))
            print("  %s: %d frames at %d fps, canvas %dx%d" % (name, LOOPS[name][1], LOOPS[name][2], cv.w, cv.h))
        return

    cv, n, fps = write(OUT)
    print("  res/loading, res/loading/light: %d frames each, %dx%d at %d fps, loading.txt"
          % (n, cv.w, cv.h, fps))


if __name__ == "__main__":
    main()
