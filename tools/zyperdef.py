"""Zyper, the mark of ZyperOS, and the ZyperOS lettering: one definition.

Zyper was a miniature schnauzer. Drawn from his photo, what made him him: a
dark cap over the forehead and around the eyes, a white muzzle bridge that
runs down between the eyes into the beard, long white brows that droop
outward past the eyes, soft natural ears folded forward with a sliver of pink
showing, a short mid-grey coat, the head tilted the way he listened, and the
tongue out to one side. Flat shapes only, the dark details knocked out in the
ground color, no strokes: it has to read at 16 px in a browser tab and at
300 px on the boot screen.

The lettering is the system's own, not a font: seven letters as round-capped,
round-joined strokes on a cap height of 100, with the Z in the accent and the
rest in the beard's white. Nothing here is borrowed from ZyperOS's mark, and
the accent is new - lime, well clear of every card accent in
config/systems.cfg and of the launcher's cyan, which the menus keep.

This module is the source and imports nothing outside the standard library,
so tools/checkmark.py can run it on any machine:

  tools/genzyper.py  draws it with PIL into res/readme, res/web and
                     src/zypermark.h (triangles, since C cannot import this)
  tools/genboot.py   animates it into the boot splash
  tools/checkmark.py proves src/zypermark.h still matches

Design units: y DOWN, the head about its own origin, the word on a baseline
at y = 0 with the cap at -100, the x-height at -60 and descenders to +45.
"""
import math

BG     = (17, 19, 16)         # the ground, the launcher's own
INK    = BG                   # eyes, nose, the open mouth: knocked out
CAP    = (82, 86, 88)         # the dark cap, and the ears
COAT   = (150, 153, 152)      # the short grey coat
BEARD  = (228, 226, 216)      # brows, muzzle bridge, beard; and the word
PINK   = (222, 146, 158)      # the ear insides
TONGUE = (214, 118, 146)
LIME   = (190, 242, 60)       # the accent: the Z
LIME_INK = (71, 107, 0)       # the Z on a light ground: DESIGN.md's accent-ink
                              # in Beard, 4.9:1 there, where lime is 1.03:1

TILT = 10.0                   # degrees, clockwise, the way he cocked his head
BROW_LIFT = 8.0               # the brow on the high side rises this much
TONGUE_Y0 = 54.0              # where the tongue leaves the mouth; it retracts
                              # toward this line in the shutdown animation


def _circ(cx, cy, r, n=28):
    return [(cx + r * math.cos(2 * math.pi * i / n),
             cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def _arc(cx, cy, rx, a0, a1, n=12, ry=None):
    ry = rx if ry is None else ry
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n)))
            for i in range(n + 1)]


def _mirror(pts):
    return [(-x, y) for x, y in reversed(pts)]


def rotate(shapes, deg, about=(0.0, 0.0)):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    ax, ay = about
    return [(col, [((x - ax) * c - (y - ay) * s + ax,
                    (x - ax) * s + (y - ay) * c + ay) for x, y in p])
            for col, p in shapes]


def _swing(pts, deg, about):
    """Rotate points about a pivot: an ear about its fold."""
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    ax, ay = about
    return [((x - ax) * c - (y - ay) * s + ax, (x - ax) * s + (y - ay) * c + ay)
            for x, y in pts]


