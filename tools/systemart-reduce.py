#!/usr/bin/env python3
"""Reduce one real photo to one part of a pixel map in res/system-art/drawn.

Run: systemart-reduce.py KEY PART PHOTO --size 48x40 --url URL \\
         --author NAME --license LICENSE [--crop X0,Y0,X1,Y1] [--rotate DEG] \\
         [--backdrop RRGGBB | --vignette] [--tolerance 60] [--colors 16]
     (writes PART into res/system-art/drawn/<KEY>.txt, replacing a part of
     the same name; then systemart-draw.py styles the maps into GIFs)

PHOTO is a real photo from Wikimedia Commons under public domain, CC0, CC BY
or CC BY-SA; URL is its file page. A system with no hardware (a fantasy
console, as the pack draws PICO-8 by its logo) takes its own logo from
Commons instead, under MIT where that is the project's license (TIC-80's). The part's "## photo:" line names it, its
author and its license, and its "## reduced:" line the options that made it.
I rejected invented hardware (2026-11-04), so the map is the photo and
nothing else: cropped (fractions of the photo's width and height, so any copy
of the same file gives the same map) and turned to the object, its backdrop
made transparent (the photo's own alpha, or the backdrop flooded in from the
edges: pixels near one color, or with --vignette near the blend of the four
edges, for a backdrop that darkens toward a corner), trimmed to the object,
area-averaged (box filter) to fit inside the part's size with its aspect
kept, and cut to at most 16 colors by median cut, listed dark to light; "."
is transparent. The photo itself stays outside the repository.
Python 3.9+ and Pillow 9.1+.
"""
import argparse
from pathlib import Path
import re
import sys

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
DRAWN = ROOT / "res/system-art/drawn"
CHARS = "0123456789abcdefghijklmnopqrstuv"
WORK = 16  # the backdrop is found at this many photo pixels per map pixel
LICENSE = re.compile(r"Public domain|CC0( 1\.0)?|CC BY(-SA)? \d\.\d|MIT")
URL = re.compile(r"https://commons\.wikimedia\.org/wiki/File:\S+")


def numbers(text, count, kind, sep=","):
    try:
        values = [kind(v) for v in text.split(sep)]
    except ValueError:
        values = []
    if len(values) != count:
        raise argparse.ArgumentTypeError(f"expected {count} values: {text!r}")
    return values


def data(im):
    """Pixels in order (Pillow 12 renamed getdata)."""
    return list(getattr(im, "get_flattened_data", im.getdata)())


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def border(w, h):
    return [(x, y) for x in range(w) for y in (0, h - 1)] + \
           [(x, y) for y in range(1, h - 1) for x in (0, w - 1)]


def far(c, d):
    return abs(c[0] - d[0]) + abs(c[1] - d[1]) + abs(c[2] - d[2])


