#!/usr/bin/env python3
"""DraStic's Nintendo DS saves to DSperate's, and back.

DraStic writes a battery save as <rom name>.dsv: the save chip's bytes, then
the 122-byte footer DeSmuME introduced and DraStic kept -

    "|<--Snip above here to create a raw sav by excluding this DeSmuME
    savedata footer:" (82 bytes), six little-endian u32s (the bytes in use,
    the chip's size, a type DraStic leaves 0, the address width in bytes, the
    chip's size again and a zero), and "|-DESMUME SAVE-|" (16 bytes).

Read from ROGUE OS's own .dsv files on 2026-11-03: New Super Mario Bros is
8314 bytes, an 8 KB EEPROM (2-byte addresses) and the footer; Mario Kart DS
is 262266, a 256 KB FLASH (3-byte addresses) and the footer.

Diatom keeps a libretro core's battery save as <rom name>.srm, the chip's
bytes alone, and hands the core exactly as many bytes as the core's save chip
holds. So a .dsv renamed .srm would mostly work - the footer lies past the
chip and is never read - but Diatom would log the size mismatch every launch
and write the file back without the footer, and a game DSperate's save list
does not name would have its chip sized from the file, footer included. The
footer comes off here instead, and goes back on for the other way.

ZyperOS does the .dsv to .srm step by itself (src/main.c, dsv_import) the
first time a DS game runs on DSperate with a .dsv and no .srm beside it in
Saves/, so this script is for doing it by hand, for going back to DraStic,
and for checking a file:

    tools/dsv2srm.py info  <file>              what it is
    tools/dsv2srm.py tosrm <file.dsv> [out]    footer off  (default: .srm beside it)
    tools/dsv2srm.py todsv <file.srm> [out]    footer on   (default: .dsv beside it)

It never overwrites an existing file.
"""
import os
import struct
import sys

COOKIE = b"|-DESMUME SAVE-|"
SNIP = (b"|<--Snip above here to create a raw sav by excluding this "
        b"DeSmuME savedata footer:")
FOOTER = 122
assert len(SNIP) + 6 * 4 + len(COOKIE) == FOOTER

# The address width for each chip size: 1 byte for the 512 B EEPROM, 2 for
# the 8 and 64 KB ones, 3 for FLASH - 2 and 3 in the two files above.
CHIPS = {512: 1, 8192: 2, 65536: 2, 131072: 3, 262144: 3, 524288: 3,
         1048576: 3, 8388608: 3}


def is_dsv(data):
    return len(data) >= FOOTER and data.endswith(COOKIE) and \
        data[-FOOTER:-FOOTER + len(SNIP)] == SNIP


def footer(size):
    return SNIP + struct.pack("<6I", size, size, 0, CHIPS.get(size, 3), size, 0) + COOKIE


def write_new(path, data):
    if os.path.exists(path):
        sys.exit(f"{path} exists; not overwriting it")
    with open(path + ".tmp", "wb") as f:
        f.write(data)
    os.rename(path + ".tmp", path)
    print(f"wrote {path} ({len(data)} bytes)")


def main(argv):
    if len(argv) < 3 or argv[1] not in ("info", "tosrm", "todsv"):
        sys.exit(__doc__)
    with open(argv[2], "rb") as f:
        data = f.read()
    stem = os.path.splitext(argv[2])[0]

    if argv[1] == "info":
        if is_dsv(data):
            used, size, kind, width, _, _ = struct.unpack(
                "<6I", data[-FOOTER + len(SNIP):-len(COOKIE)])
            print(f"DraStic/DeSmuME .dsv: {len(data) - FOOTER} bytes of save, "
                  f"footer says {used} used of a {size}-byte chip, type {kind}, "
                  f"{width}-byte addresses"
                  + ("" if footer(len(data) - FOOTER) == data[-FOOTER:]
                     else " (a footer todsv would not write the same)"))
        else:
            print(f"raw save, {len(data)} bytes"
                  + ("" if len(data) in CHIPS else " - no DS save chip is that size"))
        return
    if argv[1] == "tosrm":
        if not is_dsv(data):
            sys.exit(f"{argv[2]} has no DeSmuME footer; it is raw already")
        write_new(argv[3] if len(argv) > 3 else stem + ".srm", data[:-FOOTER])
        return
    if is_dsv(data):
        sys.exit(f"{argv[2]} has a footer already")
    if len(data) not in CHIPS:
        sys.exit(f"{argv[2]} is {len(data)} bytes, which no DS save chip is")
    write_new(argv[3] if len(argv) > 3 else stem + ".dsv", data + footer(len(data)))


if __name__ == "__main__":
    main(sys.argv)
