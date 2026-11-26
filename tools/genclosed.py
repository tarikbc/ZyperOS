#!/usr/bin/env python3
"""The TrimUI Brick's splash's words: the screen for a card without its
closed files, and the letters of the line its start says.

    python3 tools/genclosed.py               the splash's pictures, in plastron
    python3 tools/genclosed.py --check       whether they are what this draws
                                             (make check-genclosed)
    python3 tools/genclosed.py --preview F [CODE]   F: one as a PNG, to look at

The Brick's graphics and its Wi-Fi need closed files (Imagination's PowerVR
driver, XRadio's firmware) that ZyperOS does not pass on: the ZyperOS
Installer puts them on the card, in the boot partition's closed/ (the
player's own copy, docs/building.md, Devices). A card without them all can't start the launcher, and the start
says so instead of leaving the screen black: plastron's init.d/brick finds
one missing and its splash shows this, the two lines of res/strings'
boot.closed_missing and boot.closed_missing_do, until POWER turns the Brick
off.

The start's line (glyphs.bin): which panel the kernel found, said under the
boot animation as the start reads it from the kernel's log (plastron's
init.d/brick, /tmp/splash-say), so a tester sees it without a computer. The
printable ASCII letters in Lexend at 22 px, each an 8-bit coverage block the
splash lays over its frames: "TSPG", u16 height, u16 baseline, u16 first
letter, u16 count, then each letter's u8 advance and its advance x height
bytes.

The Smart Pro's (its splash is the Brick's, on its 1280x720 framebuffer) are
drawn on the launcher's 16:9 canvas, 1368x768 (src/canvas.h), and scaled to
its framebuffer as the launcher's are, into the package's smartpro/.

One picture a language, at the Brick's 1024x768, on the boot screen's ground
(tools/zyperdef.BG), written as the Brick's splash reads its frames (plastron's
scripts/make-splash-frames.py, "TSPZ"): one frame, every row, each byte less
the same byte of the pixel to its left, zlib'd. English's is
closed-missing.bin, every other language's closed-missing-CODE.bin, in
plastron/buildroot/package/splash. Chinese and Japanese in their own faces
(i18n.FONTS), the rest in Lexend.
"""
import os
import struct
import sys
import tempfile
import zlib

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import designdef as D   # noqa: E402
import i18n             # noqa: E402
import zyperdef as Z    # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "plastron", "buildroot", "package", "splash")
W, H = 1024, 768                 # the Brick's panel
SMARTPRO = (1368, 768, 1280, 720)  # the Smart Pro's canvas, and its framebuffer
WIDTH = 900                      # the widest a line may be
HEADING, BODY = 40, 30           # DESIGN.md 5's title and body, at the Brick's 1:1
GAP = 14                         # between the heading and what to do


def _rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


# Salt & Pepper's text roles (DESIGN.md 3.1): the boot screen is dark
COLORS = {name: _rgb(dark) for name, _, dark in D.COLORS}


def words(code, device=""):
    """boot.closed_missing and boot.closed_missing_do in a language, or a
    device's own pair (boot.closed_missing_smartpro)"""
    errors = []
    by_id = {x.id: x for x in i18n.parse(errors)}
    key = "boot.closed_missing" + ("_" + device if device else "")
    return (i18n.get(by_id[key], code), i18n.get(by_id[key + "_do"], code))


def font_for(code, size):
    name = i18n.FONTS.get(code, i18n.LATIN_FONTS)[0]
    return ImageFont.truetype(os.path.join(ROOT, "res", "fonts", name), size)


def wrap(text, font, cjk):
    """text in lines no wider than WIDTH: at spaces, or between any two
    characters in Chinese and Japanese, which have none to break at"""
    pieces = list(text) if cjk else text.split(" ")
    glue = "" if cjk else " "
    lines, line = [], ""
    for p in pieces:
        trial = (line + glue + p) if line else p
        if font.getlength(trial) <= WIDTH or not line:
            line = trial
        else:
            lines.append(line)
            line = p.lstrip() if cjk else p
    if line:
        lines.append(line)
    return lines