def head(tilt=TILT, tongue=1.0, brow_lift=None, ear_swing=0.0, blink=0.0,
         tongue_dx=0.0, mouth=0.0):
    """(color, points) for every shape, back to front. `tilt` in degrees;
    `tongue` 0..1 is how far it is out; the brow lift follows the tilt
    unless given. The last three are for animation and change nothing at
    their defaults: `ear_swing` lifts both ear tips outward by that many
    degrees about the fold (negative lets them hang closer), `blink` 0..1
    closes the eyes, flattening each to a tenth of its height at 1, and
    `tongue_dx` moves the tongue sideways in design units, and `mouth` 0..1
    shuts the mouth onto its top line, to a tenth of its depth at 1 (asleep,
    on the charging screen; the tongue goes in with it, `tongue` 0)."""
    if brow_lift is None:
        brow_lift = BROW_LIFT * (tilt / TILT if TILT else 0.0)
    S = []
    # the head, coat grey, a little wider at the cheeks
    S.append((COAT, [(-78, -96), (78, -96), (92, -60), (92, 28), (-92, 28), (-92, -60)]))
    # the dark cap: the forehead down to the brow line, dipping between the eyes
    S.append((CAP, [(-78, -96), (78, -96), (92, -60), (92, -30), (60, -36), (28, -46),
                    (0, -40), (-28, -46), (-60, -36), (-92, -30), (-92, -60)]))
    # ears: natural, folded forward, hanging down the side of the head with
    # the tip pointing down and out; a sliver of pink where the fold opens
    ear = [(-50, -100), (-96, -104), (-118, -70), (-112, -10), (-92, -20), (-78, -60)]
    pink = [(-68, -96), (-94, -98), (-106, -70), (-98, -46), (-84, -72)]
    if ear_swing:
        ear = _swing(ear, ear_swing, (-50, -100))
        pink = _swing(pink, ear_swing, (-50, -100))
    S.append((CAP, ear)); S.append((CAP, _mirror(ear)))
    S.append((PINK, pink)); S.append((PINK, _mirror(pink)))
    # the white muzzle bridge, narrow, between the eyes, running into the beard
    S.append((BEARD, [(-9, -48), (9, -48), (18, -10), (-18, -10)]))
    # eyes, on the coat under the brows
    eye = _circ(-32, -22, 10)
    if blink:
        eye = [(x, -22 + (y + 22) * (1.0 - 0.9 * min(1.0, blink))) for x, y in eye]
    S.append((INK, eye)); S.append((INK, _mirror(eye)))
    # brows: two tufts rising from the bridge, drooping at the outer end past
    # the eye; the one on the high side of the tilt lifts
    brow = [(-10, -50), (-22, -62), (-54, -66), (-78, -56), (-90, -36), (-82, -34),
            (-66, -46), (-44, -46), (-20, -42)]
    S.append((BEARD, brow))
    S.append((BEARD, [(-x, y - brow_lift) for x, y in reversed(brow)]))
    # the beard: wide, soft, hanging well below the chin
    br = [(0, -12), (30, -12), (66, 6), (90, 40), (90, 76), (76, 108), (54, 122),
          (38, 112), (20, 130), (0, 122)]
    S.append((BEARD, br + [(-x, y) for x, y in reversed(br[1:-1])]))
    # the open mouth, and the nose
    mo = [(-40, 44), (40, 44)] + _arc(0, 44, 40, 0, 180, 14, ry=34)[1:-1]
    if mouth:
        mo = [(x, MOUTH_Y + (y - MOUTH_Y) * (1.0 - MOUTH_SHUT * min(1.0, mouth))) for x, y in mo]
    S.append((INK, mo))
    S.append((INK, [(-24, -14), (24, -14), (26, 0), (14, 14), (-14, 14), (-26, 0)]))
    # the tongue, last, hanging out to his left; scaled toward TONGUE_Y0 so
    # it can be drawn going back in
    tx = 12 + tongue_dx
    tongue_pts = [(tx - 16, TONGUE_Y0), (tx + 16, TONGUE_Y0)] + _arc(tx, 94, 16, 0, 180, 10)
    S.append((TONGUE, [(x, TONGUE_Y0 + (y - TONGUE_Y0) * tongue) for x, y in tongue_pts]))
    return rotate(S, tilt) if tilt else S


def bbox(shapes):
    xs = [x for _, p in shapes for x, _ in p]
    ys = [y for _, p in shapes for _, y in p]
    return min(xs), min(ys), max(xs), max(ys)


# ---- the lettering ----------------------------------------------------------
#
# A letter is an advance and a list of strokes. A stroke is ("l", points), a
# polyline with round joins and round caps, or ("a", cx, cy, rx, ry, a0, a1),
# an elliptical arc from angle a0 to a1 clockwise on screen (PIL's and SVG's
# sense, y down), with round caps. Every stroke is STROKE wide.

STROKE = 19.0
GAP = 24.0
CAPH = 100.0

LETTERS = {
    "Z": (78, [("l", [(0, -100), (78, -100), (0, 0), (78, 0)])]),
    "y": (64, [("l", [(0, -60), (32, 0)]),
               ("l", [(64, -60), (8, 45)])]),
    "p": (58, [("l", [(0, -60), (0, 45)]),
               ("l", [(0, -60), (28, -60)]),
               ("l", [(0, 0), (28, 0)]),
               ("a", 28, -30, 30, 30, -90, 90)]),
    "e": (60, [("l", [(2, -30), (58, -30)]),
               ("a", 30, -30, 30, 30, 40, 360)]),
    "r": (50, [("l", [(0, -60), (0, 0)]),
               ("a", 28, -32, 28, 28, 180, 335)]),
    "O": (92, [("a", 46, -50, 46, 50, 0, 360)]),
    "S": (68, [("a", 34, -75, 32, 25, 90, 335),
               ("a", 34, -25, 32, 25, -90, 155)]),
}

