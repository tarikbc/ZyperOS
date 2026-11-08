#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Write a small Nintendo DS test card: ZyperOS's own, so the DS shelf can be
tried in a QEMU VM (or on a Pixel 2) with no game of anyone else's.

    tools/qemu/make-test-nds.py OUT.nds

What it shows is the language the game was given: a DS game reads it from
the firmware's user settings, which the firmware (or DSperate's direct boot)
copies into main RAM at 0x027FFC80 as the game starts, the language in the
low three bits of the halfword at +0x64. The program reads that halfword over
and over and paints the top screen's backdrop with its language's colour,
so a screenshot says which language the game's memory holds - after a
start, a Reset, or a save state put back:

    japanese white   english blue    french green    german cyan
    italian magenta  spanish red     chinese yellow  (7, none) black

The bottom screen is grey. Nothing else: no buttons, no sound, no save.

A card image with no secure area (the ARM9 binary at 0x200, which direct
boot copies as it is) and no Nintendo logo, which no emulator here checks.
The ARM9 program is the assembly below, assembled once with
`clang -target armv5te-none-eabi` and kept as its words, so making the card
needs Python alone. The ARM7 waits in a loop.
"""
import struct
import sys

# _start:  mov   r0, #0x04000000      DISPCNT, engine A (the top screen)
#          mov   r1, #0x00010000      display mode 1, no layer on: the backdrop
#          str   r1, [r0]
#          ldr   r0, =0x04001000      DISPCNT, engine B (the bottom screen)
#          str   r1, [r0]
#          ldr   r2, =0x027FFCE4      the user settings' language halfword
#          adr   r3, colours
#          mov   r4, #0x05000000      engine A's backdrop colour
#          ldr   r5, =0x05000400      engine B's
#          ldr   r6, =0x4210          grey
# loop:    ldrh  r7, [r2]
#          and   r7, r7, #7
#          lsl   r7, r7, #1
#          ldrh  r8, [r3, r7]
#          strh  r8, [r4]
#          strh  r6, [r5]
#          b     loop
# colours: .hword 0x7FFF, 0x7C00, 0x03E0, 0x7FE0, 0x7C1F, 0x001F, 0x03FF, 0x0000
#          (then the literal pool)
ARM9 = [
    0xE3A00301, 0xE3A01801, 0xE5801000, 0xE59F0040, 0xE5801000, 0xE59F203C,
    0xE28F3024, 0xE3A04405, 0xE59F5034, 0xE59F6034,
    0xE1D270B0, 0xE2077007, 0xE1A07087, 0xE19380B7, 0xE1C480B0, 0xE1C560B0,
    0xEAFFFFF8,
    0x7C007FFF, 0x7FE003E0, 0x001F7C1F, 0x000003FF,
    0x04001000, 0x027FFCE4, 0x05000400, 0x00004210,
]
ARM7 = [0xEAFFFFFE]   # b .

ARM9_ROM, ARM9_RAM = 0x200, 0x02000000
ARM7_ROM, ARM7_RAM = 0x400, 0x037F8000
SIZE = 0x20000        # 128 KB, the smallest card size the header can name


def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def card():
    rom = bytearray(SIZE)
    arm9 = struct.pack("<%dI" % len(ARM9), *ARM9)
    arm7 = struct.pack("<%dI" % len(ARM7), *ARM7)
    rom[0x000:0x00C] = b"ZYPEROS LANG"         # title
    rom[0x00C:0x010] = b"ZLNG"                 # game code: no game's
    rom[0x010:0x012] = b"00"                   # maker
    struct.pack_into("<IIII", rom, 0x020, ARM9_ROM, ARM9_RAM, ARM9_RAM, len(arm9))
    struct.pack_into("<IIII", rom, 0x030, ARM7_ROM, ARM7_RAM, ARM7_RAM, len(arm7))
    struct.pack_into("<II", rom, 0x060, 0x00586000, 0x001808F8)   # the usual card timings
    struct.pack_into("<I", rom, 0x080, ARM7_ROM + len(arm7))       # used size
    struct.pack_into("<I", rom, 0x084, 0x4000)                      # header size
    rom[ARM9_ROM:ARM9_ROM + len(arm9)] = arm9
    rom[ARM7_ROM:ARM7_ROM + len(arm7)] = arm7
    struct.pack_into("<H", rom, 0x15E, crc16(rom[0:0x15E]))
    return bytes(rom)


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: make-test-nds.py OUT.nds")
    with open(sys.argv[1], "wb") as f:
        f.write(card())


if __name__ == "__main__":
    main()
