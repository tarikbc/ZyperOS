#!/usr/bin/env python3
"""Render ZyperOS's boot animation: the name wakes Zyper up.

Run from the repo root:  python3 tools/genboot.py   (or make boot)
                         python3 tools/genboot.py --light   Beard's frames only

Output: its PNG frames in build/boot-frames (1024x768, 60 fps) with
build/boot-frames/loop.txt, and res/boot/zyperos-fly.mp4, the intro and the
loop once, H.264. plastron's scripts/make-splash-frames.py takes either, and
prefers the PNG folder, where the flat ground stays cheap.

plastron plays the frames from the moment the panel lights and loops from
the frame loop.txt names (its one line) until the launcher's first frame
replaces them, about 3.3 s later on a Pixel 2. So the animation is an intro
that ends on the lockup and a loop that starts and ends on it, with no seam.

The intro: Zyper is asleep when the panel lights, eyes shut and ears down.
The word writes itself under him, a pen stroke per letter, the lime Z first,
and the writing wakes him: his eyes open as the Z is finished, his ears
perk, and once the word is written he hops up, lands with his head cocked
and puts his tongue out. The loop: he pants, the tongue a little in and out
three times a second, and blinks once. Only the tongue and the eyes move, so
the loop changes a narrow band of the screen and costs little in plastron's
splash file, which stores the rows of the panel that changed.

Every frame is drawn on twos (30 fps motion): plastron stores a frame that
repeats the one before as nothing, and the splash file stays near 0.9 MB.

Motion follows DESIGN.md section 8: the interface's curves for the word (the
pen's smoothstep), and Zyper's own moves (8.3: hop, tilt, tongue), the only
thing allowed to overshoot and settle. The head, the word and the colors are
tools/zyperdef.py's, drawn through tools/genzyper.py's PIL routines, so this
cannot drift from the README's lockup or from src/zypermark.h. Everything is
drawn at 3x and downsampled with LANCZOS: PIL's polygon rasterizer has no
antialiasing of its own.
"""

import math
import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zyperdef as Z   # noqa: E402
import genzyper as G   # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(ROOT, "res", "boot")
TMP_DIR = os.path.join(ROOT, "build", "boot-frames")
# The animation in each theme (DESIGN.md 3.1): plastron's splash plays the one
# the card's Appearance asks for (DESIGN.md 11.21). Salt & Pepper's is the
# original, on Zyper's own ground; Beard's is on its ground, the word in the
# cap's dark and the Z in accent-ink (tools/zyperdef.word_colors, the
# README's light lockup), and
# Zyper with the focus-small shadow of DESIGN.md 7 under him, as the light
# loading loop has it (tools/genloading.py): on Beard's ground his beard and
# brows are nearly its own color, and the shadow gives him his edge.
THEMES = {
    "dark": dict(ground=Z.BG, on_dark=True, dir=TMP_DIR),
    "light": dict(ground=(230, 228, 218), on_dark=False, dir=TMP_DIR + "-light"),
}
# focus-small in Beard, in the launcher's units (1 px here): 8 down, 20 blur
# (src/blur.h's box, as a Gaussian of the same spread), ink at 18%
SHADOW_Y, SHADOW_A = 8.0, 0.18
SHADOW_SIGMA = math.sqrt(3 * (21 * 21 - 1) / 12.0)
SHADOW_SOFTEN, SHADOW_FADE = 0.20 / 11.2, 0.25 / 11.2   # per px of lift, as genloading's per 7 panel px

W, H = 1024, 768
SS = 3                             # supersample factor

FPS = 60
INTRO = 60                         # frames, 1.0 s
LOOP = 120                         # frames, 2.0 s: six pants
PANT = 20                          # frames a pant, three a second
WORD_W = 560.0                     # px, the word across the screen
WORD_BASE = 0.79 * H               # px, its baseline
HEAD_H = 300.0                     # px, his tilted height, at rest
HEAD_CY = 0.40 * H                 # px, his center


def cl(t): return max(0.0, min(1.0, t))


def ease_out(t):
    """DESIGN.md's out: the quintic ease-out."""
    return 1.0 - (1.0 - cl(t)) ** 5


def smooth(t):
    """DESIGN.md's turn: smoothstep, for a pen drawing a stroke."""
    t = cl(t)
    return t * t * (3.0 - 2.0 * t)


def span(n, a, b):
    """Where frame n is between frames a and b, 0..1."""
    return cl((n - a) / float(b - a))


# ---- the animation -------------------------------------------------------------
#
# A frame is (pose, letter, Zyper's alpha): his pose for zyperdef.head and
# the transform, and each letter's share drawn, 0..1.

REST = {"tilt": Z.TILT, "tongue": 1.0}