def draw(code, w=W, h=H, device=""):
    """The picture in a language: the heading, then what to do, centered on
    the ground, on a w x h canvas, in a device's own words"""
    heading, todo = words(code, device)
    cjk = code in ("zh-Hans", "ja")
    hf, bf = font_for(code, HEADING), font_for(code, BODY)
    # What to do a sentence at a time: the Update, then what POWER does
    sentences = [s + "。" for s in todo.split("。") if s] if cjk else \
        [s if s.endswith(".") else s + "." for s in todo.split(". ")]
    blocks = [(line, hf, COLORS["text"]) for line in wrap(heading, hf, cjk)] + \
             [(line, bf, COLORS["text-soft"]) for s in sentences for line in wrap(s, bf, cjk)]
    heights = [int(f.size * 1.4) for _, f, _ in blocks]
    total = sum(heights) + GAP
    im = Image.new("RGBA", (w, h), Z.BG + (255,))
    d = ImageDraw.Draw(im)
    y = (h - total) // 2
    n_heading = len(wrap(heading, hf, cjk))
    for i, ((line, font, color), lh) in enumerate(zip(blocks, heights)):
        if i == n_heading:
            y += GAP
        if font.getlength(line) > WIDTH:
            sys.exit("genclosed: %s's line is %d wide, past %d: %s" % (code, font.getlength(line), WIDTH, line))
        d.text((w / 2, y + lh * 0.75), line, font=font, fill=color + (255,), anchor="ms")
        y += lh
    return im


def draw_smartpro(code):
    """The Smart Pro's: drawn on its 1368x768 canvas, scaled to 1280x720"""
    cw, ch, fw, fh = SMARTPRO
    return draw(code, cw, ch, "smartpro").resize((fw, fh), Image.LANCZOS)


def sub_filter(band, stride):
    """Each byte less the same byte one pixel to the left (row starts kept),
    as plastron's make-splash-frames.py does"""
    out = bytearray(band)
    for start in range(0, len(band), stride):
        row = band[start:start + stride]
        for i in range(4, stride):
            out[start + i] = (row[i] - row[i - 4]) & 0xff
    return bytes(out)


def encode(im):
    """One "TSPZ" frame, every row: the Brick's splash's own format"""
    w, h = im.size
    px = im.convert("RGBA").tobytes("raw", "BGRA")
    data = zlib.compress(sub_filter(px, w * 4), 9)
    band = struct.pack("<HHI", 0, h, len(data)) + data
    header = struct.pack("<4sHHHH", b"TSPZ", w, h, 1, 1)
    return header + struct.pack("<I", len(header) + 4) + band


def name_of(code):
    return "closed-missing.bin" if code == "en" else "closed-missing-%s.bin" % code


GLYPH_SIZE = 22
GLYPH_FIRST, GLYPH_COUNT = 32, 95


def glyphs(size=GLYPH_SIZE):
    """The splash's letters: "TSPG" and each printable ASCII letter's
    coverage, its advance wide, at `size` pixels (the boot screen's tester's
    view draws them at each handheld's scale: tools/genbootslow.py)"""
    font = ImageFont.truetype(os.path.join(ROOT, "res", "fonts", "menu.ttf"), size)
    ascent, descent = font.getmetrics()
    height = ascent + descent
    out = bytearray(struct.pack("<4sHHHH", b"TSPG", height, ascent, GLYPH_FIRST, GLYPH_COUNT))
    for c in range(GLYPH_FIRST, GLYPH_FIRST + GLYPH_COUNT):
        adv = max(1, int(round(font.getlength(chr(c)))))
        im = Image.new("L", (adv, height), 0)
        ImageDraw.Draw(im).text((0, ascent), chr(c), font=font, fill=255, anchor="ls")
        out += struct.pack("<B", adv) + im.tobytes()
    return bytes(out)


def write(out):
    os.makedirs(os.path.join(out, "smartpro"), exist_ok=True)
    for code in i18n.CODES:
        with open(os.path.join(out, name_of(code)), "wb") as f:
            f.write(encode(draw(code)))
        with open(os.path.join(out, "smartpro", name_of(code)), "wb") as f:
            f.write(encode(draw_smartpro(code)))
    with open(os.path.join(out, "glyphs.bin"), "wb") as f:
        f.write(glyphs())


def names():
    """Every file write() makes, relative to its folder"""
    return [n for c in i18n.CODES for n in (name_of(c), os.path.join("smartpro", name_of(c)))] + ["glyphs.bin"]


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--preview":
        draw(sys.argv[3] if len(sys.argv) > 3 else "en").save(sys.argv[2])
        return 0
    if len(sys.argv) > 1 and sys.argv[1] == "--check":
        with tempfile.TemporaryDirectory() as tmp:
            write(tmp)
            bad = [n for n in names()
                   if not os.path.isfile(os.path.join(OUT, n)) or
                   open(os.path.join(tmp, n), "rb").read() != open(os.path.join(OUT, n), "rb").read()]
        if bad:
            print("check-genclosed: not what tools/genclosed.py draws: %s (run it)" % ", ".join(bad))
            return 1
        print("check-genclosed: the missing-files screen in %d languages on the Brick and the Smart Pro, and its start's letters, as drawn" % len(i18n.CODES))
        return 0
    write(OUT)
    print("genclosed: %d pictures a handheld and glyphs.bin in %s" % (len(i18n.CODES), os.path.relpath(OUT, ROOT)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