WORD = "ZyperOS"


def word(text=WORD):
    """[(index, char, x_offset, strokes)] and the total width."""
    out, x = [], 0.0
    for i, ch in enumerate(text):
        adv, strokes = LETTERS[ch]
        out.append((i, ch, x, strokes))
        x += adv + (GAP if i < len(text) - 1 else 0)
    return out, x


def word_colors(on_dark=True):
    """On a dark ground the Z in lime and the rest in the beard's white; on a
    light one the Z in accent-ink, lime's dark green, and the rest in the
    cap's dark, since lime itself all but vanishes on a light ground."""
    if on_dark:
        return [LIME if ch == "Z" else BEARD for ch in WORD]
    return [LIME_INK if ch == "Z" else CAP for ch in WORD]


def arc_point(cx, cy, rx, ry, a):
    return (cx + rx * math.cos(math.radians(a)), cy + ry * math.sin(math.radians(a)))


# ---- triangles, for C ---------------------------------------------------------
#
# SDL has no polygon fill; src/main.c draws with SDL_RenderGeometry, which
# wants triangles. The beard, the brows and the ears are concave, so a fan
# from the centroid would fold over itself: ear clipping instead, which works
# for any simple polygon. The result goes into src/zypermark.h as a flat
# vertex table, and tools/checkmark.py regenerates it to prove it is current.

def _area2(p):
    return sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1]
               for i in range(len(p)))


def _cross(o, a, b):
    return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])


def _inside(pt, a, b, c):
    d1, d2, d3 = _cross(a, b, pt), _cross(b, c, pt), _cross(c, a, pt)
    return not ((d1 < 0 or d2 < 0 or d3 < 0) and (d1 > 0 or d2 > 0 or d3 > 0))


def triangulate(poly):
    """Ear clipping. Returns a list of (a, b, c) point triples."""
    pts = [p for i, p in enumerate(poly) if p != poly[i - 1]]   # drop repeats
    if _area2(pts) < 0:
        pts.reverse()                                            # CCW in y-down terms
    idx = list(range(len(pts)))
    tris = []
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        found = False
        for k in range(len(idx)):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            a, b, c = pts[i0], pts[i1], pts[i2]
            if _cross(a, b, c) <= 1e-9:                          # reflex or flat
                continue
            if any(_inside(pts[j], a, b, c) for j in idx if j not in (i0, i1, i2)):
                continue
            tris.append((a, b, c))
            del idx[k]
            found = True
            break
        if not found:
            # Only collinear slivers are left on an arc; drop the flattest.
            k = min(range(len(idx)), key=lambda k: abs(_cross(
                pts[idx[k - 1]], pts[idx[k]], pts[idx[(k + 1) % len(idx)]])))
            del idx[k]
    if len(idx) == 3:
        tris.append((pts[idx[0]], pts[idx[1]], pts[idx[2]]))
    return tris


# Where the C moves the parts head() moves (src/main.c, draw_zyper): the line
# a blink shuts the eyes onto and how much of their height it takes, and the
# fold the ear at the picture's left swings about, the other ear's being its
# mirror. head() says the same in its own numbers; tools/checkmark.py moves
# the header's vertices by these and holds them to head()'s triangles.
EYE_Y = -22.0
EYE_SHUT = 0.9
MOUTH_Y = 44.0                 # the mouth's top line, which it shuts onto
MOUTH_SHUT = 0.9
EAR_FOLD = (-50.0, -100.0)


def _moved(a, b):
    """The indexes of the shapes that differ between two poses."""
    return [i for i, ((_, p), (_, q)) in enumerate(zip(a, b)) if p != q]


