#!/usr/bin/env python3
"""Draw PortMaster's art on the Ports shelf: res/portmaster/cover-light.png,
cover-dark.png and icon.png.

PortMaster's entry on the Ports shelf (src/pminstall.h) is there whether
PortMaster is on the card or not, and until now it was a generated card with
its name on it. This gives it a picture of its own, drawn for ZyperOS and
nobody else's: no part of PortMaster's logo or of any brand's art. The
picture is a harbor, since a port is where things are brought in: a dock
crane lowering a game cartridge onto a stack of shipping containers, at the
quay's edge over the water. The cartridge's label is ZyperOS's lime with a
gear on it, the containers are the Ports shelf's pink, the File Transfer
blue and Storage's teal.

DRAWN AS THE HOME'S SPRITES ARE (res/system-art/README.md): pixel art, tone
ramps of three, and a one-pixel ink outline round every shape, here made
from the shapes themselves (outline()), so nothing is outlined by hand. The
art is 64 by 82 pixels, drawn eight times over into the generated card's
512 by 656 (src/ui.c, CARD_W and CARD_H), with its corners (CARD_RADIUS)
and the band of the system's color across its top, so it stands in the row
of generated cards as one of them.

ONE FOR EACH THEME, as a generated card is: the sky is the card's slab in
that theme (cardgen_slab, the surface falling halfway to the ground), with
the sun in Beard and the moon and a few stars in Salt & Pepper. The crane,
the cartridge, the containers, the quay and the water are the same in both,
as a console sprite is never recolored for a theme (DESIGN.md 3.3). The
launcher shows the one of the theme that is on (src/main.c, pm_cover_path).

icon.png is the same crane, cartridge and containers on a short piece of
quay, with nothing behind them, square, each pixel four: the Install PortMaster page draws it
where System Update's page draws Zyper (DESIGN.md 11.33).

Needs Pillow (Xcode's python3 has it). Draws the same bytes every run.

    tools/genportmaster.py           write the three
    tools/genportmaster.py --check   fail when they differ from what it draws
"""
import io
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "portmaster")

W, H = 64, 82          # the art's own pixels
S = 8                  # each drawn as 8 x 8: 512 x 656, the generated card's
CARD_RADIUS = 22       # src/ui.c
BAND = 6               # the generated card's band of the system's color
ICON_K = 4             # icon.png: each of the art's pixels 4 x 4

# The theme's slab, as designdef.py has the roles and cardgen_slab mixes them
THEMES = {
    "light": {"surface": (0xf5, 0xf4, 0xee), "ground": (0xe6, 0xe4, 0xda)},
    "dark": {"surface": (0x1d, 0x20, 0x1e), "ground": (0x11, 0x13, 0x10)},
}
PORTS = (0xff, 0x6f, 0xae)           # the Ports shelf's color, systems.cfg

INK = (0x11, 0x13, 0x10)             # **ink**: every outline
# Tone ramps: light, base, shade
YELLOW = ((0xff, 0xd9, 0x66), (0xf2, 0xb3, 0x2e), (0xc2, 0x80, 0x1c))
STEEL = ((0xc9, 0xc7, 0xbc), (0x9a, 0x98, 0x8e), (0x6c, 0x6a, 0x62))
CART = ((0xd9, 0xd7, 0xcc), (0xb0, 0xae, 0xa4), (0x84, 0x82, 0x79))
LIME = ((0xd8, 0xff, 0x7a), (0xbe, 0xf2, 0x3c), (0x8a, 0xb8, 0x1a))
PINK = ((0xff, 0x9c, 0xc8), (0xff, 0x6f, 0xae), (0xc9, 0x4a, 0x86))
BLUE = ((0x9c, 0xc0, 0xff), (0x6b, 0x9e, 0xf5), (0x3f, 0x6f, 0xc4))
TEAL = ((0x86, 0xe0, 0xda), (0x4f, 0xc8, 0xc0), (0x2e, 0x95, 0x90))
CONCRETE = ((0xb8, 0xb5, 0xaa), (0x8e, 0x8b, 0x80), (0x66, 0x64, 0x5c))
GOLD = ((0xf2, 0xd2, 0x6a), (0xc2, 0x94, 0x2e))
GLASS = (0x9c, 0xd8, 0xf2)
WATER = ((0xb9, 0xdd, 0xf4), (0x5f, 0x9f, 0xd1), (0x3c, 0x77, 0xad))
SUN = ((0xff, 0xe9, 0xa8), (0xff, 0xd2, 0x5e))
MOON = ((0xf4, 0xf1, 0xdc), (0xcf, 0xcb, 0xb4))
STAR = (0xe4, 0xe2, 0xd8)

GEAR = ["..#.#..",
        ".#####.",
        "##...##",
        ".#...#.",
        "##...##",
        ".#####.",
        "..#.#.."]


class Layer:
    """The art's pixels: None where nothing is"""

    def __init__(self):
        self.px = [[None] * W for _ in range(H)]

    def put(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[y][x] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, c)

    def line(self, x0, y0, x1, y1, c):
        """One pixel wide, every step one pixel (a stepped line)"""
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.put(x0, y0, c)
            if x0 == x1 and y0 == y1:
                return
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy


def outline(layer):
    """Ink round every shape: each empty pixel beside one that is not ink.
    Ink drawn on purpose (the crane's ties, its cable) gets none, so a
    one-pixel line stays one pixel."""
    add = []
    for y in range(H):
        for x in range(W):
            if layer.px[y][x] is not None:
                continue
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                nx, ny = x + dx, y + dy
                if 0 <= nx < W and 0 <= ny < H:
                    c = layer.px[ny][nx]
                    if c is not None and c != INK:
                        add.append((x, y))
                        break
    for x, y in add:
        layer.px[y][x] = INK


def box(layer, x0, y0, x1, y1, ramp):
    """A block lit from the top left: its light along the top and left, its
    shade along the bottom and right"""
    light, base, shade = ramp
    layer.rect(x0, y0, x1, y1, base)
    layer.rect(x0, y0, x1, y0, light)
    layer.rect(x0, y0, x0, y1, light)
    layer.rect(x0 + 1, y1, x1, y1, shade)
    layer.rect(x1, y0 + 1, x1, y1, shade)


def container(layer, x0, y0, x1, y1, ramp):
    """A shipping container side on: ridges a pixel in shade every other
    pixel, a door's line near its right end"""
    light, base, shade = ramp
    box(layer, x0, y0, x1, y1, ramp)
    for x in range(x0 + 2, x1 - 1, 2):
        layer.rect(x, y0 + 1, x, y1 - 1, shade)
    layer.rect(x1 - 3, y0 + 1, x1 - 3, y1 - 1, INK)


