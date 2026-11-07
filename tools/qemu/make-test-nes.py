#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
"""Write a small NES test cartridge: ZyperOS's own, so a QEMU VM (or a Pixel 2)
has a game to start with nothing downloaded and no licence to worry about.

    tools/qemu/make-test-nes.py OUT.nes

What it shows, on a dark blue screen:
  - eight boxes in a row, gray, each turning white while its button is held:
    A, B, SELECT, START, UP, DOWN, LEFT, RIGHT (left to right), so a
    screenshot after a press says the press reached the game;
  - a red marker moving across a lower row, one step every four frames, so
    two screenshots apart say the game is running;
  - a 440 Hz tone while A is held, for the sound path.

An NROM cartridge (mapper 0, 16 KB of program, 8 KB of tiles), assembled
here by a few lines of Python rather than an assembler nobody has installed.
"""
import struct
import sys

OPS = {
    ("LDA", "imm"): 0xA9, ("LDA", "zp"): 0xA5, ("LDA", "abs"): 0xAD, ("LDA", "absx"): 0xBD,
    ("STA", "zp"): 0x85, ("STA", "abs"): 0x8D,
    ("LDX", "imm"): 0xA2, ("LDY", "imm"): 0xA0, ("STX", "abs"): 0x8E,
    ("INX", ""): 0xE8, ("DEX", ""): 0xCA, ("DEY", ""): 0x88,
    ("CPX", "imm"): 0xE0, ("CMP", "zp"): 0xC5,
    ("BNE", "rel"): 0xD0, ("BEQ", "rel"): 0xF0, ("BPL", "rel"): 0x10, ("BCC", "rel"): 0x90,
    ("JMP", "abs"): 0x4C, ("SEI", ""): 0x78, ("CLD", ""): 0xD8, ("TXS", ""): 0x9A,
    ("BIT", "abs"): 0x2C, ("LSR", ""): 0x4A, ("ROL", "zp"): 0x26, ("ASL", "zp"): 0x06,
    ("AND", "imm"): 0x29, ("ORA", "imm"): 0x09, ("RTI", ""): 0x40,
    ("PHA", ""): 0x48, ("PLA", ""): 0x68, ("TXA", ""): 0x8A, ("TAX", ""): 0xAA,
    ("TYA", ""): 0x98, ("TAY", ""): 0xA8, ("INC", "zp"): 0xE6,
}
SIZE = {"": 1, "imm": 2, "zp": 2, "rel": 2, "abs": 3, "absx": 3}

ORG = 0xC000
BUTTONS, SHIFT, TILE, FRAME, OLDCOL, NEWCOL = 0x00, 0x01, 0x02, 0x03, 0x04, 0x05
PPUCTRL, PPUMASK, PPUSTATUS, PPUSCROLL, PPUADDR, PPUDATA = 0x2000, 0x2001, 0x2002, 0x2005, 0x2006, 0x2007
PAD1, APUFRAME, APUSTATUS = 0x4016, 0x4017, 0x4015

