#!/usr/bin/env python3
"""The device's SteamGridDB match rule against the computer tool's.

    make check-sgdb

tools/scrape-sgdb.py decides what to search for (title) and whether a name is
the game (close, at 0.85, with difflib). src/sgdb.c is the same rule in C for
the device, and a drift there does not fail: it finds a different game, or
none, which looks exactly like SteamGridDB not having it. So both run over the
same names here and must agree - titles exactly, ratios to the last bit, since
0.85 is a hard line.

The tool needs Pillow to run, and this needs nothing: its two functions are
read out of its source and run beside scrape-art.py's norm(), which they use.
Names come from the real ROM library too when one is reachable.
"""
import ast
import difflib
import importlib.util
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build-native", "sgdb-check")


def tool():
    spec = importlib.util.spec_from_file_location(
        "scrape_art", os.path.join(ROOT, "tools", "scrape-art.py"))
    art = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(art)
    src = open(os.path.join(ROOT, "tools", "scrape-sgdb.py")).read()
    tree = ast.parse(src)
    keep = [n for n in tree.body
            if isinstance(n, ast.FunctionDef) and n.name in ("title", "close")]
    ns = {"re": re, "difflib": difflib, "art": art}
    exec(compile(ast.Module(body=keep, type_ignores=[]), "scrape-sgdb.py", "exec"), ns)
    return ns["title"], ns["close"]


STEMS = [
    "Celeste Classic (homebrew)",
    "036 - Super Mario Land (World)",
    "036 Super Mario Land",
    "036-Super Mario Land",
    "036 -Super Mario Land",
    "12345 Five Digits",
    "1942 (USA)",
    "2048 (homebrew)",
    "StardewValley",
    "Stardew_Valley-Port",
    "Super_Mario_Bros_3",
    "Shovel Knight [Hack] (Proto)",
    "Unclosed (tag",
    "Unclosed [tag",
    "(Proto) [b]",
    "  spaced   out  ",
    "Pokemon - Crystal Version (USA, Europe) (Rev 1)",
    "Legend of Zelda, The - A Link to the Past (USA)",
    "A",
    "",
    "Cave Story (Translated) [T+Eng]",
    "OpenTyrian2000",
    "iCarly",
    "Ys I & II",
    "Brain Lord (Translated En)",
]

PAIRS = [
    ("Celeste Classic", "Celeste"),
    ("Celeste Classic", "Celeste Classic"),
    ("Celeste Classic", "Celeste: Classic"),
    ("Super Mario Land", "Super Mario Land 2: 6 Golden Coins"),
    ("Cave Story", "Cave Story+"),
    ("Doukutsu Monogatari", "Cave Story"),
    ("Tetris", "Tetris DX"),
    ("Tetris", "Tetris 99"),
    ("abcd", "bcde"),
    ("aaaa", "aaab"),
    ("ababab", "bababa"),
    ("The Legend of Zelda", "Legend of Zelda, The"),
    ("Sonic the Hedgehog 2", "Sonic the Hedgehog"),
    ("Pokemon Crystal", "Pokémon Crystal Version"),
    ("", ""),
    ("", "Something"),
    ("Final Fantasy VII", "Final Fantasy VIII"),
    ("Shovel Knight", "Shovel Knight: Treasure Trove"),
    ("Stardew Valley", "Stardew Valley"),
    ("x" * 199, "x" * 150),
]


def library_stems(roms):
    out = []
    if not os.path.isdir(roms):
        return out
    for folder in sorted(os.listdir(roms)):
        d = os.path.join(roms, folder)
        if folder.startswith(".") or not os.path.isdir(d):
            continue
        try:
            names = sorted(os.listdir(d))
        except OSError:
            continue
        for f in names:
            if not f.startswith("."):
                out.append(os.path.splitext(f)[0])
    return out


def run(mode, lines):
    r = subprocess.run([BIN, mode], input="\n".join(lines) + "\n",
                       capture_output=True, text=True, check=True)
    return r.stdout.split("\n")[:len(lines)]


def main():
    title, close = tool()
    stems = STEMS + library_stems(os.environ.get(
        "ZYPEROS_ROMS", "/Volumes/ZYPEROS/Roms"))
    stems = [s for s in stems if "\n" not in s and "\t" not in s]

    bad = 0
    got = run("--title", stems)
    for s, c in zip(stems, got):
        if title(s) != c:
            bad += 1
            print(f"  title differs: {s!r}: tool {title(s)!r}, device {c!r}")

    # Every library title against a few others, the shape a search reply has
    pairs = list(PAIRS)
    titled = [title(s) for s in stems if title(s)]
    for i, t in enumerate(titled[:400]):
        pairs.append((t, titled[(i * 7 + 3) % len(titled)]))
        pairs.append((t, t + " Deluxe"))
    got = run("--ratio", [a + "\t" + b for a, b in pairs])
    near = 0
    for (a, b), c in zip(pairs, got):
        want = close(a, b)
        if repr(float(c)) != repr(want):
            bad += 1
            print(f"  ratio differs: {a!r} / {b!r}: tool {want!r}, device {c}")
        if abs(want - 0.85) < 0.05:
            near += 1

    print(f"  {len(stems)} titles and {len(pairs)} ratios ({near} near 0.85), "
          f"{bad} disagreement(s)")
    if bad:
        print("FAIL: the device's SteamGridDB match differs from the tool's")
        return 1
    print("ok: the device matches SteamGridDB names as the tool does")
    return 0


if __name__ == "__main__":
    sys.exit(main())
