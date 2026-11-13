#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Are the launcher's files that Diatom carries copies of still copies?

port/osdbar.c, osdbar.h, themefile.c, themefile.h and design.h, and src/p2share.h (the
shared file of the NES player 2 feature), are ZyperOS's
src/ files of the same names, which is where they are changed: the launcher,
the Pixel 2's and the Brick's ports and portkeys draw the volume bar and read
the theme from the same code. They are copies rather than an include of
../src because plastron's Buildroot takes only diatom/ to build Diatom.

A copy nothing compares goes stale without a sound. design.h did: a review on
2026-11-06 found 35 lines of src/design.h missing from it and 16 old ones in
their place (four type sizes, fifteen times, and the sounds as WAV voices
where the copy still had tones), none of which the bar reads, so nothing drew
differently and nothing failed.

So: each copy must equal its source past the first line, the SPDX line, which
is Diatom's own (it must offer MPL-2.0, Diatom's license), and past the
"Vendored from" note under it. Skipped, not failed, where there is no ../src:
the diatom/ that plastron builds from has none.
"""
import difflib, pathlib, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT.parent / "src"
# (folder under diatom/, file): the copy is diatom/<folder>/<file>, its source src/<file>
COPIES = (
    ("port", "osdbar.c"), ("port", "osdbar.h"), ("port", "themefile.c"),
    ("port", "themefile.h"), ("port", "design.h"), ("src", "p2share.h"),
)

if not SRC.is_dir():
    print("skip: no ../src beside diatom/, so nothing to compare the copies with")
    sys.exit(0)


def body(name, lines):
    """The copy past its SPDX line and its note, or a reason it has neither."""
    if not lines or "SPDX-License-Identifier:" not in lines[0]:
        return None, "line 1 is not an SPDX line"
    if "MPL-2.0" not in lines[0]:
        return None, "its SPDX line does not offer MPL-2.0, Diatom's license"
    if len(lines) < 2 or not lines[1].startswith(f"/* Vendored from ZyperOS src/{name},"):
        return None, f"line 2 is not \"/* Vendored from ZyperOS src/{name}, ...\""
    for i in range(1, len(lines)):
        if lines[i].rstrip().endswith("*/"):
            return lines[i + 1:], None
    return None, "its note does not end"


fails = 0
for folder, name in COPIES:
    src = (SRC / name).read_text().splitlines(keepends=True)
    cp = (ROOT / folder / name).read_text().splitlines(keepends=True)
    got, why = body(name, cp)
    if why:
        print(f"FAIL: {folder}/{name}: {why}")
        fails += 1
        continue
    if got != src[1:]:
        print(f"FAIL: {folder}/{name} is not src/{name} past its SPDX line and note:")
        diff = list(difflib.unified_diff(src[1:], got, f"src/{name}", f"diatom/{folder}/{name}"))
        sys.stdout.writelines(diff[:40])
        if len(diff) > 40:
            print(f"... {len(diff) - 40} more diff lines")
        print(f"      copy src/{name} over it below line {len(cp) - len(got)}")
        fails += 1
        continue
    print(f"ok: {folder}/{name} is src/{name}")

sys.exit(1 if fails else 0)