def intro(n):
    up = math.sin(math.pi * span(n, 26, 46))      # the hop, 333 ms
    land = span(n, 44, 50)                        # the squash as he lands
    pose = {
        "scale": 0.92 + 0.08 * ease_out(span(n, 0, 10)),
        "blink": 1.0 - smooth(span(n, 12, 18)),
        "ears": -8.0 + 14.0 * ease_out(span(n, 16, 28)) - 6.0 * smooth(span(n, 40, 52)),
        "bob": -14.0 * (1.0 - ease_out(span(n, 0, 12))) + 30.0 * up,
        "squash": 1.0 - 0.06 * math.sin(math.pi * land),
        "tilt": Z.TILT * ease_out(span(n, 30, 46)),
        "tongue": ease_out(span(n, 46, 58)),
    }
    # 200 ms a letter, 67 ms apart: the word is written by frame 38
    return pose, lambda i: smooth(span(n, 2 + 4 * i, 14 + 4 * i)), ease_out(span(n, 0, 8))


def blink(k, at, close=3, hold=1, open_=4):
    """0..1..0: the eyes shut in `close` frames, held, open in `open_`."""
    u = k - at
    if u < 0 or u >= close + hold + open_:
        return 0.0
    if u < close:
        return smooth((u + 1) / float(close))
    if u < close + hold:
        return 1.0
    return 1.0 - smooth((u - close - hold + 1) / float(open_))


def loop(k):
    """Frame k of the loop; loop(0) and loop(LOOP) are the lockup at rest."""
    s = 0.5 - 0.5 * math.cos(2 * math.pi * (k % PANT) / float(PANT))
    pose = dict(REST, tongue=1.0 - 0.18 * s, blink=blink(k, 64))
    return pose, lambda i: 1.0, 1.0


# ---- drawing -------------------------------------------------------------------

def partial(stroke, t):
    """The first t of a stroke, by length for a line, by angle for an arc."""
    if t >= 1.0:
        return stroke
    if stroke[0] == "a":
        _, cx, cy, rx, ry, a0, a1 = stroke
        return ("a", cx, cy, rx, ry, a0, a0 + (a1 - a0) * t)
    pts = stroke[1]
    left = stroke_len(stroke) * t
    out = [pts[0]]
    for i in range(len(pts) - 1):
        sl = math.dist(pts[i], pts[i + 1])
        if left >= sl:
            out.append(pts[i + 1])
            left -= sl
            continue
        f = left / sl if sl else 0.0
        out.append((pts[i][0] + (pts[i + 1][0] - pts[i][0]) * f,
                    pts[i][1] + (pts[i + 1][1] - pts[i][1]) * f))
        break
    return ("l", out)


def stroke_len(stroke):
    if stroke[0] == "a":
        _, cx, cy, rx, ry, a0, a1 = stroke
        return abs(a1 - a0) / 360.0 * math.pi * (rx + ry)
    pts = stroke[1]
    return sum(math.dist(pts[i], pts[i + 1]) for i in range(len(pts) - 1))


def draw_word(d, letter, on_dark=True):
    """The word, each letter drawn to its share, its strokes in order by
    length, as a pen would."""
    letters, ww = Z.word()
    k = WORD_W / ww * SS
    x0, base = (W / 2.0 - WORD_W / 2.0) * SS, WORD_BASE * SS
    cols = Z.word_colors(on_dark)
    for i, ch, dx, strokes in letters:
        left = letter(i) * sum(stroke_len(s) for s in strokes)
        for s in strokes:
            if left <= 0:
                break
            sl = stroke_len(s)
            G.draw_stroke(d, partial(s, min(1.0, left / sl) if sl else 1.0), cols[i],
                          lambda x, y: (x0 + x * k, base + y * k), k, dx)
            left -= sl


def draw_zyper(d, pose):
    """Zyper by pose: scale, tilt, tongue, ears, blink, bob (px up), squash
    (height about his chin, width the other way, volume kept)."""
    S = Z.head(tilt=pose.get("tilt", Z.TILT), tongue=pose.get("tongue", 1.0),
               ear_swing=pose.get("ears", 0.0), blink=pose.get("blink", 0.0))
    # Sized by the resting head, not this frame's box, so growing is growing
    # and tilting does not also shrink him to keep a bounding box constant.
    rx0, ry0, rx1, ry1 = Z.bbox(Z.head())
    k = HEAD_H / (ry1 - ry0) * pose.get("scale", 1.0) * SS
    cx, cy = (rx0 + rx1) / 2.0, (ry0 + ry1) / 2.0
    sq = pose.get("squash", 1.0)
    bob = pose.get("bob", 0.0) * SS

    def T(x, y):
        x = cx + (x - cx) / math.sqrt(sq)
        y = ry1 + (y - ry1) * sq
        return ((x - cx) * k + W / 2.0 * SS, (y - cy) * k + HEAD_CY * SS - bob)
    G.draw_shapes(d, S, T)


