#!/usr/bin/env python3
"""Draw the systems the Pixel UI pack lacks, from real hardware photos.

Rebuild: systemart-draw.py          (writes res/system-art/source/<KEY>.gif)
Verify:  systemart-draw.py --check  (the GIFs are what the maps make)

Each res/system-art/drawn/<KEY>.txt holds one or more parts (a console and
its pad, a board and its cartridge) as pixel maps reduced from a real photo
on Wikimedia Commons: area-averaged to the part's size and cut to a few
colors (tools/systemart-reduce.py makes one from a photo); every part names
its photo, author and license. I rejected invented hardware (2026-11-04),
so nothing here is drawn from memory: the maps are the photos, and this file
only styles them like the pack. The same goes for the File Transfer page's
folder icons (ROMS, BIOS, SAVES, ...), which are not shelves.

The style is the pack's (BenJelter's console icons, YOjeremyRaps' motion):
a 64 x 64 canvas, black hardware lifted onto the pack's slate or neutral
ramps (Genesis and Neo Geo use the same), colored plastic kept with a little
more contrast, a lone pixel taking its neighbors' color, a one-pixel black
outline per part, the pad in front of the console, and two frames of 400 ms
in which the console rises one pixel while the pad stays, as the pack's
console sets move. Python 3.9+ and Pillow 9.1+.
"""
import argparse
import colorsys
import io
from pathlib import Path
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DRAWN = ROOT / "res/system-art/drawn"
SOURCE = ROOT / "res/system-art/source"
SIZE, SCALE, MS = 64, 10, 400
CHARS = "0123456789abcdefghijklmnopqrstuv"

# Tone ramps, dark to light. slate and neutral are the pack's own black
# plastics (its Genesis/Sega CD and its Neo Geo); navy is the NAOMI case's
# blue on the same steps; gray is a silver body's.
RAMPS = {
    "slate": "1b1e22 25292e 30343a 3b4047 474d55 565e68 6c7480 8a929e b3b9c4",
    "neutral": "121212 1b1b1b 242424 2e2e2e 3a3a3a 4a4d52 62666d 858a92 a9adb3",
    "navy": "121725 1a2131 232c40 2c374f 36435f 425170 536387 6c7ea0 93a3bf",
    "gray": "3e3e3e 575757 6e6e6e 858585 9a9a9a adadad c0c0c0 d6d6d6 ececec",
}

# Per system: its parts back to front, each (part, x, y, rises, style).
# style: ("ramp", name, gamma) maps tones onto a ramp and keeps saturated
# colors; ("keep", contrast, saturation) keeps the photo's colors. strip
# names map characters to clear where they touch the map's edge (the
# photo's backdrop the reduction kept).
RECIPES = {
    "NEOCD": [("console", 18, 14, True, ("ramp", "slate", 0.85), ""),
              ("pad", 3, 34, False, ("ramp", "slate", 0.8), "")],
    "3DO": [("console", 16, 12, True, ("ramp", "slate", 1.15), ""),
            ("pad", 2, 33, False, ("ramp", "slate", 0.9), "")],
    "VECTREX": [("console", 20, 4, True, ("ramp", "neutral", 0.9), ""),
                ("pad", 4, 44, False, ("ramp", "neutral", 0.8), "")],
    "GW": [("body", 4, 10, True, ("keep", 1.2, 1.2), "")],
    "MSX2": [("body", 9, 18, True, ("ramp", "slate", 0.9), "")],
    "AW": [("board", 17, 6, True, ("keep", 1.15, 1.1), ""),
           ("cart", 3, 38, False, ("keep", 1.2, 1.0), "fed")],
    "NAOMI": [("board", 4, 8, True, ("ramp", "navy", 1.0), ""),
              ("disc", 38, 38, False, ("keep", 1.15, 1.2), "")],
    "MUSE": [("body", 8, 12, True, ("ramp", "gray", 1.0), "fedc")],
    "J2ME": [("body", 8, 20, True, ("ramp", "slate", 0.9), "")],
    # Not shelves: the File Transfer page shows these beside the card's
    # folders (tools/systemart-reduce.py made their maps from the photos).
    "ROMS": [("body", 10, 11, True, ("ramp", "slate", 0.9), "")],
    "BIOS": [("body", 4, 21, True, ("ramp", "slate", 0.75), "")],
    "SAVES": [("body", 9, 12, True, ("keep", 1.2, 1.0), "")],
    "STATES": [("body", 9, 9, True, ("keep", 1.15, 1.1), "b")],
    "BGM": [("body", 5, 19, True, ("keep", 1.15, 1.1), "")],
    "VIDEOS": [("body", 4, 17, True, ("ramp", "slate", 0.9), "")],
    "BOOKS": [("body", 17, 7, True, ("keep", 1.15, 1.1), "")],
    "FOLDER": [("body", 5, 15, True, ("keep", 1.15, 1.1), "")],
}


def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def load(key):
    parts, cur = {}, None
    for line in (DRAWN / f"{key}.txt").read_text().splitlines():
        if line.startswith("## ") or not line:
            continue
        if line.startswith("# "):
            cur = parts[line.split()[1]] = {"pal": None, "rows": []}
        elif cur["pal"] is None:
            cur["pal"] = [rgb(h) for h in line.split()]
        else:
            cur["rows"].append(line)
    return parts


