#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The word in plastron's splash frames, in its theme's colors (make check-bootword).

The boot animation ends on the lockup and loops on it for the whole boot, and
on a first start or after a system update the copy's line and bar sit under
it for most of a minute ("Updating ZyperOS on this card", DESIGN.md 11.20).
Beard's frames once kept the Z in Salt & Pepper's lime, which is 1.0:1 on
Beard's ground: the Z all but vanished (Pixel 2, 1.1.1-test.3, 2026-11-26),
though tools/genboot.py had long drawn it in accent-ink, lime's dark green,
as the README's light lockup is. frames-light.bin had not been made again
since.

So this reads every splash frames file the images carry (the Pixel 2's both
themes, the R36S's, the TrimUI Brick's and the Smart Pro's, whose splash-fb.c
plays Salt & Pepper's only), takes the last frame, the lockup at rest, and at
the middle of each letter's first stroke checks the color is the one
tools/zyperdef.word_colors gives that theme, and that it keeps TEXT_ON's
4.5:1 on the frame's own ground (tools/designdef.py: accent-ink on ground,
which is what the Z is in Beard). Standard library only: the frames are zlib
bands (plastron/scripts/make-splash-frames.py).
"""
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import designdef as D   # noqa: E402
import zyperdef as Z    # noqa: E402

PKG = os.path.join(ROOT, "plastron", "buildroot", "package", "splash")

# genboot.py's lockup, in the launcher's 1024x768
W, H = 1024, 768
WORD_W, WORD_BASE = 560.0, 0.79 * H

# Each file, its theme, and how the launcher's units land on its pixels
FILES = (
    ("frames.bin", "dark", "pixel2"),
    ("frames-light.bin", "light", "pixel2"),
    ("r36s/frames.bin", "dark", "r36s"),
    ("r36s/frames-light.bin", "light", "r36s"),
    ("frames-brick.bin", "dark", "brick"),
    ("smartpro/frames.bin", "dark", "smartpro"),
)

NEED = 4.5          # designdef.TEXT_ON: accent-ink (and text) on ground
CLOSE = 16          # how far a channel may be from word_colors' (LANCZOS, scaling)


def last_frame(path, want):
    """The rows `want` of the last frame, {row: BGRX bytes}, and the width:
    "TSPY" (the Pixel 2's, the R36S's) and "TSPZ" (the Brick's, the Smart
    Pro's) share the bands. Only those rows are unfiltered: every row of
    every frame took 12 s in Python."""
    data = open(path, "rb").read()
    magic, w, h, count = struct.unpack_from("<4sHHH", data, 0)
    if magic == b"TSPY":
        table = 16
    elif magic == b"TSPZ":
        table = 12
    else:
        sys.exit("bootword-check: %s is no splash frames file" % path)
    offsets = struct.unpack_from("<%dI" % count, data, table)
    stride = w * 4
    rows_of = {}
    for at in offsets:
        first, rows, length = struct.unpack_from("<HHI", data, at)
        hit = [y for y in want if first <= y < first + rows]
        if not hit:
            continue
        band = zlib.decompress(data[at + 8:at + 8 + length])
        for y in hit:
            row = bytearray(band[(y - first) * stride:(y - first + 1) * stride])
            for i in range(4, stride):
                row[i] = (row[i] + row[i - 4]) & 0xff
            rows_of[y] = bytes(row)
    return w, rows_of


def to_pixel(device, x, y):
    """The launcher's (x, y) on the file's pixels"""
    if device == "pixel2":       # 640x480, turned counter-clockwise onto 480x640
        lx, ly = x * 0.625, y * 0.625
        return ly, 639 - lx
    if device == "r36s":         # the Pixel 2's, turned back
        return x * 0.625, y * 0.625
    if device == "smartpro":     # 960x720 in the middle of 1280x720
        return 160 + x * 0.9375, y * 0.9375
    return x, y                  # the Brick: the launcher's own 1024x768


def letter_points():
    """(char, (x, y)) at the middle of each letter's first stroke"""
    letters, ww = Z.word()
    k = WORD_W / ww
    x0 = W / 2.0 - WORD_W / 2.0
    out = []
    for _, ch, dx, strokes in letters:
        s = strokes[0]
        if s[0] == "l":
            (ax, ay), (bx, by) = s[1][0], s[1][1]
            px, py = (ax + bx) / 2.0, (ay + by) / 2.0
        else:
            _, cx, cy, rx, ry, a0, a1 = s
            px, py = Z.arc_point(cx, cy, rx, ry, (a0 + a1) / 2.0)
        out.append((ch, (x0 + (px + dx) * k, WORD_BASE + py * k)))
    return out


def around(x, y):
    """The 3x3 about (x, y)"""
    x, y = int(round(x)), int(round(y))
    return [(xx, yy) for yy in (y - 1, y, y + 1) for xx in (x - 1, x, x + 1)]


def pixel(rows, x, y):
    """The mean of the 3x3 about (x, y), RGB"""
    acc = [0, 0, 0]
    for xx, yy in around(x, y):
        b, g, r = rows[yy][xx * 4:xx * 4 + 3]
        acc[0] += r; acc[1] += g; acc[2] += b
    return tuple(int(round(v / 9.0)) for v in acc)


def read(name, device, points):
    """The file's ground (its top left pixel) and each point's color"""
    at = [to_pixel(device, x, y) for _, (x, y) in points]
    w, rows = last_frame(os.path.join(PKG, name), {0} | {yy for x, y in at for _, yy in around(x, y)})
    b, g, r = rows[0][0:3]
    return (r, g, b), [pixel(rows, x, y) for x, y in at]


def hexof(c):
    return "%02x%02x%02x" % c


def main():
    fails = []
    points = letter_points()
    beard_z = None
    for name, theme, device in FILES:
        ground, got = read(name, device, points)
        want = Z.word_colors(on_dark=(theme == "dark"))
        for i, (ch, _) in enumerate(points):
            c = D.ratio(hexof(got[i]), hexof(ground))
            if name == "frames-light.bin" and ch == "Z":
                beard_z = c
            if max(abs(a - b) for a, b in zip(got[i], want[i])) > CLOSE:
                fails.append("%s: the %s is #%s, not #%s (%s's, tools/zyperdef.word_colors);"
                             " run tools/genboot.py%s and plastron/scripts/make-splash-frames.py again"
                             % (name, ch, hexof(got[i]), hexof(want[i]), theme,
                                " --light" if theme == "light" else ""))
            if c < NEED:
                fails.append("%s: the %s is %.2f:1 on the ground #%s, owed %.1f:1"
                             % (name, ch, c, hexof(ground), NEED))
    if fails:
        print("check-bootword:\n  " + "\n  ".join(fails))
        sys.exit(1)
    print("check-bootword: the word in %d splash frames files is its theme's;"
          " Beard's Z %.1f:1 on its ground" % (len(FILES), beard_z))


if __name__ == "__main__":
    main()