PROGRAM = [
    "reset:",
    ("SEI",), ("CLD",), ("LDX", "imm", 0x40), ("STX", "abs", APUFRAME),
    ("LDX", "imm", 0xFF), ("TXS",), ("INX",),
    ("STX", "abs", PPUCTRL), ("STX", "abs", PPUMASK), ("STX", "abs", 0x4010),
    ("BIT", "abs", PPUSTATUS),
    "wait1:", ("BIT", "abs", PPUSTATUS), ("BPL", "rel", "wait1"),
    "wait2:", ("BIT", "abs", PPUSTATUS), ("BPL", "rel", "wait2"),
    # The palette
    ("LDA", "abs", PPUSTATUS), ("LDA", "imm", 0x3F), ("STA", "abs", PPUADDR),
    ("LDA", "imm", 0x00), ("STA", "abs", PPUADDR), ("LDX", "imm", 0),
    "pal:", ("LDA", "absx", "palette"), ("STA", "abs", PPUDATA), ("INX",),
    ("CPX", "imm", 32), ("BNE", "rel", "pal"),
    # The first name table and its attributes, all tile 0
    ("LDA", "imm", 0x20), ("STA", "abs", PPUADDR), ("LDA", "imm", 0x00), ("STA", "abs", PPUADDR),
    ("LDY", "imm", 4), ("LDX", "imm", 0), ("LDA", "imm", 0),
    "clear:", ("STA", "abs", PPUDATA), ("INX",), ("BNE", "rel", "clear"),
    ("DEY",), ("BNE", "rel", "clear"),
    # The tone: square 1, 50% duty, length held, constant volume 0 until A
    ("LDA", "imm", 0x01), ("STA", "abs", APUSTATUS), ("LDA", "imm", 0xB0), ("STA", "abs", 0x4000),
    ("LDA", "imm", 0x08), ("STA", "abs", 0x4001), ("LDA", "imm", 0xFD), ("STA", "abs", 0x4002),
    ("LDA", "imm", 0x08), ("STA", "abs", 0x4003),
    ("LDA", "imm", 0), ("STA", "zp", FRAME), ("STA", "zp", OLDCOL),
    ("STA", "abs", PPUSCROLL), ("STA", "abs", PPUSCROLL),
    ("LDA", "imm", 0x80), ("STA", "abs", PPUCTRL),       # NMI on
    ("LDA", "imm", 0x0A), ("STA", "abs", PPUMASK),       # background on
    "forever:", ("JMP", "abs", "forever"),

    "nmi:",
    ("PHA",), ("TXA",), ("PHA",), ("TYA",), ("PHA",),
    # The pad: A B SELECT START UP DOWN LEFT RIGHT, bit 7 to bit 0
    ("LDA", "imm", 1), ("STA", "abs", PAD1), ("LDA", "imm", 0), ("STA", "abs", PAD1),
    ("LDX", "imm", 8),
    "read:", ("LDA", "abs", PAD1), ("LSR",), ("ROL", "zp", BUTTONS), ("DEX",), ("BNE", "rel", "read"),
    # A box per button, row 14, two tiles square, three columns apart
    ("LDA", "zp", BUTTONS), ("STA", "zp", SHIFT), ("LDX", "imm", 0),
    "box:", ("ASL", "zp", SHIFT), ("LDA", "imm", 2), ("BCC", "rel", "off"), ("LDA", "imm", 1),
    "off:", ("STA", "zp", TILE),
    ("LDA", "abs", PPUSTATUS),
    ("LDA", "imm", 0x21), ("STA", "abs", PPUADDR), ("LDA", "absx", "row1"), ("STA", "abs", PPUADDR),
    ("LDA", "zp", TILE), ("STA", "abs", PPUDATA), ("STA", "abs", PPUDATA),
    ("LDA", "imm", 0x21), ("STA", "abs", PPUADDR), ("LDA", "absx", "row2"), ("STA", "abs", PPUADDR),
    ("LDA", "zp", TILE), ("STA", "abs", PPUDATA), ("STA", "abs", PPUDATA),
    ("INX",), ("CPX", "imm", 8), ("BNE", "rel", "box"),
    # The marker, row 22, a column every four frames
    ("INC", "zp", FRAME), ("LDA", "zp", FRAME), ("LSR",), ("LSR",), ("AND", "imm", 0x1F),
    ("STA", "zp", NEWCOL), ("CMP", "zp", OLDCOL), ("BEQ", "rel", "same"),
    ("LDA", "imm", 0x22), ("STA", "abs", PPUADDR), ("LDA", "zp", OLDCOL), ("ORA", "imm", 0xC0),
    ("STA", "abs", PPUADDR), ("LDA", "imm", 0), ("STA", "abs", PPUDATA),
    ("LDA", "imm", 0x22), ("STA", "abs", PPUADDR), ("LDA", "zp", NEWCOL), ("ORA", "imm", 0xC0),
    ("STA", "abs", PPUADDR), ("LDA", "imm", 3), ("STA", "abs", PPUDATA),
    ("LDA", "zp", NEWCOL), ("STA", "zp", OLDCOL),
    "same:",
    # The tone while A is held
    ("LDA", "zp", BUTTONS), ("AND", "imm", 0x80), ("BEQ", "rel", "quiet"),
    ("LDA", "imm", 0xBF), ("STA", "abs", 0x4000), ("JMP", "abs", "done"),
    "quiet:", ("LDA", "imm", 0xB0), ("STA", "abs", 0x4000),
    "done:",
    ("LDA", "imm", 0), ("STA", "abs", PPUSCROLL), ("STA", "abs", PPUSCROLL),
    ("PLA",), ("TAY",), ("PLA",), ("TAX",), ("PLA",),
    ("RTI",),
    "irq:", ("RTI",),
    # $2000 + 14 * 32 + 4 + 3i, and the row under it
    "row1:", bytes(0xC4 + 3 * i for i in range(8)),
    "row2:", bytes(0xE4 + 3 * i for i in range(8)),
    # Dark blue, then white (on), gray (off) and red (the marker)
    "palette:", bytes([0x02, 0x30, 0x00, 0x16] * 8),
]


def assemble(program):
    labels, pc = {}, ORG
    for item in program:
        if isinstance(item, str):
            labels[item.rstrip(":")] = pc
        elif isinstance(item, bytes):
            pc += len(item)
        else:
            pc += SIZE[item[1] if len(item) > 1 else ""]
    out, pc = bytearray(), ORG
    for item in program:
        if isinstance(item, str):
            continue
        if isinstance(item, bytes):
            out += item
            pc += len(item)
            continue
        mode = item[1] if len(item) > 1 else ""
        arg = item[2] if len(item) > 2 else None
        if isinstance(arg, str):
            arg = labels[arg]
        out.append(OPS[(item[0], mode)])
        if mode == "rel":
            delta = arg - (pc + 2)
            assert -128 <= delta <= 127, item
            out.append(delta & 0xFF)
        elif mode in ("imm", "zp"):
            out.append(arg)
        elif mode in ("abs", "absx"):
            out += struct.pack("<H", arg)
        pc += SIZE[mode]
    return bytes(out), labels


def main():
    code, labels = assemble(PROGRAM)
    prg = bytearray(code.ljust(0x4000 - 6, b"\xff"))
    prg += struct.pack("<HHH", labels["nmi"], labels["reset"], labels["irq"])
    # Tiles: 0 empty, 1 solid color 1, 2 solid color 2, 3 solid color 3
    chr_ = bytearray(0x2000)
    for tile, (p0, p1) in {1: (0xFF, 0x00), 2: (0x00, 0xFF), 3: (0xFF, 0xFF)}.items():
        chr_[tile * 16:tile * 16 + 8] = bytes([p0]) * 8
        chr_[tile * 16 + 8:tile * 16 + 16] = bytes([p1]) * 8
    header = b"NES\x1a" + bytes([1, 1, 0, 0]) + bytes(8)
    with open(sys.argv[1], "wb") as f:
        f.write(header + prg + chr_)


if __name__ == "__main__":
    main()