def shadow(pose, alpha):
    """Beard's focus-small shadow: his silhouette where he rests (no bob),
    lowered and blurred, ink at its alpha; softer and lighter as a hop lifts
    him off the page, as tools/genloading.py's"""
    lift = max(0.0, pose.get("bob", 0.0))
    sil = Image.new("L", (W * SS, H * SS), 0)
    draw_zyper(_Fill(ImageDraw.Draw(sil)), dict(pose, bob=0.0))
    moved = Image.new("L", sil.size, 0)
    moved.paste(sil, (0, int(round(SHADOW_Y * SS))))
    blur = moved.filter(ImageFilter.GaussianBlur(SHADOW_SIGMA * SS * (1.0 + SHADOW_SOFTEN * lift)))
    k = SHADOW_A * max(0.0, 1.0 - SHADOW_FADE * lift) * alpha
    layer = Image.new("RGBA", sil.size, Z.INK + (0,))
    layer.putalpha(blur.point(lambda v: int(v * k + 0.5)))
    return layer


class _Fill:
    """An ImageDraw that fills every shape white: a silhouette"""
    def __init__(self, d): self.d = d
    def __getattr__(self, name):
        f = getattr(self.d, name)
        def white(*a, **kw):
            if "fill" in kw: kw["fill"] = 255
            if "outline" in kw and kw["outline"] is not None: kw["outline"] = 255
            return f(*a, **kw)
        return white


def render(frame, theme="dark"):
    t = THEMES[theme]
    pose, letter, alpha = frame
    im = Image.new("RGBA", (W * SS, H * SS), t["ground"] + (255,))
    draw_word(ImageDraw.Draw(im), letter, t["on_dark"])
    if alpha > 0:
        if not t["on_dark"]:
            im = Image.alpha_composite(im, shadow(pose, alpha))
        L = Image.new("RGBA", im.size, (0, 0, 0, 0))
        draw_zyper(ImageDraw.Draw(L), pose)
        if alpha < 1:
            L.putalpha(L.getchannel("A").point(lambda v: int(v * alpha)))
        im = Image.alpha_composite(im, L)
    return im.convert("RGB").resize((W, H), Image.LANCZOS)


def main():
    if "--light" in sys.argv[1:]:
        frames_only("light")
        return
    if len(sys.argv) == 3 and sys.argv[1] == "--frame-light":
        render(loop(0), "light").save(sys.argv[2])
        return
    os.makedirs(OUT_DIR, exist_ok=True)
    if os.path.isdir(TMP_DIR):
        shutil.rmtree(TMP_DIR)
    os.makedirs(TMP_DIR)
    # on twos: an odd frame is the even one before it, saved again
    frames = [intro(n) for n in range(INTRO)] + [loop(k) for k in range(LOOP)]
    for i, f in enumerate(frames):
        if i % 2 == 0:
            im = render(f)
        im.save(os.path.join(TMP_DIR, "f%04d.png" % i))
    # the seam: the frame after the loop's last is its first
    assert repr(loop(0)[0]) == repr(loop(LOOP)[0]) == repr(dict(REST, tongue=1.0, blink=0.0))
    with open(os.path.join(TMP_DIR, "loop.txt"), "w") as f:
        f.write("%d\n" % INTRO)
    mp4 = os.path.join(OUT_DIR, "zyperos-fly.mp4")
    subprocess.run([
        find_ffmpeg(), "-y", "-loglevel", "error",
        "-framerate", str(FPS), "-start_number", "0",
        "-i", os.path.join(TMP_DIR, "f%04d.png"),
        "-frames:v", str(len(frames)), "-fps_mode", "passthrough",
        "-c:v", "libx264", "-pix_fmt", "yuv420p", "-crf", "20",
        "-movflags", "+faststart", mp4,
    ], check=True)
    print("%s  %d frames (intro %d, loop %d), %d bytes" % (
        mp4, len(frames), INTRO, LOOP, os.path.getsize(mp4)))


def frames_only(theme):
    """The animation's PNG frames and loop.txt in one theme's folder, for
    plastron's scripts/make-splash-frames.py (no video)"""
    out = THEMES[theme]["dir"]
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(out)
    frames = [intro(n) for n in range(INTRO)] + [loop(k) for k in range(LOOP)]
    for i, f in enumerate(frames):
        if i % 2 == 0:
            im = render(f, theme)
        im.save(os.path.join(out, "f%04d.png" % i))
    with open(os.path.join(out, "loop.txt"), "w") as f:
        f.write("%d\n" % INTRO)
    print("%s  %d frames (intro %d, loop %d)" % (out, len(frames), INTRO, LOOP))


def find_ffmpeg():
    for cand in ("/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg"):
        if os.path.exists(cand):
            return cand
    found = shutil.which("ffmpeg")
    if not found:
        sys.exit("ffmpeg not found on PATH")
    return found


if __name__ == "__main__":
    main()
