#!/usr/bin/env python3
"""Prove src/zypermark.h is what tools/zyperdef.py says.

zyperdef.py is the definition and the generators import it, but
src/zyperdraw.c cannot: it is C. So tools/genzyper.py writes the head out as
a vertex table, src/zypermark.h, and that file is a copy that can go stale
the moment someone edits the definition and forgets to rerun the generator.
The failure is silent otherwise: the README and the boot animation would
show one Zyper and the shutdown animation another.

So the check regenerates the header in memory and compares. No PIL needed,
which is why zyperdef.py imports only the standard library: this runs from
`make check` on a machine with nothing installed.

The shelf's heart is still hand-copied in C, from the heart of
tools/markdef.py, which is gone with the marks it drew. This compared
UI_CYAN_* in src/ui.h and MENU_ACCENT in src/main.c with its CYAN as well,
until cyan left the system with DESIGN.md (3.3): the launcher's colors are
the theme's roles now, read from src/design.h, and there is no cyan in them
to compare.

What it compares instead is the one color the mark and the launcher share:
Zyper is drawn on the ground, his eyes and nose knocked out in it, so
zyperdef.BG is Salt & Pepper's **ground** in tools/designdef.py, the table
src/design.h is written from. The boot splash and the power-off screen are
his ground and not a theme's; a ground that moved in the tokens without him
would put a seam around the mark at the moment the shelf comes up.

Exits non-zero and prints what drifted. No output when everything agrees.
"""
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import zyperdef as z  # noqa: E402
import designdef as d  # noqa: E402

DRAW = os.path.join(ROOT, "src", "zyperdraw.c")
HDR = os.path.join(ROOT, "src", "zypermark.h")
RIG = os.path.join(ROOT, "src", "zyperrig.c")


def defines(text):
    out = {}
    for line in text.splitlines():
        p = line.split()
        if len(p) >= 3 and p[0] == "#define":
            out[p[1]] = float(p[2].rstrip("f"))
    return out


def verts(text):
    return [(float(x), float(y))
            for x, y in re.findall(r"\{(-?[0-9.]+)f,(-?[0-9.]+)f,", text)]


def brow(d):
    """head()'s brow at a tilt, when it is not given, is the header's brow
    lifted by zyper_brow_lift() - ZYPER_BROW_LIFT * tilt / ZYPER_TILT_DEG,
    the rule the menu draws him by - and turned by that tilt."""
    base = z.head(tilt=0.0, tongue=1.0, brow_lift=z.BROW_LIFT)
    high = z._moved(base, z.head(0.0, 1.0, 0.0))
    bad = []
    for t in (0.0, 5.0, 10.0, 18.0, 22.0):
        lift = d["ZYPER_BROW_LIFT"] * t / d["ZYPER_TILT_DEG"]
        moved = [(col, [(x, y + (z.BROW_LIFT - lift if i in high else 0.0)) for x, y in pts])
                 for i, (col, pts) in enumerate(base)]
        got = z.rotate(moved, t) if t else moved
        want = z.head(tilt=t)
        if any(abs(p[0] - q[0]) > 1e-6 or abs(p[1] - q[1]) > 1e-6
               for (_, a), (_, b) in zip(got, want) for p, q in zip(a, b)):
            bad.append("tools/zyperdef.py's head() lifts the brow at tilt %g other than "
                       "zyper_brow_lift() in src/zypermark.h does" % t)
    return bad


