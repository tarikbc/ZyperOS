#!/usr/bin/env python3
"""Export Zyper and the ZyperOS lettering everywhere a still of them is kept.

    python3 tools/genzyper.py

Writes, from tools/zyperdef.py alone:

  res/readme/lockup.png    the head beside the word, transparent, for the README
  res/readme/lockup-light.png  the same with the word dark, for a light page
  res/readme/zyper.png     the head alone, transparent
  res/readme/wordmark.png  the word alone, transparent
  res/web/mark.svg         the lockup, Over The Hare's logo
  res/web/mark-light.svg   the same with the word dark, for the page in Beard
  res/web/icon.svg         the head, square, the favicon
  res/web/zyper.svg        the head, on the PIN gate and beside the name
  res/web/zyper-shut.svg   the same with the eyes shut, laid over it while a
                           PIN is typed and for a blink
  src/zypermark.h          the head as triangles, for SDL_RenderGeometry

PNGs are drawn at 4x and downsampled with LANCZOS, since PIL's polygon
rasterizer has no antialiasing of its own. The SVGs are polygons for the head
and stroked paths for the letters, with round caps and joins, the same model
PIL draws from, so the two cannot drift.

Transparent PNGs, because GitHub renders the README on a light or a dark
ground depending on the reader; the word is in the beard's white, which is
for a dark ground, and the README's header is a dark card for that reason.
"""
import math
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import zyperdef as Z  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SS = 4


# ---- PIL ----------------------------------------------------------------------

def draw_shapes(d, shapes, T):
    for col, pts in shapes:
        d.polygon([T(*p) for p in pts], fill=col + (255,))


def draw_stroke(d, stroke, col, T, k, dx=0.0):
    w = Z.STROKE * k
    if stroke[0] == "l":
        P = [T(x + dx, y) for x, y in stroke[1]]
        d.line(P, fill=col + (255,), width=int(round(w)), joint="curve")
        ends = [P[0], P[-1]]
    else:
        _, cx, cy, rx, ry, a0, a1 = stroke
        x0, y0 = T(cx + dx - rx, cy - ry); x1, y1 = T(cx + dx + rx, cy + ry)
        d.arc([x0 - w / 2, y0 - w / 2, x1 + w / 2, y1 + w / 2], a0, a1,
              fill=col + (255,), width=int(round(w)))
        ends = [T(*Z.arc_point(cx + dx, cy, rx, ry, a)) for a in (a0, a1)]
    for x, y in ends:
        d.ellipse([x - w / 2, y - w / 2, x + w / 2, y + w / 2], fill=col + (255,))


def draw_word(d, x0, base, k, on_dark=True):
    cols = Z.word_colors(on_dark)
    letters, _ = Z.word()
    for i, ch, dx, strokes in letters:
        for s in strokes:
            draw_stroke(d, s, cols[i], lambda x, y: (x0 + x * k, base + y * k), k, dx)


def png_head(size, pad=8):
    S = Z.head()
    x0, y0, x1, y1 = Z.bbox(S)
    side = max(x1 - x0, y1 - y0) + 2 * pad
    k = size * SS / side
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    im = Image.new("RGBA", (size * SS, size * SS), (0, 0, 0, 0))
    draw_shapes(ImageDraw.Draw(im), S,
                lambda x, y: ((x - cx) * k + size * SS / 2, (y - cy) * k + size * SS / 2))
    return im.resize((size, size), Image.LANCZOS)


def lockup_layout():
    """The head at the left, the word to its right, the word's cap height
    about two fifths of the head's. Everything in design units; returns the
    head's center, the word's origin and scale, and the total box."""
    S = Z.head()
    hx0, hy0, hx1, hy1 = Z.bbox(S)
    hh = hy1 - hy0
    kw = 0.42 * hh / Z.CAPH
    _, ww = Z.word()
    gap = 0.16 * hh
    wx = hx1 + gap
    base = (hy0 + hy1) / 2 + Z.CAPH / 2 * kw
    pad = 0.05 * hh
    box = (hx0 - pad, hy0 - pad, wx + ww * kw + Z.STROKE * kw + pad, hy1 + pad)
    return S, wx, base, kw, box


