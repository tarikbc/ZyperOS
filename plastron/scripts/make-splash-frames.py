#!/usr/bin/env python3
"""
Turns ZyperOS's boot animation, its flyby, into the frames the splash plays.

Runs on the Mac, not in the build: it needs FFmpeg, and the result is small
enough to keep in the repository (buildroot/package/splash/frames.bin), so
the image build never has to decode video. Rerun it when the animation
changes:

    scripts/make-splash-frames.py [path/to/zyperos-fly.mp4]
    scripts/make-splash-frames.py --brick [path/to/zyperos-fly.mp4]

The boot animation is ZyperOS's res/boot/zyperos-fly.mp4, from its
tools/genboot.py, at 60 fps: the name from the first frame, and Zyper. Without --brick the frames are the Pixel 2's
(frames.bin, for splash.c); --brick makes the TrimUI Brick's (frames-brick.bin,
for splash-fb.c): its panel is the video's own 1024x768 landscape, so the
frames are only encoded.

The input may also be the folder of PNG frames genboot.py leaves in ZyperOS's
build/boot-frames: lossless, so the flat ground stays one run where the
video's compression noise broke it up (on the Brick, an earlier animation took
7.6 MB from the video and 5.1 from the frames).

The video is 1024x768 landscape. Each frame is scaled to 640x480 and turned
90 degrees counter-clockwise onto the panel's 480x640, the way ZyperOS draws,
so the splash only copies. An animation that plays once ends on the ZyperOS
logo held still; frames from the first one that already matches the last are
left out, and the splash keeps showing the last one it has.

A folder of frames may hold loop.txt, one line: the index of the first frame
of a loop. Frames before it are an intro, played once; from it to the last
they play over and over until ZyperOS is ready (its SIGUSR1), so the last
frame must lead into that first one as if it were the next. Every frame is
kept then. The Brick plays a looping animation once through and holds its
last frame.

Format, little-endian, the Pixel 2's (splash.c):
    "TSPY", u16 width, u16 height, u16 frames, u16 fps, u16 loop, u16 0
    u32 offset of each frame and of one more, from the start of the file
    per frame: its band, as in the Brick's below
loop is the index of the loop's first frame, or frames when there is no loop,
and the extra band, after the last frame's, goes from the last frame back to
the loop's first (no rows when there is no loop). Bands rather than whole
frames because a loop stays on screen for the whole boot: an idle Zyper
changes a little of the frame, and whole frames, as runs, made a second of it
5.7 MB ("TSPL", which this was until 2026-11-04).

--first-start DIR turns the first start's line and bar (ZyperOS's
res/boot/first-start/, from its tools/genfirststart.py; its DESIGN.md 11.20)
into first-start.bin, for splash.c: the splash lays them over the loop while
the image's init copies ZyperOS onto the card, and fills the bar.

    scripts/make-splash-frames.py --first-start [path/to/res/boot/first-start]

It writes first-start.bin from the folder's Salt & Pepper pictures and
first-start-light.bin from its light/, Beard's, each on its theme's ground:
English's. Each other language ZyperOS has (its res/strings, src/i18n.h) is
a folder of its code beside them, setup.png and update.png, and becomes
first-start-CODE.bin and first-start-light-CODE.bin, with the same bar.png:
the splash shows the one the card's language asks for (zylang, or the
copy's line; ZyperOS's DESIGN.md 11.20), English where it has none.

Format, little-endian:
    "TSPC", u16 x, u16 y, u16 w, u16 h: the panel's rect the pictures cover
    u16 bar_x0, bar_x1, bar_y0, bar_y1: the bar in it, the rect's own pixels
    u16 count (3), u16 0
    per picture: u32 length, then that many bytes of zlib: the rect's XRGB
    pixels, row by row, laid on the ground
The pictures are setup.png's line and empty bar, update.png's, and bar.png's
full bar. The bar fills left to right on the landscape screen, which is from
the bottom of its rows up on the panel (turned counter-clockwise): bar_y1
first.

--light writes the Pixel 2's frames-light.bin instead of frames.bin, from
ZyperOS's Beard frames (tools/genboot.py --light, build/boot-frames-light):
the splash plays the one the card's Appearance asks for (ZyperOS's DESIGN.md
11.21).

    scripts/make-splash-frames.py --light path/to/build/boot-frames-light

The Brick's (--brick) is "TSPZ" instead, for splash-fb.c: the same header and
offsets, then per frame only the band of rows that changed from the frame
before (frame 0 whole), as u16 first row, u16 rows, u32 length, and that
many bytes of zlib: the band's XRGB pixels, each byte less the same byte of
the pixel to its left (as PNG's Sub filter does), so the wordmark costs one
frame and the bars' smooth fade compresses. Runs alone made the flyby 7.8 MB:
the fade has no two pixels alike.
"""
import os
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_VIDEO = os.path.expanduser("~/Developer/ZyperOS/res/boot/zyperos-fly.mp4")
PACKAGE = os.path.join(HERE, "..", "buildroot", "package", "splash")
DEFAULT_FIRST_START = os.path.expanduser("~/Developer/ZyperOS/res/boot/first-start")
FPS = 60