def c_header():
    """src/zypermark.h: the untilted head with the tongue fully out, as
    colored triangles. The tilt and the tongue are applied in C, because the
    shutdown animation moves both; so are a blink, the ears' swing and the
    high brow's lift, because the menu's Zyper moves those, and the header
    marks which vertices are the eyes, the ears and that brow. A part is
    found by what moves when head() moves it, not by its place in the list,
    so a shape added to head() cannot shift a mark onto its neighbour."""
    shapes = head(tilt=0.0, tongue=1.0, brow_lift=BROW_LIFT)
    x0, y0, x1, y1 = bbox(head())                 # the tilted extent, for sizing
    parts = {"EYES": _moved(shapes, head(0.0, 1.0, BROW_LIFT, blink=1.0)),
             "EARS": _moved(shapes, head(0.0, 1.0, BROW_LIFT, ear_swing=10.0)),
             "BROW": _moved(shapes, head(0.0, 1.0, 0.0)),
             "MOUTH": _moved(shapes, head(0.0, 1.0, BROW_LIFT, mouth=1.0))}
    verts, tongue_first, span = [], None, []
    for col, pts in shapes:
        if col == TONGUE:
            tongue_first = len(verts)
        first = len(verts)
        for tri in triangulate(pts):
            for (x, y) in tri:
                verts.append((x, y, col))
        span.append((first, len(verts)))
    marks = []
    for name, idx in parts.items():
        # one run of shapes each, so one run of vertices
        assert idx and idx == list(range(idx[0], idx[-1] + 1)), name
        marks += ["#define ZYPER_%s_FIRST %d" % (name, span[idx[0]][0]),
                  "#define ZYPER_%s_END %d" % (name, span[idx[-1]][1])]
    o = ["/* Zyper, as triangles. Generated by tools/genzyper.py from",
         " * tools/zyperdef.py, which is the definition: edit that and rerun it.",
         " * tools/checkmark.py fails when this file and the definition disagree.",
         " *",
         " * Untilted, tongue fully out, in design units about the head's origin.",
         " * The tilt and the tongue are applied by draw_zyper() in src/main.c,",
         " * since the shutdown animation moves both. ZYPER_H is the tilted",
         " * height, which is what a caller sizes by, and ZYPER_W the tilted",
         " * width, which is what one beside something else is placed by.",
         " *",
         " * The menu's Zyper moves three parts more, each a run of vertices from",
         " * _FIRST up to but not including _END: the EYES shut toward ZYPER_EYE_Y",
         " * by ZYPER_EYE_SHUT of their height, the EARS swing about their folds,",
         " * (ZYPER_EAR_FOLD_X, ZYPER_EAR_FOLD_Y) for the one at the left and its",
         " * mirror for the other, and the high-side BROW is drawn ZYPER_BROW_LIFT",
         " * up, which a brow that follows the tilt takes back down. Asleep (the",
         " * charging screen), the MOUTH shuts onto ZYPER_MOUTH_Y by",
         " * ZYPER_MOUTH_SHUT of its depth. */",
         "#define ZYPER_TILT_DEG %.1ff" % TILT,
         "#define ZYPER_TONGUE_Y0 %.1ff" % TONGUE_Y0,
         "#define ZYPER_BROW_LIFT %.1ff" % BROW_LIFT,
         "#define ZYPER_EYE_Y %.1ff" % EYE_Y,
         "#define ZYPER_EYE_SHUT %.1ff" % EYE_SHUT,
         "#define ZYPER_MOUTH_Y %.1ff" % MOUTH_Y,
         "#define ZYPER_MOUTH_SHUT %.1ff" % MOUTH_SHUT,
         "#define ZYPER_EAR_FOLD_X %.1ff" % EAR_FOLD[0],
         "#define ZYPER_EAR_FOLD_Y %.1ff" % EAR_FOLD[1],
         "#define ZYPER_H %.1ff" % (y1 - y0),
         "#define ZYPER_W %.1ff" % (x1 - x0),
         "#define ZYPER_CX %.3ff" % ((x0 + x1) / 2),
         "#define ZYPER_CY %.3ff" % ((y0 + y1) / 2),
         "#define ZYPER_NVERT %d" % len(verts),
         "#define ZYPER_TONGUE_FIRST %d" % tongue_first] + marks + [
         "/* The high brow's lift at a tilt, as head() lifts it when it is not",
         " * given: in step with the tilt, ZYPER_BROW_LIFT at rest. checkmark holds",
         " * head() to this. */",
         "static inline float zyper_brow_lift(float tilt)",
         "{",
         "\treturn ZYPER_BROW_LIFT * tilt / ZYPER_TILT_DEG;",
         "}",
         "static const struct { float x, y; unsigned char r, g, b; } zyper_verts[ZYPER_NVERT] = {"]
    for i in range(0, len(verts), 3):
        o.append("\t" + " ".join("{%.2ff,%.2ff,%d,%d,%d}," % (x, y, c[0], c[1], c[2])
                                 for (x, y, c) in verts[i:i + 3]))
    o.append("};")
    return "\n".join(o) + "\n"


def hexf(rgb):
    return "#%02X%02X%02X" % rgb