def moves(text):
    """The header's vertices moved as draw_zyper moves them - the high brow
    to its lift, the eyes shut toward their line, the ears about their folds,
    the tongue toward its line - are the triangles of head() in that pose.
    The marks are what say which vertices are which, and a mark one vertex
    off would swing a sliver of the cap with the ear, which nothing else
    would catch until it was seen on the panel."""
    d, v = defines(text), verts(text)
    names = ("ZYPER_BROW_LIFT", "ZYPER_EYES_FIRST", "ZYPER_EYES_END", "ZYPER_EYE_Y",
             "ZYPER_EYE_SHUT", "ZYPER_EARS_FIRST", "ZYPER_EARS_END", "ZYPER_EAR_FOLD_X",
             "ZYPER_EAR_FOLD_Y", "ZYPER_BROW_FIRST", "ZYPER_BROW_END", "ZYPER_TONGUE_FIRST",
             "ZYPER_TONGUE_Y0", "ZYPER_MOUTH_FIRST", "ZYPER_MOUTH_END", "ZYPER_MOUTH_Y",
             "ZYPER_MOUTH_SHUT")
    missing = [n for n in names if n not in d]
    if missing:
        return ["src/zypermark.h does not mark %s: run python3 tools/genzyper.py"
                % ", ".join(missing)]
    bad = []
    for lift, swing, blink, tongue, mouth in ((0.0, 0.0, 0.0, 1.0, 0.0), (14.4, 0.0, 1.0, 0.05, 1.0),
                                              (3.0, 7.0, 0.5, 0.82, 0.0), (8.0, -8.0, 0.25, 0.5, 0.4)):
        want = [p for col, pts in z.head(tilt=0.0, tongue=tongue, brow_lift=lift,
                                         ear_swing=swing, blink=blink, mouth=mouth)
                for tri in z.triangulate(pts) for p in tri]
        got = []
        c, s = math.cos(math.radians(swing)), math.sin(math.radians(swing))
        for i, (x, y) in enumerate(v):
            if d["ZYPER_BROW_FIRST"] <= i < d["ZYPER_BROW_END"]:
                y += d["ZYPER_BROW_LIFT"] - lift
            if d["ZYPER_EYES_FIRST"] <= i < d["ZYPER_EYES_END"]:
                y = d["ZYPER_EYE_Y"] + (y - d["ZYPER_EYE_Y"]) * (1 - d["ZYPER_EYE_SHUT"] * blink)
            if d["ZYPER_MOUTH_FIRST"] <= i < d["ZYPER_MOUTH_END"]:
                y = d["ZYPER_MOUTH_Y"] + (y - d["ZYPER_MOUTH_Y"]) * (1 - d["ZYPER_MOUTH_SHUT"] * mouth)
            if d["ZYPER_EARS_FIRST"] <= i < d["ZYPER_EARS_END"]:
                # each ear about its own fold, the far one the mirror way
                k = 1.0 if x < 0 else -1.0
                fx, fy = d["ZYPER_EAR_FOLD_X"] * k, d["ZYPER_EAR_FOLD_Y"]
                x, y = (fx + (x - fx) * c - (y - fy) * s * k,
                        fy + (x - fx) * s * k + (y - fy) * c)
            if i >= d["ZYPER_TONGUE_FIRST"]:
                y = d["ZYPER_TONGUE_Y0"] + (y - d["ZYPER_TONGUE_Y0"]) * tongue
            got.append((x, y))
        far = [i for i, (p, q) in enumerate(zip(got, want))
               if abs(p[0] - q[0]) > 0.02 or abs(p[1] - q[1]) > 0.02]
        if len(got) != len(want) or far:
            bad.append("src/zypermark.h's marks do not move him as head(brow_lift=%g, "
                       "ear_swing=%g, blink=%g, tongue=%g, mouth=%g) does: vertex %s"
                       % (lift, swing, blink, tongue, mouth, far[0] if far else "count"))
    return bad


def main():
    bad = []

    want = z.c_header()
    try:
        have = open(HDR).read()
    except OSError:
        have = None
    if have != want:
        bad.append("src/zypermark.h is not what tools/zyperdef.py draws: "
                   "run python3 tools/genzyper.py")

    ground = {role: dict(zip(d.THEMES, v)) for role, *v in d.COLORS}["ground"]
    if tuple(z.BG) != d.rgb(ground["dark"]):
        bad.append("zyperdef.BG is %s but Salt & Pepper's ground in "
                   "tools/designdef.py is %s: they are one color"
                   % ("#%02x%02x%02x" % tuple(z.BG), "#" + ground["dark"]))

    if have is not None:
        bad += moves(have)
        bad += brow(defines(have))

    # The menu's rig rests at the definition's tilt; it cannot include the
    # header, which holds his vertices
    rig = open(RIG).read()
    m = re.search(r"#define REST_TILT\s+(-?[0-9.]+)f", rig)
    if not m or float(m.group(1)) != z.TILT:
        bad.append("src/zyperrig.c's REST_TILT is %s but tools/zyperdef.py's TILT is %g: "
                   "they are one tilt" % (m.group(1) if m else "missing", z.TILT))

    src = open(DRAW).read()
    if "draw_zyper" not in src:
        bad.append("src/zyperdraw.c does not draw Zyper (no draw_zyper)")

    for b in bad:
        print("checkmark:", b, file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