def main():
    args = sys.argv[1:]
    if args and args[0] == "--first-start":
        first_start(args[1] if len(args) > 1 else DEFAULT_FIRST_START)
        return
    brick = "--brick" in args
    light = "--light" in args
    args = [a for a in args if a not in ("--brick", "--light")]
    video = args[0] if args else DEFAULT_VIDEO
    fps = FPS
    loop = None
    if os.path.isdir(video) and os.path.exists(os.path.join(video, "loop.txt")):
        with open(os.path.join(video, "loop.txt")) as f:
            loop = int(f.read().split()[0])
    if brick:
        W, H, OUT = 1024, 768, os.path.join(PACKAGE, "frames-brick.bin")
        vf = "fps=%d" % fps
    else:
        W, H, OUT = 480, 640, os.path.join(PACKAGE, "frames-light.bin" if light else "frames.bin")
        vf = "scale=640:480:flags=area,transpose=2,fps=%d" % fps
    source = ["-i", video]
    if os.path.isdir(video):
        source = ["-framerate", str(fps), "-start_number", "0",
                  "-i", os.path.join(video, "f%04d.png")]
    raw = subprocess.run(
        ["ffmpeg", "-v", "error"] + source + [
         "-vf", vf,
         "-f", "rawvideo", "-pix_fmt", "bgr0", "-"],
        check=True, stdout=subprocess.PIPE).stdout
    size = W * H * 4
    frames = [raw[i:i + size] for i in range(0, len(raw) - size + 1, size)]

    # Up to the first frame that is already the finished logo, then the last
    # frame itself. "Already" within a tolerance: the held frames differ by
    # the video's compression noise, at most 10 of 255 from frame 46 of
    # ZyperOS's earlier boot animation on (measured 2026-10-01), where the logo
    # still settling was off by up to 120.
    if loop is None:
        last = frames[-1]
        keep = len(frames) - 1
        while keep > 1 and max(abs(a - b) for a, b in zip(frames[keep - 1], last)) <= 12:
            keep -= 1
        frames = frames[:keep] + [last]
    elif not 0 < loop < len(frames):
        sys.exit("loop.txt: the loop starts at frame %d of %d" % (loop, len(frames)))

    if brick:
        write_bands(OUT, frames, W, H, fps)
    else:
        write_bands(OUT, frames, W, H, fps, loop=len(frames) if loop is None else loop)


def first_start(folder):
    """first-start.bin and first-start-light.bin from ZyperOS's
    res/boot/first-start, and a pair for each language's folder in it (see
    the top)"""
    first_start_one(folder, folder, "dark", "first-start.bin")
    first_start_one(folder, os.path.join(folder, "light"), "light", "first-start-light.bin")
    for code in sorted(os.listdir(folder)):
        if code == "light" or not os.path.isfile(os.path.join(folder, code, "setup.png")):
            continue
        first_start_one(folder, os.path.join(folder, code), "dark", "first-start-%s.bin" % code,
                        os.path.join(folder, "bar.png"))
        first_start_one(folder, os.path.join(folder, "light", code), "light",
                        "first-start-light-%s.bin" % code, os.path.join(folder, "light", "bar.png"))