def png_lockup(height, on_dark=True):
    S, wx, base, kw, (bx0, by0, bx1, by1) = lockup_layout()
    k = height * SS / (by1 - by0)
    W = int(round((bx1 - bx0) * k / SS))
    im = Image.new("RGBA", (W * SS, height * SS), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    T = lambda x, y: ((x - bx0) * k, (y - by0) * k)
    draw_shapes(d, S, T)
    draw_word(d, *T(wx, base), kw * k, on_dark)
    return im.resize((W, height), Image.LANCZOS)


def png_word(height):
    _, ww = Z.word()
    pad = Z.STROKE
    k = height * SS / (Z.CAPH + 45 + 2 * pad)
    W = int(round((ww + 2 * pad) * k / SS))
    im = Image.new("RGBA", (W * SS, height * SS), (0, 0, 0, 0))
    draw_word(ImageDraw.Draw(im), pad * k, (Z.CAPH + pad) * k, k)
    return im.resize((W, height), Image.LANCZOS)


# ---- SVG ----------------------------------------------------------------------

def svg_shapes(shapes):
    return "".join('<polygon points="%s" fill="%s"/>'
                   % (" ".join("%.2f,%.2f" % p for p in pts), Z.hexf(col))
                   for col, pts in shapes)


def svg_word(x0, base, k, on_dark=True):
    cols = Z.word_colors(on_dark)
    letters, _ = Z.word()
    out = []
    for i, ch, dx, strokes in letters:
        d = []
        for s in strokes:
            if s[0] == "l":
                d.append("M" + "L".join("%.2f %.2f" % (x0 + (x + dx) * k, base + y * k)
                                        for x, y in s[1]))
            else:
                _, cx, cy, rx, ry, a0, a1 = s
                sx, sy = Z.arc_point(cx + dx, cy, rx, ry, a0)
                if a1 - a0 >= 360:
                    # a full circle is two half arcs: one A command cannot close
                    mx, my = Z.arc_point(cx + dx, cy, rx, ry, a0 + 180)
                    d.append("M%.2f %.2fA%.2f %.2f 0 1 1 %.2f %.2fA%.2f %.2f 0 1 1 %.2f %.2f"
                             % (x0 + sx * k, base + sy * k, rx * k, ry * k,
                                x0 + mx * k, base + my * k, rx * k, ry * k,
                                x0 + sx * k, base + sy * k))
                else:
                    ex, ey = Z.arc_point(cx + dx, cy, rx, ry, a1)
                    d.append("M%.2f %.2fA%.2f %.2f 0 %d 1 %.2f %.2f"
                             % (x0 + sx * k, base + sy * k, rx * k, ry * k,
                                1 if a1 - a0 > 180 else 0, x0 + ex * k, base + ey * k))
        out.append('<path d="%s" fill="none" stroke="%s" stroke-width="%.2f" '
                   'stroke-linecap="round" stroke-linejoin="round"/>'
                   % ("".join(d), Z.hexf(cols[i]), Z.STROKE * k))
    return "".join(out)


def svg_doc(box, inner, label):
    x0, y0, x1, y1 = box
    return ('<svg viewBox="%.1f %.1f %.1f %.1f" xmlns="http://www.w3.org/2000/svg" '
            'role="img" aria-label="%s">%s</svg>\n' % (x0, y0, x1 - x0, y1 - y0, label, inner))


def svg_lockup(on_dark=True):
    S, wx, base, kw, box = lockup_layout()
    return svg_doc(box, svg_shapes(S) + svg_word(wx, base, kw, on_dark), "ZyperOS")


def svg_head(square, pad=6.0, blink=0.0):
    """The head, in a box worked out from the open head, so the shut one
    (`blink` 1, the eyes as a blink shuts them) lays exactly over it."""
    x0, y0, x1, y1 = Z.bbox(Z.head())
    S = Z.head(blink=blink) if blink else Z.head()
    if square:
        # A favicon is drawn into a square whatever we say, so the box is
        # chosen here where the padding can stay even.
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        half = max(x1 - x0, y1 - y0) / 2 + pad
        box = (cx - half, cy - half, cx + half, cy + half)
    else:
        box = (x0 - pad, y0 - pad, x1 + pad, y1 + pad)
    return svg_doc(box, svg_shapes(S), "Zyper")


def main():
    rd = os.path.join(ROOT, "res", "readme")
    web = os.path.join(ROOT, "res", "web")
    os.makedirs(rd, exist_ok=True)
    # Two lockups: GitHub picks one by the reader's color scheme through a
    # <picture> element in the README, since the word is white for a dark
    # ground and the cap's dark for a light one.
    png_lockup(400).save(os.path.join(rd, "lockup.png"), dpi=(144, 144))
    png_lockup(400, on_dark=False).save(os.path.join(rd, "lockup-light.png"), dpi=(144, 144))
    png_head(512).save(os.path.join(rd, "zyper.png"), dpi=(144, 144))
    png_word(320).save(os.path.join(rd, "wordmark.png"), dpi=(144, 144))
    print("  res/readme/lockup.png lockup-light.png zyper.png wordmark.png")
    # Two lockups for Over The Hare as for the README: the page wears Beard
    # or Salt & Pepper as the browser asks, and picks the one for its ground.
    for name, doc in (("mark.svg", svg_lockup()), ("mark-light.svg", svg_lockup(False)),
                      ("icon.svg", svg_head(True)), ("zyper.svg", svg_head(False)),
                      ("zyper-shut.svg", svg_head(False, blink=1.0))):
        open(os.path.join(web, name), "w").write(doc)
    print("  res/web/mark.svg mark-light.svg icon.svg zyper.svg zyper-shut.svg")
    open(os.path.join(ROOT, "src", "zypermark.h"), "w").write(Z.c_header())
    print("  src/zypermark.h")


if __name__ == "__main__":
    main()
