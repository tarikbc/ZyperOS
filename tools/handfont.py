#!/usr/bin/env python3
"""MP4's tape labels' handwriting: Caveat SemiBold, a static cut.

    python3 tools/handfont.py SRC     res/fonts/hand.ttf from SRC, Caveat's
                                      variable font (needs fontTools)

The tape labels are handwritten (my pick in the Tape Styles labs,
2026-11-23: the song and the line under the window, on every style): Caveat,
by Impallari Type (github.com/googlefonts/caveat), as Google Fonts ships it,
ofl/caveat/Caveat[wght].ttf at google/fonts 82823f5 (2026-11-18), under the
SIL Open Font License 1.1, LICENSES/OFL-Caveat.txt. It is a variable font
from 400 to 700, and SDL_ttf opens a variable font at its default, Regular;
this makes the static instance at 600, SemiBold, the weight the labs drew,
named so (family "Caveat SemiBold", the typographic family Caveat). Its
characters are Caveat's own: Latin and Cyrillic; a label writes what it has
not run by run in Lexend or Noto (src/ui.c, ui_hand_face). 267 KB.

The source is not kept here: SRC is wherever a copy is.
"""
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "res", "fonts", "hand.ttf")


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    try:
        from fontTools.ttLib import TTFont
        from fontTools.varLib import instancer
    except ImportError:
        sys.exit("handfont: needs fontTools (pip install fonttools)")
    font = TTFont(sys.argv[1])
    cut = instancer.instantiateVariableFont(font, {"wght": 600})
    cut["OS/2"].usWeightClass = 600
    names = cut["name"]
    for rec in list(names.names):
        if rec.nameID in (16, 17, 25):
            names.removeNames(nameID=rec.nameID)
    for nid, value in ((1, "Caveat SemiBold"), (2, "Regular"), (4, "Caveat SemiBold"),
                       (6, "Caveat-SemiBold"), (16, "Caveat"), (17, "SemiBold")):
        names.setName(value, nid, 3, 1, 0x409)
        names.setName(value, nid, 1, 0, 0)
    cut.save(OUT)
    print("handfont: wrote %s, %d bytes" % (os.path.relpath(OUT, ROOT), os.path.getsize(OUT)))


if __name__ == "__main__":
    main()