def strip(part, chars):
    """Clear backdrop characters reachable from the map's edge."""
    if not chars:
        return part
    rows = [list(r) for r in part["rows"]]
    h, w = len(rows), max(map(len, rows))
    for r in rows:
        r += ["."] * (w - len(r))
    todo = [(x, y) for x in range(w) for y in (0, h - 1)] + [(x, y) for y in range(h) for x in (0, w - 1)]
    seen = set()
    while todo:
        x, y = todo.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        if rows[y][x] != "." and rows[y][x] not in chars:
            continue
        rows[y][x] = "."
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return {"pal": part["pal"], "rows": ["".join(r) for r in rows]}


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def styled(part, style):
    pts = {(x, y): part["pal"][CHARS.index(c)]
           for y, row in enumerate(part["rows"]) for x, c in enumerate(row) if c != "."}
    out = {}
    if style[0] == "keep":
        _, contrast, sat = style
        for p, c in pts.items():
            h, s, v = colorsys.rgb_to_hsv(*(k / 255 for k in c))
            s, v = min(1, s * sat), min(1, max(0, (v - 0.5) * contrast + 0.5))
            out[p] = tuple(round(k * 255) for k in colorsys.hsv_to_rgb(h, s, v))
        return out
    _, ramp, gamma = style
    tones = [rgb(h) for h in RAMPS[ramp].split()]
    ls = sorted(lum(c) for c in pts.values())
    lo, hi = ls[len(ls) * 3 // 100], ls[min(len(ls) - 1, len(ls) * 97 // 100)]
    for p, c in pts.items():
        h, s, v = colorsys.rgb_to_hsv(*(k / 255 for k in c))
        if s >= 0.32 and v > 0.2:  # a colored button or logo stays, brighter
            out[p] = tuple(round(k * 255) for k in colorsys.hsv_to_rgb(h, max(s, 0.55), max(v, 0.62)))
            continue
        t = 0.0 if hi <= lo else min(1.0, max(0.0, (lum(c) - lo) / (hi - lo)))
        out[p] = tones[round(t ** gamma * (len(tones) - 1))]
    return out


def tidy(pts):
    """A pixel none of whose eight neighbors shares its color, with six or
    more of them agreeing, takes theirs: photo noise, not detail."""
    out = dict(pts)
    for (x, y), c in pts.items():
        near = [pts.get((x + dx, y + dy)) for dx in (-1, 0, 1) for dy in (-1, 0, 1) if dx or dy]
        if None in near or c in near:
            continue
        best = max(set(near), key=near.count)
        if near.count(best) >= 6:
            out[(x, y)] = best
    return out


def frame(layers, rise):
    canvas = {}
    for pts, ox, oy, rises in layers:
        dy = -1 if rise and rises else 0
        layer = {(x + ox, y + oy + dy): c for (x, y), c in pts.items()}
        for (x, y) in list(layer):
            for q in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
                if q not in layer and 0 <= q[0] < SIZE and 0 <= q[1] < SIZE:
                    layer[q] = (0, 0, 0)
        canvas.update(layer)
    im = Image.new("RGBA", (SIZE, SIZE))
    for (x, y), c in canvas.items():
        if not (0 <= x < SIZE and 0 <= y < SIZE):
            raise SystemExit(f"a part leaves the {SIZE}px canvas at {x},{y}")
        im.putpixel((x, y), c + (255,))
    return im


def draw(key):
    parts = load(key)
    layers = [(tidy(styled(strip(parts[name], chars), style)), x, y, rises)
              for name, x, y, rises, style, chars in RECIPES[key]]
    return [frame(layers, False), frame(layers, True)]


def gif(frames):
    colors = sorted({c[:3] for f in frames for c in f.getdata() if c[3]})
    if len(colors) > 255:
        raise SystemExit("too many colors for one GIF palette")
    index = {c: i + 1 for i, c in enumerate(colors)}
    palette = [0, 0, 0] + [v for c in colors for v in c]
    out = []
    for f in frames:
        p = Image.new("P", f.size, 0)
        p.putpalette(palette + [0] * (768 - len(palette)))
        p.putdata([index[c[:3]] if c[3] else 0 for c in f.getdata()])
        out.append(p.resize((SIZE * SCALE, SIZE * SCALE), Image.Resampling.NEAREST))
    stream = io.BytesIO()
    out[0].save(stream, format="GIF", save_all=True, append_images=out[1:], duration=MS,
                loop=0, transparency=0, disposal=2, optimize=False)
    return stream.getvalue()


def same(path, frames):
    try:
        with Image.open(path) as im:
            for i, f in enumerate(frames):
                im.seek(i)
                got = im.convert("RGBA").resize((SIZE, SIZE), Image.Resampling.NEAREST)
                if got.tobytes() != f.tobytes() or im.info.get("duration") != MS:
                    return False
            return im.n_frames == len(frames)
    except (OSError, EOFError):
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    stale = []
    for key in RECIPES:
        frames = draw(key)
        path = SOURCE / f"{key}.gif"
        if args.check:
            if not same(path, frames):
                stale.append(key)
        else:
            path.write_bytes(gif(frames))
    if stale:
        print("systemart-draw: stale or missing: " + ", ".join(stale), file=sys.stderr)
        return 1
    print(f"systemart-draw: {len(RECIPES)} systems {'verified' if args.check else 'drawn'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