def first_start_one(folder, pics_dir, theme, name, bar_png=None):
    W, H = 480, 640
    geo = {}
    with open(os.path.join(folder, "first-start.txt")) as f:
        for line in f:
            w = line.split()
            if w and not w[0].startswith("#"):
                geo[w[0]] = [int(v) for v in w[1:]]
    ground = geo["ground." + theme]
    pics = []
    for pic in ("setup", "update", "bar"):
        path = bar_png if pic == "bar" and bar_png else os.path.join(pics_dir, pic + ".png")
        raw = subprocess.run(
            ["ffmpeg", "-v", "error", "-i", path,
             "-vf", "transpose=2", "-f", "rawvideo", "-pix_fmt", "bgra", "-"],
            check=True, stdout=subprocess.PIPE).stdout
        if len(raw) != W * H * 4:
            sys.exit("%s.png: not 640x480" % pic)
        pics.append(raw)
    # The rect: every pixel any of them draws
    xs, ys = [], []
    for raw in pics:
        for y in range(H):
            row = raw[y * W * 4:(y + 1) * W * 4]
            hit = [x for x in range(W) if row[x * 4 + 3]]
            if hit:
                ys.append(y); xs += [hit[0], hit[-1]]
    x0, x1, y0, y1 = min(xs), max(xs) + 1, min(ys), max(ys) + 1
    # The bar, landscape (bx, by, bw, bh) on 640x480, turned counter-clockwise:
    # landscape x is the panel's row 639 - x, landscape y its column
    bx, by, bw, bh = geo["bar"]
    bar = (by - x0, by + bh - x0, 640 - (bx + bw) - y0, 640 - bx - y0)
    out = bytearray(b"TSPC" + struct.pack("<HHHH", x0, y0, x1 - x0, y1 - y0))
    out += struct.pack("<HHHHHH", *bar, len(pics), 0)
    for raw in pics:
        px = bytearray()
        for y in range(y0, y1):
            for x in range(x0, x1):
                b, g, r, a = raw[(y * W + x) * 4:(y * W + x) * 4 + 4]
                mix = lambda c, gr: (c * a + gr * (255 - a) + 127) // 255
                px += bytes((mix(b, ground[2]), mix(g, ground[1]), mix(r, ground[0]), 0))
        data = zlib.compress(bytes(px), 9)
        out += struct.pack("<I", len(data)) + data
    path = os.path.join(PACKAGE, name)
    with open(path, "wb") as f:
        f.write(out)
    print("%s: rect %d,%d %dx%d, bar cols %d-%d rows %d-%d, %d bytes" % (
        os.path.normpath(path), x0, y0, x1 - x0, y1 - y0, *bar, len(out)))


def sub_filter(band, stride):
    """Each byte less the same byte one pixel to the left (row starts kept)."""
    out = bytearray(band)
    for start in range(0, len(band), stride):
        row = band[start:start + stride]
        for i in range(4, stride):
            out[start + i] = (row[i] - row[i - 4]) & 0xff
    return bytes(out)


def band(f, prev, w, h):
    """The rows of f that differ from prev (all of them with no prev), packed:
    u16 first row, u16 rows, u32 length, then the Sub-filtered rows, zlib'd."""
    stride = w * 4
    if prev is None:
        first, rows = 0, h
    else:
        changed = [y for y in range(h)
                   if f[y * stride:(y + 1) * stride] != prev[y * stride:(y + 1) * stride]]
        first, rows = (changed[0], changed[-1] - changed[0] + 1) if changed else (0, 0)
    rowsdata = bytes(f[first * stride:(first + rows) * stride])
    data = zlib.compress(sub_filter(rowsdata, stride), 9) if rows else b""
    return struct.pack("<HHI", first, rows, len(data)) + data


def write_bands(out_path, frames, w, h, fps, loop=None):
    """The Brick's "TSPZ" with loop None, else the Pixel's "TSPY": the same
    bands, with the loop's first frame in the header and the band from the
    last frame back to it after the rest."""
    frames = [bytearray(f) for f in frames]
    for f in frames:
        f[3::4] = b"\xff" * (len(f) // 4)      # opaque: the display blends by alpha
    encoded = [band(f, frames[n - 1] if n else None, w, h) for n, f in enumerate(frames)]
    if loop is None:
        header = struct.pack("<4sHHHH", b"TSPZ", w, h, len(encoded), fps)
    else:
        header = struct.pack("<4sHHHHHH", b"TSPY", w, h, len(encoded), fps, loop, 0)
        encoded.append(band(frames[loop], frames[-1], w, h) if loop < len(frames)
                       else struct.pack("<HHI", 0, 0, 0))
    offset = len(header) + 4 * len(encoded)
    table = bytearray()
    for e in encoded:
        table += struct.pack("<I", offset)
        offset += len(e)
    with open(out_path, "wb") as out:
        out.write(header + table + b"".join(encoded))
    print("%s: %d frames%s, changed bands only, %.2f MB" % (
        os.path.relpath(out_path), len(frames),
        ", looping from %d" % loop if loop is not None and loop < len(frames) else "",
        offset / 1e6))


if __name__ == "__main__":
    main()
