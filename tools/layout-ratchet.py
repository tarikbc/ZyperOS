#!/usr/bin/env python3
"""The layout ratchet (DESIGN.md "The system", round 4): the launcher's typed
layout numbers may only go down.

A screen moved onto the layout engine (src/ds/flex.h) or the tokens
(tools/designdef.py) takes its places by rule; one still typing them keeps its
numbers. This counts, in each of the launcher's own sources, the three kinds
of a typed place:

  - a layout #define set to a number: a name ending in _X, _Y, _W, _H, _TOP,
    _GAP, _PAD, _CX, _CY, _X0, _X1, _Y0, _Y1, _ROW, _STEP, _INSET, _MARGIN,
    _LEFT, _RIGHT, _DX or _DY, with a value of two digits or more;
  - words placed at a typed height: ui_cap_y(font, NUMBER);
  - a rectangle typed in place: (SDL_Rect){ or (SDL_FRect){ with a number of
    two digits or more for its x or its y;

and holds each file's count to tools/layout-ratchet.txt. A count over its
line fails: take the place from the engine or a token, or, where a number is
the screen's own with a reason beside it, raise the line in the same change so
the review sees it. A count under its line fails too, until the line is
lowered (--write), so a number taken out cannot come back unseen.

    python3 tools/layout-ratchet.py           check (make check-layout-ratchet)
    python3 tools/layout-ratchet.py --write   write tools/layout-ratchet.txt
    python3 tools/layout-ratchet.py --show F  the places counted in src/F
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
BASE = os.path.join(ROOT, "tools", "layout-ratchet.txt")

# Generated, or the engine itself: never typed by a screen
SKIP = {"design.h", "ds/layout.h", "ds/flex.c", "ds/flex.h", "hudglyphs.h", "i18n_tables.c", "i18n_ids.h"}
SKIP_RE = re.compile(r"(_data\.h|_glyphs?\.h|^i18n_)")
# Drawings: their numbers are the shape of a picture (a handheld in
# millimeters, Zyper's face, MP4's cassette, its pixel tape and record), not places on a screen, and no
# token holds them
ART = {"tourdevice.c", "zypermark.h", "cassette.c", "cassette.h", "pixtape.c", "pixtape.h", "vinyl.c", "vinyl.h"}

SUFFIX = r"(?:X|Y|W|H|TOP|GAP|PAD|CX|CY|X0|X1|Y0|Y1|ROW|STEP|INSET|MARGIN|LEFT|RIGHT|DX|DY)"
# A value of numbers alone, an arithmetic of them included ("(800-40)"): a
# value naming another token or a place is a rule, not a typed number
DEFINE = re.compile(r"^\s*#\s*define\s+[A-Z][A-Z0-9_]*_" + SUFFIX + r"\s+(?=[\d\s()+\-*/.f]*\d{2,})[\d\s()+\-*/.f]+$")
CAP_Y = re.compile(r"\bui_cap_y\s*\(\s*[^,()]+(?:\([^()]*\))?\s*,\s*-?\d{2,}\s*\)")
# A rectangle typed in place: a compound literal, or one declared with its
# numbers ("static const SDL_FRect FOCUS={536,112,448,448}")
RECT = re.compile(r"(?:\(\s*SDL_F?Rect\s*\)|\bSDL_F?Rect\s+\w+\s*=)\s*\{\s*([^,{}]+?)\s*,\s*([^,{}]+?)\s*,")
NUM = re.compile(r"^-?\d{2,}(?:\.\d*)?f?$")


def strip_comments(text):
    """The text with its comments and strings blanked, its lines kept"""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append('"' + " " * (min(j, n) - i - 1) + '"')
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def places(path):
    """The typed places in a source: (line number, line)"""
    with open(path, encoding="utf-8", errors="replace") as f:
        raw = f.read()
    text = strip_comments(raw)
    lines = raw.split("\n")
    found = []
    for k, line in enumerate(text.split("\n")):
        hits = 0
        if DEFINE.match(line):
            hits += 1
        hits += len(CAP_Y.findall(line))
        for x, y in RECT.findall(line):
            if NUM.match(x.strip()) or NUM.match(y.strip()):
                hits += 1
        for _ in range(hits):
            found.append((k + 1, lines[k].strip()))
    return found


def sources():
    for dirpath, _, names in os.walk(SRC):
        for name in sorted(names):
            if not name.endswith((".c", ".h")):
                continue
            rel = os.path.relpath(os.path.join(dirpath, name), SRC)
            if rel in SKIP or rel in ART or SKIP_RE.search(name):
                continue
            yield rel


def counts():
    return {rel: len(places(os.path.join(SRC, rel))) for rel in sorted(sources())}


def read_base():
    base = {}
    if os.path.exists(BASE):
        for line in open(BASE, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            n, rel = line.split(None, 1)
            base[rel] = int(n)
    return base


def main():
    args = sys.argv[1:]
    if args[:1] == ["--show"] and len(args) == 2:
        for k, line in places(os.path.join(SRC, args[1])):
            print("%s:%d: %s" % (args[1], k, line))
        return 0
    now = counts()
    if args == ["--write"]:
        with open(BASE, "w", encoding="utf-8") as f:
            f.write("# The launcher's typed layout numbers per source (tools/layout-ratchet.py).\n"
                    "# Only ever lowered: a screen on the layout engine or the tokens has none.\n")
            for rel, n in now.items():
                if n:
                    f.write("%d %s\n" % (n, rel))
        print("wrote %s: %d typed layout numbers in %d files" % (os.path.relpath(BASE, ROOT), sum(now.values()),
                                                         sum(1 for n in now.values() if n)))
        return 0
    if args:
        print(__doc__)
        return 2
    base = read_base()
    bad = 0
    for rel, n in now.items():
        was = base.get(rel, 0)
        if n > was:
            bad += 1
            print("FAIL src/%s: %d typed layout numbers, %d allowed: take the new ones from the layout engine "
                  "or a token (tools/layout-ratchet.py --show %s lists them)" % (rel, n, was, rel))
        elif n < was:
            bad += 1
            print("FAIL src/%s: %d typed layout numbers, the ratchet says %d: lower it "
                  "(python3 tools/layout-ratchet.py --write) so they cannot come back" % (rel, n, was))
    for rel in base:
        if rel not in now:
            bad += 1
            print("FAIL src/%s is gone: take its line out (python3 tools/layout-ratchet.py --write)" % rel)
    total = sum(now.values())
    if bad:
        print("layout ratchet: %d file(s) off their line" % bad)
        return 1
    print("ok: %d typed layout numbers in %d files, none more than the ratchet allows" %
          (total, sum(1 for n in now.values() if n)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
