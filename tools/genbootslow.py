#!/usr/bin/env python3
"""The boot screen's words for a start that is taking longer than it should.

    python3 tools/genbootslow.py             the splash's strips, in plastron
    python3 tools/genbootslow.py --check     whether every file is there, in its
                                             format, with this Pillow's lines
                                             (their ids, their widths within a
                                             few pixels) and every line fits
                                             (make check-genbootslow)
    python3 tools/genbootslow.py --drift     whether they are byte for byte
                                             what this Pillow draws (make
                                             check-genbootslow-drift, out of the
                                             suite: another Pillow or FreeType
                                             draws other pixels)

A start reaches ZyperOS's home in 1.4 to 2 s on a GKD Pixel 2 (its kernel's
log, seven starts) and about 2.2 s on a TrimUI Brick. One that takes longer
says so (DESIGN.md 11.20, "Slower than usual"): from 5 s the step under way
at the foot, with its code ("Checking the card · C3"); from 20 s "Taking
longer than usual" over Zyper and, under it, what to do if it never ends
("Stuck? Hold POWER for 10 s, then turn it on."). The splash has no
fonts, so each line is drawn here, in every language (res/strings' boot.slow,
boot.stage_*, boot.stuck), as the first start's line is (tools/genfirststart.py):
at the launcher's 1024-unit sizes (label 55 for the first line, meta 37 for
the rest) in Lexend, Chinese and Japanese in their faces, then brought down to
the handheld's pixels with LANCZOS, as the boot frames are.

Coverage only, no color: the splash lays each strip on the animation's
ground in the theme's ink (text-soft, the code and the help in text-dim), so
one file serves Beard and Salt & Pepper. One file a language and a handheld's
scale, in plastron/buildroot/package/splash/slow/:

    px2/CODE.bin       the GKD Pixel 2 and the R36S, 640x480 (0.625 a unit)
    brick/CODE.bin     the TrimUI Brick, 1024x768 (1:1)
    smartpro/CODE.bin  the TrimUI Smart Pro, its 768-unit canvas on 720 rows

Format, little-endian ("TSPN"), read by the splash's slowline.h:
    "TSPN", u16 version (1), u16 scale in thousandths, u16 lines, u16 0
    per line: u8 id, u8 segments, u16 height, u16 ascent (the baseline
      from its top), u16 width (all its segments), in the handheld's pixels
    per segment: u8 ink (0 the line's own, 1 text-dim), u8 0, u16 width,
      u32 length, then that many bytes of zlib: width x height coverage
The ids: 0 the first line (boot.slow), 1 to 5 the steps (mount, format,
check, switch, launch), each its words and then its code; 6 the help
(boot.stuck).

Beside them, each handheld's glyphs.bin: the tester's view's ASCII letters
(tools/genclosed.py's format), TESTER units tall, drawn at the handheld's own
pixel size, so the view looks the same on every panel.
"""
import os
import struct
import sys
import tempfile
import zlib

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n             # noqa: E402
import genclosed        # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "plastron", "buildroot", "package", "splash", "slow")
LABEL, META = 55, 37             # DESIGN.md 4's label and meta, in units
LINE_MAX = 1024 - 2 * 64         # 64 in from each edge, as the first start's line
# The handhelds' scales: panel pixels a unit
SCALES = {"px2": 0.625, "brick": 1.0, "smartpro": 720 / 768}
# The steps, in the order a start goes through them, with their codes
STAGES = (("C1", "boot.stage_mount"), ("C2", "boot.stage_format"), ("C3", "boot.stage_check"),
          ("U1", "boot.stage_switch"), ("L1", "boot.stage_launch"))
DOT = " · "
# The tester's view's letters, in units: the Pixel 2's 22 px (glyphs.bin's)
TESTER = 35.2


def strings(code):
    errors = []
    by_id = {x.id: x for x in i18n.parse(errors)}
    return lambda sid: i18n.get(by_id[sid], code)


def font_for(code, size):
    name = i18n.FONTS.get(code, i18n.LATIN_FONTS)[0]
    return ImageFont.truetype(os.path.join(ROOT, "res", "fonts", name), size)


def lines(code):
    """(id, size, [(ink, text, face code)]) for a language: the code after a
    step is Lexend's in every language, as the launcher's codes are"""
    s = strings(code)
    out = [(0, LABEL, [(0, s("boot.slow"), code)])]
    for n, (c, sid) in enumerate(STAGES):
        out.append((n + 1, META, [(0, s(sid), code), (1, DOT + c, "en")]))
    out.append((6, META, [(0, s("boot.stuck"), code)]))
    return out


def strip(text, face, size, scale, ascent, height):
    """One segment's coverage: drawn at units, brought down to pixels"""
    w = max(1, int(round(face.getlength(text))))
    im = Image.new("L", (w, height), 0)
    ImageDraw.Draw(im).text((0, ascent), text, font=face, fill=255, anchor="ls")
    if scale == 1.0:
        return im
    return im.resize((max(1, int(round(w * scale))), max(1, int(round(height * scale)))), Image.LANCZOS)