def median(colors):
    return tuple(sorted(c[i] for c in colors)[len(colors) // 2] for i in range(3))


def smooth(colors, n=7):
    """A running median along one edge of the photo: noise and specks out."""
    return [median(colors[max(0, k - n):k + n + 1]) for k in range(len(colors))]


def coons(top, bottom, left, right):
    """The color at (x, y) that the four edges blend to (a Coons patch)."""
    w, h = len(top), len(left)
    c00, c10, c01, c11 = top[0], top[-1], bottom[0], bottom[-1]

    def at(x, y):
        u, v = x / max(1, w - 1), y / max(1, h - 1)
        return tuple((1 - u) * left[y][i] + u * right[y][i] + (1 - v) * top[x][i] + v * bottom[x][i]
                     - ((1 - u) * (1 - v) * c00[i] + u * (1 - v) * c10[i]
                        + (1 - u) * v * c01[i] + u * v * c11[i]) for i in range(3))
    return at


def backdrop(im, ref, tolerance, vignette=False):
    """Alpha 0 where the backdrop reaches from the photo's edge, 255 elsewhere.

    A pixel is backdrop if it is transparent already or within tolerance of
    the backdrop color: one color (given, or the edge's median), or with
    vignette the color the four edges blend to at that pixel (a Coons patch
    over them), for a photo whose backdrop darkens toward a corner."""
    w, h = im.size
    px = im.load()
    if vignette:
        at = coons(smooth([px[x, 0] for x in range(w)]), smooth([px[x, h - 1] for x in range(w)]),
                   smooth([px[0, y] for y in range(h)]), smooth([px[w - 1, y] for y in range(h)]))
    else:
        if ref is None:  # the edge's median color
            edge = [px[p] for p in border(w, h) if px[p][3]]
            if not edge:
                raise SystemExit("the photo's edge is transparent; give --backdrop")
            ref = median(edge)
        at = lambda x, y: ref  # noqa: E731
    mask = Image.new("L", (w, h), 255)
    out = mask.load()
    done = bytearray(w * h)
    todo = []
    for x, y in border(w, h):
        c = px[x, y]
        if not done[y * w + x] and (not c[3] or far(c, at(x, y)) <= tolerance):
            done[y * w + x] = 1
            todo.append((x, y))
    while todo:
        x, y = todo.pop()
        out[x, y] = 0
        for q in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if not (0 <= q[0] < w and 0 <= q[1] < h) or done[q[1] * w + q[0]]:
                continue
            d = px[q]
            if not d[3] or far(d, at(*q)) <= tolerance:
                done[q[1] * w + q[0]] = 1
                todo.append(q)
    return mask


def reduce(photo, size, crop=None, rotate=0.0, ref=None, tolerance=60, vignette=False, colors=16,
           alpha=128):
    """The part as (palette, rows) from one photo."""
    with Image.open(photo) as im:
        own_alpha = "A" in im.getbands() or "transparency" in im.info
        im = im.convert("RGBA")
    if crop:
        w, h = im.size
        x0, y0, x1, y1 = crop
        if not 0 <= x0 < x1 <= 1 or not 0 <= y0 < y1 <= 1:
            raise SystemExit("--crop takes fractions of the photo: 0 <= x0 < x1 <= 1, 0 <= y0 < y1 <= 1")
        im = im.crop((round(x0 * w), round(y0 * h), round(x1 * w), round(y1 * h)))
    if rotate:
        im = im.rotate(rotate, Image.Resampling.BICUBIC, expand=True)
    # Shrink first (a box filter on premultiplied alpha), so the flood is quick.
    w, h = im.size
    k = max(1.0, min(w / (size[0] * WORK), h / (size[1] * WORK)))
    im = im.resize((max(1, round(w / k)), max(1, round(h / k))), Image.Resampling.BOX)
    if not own_alpha or ref is not None or vignette:
        im.putalpha(ImageChops.darker(im.getchannel("A"), backdrop(im, ref, tolerance, vignette)))
    bounds = im.getchannel("A").point(lambda v: 255 if v >= alpha else 0).getbbox()
    if bounds is None:
        raise SystemExit("nothing left once the backdrop is clear; lower --tolerance")
    im = im.crop(bounds)
    w, h = im.size
    scale = min(size[0] / w, size[1] / h)
    small = im.resize((max(1, round(w * scale)), max(1, round(h * scale))), Image.Resampling.BOX)
    pixels = data(small)
    solid = [p[:3] for p in pixels if p[3] >= alpha]
    strip = Image.new("RGB", (len(solid), 1))
    strip.putdata(solid)
    q = strip.quantize(colors=min(colors, len(CHARS)), method=Image.Quantize.MEDIANCUT,
                       dither=Image.Dither.NONE)
    flat = q.getpalette()
    picks = data(q)
    used = sorted(set(picks), key=lambda i: (lum(flat[3 * i:3 * i + 3]), i))
    palette = [tuple(flat[3 * i:3 * i + 3]) for i in used]
    char = {i: CHARS[n] for n, i in enumerate(used)}
    picked = iter(picks)
    cells = [char[next(picked)] if p[3] >= alpha else "." for p in pixels]
    sw = small.size[0]
    return palette, ["".join(cells[y:y + sw]) for y in range(0, len(cells), sw)]


def part_text(name, palette, rows, url, author, license, how):
    return "\n".join([f"# {name} {len(rows[0])}x{len(rows)}",
                      f"## photo: {url} | {author} | {license}",
                      f"## reduced: {how}",
                      " ".join("%02x%02x%02x" % c for c in palette)] + rows)


def options(args):
    """The options that made the map, so it can be made again."""
    how = [f"--size {args.size[0]}x{args.size[1]}"]
    if args.crop:
        how.append("--crop " + ",".join(f"{v:g}" for v in args.crop))
    if args.rotate:
        how.append(f"--rotate {args.rotate:g}")
    if args.backdrop:
        how.append("--backdrop " + "".join("%02x" % v for v in args.backdrop))
    if args.vignette:
        how.append("--vignette")
    how.append(f"--tolerance {args.tolerance}")
    if args.colors != 16:
        how.append(f"--colors {args.colors}")
    return " ".join(how)


def write(path, key, name, text):
    """Replace the part called name in path, or add it at the end."""
    head = [f"## {key}: pixel maps reduced from real photos (tools/systemart-reduce.py).",
            '## Each part: area-averaged to its size, median-cut colors; "." is transparent.']
    parts = []
    if path.exists():
        head = []
        for line in path.read_text().splitlines():
            if line.startswith("# "):
                parts.append([line.split()[1], [line]])
            elif parts:
                parts[-1][1].append(line)
            else:
                head.append(line)
    blocks = [(n, "\n".join(lines)) for n, lines in parts]
    if any(n == name for n, _ in blocks):
        blocks = [(n, text if n == name else b) for n, b in blocks]
    else:
        blocks.append((name, text))
    path.write_text("\n".join(head + [b for _, b in blocks]) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("key", help="the system or folder key, e.g. BIOS")
    parser.add_argument("part", help="the part's name, e.g. body")
    parser.add_argument("photo", type=Path)
    parser.add_argument("--size", required=True, type=lambda t: numbers(t, 2, int, "x"),
                        help="the box the part fits inside, WxH")
    parser.add_argument("--url", required=True, help="the photo's Commons file page")
    parser.add_argument("--author", required=True)
    parser.add_argument("--license", required=True)
    parser.add_argument("--crop", type=lambda t: numbers(t, 4, float),
                        help="X0,Y0,X1,Y1 as fractions of the photo's width and height")
    parser.add_argument("--rotate", type=float, default=0.0, help="degrees, counterclockwise")
    parser.add_argument("--backdrop", type=lambda t: tuple(int(t[i:i + 2], 16) for i in (0, 2, 4)),
                        help="RRGGBB to flood from the edges (default: the edge's median color; "
                             "a photo with its own alpha is flooded only with this or --vignette)")
    parser.add_argument("--tolerance", type=int, default=60,
                        help="largest |dR|+|dG|+|dB| from the backdrop color that floods")
    parser.add_argument("--vignette", action="store_true",
                        help="compare each pixel with the blend of the photo's four edges, "
                             "not one color (a backdrop that darkens toward a corner)")
    parser.add_argument("--colors", type=int, default=16, choices=range(2, 17), metavar="2..16")
    parser.add_argument("--out", type=Path, help="the map file (default: res/system-art/drawn/KEY.txt)")
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Z0-9_]{1,32}", args.key) or not re.fullmatch(r"[a-z]+", args.part):
        parser.error("KEY is A-Z, 0-9 and _; PART is a-z")
    if not LICENSE.fullmatch(args.license):
        parser.error("the photo must be Public domain, CC0, CC BY x.y, CC BY-SA x.y or MIT")
    if not URL.fullmatch(args.url) or "|" in args.author:
        parser.error("--url is a Commons file page; --author has no |")
    if args.backdrop and args.vignette:
        parser.error("--backdrop and --vignette do not go together")
    if min(args.size) < 1 or max(args.size) > 64:
        parser.error("--size is at most 64x64")
    palette, rows = reduce(args.photo, args.size, args.crop, args.rotate, args.backdrop,
                           args.tolerance, args.vignette, args.colors)
    text = part_text(args.part, palette, rows, args.url, args.author, args.license, options(args))
    out = args.out or DRAWN / f"{args.key}.txt"
    write(out, args.key, args.part, text)
    print(f"systemart-reduce: {args.key} {args.part} {len(rows[0])}x{len(rows)}, "
          f"{len(palette)} colors -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
