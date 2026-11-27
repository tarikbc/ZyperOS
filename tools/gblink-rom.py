#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A Game Boy ROM that tests the link cable: the Link Cable's game
(DESIGN.md 11.72) without a commercial one. Original code and font, a few
hundred bytes, written here as bytes: no assembler to install.

    python3 tools/gblink-rom.py out.gb

Both Game Boys run the same ROM. Each starts on the external clock (SC
0x80), waiting. START on one makes it the first to clock (the internal
clock, SC 0x81). From then on the two take turns: every 32 transfers both
swap, so each side sends and receives on both clocks. Each sends its own
count, one more every transfer, and checks that what comes in is one more
than what came last.

The screen (20 x 18 tiles, the counts in hex):

     ZYPEROS LINK TEST
     ROLE  INT CLOCK      INT CLOCK, EXT CLOCK, or WAITING before the first
     INT TX     0000      transfers this side clocked
     INT OK     0000      ...whose byte in was the one after the last
     EXT TX     0000      transfers the other side clocked
     EXT OK     0000
     BAD        0000      bytes in that were not the next (0xFF: no peer)
     LAST RX    00
     LAST TX    00
     PASS                 16 good bytes on each clock and none bad (FAIL
                          once one is bad)
     START ON ONE SIDE

A side whose game clocks with no peer reads 0xFF, as a Game Boy with no
cable does, so START before the link is up counts BAD. tools/gblink-diatom.py
reads these rows back off Diatom's frames (`screen_rows`), so FONT here is
also what it matches against.
"""
import sys

# ---- the font: 5 x 7, in an 8 x 8 tile (columns 1 to 5, rows 0 to 6) ------

CHARS = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.:-+"
GLYPHS = {
    " ": ["     "] * 7,
    "0": [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    "1": ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    "2": [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    "3": ["#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "],
    "4": ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    "5": ["#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "],
    "6": ["  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "],
    "7": ["#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "],
    "8": [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    "9": [" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "],
    "A": [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "B": ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    "C": [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    "D": ["###  ", "#  # ", "#   #", "#   #", "#   #", "#  # ", "###  "],
    "E": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    "F": ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "],
    "G": [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"],
    "H": ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    "I": [" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    "J": ["  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "],
    "K": ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    "L": ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    "M": ["#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"],
    "N": ["#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"],
    "O": [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "P": ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    "Q": [" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"],
    "R": ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    "S": [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    "T": ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    "U": ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    "V": ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    "W": ["#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "],
    "X": ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    "Y": ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    "Z": ["#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"],
    ".": ["     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "],
    ":": ["     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "],
    "-": ["     ", "     ", "     ", "#####", "     ", "     ", "     "],
    "+": ["     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "],
}


def tile_rows(ch):
    """The glyph as eight rows of eight bits, the top bit the left pixel"""
    rows = []
    for r in range(8):
        line = GLYPHS[ch][r] if r < 7 else "     "
        bits = 0
        for c, px in enumerate(line):
            if px == "#": bits |= 0x80 >> (c + 1)
        rows.append(bits)
    return rows


def font_bytes():
    out = bytearray()
    for ch in CHARS:
        for bits in tile_rows(ch):
            out += bytes([bits, bits])          # colour 3 (both planes): black
    return bytes(out)


def text(s):
    return bytes(CHARS.index(c) for c in s)


# ---- a few bytes of assembler ----------------------------------------------

class Asm:
    def __init__(self, org):
        self.org, self.b, self.labels, self.fix = org, bytearray(), {}, []

    def pc(self): return self.org + len(self.b)
    def label(self, name): self.labels[name] = self.pc()
    def db(self, *bs):
        for x in bs: self.b += x if isinstance(x, (bytes, bytearray)) else bytes([x & 0xFF])
    def dw(self, v): self.db(v & 0xFF, v >> 8)
    def rel(self, op, name): self.db(op, 0); self.fix.append(("rel", len(self.b) - 1, name))
    def abs(self, op, name): self.db(op, 0, 0); self.fix.append(("abs", len(self.b) - 2, name))
    def ref(self, op, name): self.abs(op, name)            # ld rr,label

    def done(self):
        for kind, at, name in self.fix:
            t = self.labels[name]
            if kind == "rel":
                d = t - (self.org + at + 1)
                assert -128 <= d <= 127, (name, d)
                self.b[at] = d & 0xFF
            else:
                self.b[at], self.b[at + 1] = t & 0xFF, t >> 8
        return bytes(self.b)


# Registers and variables
SB, SC, LY, P1, LCDC, BGP, SCY, SCX = 0x01, 0x02, 0x44, 0x00, 0x40, 0x47, 0x42, 0x43
V = dict(ROLE=0xC000, STARTED=0xC001, SEQ=0xC002, LASTRX=0xC003, HAVERX=0xC004,
         BUSY=0xC005, COOL=0xC006, FRAME=0xC007, INTTX=0xC008, INTOK=0xC00A,
         EXTTX=0xC00C, EXTOK=0xC00E, BAD=0xC010, BLK=0xC012, PREVJOY=0xC013,
         VBF=0xC014, LASTTX=0xC015)
SWAP_EVERY = 32
PASS_AT = 16


def at(row, col): return 0x9800 + row * 32 + col


def code():
    a = Asm(0x150)
    lo = lambda v: v & 0xFF
    hi = lambda v: v >> 8
    ld_a_mem = lambda v: a.db(0xFA, lo(v), hi(v))
    ld_mem_a = lambda v: a.db(0xEA, lo(v), hi(v))
    ld_hl = lambda v: a.db(0x21, lo(v), hi(v))
    ld_de = lambda v: a.db(0x11, lo(v), hi(v))

    a.db(0xF3)                                  # di
    a.db(0x31, 0xFE, 0xFF)                      # ld sp,$FFFE
    a.label("wait_vb")                          # the LCD off only in VBlank
    a.db(0xF0, LY, 0xFE, 144); a.rel(0x38, "wait_vb")   # ldh a,(LY); cp 144; jr c
    a.db(0xAF, 0xE0, LCDC)                      # xor a; ldh (LCDC),a
    # the font into $8000
    a.ref(0x21, "font"); ld_de(0x8000); a.db(0x01); a.dw(len(font_bytes()))
    a.label("cp_font")
    a.db(0x2A, 0x12, 0x13, 0x0B, 0x78, 0xB1)    # ld a,(hl+); ld (de),a; inc de; dec bc; ld a,b; or c
    a.rel(0x20, "cp_font")
    # the map blank
    ld_hl(0x9800); a.db(0x01); a.dw(0x400)
    a.label("clr_map")
    a.db(0xAF, 0x22, 0x0B, 0x78, 0xB1); a.rel(0x20, "clr_map")
    # the variables zero
    ld_hl(0xC000); a.db(0x06, 0x20)             # ld b,32
    a.label("clr_var")
    a.db(0xAF, 0x22, 0x05); a.rel(0x20, "clr_var")   # xor a; ld (hl+),a; dec b; jr nz
    a.ref(0x21, "t_static"); a.abs(0xCD, "print")
    a.db(0x3E, 0xE4, 0xE0, BGP)                 # BGP: 0 white .. 3 black
    a.db(0xAF, 0xE0, SCX, 0xE0, SCY)
    a.db(0x3E, 0x91, 0xE0, LCDC)                # LCD on, BG on, tiles at $8000
    a.abs(0xCD, "arm")                          # waiting on the external clock

    a.label("main")
    a.abs(0xCD, "serial")
    a.db(0xF0, LY, 0xFE, 144); a.rel(0x28, "vb")
    a.db(0xAF); ld_mem_a(V["VBF"]); a.rel(0x18, "main")
    a.label("vb")
    ld_a_mem(V["VBF"]); a.db(0xB7); a.rel(0x20, "main")
    a.db(0x3E, 1); ld_mem_a(V["VBF"])
    a.abs(0xCD, "frame")
    a.rel(0x18, "main")

    # ---- once a frame, in VBlank: draw, START, the internal clock's next
    a.label("frame")
    for row, var in ((5, "INTTX"), (6, "INTOK"), (8, "EXTTX"), (9, "EXTOK"), (11, "BAD")):
        ld_a_mem(V[var] + 1); ld_hl(at(row, 12)); a.abs(0xCD, "hex")
        ld_a_mem(V[var]); a.abs(0xCD, "hex")
    ld_a_mem(V["LASTRX"]); ld_hl(at(13, 12)); a.abs(0xCD, "hex")
    ld_a_mem(V["LASTTX"]); ld_hl(at(14, 12)); a.abs(0xCD, "hex")
    # the role
    ld_a_mem(V["ROLE"]); a.db(0xB7); a.rel(0x28, "r0")
    a.ref(0x21, "t_int"); a.rel(0x18, "rp")
    a.label("r0")
    ld_a_mem(V["STARTED"]); a.db(0xB7); a.ref(0x21, "t_wait"); a.rel(0x28, "rp")
    a.ref(0x21, "t_ext")
    a.label("rp"); a.abs(0xCD, "print")
    # PASS, FAIL or nothing yet
    ld_hl(V["BAD"]); a.db(0x2A, 0xB6); a.rel(0x20, "st_fail")   # ld a,(hl+); or (hl)
    ld_a_mem(V["INTOK"] + 1); a.db(0xB7); a.rel(0x20, "st_i")
    ld_a_mem(V["INTOK"]); a.db(0xFE, PASS_AT); a.rel(0x38, "st_none")
    a.label("st_i")
    ld_a_mem(V["EXTOK"] + 1); a.db(0xB7); a.rel(0x20, "st_pass")
    ld_a_mem(V["EXTOK"]); a.db(0xFE, PASS_AT); a.rel(0x38, "st_none")
    a.label("st_pass"); a.ref(0x21, "t_pass"); a.rel(0x18, "st_p")
    a.label("st_fail"); a.ref(0x21, "t_fail"); a.rel(0x18, "st_p")
    a.label("st_none"); a.ref(0x21, "t_none")
    a.label("st_p"); a.abs(0xCD, "print")
    # the frame's count
    ld_hl(V["FRAME"]); a.db(0x34)
    # START, pressed since the last frame
    a.db(0x3E, 0x10, 0xE0, P1)                  # the buttons' half
    a.db(0xF0, P1, 0xF0, P1, 0xF0, P1, 0xF0, P1)
    a.db(0x2F, 0xE6, 0x08, 0x47)                # cpl; and START; ld b,a
    a.db(0x3E, 0x30, 0xE0, P1)
    ld_a_mem(V["PREVJOY"]); a.db(0x4F, 0x78); ld_mem_a(V["PREVJOY"])   # ld c,a; ld a,b
    a.db(0x79, 0x2F, 0xA0); a.rel(0x28, "nostart")                   # ld a,c; cpl; and b
    ld_a_mem(V["STARTED"]); a.db(0xB7); a.rel(0x20, "nostart")
    a.db(0x3E, 1); ld_mem_a(V["ROLE"]); ld_mem_a(V["STARTED"])
    a.db(0xAF); ld_mem_a(V["COOL"]); ld_mem_a(V["BUSY"])
    a.label("nostart")
    ld_a_mem(V["ROLE"]); a.db(0xB7, 0xC8)       # or a; ret z
    ld_a_mem(V["BUSY"]); a.db(0xB7, 0xC0)       # ret nz
    ld_a_mem(V["COOL"]); a.db(0xB7); a.rel(0x28, "go")
    a.db(0x3D); ld_mem_a(V["COOL"]); a.db(0xC9)
    a.label("go")
    ld_a_mem(V["FRAME"]); a.db(0xE6, 1, 0xC0)   # every other frame
    ld_a_mem(V["SEQ"]); a.db(0xE0, SB); ld_mem_a(V["LASTTX"])
    a.db(0x3E, 1); ld_mem_a(V["BUSY"])
    a.db(0x3E, 0x81, 0xE0, SC)                  # the internal clock: go
    a.db(0xC9)

    # ---- a transfer that ended, on either clock
    a.label("serial")
    a.db(0xF0, SC, 0xCB, 0x7F, 0xC0)            # ldh a,(SC); bit 7,a; ret nz
    ld_a_mem(V["ROLE"]); a.db(0xB7); a.rel(0x28, "s_ext")
    ld_a_mem(V["BUSY"]); a.db(0xB7, 0xC8)       # idle: nothing ended
    a.db(0xAF); ld_mem_a(V["BUSY"])
    ld_hl(V["INTTX"]); a.abs(0xCD, "inc16")
    ld_de(V["INTOK"]); a.rel(0x18, "s_got")
    a.label("s_ext")
    ld_hl(V["EXTTX"]); a.abs(0xCD, "inc16")
    ld_de(V["EXTOK"])
    a.label("s_got")
    a.db(0xF0, SB, 0x47)                        # ldh a,(SB); ld b,a
    ld_a_mem(V["HAVERX"]); a.db(0xB7); a.rel(0x28, "s_first")
    ld_a_mem(V["LASTRX"]); a.db(0x3C, 0xB8); a.rel(0x20, "s_bad")    # inc a; cp b
    a.db(0x62, 0x6B); a.abs(0xCD, "inc16"); a.rel(0x18, "s_store")  # ld h,d; ld l,e
    a.label("s_bad")
    ld_hl(V["BAD"]); a.abs(0xCD, "inc16"); a.rel(0x18, "s_store")
    a.label("s_first")
    a.db(0x3E, 1); ld_mem_a(V["HAVERX"])
    a.label("s_store")
    a.db(0x78); ld_mem_a(V["LASTRX"])
    a.db(0x3E, 1); ld_mem_a(V["STARTED"])
    ld_hl(V["SEQ"]); a.db(0x34)
    ld_a_mem(V["BLK"]); a.db(0x3C, 0xFE, SWAP_EVERY); a.rel(0x20, "s_keep")
    ld_a_mem(V["ROLE"]); a.db(0xEE, 1); ld_mem_a(V["ROLE"])
    a.db(0x3E, 4); ld_mem_a(V["COOL"])          # the new clock waits 4 frames
    a.db(0xAF)
    a.label("s_keep")
    ld_mem_a(V["BLK"])
    ld_a_mem(V["ROLE"]); a.db(0xB7, 0xC0)       # the internal clock's: frame starts it
    a.label("arm")                              # the external clock's: armed now
    ld_a_mem(V["SEQ"]); a.db(0xE0, SB); ld_mem_a(V["LASTTX"])
    a.db(0x3E, 0x80, 0xE0, SC)
    a.db(0xC9)

    # ---- helpers
    a.label("inc16")                            # (hl) += 1, 16 bits
    a.db(0x34, 0xC0, 0x23, 0x34, 0xC9)          # inc (hl); ret nz; inc hl; inc (hl); ret
    a.label("hex")                              # a as two digits at hl+
    a.db(0x47, 0xCB, 0x37, 0xE6, 0x0F, 0x3C, 0x22, 0x78, 0xE6, 0x0F, 0x3C, 0x22, 0xC9)
    a.label("print")                            # [dest lo, hi, tiles..., $FF]..., 0 0
    a.db(0x2A, 0x5F, 0x2A, 0x57, 0xB7, 0xC8)    # ld a,(hl+); ld e,a; ld a,(hl+); ld d,a; or a; ret z
    a.label("p_ch")
    a.db(0x2A, 0xFE, 0xFF); a.rel(0x28, "print")
    a.db(0x12, 0x13); a.rel(0x18, "p_ch")

    def table(name, items):
        a.label(name)
        for (row, col), s in items:
            a.dw(at(row, col)); a.db(text(s), 0xFF)
        a.db(0, 0)

    table("t_static", [((1, 1), "ZYPEROS LINK TEST"), ((3, 1), "ROLE"),
                       ((5, 1), "INT TX"), ((6, 1), "INT OK"), ((8, 1), "EXT TX"),
                       ((9, 1), "EXT OK"), ((11, 1), "BAD"), ((13, 1), "LAST RX"),
                       ((14, 1), "LAST TX"), ((17, 1), "START ON ONE SIDE")])
    table("t_int", [((3, 7), "INT CLOCK")])
    table("t_ext", [((3, 7), "EXT CLOCK")])
    table("t_wait", [((3, 7), "WAITING  ")])
    table("t_pass", [((16, 1), "PASS")])
    table("t_fail", [((16, 1), "FAIL")])
    table("t_none", [((16, 1), "    ")])
    a.label("font")
    a.db(font_bytes())
    return a.done()


# The cartridge header's logo, which a Game Boy's boot ROM checks before it
# runs a cartridge (Gambatte, with no boot ROM, does not)
LOGO = bytes.fromhex(
    "CEED6666CC0D000B03730083000C000D0008111F8889000E"
    "DCCC6EE6DDDDD999BBBB67636E0EECCCDDDC999FBBB9333E")


def rom():
    r = bytearray(0x8000)
    r[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])   # nop; jp $0150
    r[0x104:0x134] = LOGO
    r[0x134:0x143] = b"ZYPEROSLINK".ljust(15, b"\0")
    # 0x143 0 (DMG), licensee 0, cartridge 0 (ROM only), 32 KiB, no RAM
    c = code()
    assert 0x150 + len(c) <= 0x8000
    r[0x150:0x150 + len(c)] = c
    x = 0
    for i in range(0x134, 0x14D): x = (x - r[i] - 1) & 0xFF
    r[0x14D] = x
    s = (sum(r) - r[0x14E] - r[0x14F]) & 0xFFFF
    r[0x14E], r[0x14F] = s >> 8, s & 0xFF
    return bytes(r)


# ---- reading the screen back (tools/gblink-diatom.py) ----------------------

def screen_rows(pixels, w, h):
    """The 18 rows of text on a 160 x 144 frame, `pixels` a function (x, y)
    giving a luminance: each 8 x 8 cell matched against the font, '?' for
    one that is no glyph"""
    if (w, h) != (160, 144): return None
    lums = [pixels(x, y) for y in range(h) for x in range(w)]
    mid = (min(lums) + max(lums)) / 2
    glyph = {tuple(tile_rows(c)): c for c in CHARS}
    rows = []
    for ty in range(18):
        line = ""
        for tx in range(20):
            cell = []
            for y in range(8):
                bits = 0
                for x in range(8):
                    if lums[(ty * 8 + y) * w + tx * 8 + x] < mid: bits |= 0x80 >> x
                cell.append(bits)
            line += glyph.get(tuple(cell), "?")
        rows.append(line.rstrip())
    return rows


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: gblink-rom.py out.gb")
    assert len(LOGO) == 48
    with open(sys.argv[1], "wb") as f:
        f.write(rom())