def encode(code, scale):
    """A language's file at a scale; exits on a line past LINE_MAX"""
    out = []
    for lid, size, segs in lines(code):
        faces = [font_for(fc, size) for _, _, fc in segs]
        asc = max(f.getmetrics()[0] for f in faces)
        desc = max(f.getmetrics()[1] for f in faces)
        width = sum(f.getlength(t) for f, (_, t, _) in zip(faces, segs))
        if width > LINE_MAX:
            sys.exit("genbootslow: %s's line %d is %d units wide, past %d: %s"
                     % (code, lid, width, LINE_MAX, "".join(t for _, t, _ in segs)))
        pics = [strip(t, f, size, scale, asc, asc + desc) for f, (_, t, _) in zip(faces, segs)]
        h = pics[0].height
        head = struct.pack("<BBHHH", lid, len(pics), h, int(round(asc * scale)), sum(p.width for p in pics))
        body = b""
        for (ink, _, _), p in zip(segs, pics):
            data = zlib.compress(p.tobytes(), 9)
            body += struct.pack("<BBHI", ink, 0, p.width, len(data)) + data
        out.append(head + body)
    return struct.pack("<4sHHHH", b"TSPN", 1, int(round(scale * 1000)), len(out), 0) + b"".join(out)


def names():
    return [os.path.join(dev, code + ".bin") for dev in SCALES for code in i18n.CODES] + \
        [os.path.join(dev, "glyphs.bin") for dev in SCALES]


def write(out):
    for dev, scale in SCALES.items():
        os.makedirs(os.path.join(out, dev), exist_ok=True)
        for code in i18n.CODES:
            with open(os.path.join(out, dev, code + ".bin"), "wb") as f:
                f.write(encode(code, scale))
        with open(os.path.join(out, dev, "glyphs.bin"), "wb") as f:
            f.write(genclosed.glyphs(int(round(TESTER * scale))))


def widest():
    """Each language's widest line, in units, for the check's report"""
    out = {}
    for code in i18n.CODES:
        w = 0
        for lid, size, segs in lines(code):
            w = max(w, sum(font_for(fc, size).getlength(t) for _, t, fc in segs))
        out[code] = int(w)
    return out


def read_lines(path):
    """A file's lines as (id, segments, height, width), or None if it is not
    in the format"""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 12:
        return None
    magic, version, _, n, _ = struct.unpack_from("<4sHHHH", data, 0)
    if magic != b"TSPN" or version != 1:
        return None
    out, at = [], 12
    for _ in range(n):
        if at + 8 > len(data):
            return None
        lid, nseg, h, _, w = struct.unpack_from("<BBHHH", data, at)
        at += 8
        for _ in range(nseg):
            if at + 8 > len(data):
                return None
            at += 8 + struct.unpack_from("<BBHI", data, at)[3]
        out.append((lid, nseg, h, w))
    return out if at == len(data) else None


def expected(code, scale):
    """The lines encode() draws, as read_lines() gives them, without drawing"""
    out = []
    for lid, size, segs in lines(code):
        faces = [font_for(fc, size) for _, _, fc in segs]
        asc = max(f.getmetrics()[0] for f in faces)
        desc = max(f.getmetrics()[1] for f in faces)
        w = 0
        for f, (_, t, _) in zip(faces, segs):
            u = max(1, int(round(f.getlength(t))))
            w += u if scale == 1.0 else max(1, int(round(u * scale)))
        h = asc + desc if scale == 1.0 else max(1, int(round((asc + desc) * scale)))
        out.append((lid, len(segs), h, w))
    return out


def check():
    """The files against this Pillow's lines, loosely: the same lines, each as
    wide and as tall within a few pixels, so a string changed and not drawn
    again shows, and another Pillow's hinting does not"""
    bad = []
    for dev, scale in SCALES.items():
        for code in i18n.CODES:
            path = os.path.join(OUT, dev, code + ".bin")
            got = read_lines(path) if os.path.isfile(path) else None
            want = expected(code, scale)
            if got is None or len(got) != len(want) or any(
                    g[:2] != e[:2] or abs(g[2] - e[2]) > 2 or abs(g[3] - e[3]) > max(3, e[3] // 50)
                    for g, e in zip(got, want)):
                bad.append(os.path.join(dev, code + ".bin"))
        path = os.path.join(OUT, dev, "glyphs.bin")
        if not os.path.isfile(path) or os.path.getsize(path) == 0:
            bad.append(os.path.join(dev, "glyphs.bin"))
    if bad:
        print("check-genbootslow: not what tools/genbootslow.py draws: %s (run it)" % ", ".join(bad))
        return 1
    w = widest()
    if max(w.values()) > LINE_MAX:
        print("check-genbootslow: %s's widest line is %d units, past %d" % (max(w, key=w.get), max(w.values()), LINE_MAX))
        return 1
    print("check-genbootslow: the slow start's lines in %d languages for the Pixel 2, the Brick and the Smart Pro, and the"
          " tester's letters, there and as wide as this draws them; the widest %d of %d units (%s)"
          % (len(w), max(w.values()), LINE_MAX, max(w, key=w.get)))
    return 0


def main():
    if sys.argv[1:] == ["--check"]:
        return check()
    if sys.argv[1:] == ["--drift"]:
        with tempfile.TemporaryDirectory() as tmp:
            write(tmp)
            bad = [n for n in names()
                   if not os.path.isfile(os.path.join(OUT, n)) or
                   open(os.path.join(tmp, n), "rb").read() != open(os.path.join(OUT, n), "rb").read()]
        if bad:
            print("check-genbootslow-drift: not what tools/genbootslow.py draws: %s (run it)" % ", ".join(bad))
            return 1
        w = widest()
        print("check-genbootslow-drift: the slow start's lines in %d languages for the Pixel 2, the Brick and the Smart Pro, and the"
              " tester's letters, as drawn; the widest %d of %d units (%s)" % (len(w), max(w.values()), LINE_MAX, max(w, key=w.get)))
        return 0
    write(OUT)
    print("genbootslow: %d files in %s" % (len(names()), os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