def crane(layer):
    """The dock crane: mast, jib, counter-jib, cab, ties, trolley, cable and
    hook. Its feet on the quay at y 59."""
    light, base, shade = YELLOW
    # The mast, a lattice: light and shade sides, a cross brace every 4
    layer.rect(11, 18, 15, 57, base)
    layer.rect(11, 18, 11, 57, light)
    layer.rect(15, 18, 15, 57, shade)
    for y in range(20, 56, 4):
        layer.put(12, y, shade); layer.put(13, y + 1, shade); layer.put(14, y + 2, shade)
    # Its foot, on rails
    box(layer, 8, 56, 18, 59, STEEL)
    # The apex over the mast, and the ties from it to both ends of the jib
    layer.rect(12, 9, 14, 13, base)
    layer.rect(12, 9, 12, 13, light)
    layer.line(13, 8, 56, 13, INK)
    layer.line(13, 8, 3, 13, INK)
    # The jib, a lattice: a light top chord, a shade bottom one, a zigzag
    layer.rect(4, 14, 58, 17, base)
    layer.rect(4, 14, 58, 14, light)
    layer.rect(4, 17, 58, 17, shade)
    for x in range(5, 58):
        layer.put(x, 15 + (x // 2) % 2, shade if x % 4 else base)
    # The counterweight under the counter-jib
    box(layer, 3, 18, 8, 22, CONCRETE)
    # The cab, beside the mast under the jib, its window
    box(layer, 16, 18, 21, 23, YELLOW)
    layer.rect(18, 19, 20, 20, GLASS)
    # The trolley on the jib, the cable, the hook
    box(layer, 40, 18, 44, 19, STEEL)
    layer.rect(42, 20, 42, 22, INK)
    layer.rect(40, 23, 44, 23, STEEL[1])


def cartridge(layer):
    """The game it lowers: a cartridge with a corner cut, ZyperOS's lime on
    its label with a gear, a grip line under it, and its contacts showing at
    the foot, which is what says cartridge at the shelf's sizes"""
    x0, y0, x1, y1 = 35, 25, 49, 38
    box(layer, x0, y0, x1, y1, CART)
    # The corner cut, top right
    for i in range(3):
        for x in range(x1 - 2 + i, x1 + 1):
            layer.px[y0 + i][x] = None
    layer.put(x1 - 3, y0, CART[1]); layer.put(x1 - 2, y0 + 1, CART[2]); layer.put(x1 - 1, y0 + 2, CART[2])
    # The label
    box(layer, x0 + 3, y0 + 2, x1 - 3, y0 + 10, LIME)
    for j, row in enumerate(GEAR):
        for i, ch in enumerate(row):
            if ch == "#":
                layer.put(x0 + 4 + i, y0 + 3 + j, INK)
    # A grip line under the label
    layer.rect(x0 + 3, y1 - 1, x1 - 3, y1 - 1, CART[2])
    # The contacts, gold, a pin every other pixel
    for x in range(x0 + 3, x1 - 2):
        c = GOLD[0] if x % 2 else GOLD[1]
        layer.put(x, y1 + 1, c)
        layer.put(x, y1 + 2, GOLD[1])


def quay(layer, x0, x1):
    """The quay, its top lit, a bollard on it"""
    layer.rect(x0, 60, x1, 65, CONCRETE[1])
    layer.rect(x0, 60, x1, 60, CONCRETE[0])
    layer.rect(x0, 65, x1, 65, CONCRETE[2])
    for x in range(x0 + 3, x1, 9):
        layer.put(x, 63, CONCRETE[2])
    box(layer, 24, 57, 26, 59, STEEL)


def foreground(full):
    """The crane, the cartridge, the containers and the quay, outlined.
    `full`: the quay across the card; else a short piece, for the icon."""
    layer = Layer()
    crane(layer)
    container(layer, 26, 53, 39, 59, PINK)
    container(layer, 41, 53, 54, 59, BLUE)
    container(layer, 34, 46, 47, 51, TEAL)
    cartridge(layer)
    if full:
        quay(layer, 0, W - 1)
    else:
        quay(layer, 5, 58)
    outline(layer)
    if full:
        # The quay runs off both sides of the card: no ink up its ends
        for y in range(59, 67):
            for x in (0, W - 1):
                if layer.px[y][x] == INK and y not in (59, 66):
                    layer.px[y][x] = CONCRETE[1]
    return layer


def mix(a, b, t):
    return tuple(int(a[i] + (b[i] - a[i]) * t + 0.5) for i in range(3))


def sky(theme):
    """The card's slab, cardgen_slab's, in steps of the art's pixels, with
    the sun or the moon and stars"""
    t = THEMES[theme]
    layer = Layer()
    for y in range(H):
        c = mix(t["surface"], t["ground"], 0.5 * y / H)
        for x in range(W):
            layer.px[y][x] = c
    if theme == "light":
        for y in range(3, 12):
            for x in range(47, 58):
                dx, dy = x - 52, y - 7
                d = dx * dx + dy * dy
                if d <= 20:
                    layer.px[y][x] = SUN[0] if d <= 8 else SUN[1]
    else:
        for y in range(3, 12):
            for x in range(47, 58):
                dx, dy = x - 52, y - 7
                ex, ey = x - 54, y - 6
                if dx * dx + dy * dy <= 20 and ex * ex + ey * ey > 14:
                    layer.px[y][x] = MOON[0] if dx < -1 else MOON[1]
        for x, y in ((30, 4), (38, 9), (23, 6), (61, 15), (44, 3), (27, 26), (58, 31)):
            layer.px[y][x] = STAR
    return layer


def water(layer):
    """The water under the quay: two blues, a crest line every few rows"""
    light, base, shade = WATER
    for y in range(66, H):
        for x in range(W):
            layer.px[y][x] = base if y < 72 else shade
    for y, off in ((68, 0), (71, 3), (75, 1), (78, 4)):
        for x in range(W):
            if (x + off) % 8 in (0, 1, 2):
                layer.px[y][x] = light if y < 72 else base


def flatten(*layers, transparent=False):
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    px = img.load()
    for layer in layers:
        for y in range(H):
            for x in range(W):
                c = layer.px[y][x]
                if c is not None:
                    px[x, y] = c + (255,)
    return img


def corners(img):
    """src/ui.c's round_corners: a pixel past the radius is clear"""
    px = img.load()
    w, h = img.size
    r = CARD_RADIUS
    for y in range(r):
        for x in range(r):
            dx, dy = r - x, r - y
            if dx * dx + dy * dy > r * r:
                for xx, yy in ((x, y), (w - 1 - x, y), (x, h - 1 - y), (w - 1 - x, h - 1 - y)):
                    px[xx, yy] = (0, 0, 0, 0)


def cover(theme):
    bg = sky(theme)
    water(bg)
    img = flatten(bg, foreground(True)).resize((W * S, H * S), Image.NEAREST)
    # The band of the system's color, the generated card's (src/ui.c)
    img.paste(PORTS + (255,), (0, 0, W * S, BAND))
    corners(img)
    return img


def icon():
    """Square, the art's bounds with a pixel round them, each pixel 4 x 4:
    the page draws it at 0.8, two of the Pixel 2's pixels to one of the
    art's"""
    art = flatten(foreground(False))
    art = art.crop(art.getbbox())
    side = max(art.size) + 2
    sq = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    sq.paste(art, ((side - art.size[0]) // 2, side - art.size[1] - 1))
    return sq.resize((side * ICON_K, side * ICON_K), Image.NEAREST)


def png(img):
    b = io.BytesIO()
    img.save(b, "PNG", optimize=True)
    return b.getvalue()


def main():
    check = "--check" in sys.argv[1:]
    want = {"cover-light.png": cover("light"), "cover-dark.png": cover("dark"),
            "icon.png": icon()}
    bad = 0
    for name, img in want.items():
        path = os.path.join(OUT, name)
        if check:
            try:
                have = Image.open(path).convert("RGBA")
            except OSError:
                have = None
            if have is None or have.size != img.size or have.tobytes() != img.tobytes():
                print("genportmaster: %s differs from what it draws" % os.path.relpath(path, ROOT))
                bad += 1
        else:
            with open(path, "wb") as f:
                f.write(png(img))
            print("wrote", os.path.relpath(path, ROOT))
    if check:
        print("genportmaster: %s" % ("ok" if not bad else "%d differ" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
