#!/usr/bin/env python3
"""Each handheld's package list (plastron/buildroot/board/<board>/
third-party-packages.md) has a row for every package its defconfig turns on
that any board's list names:

    make check-third-party-rows

The list is made from a release build's manifest (plastron's
scripts/third-party.py), and release.sh refuses a list that no longer
matches - at the very end of a release (the Smart Pro's lacked dav1d,
usbdrive and zyperos-eq on 2026-11-24). This finds that gap offline: a
package another board's list credits and this board's defconfig enables is
one this board's list must credit too. A package no list names yet is the
release build's to find."""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BR = os.path.join(ROOT, "plastron", "buildroot")
BOARDS = {"brick": "brick", "px2": "px2", "smartpro": "smartpro"}


def norm(name):
    return name.strip().lower().replace("_", "-")


def rows(board):
    with open(os.path.join(BR, "board", board, "third-party-packages.md"), encoding="utf-8") as f:
        return {norm(m.group(1)) for m in re.finditer(r"^\| ([^|]+?) \|", f.read(), re.M)} - {"package", "---"}


def enabled(board):
    with open(os.path.join(BR, "configs", board + "_defconfig"), encoding="utf-8") as f:
        return {norm(m.group(1)) for m in re.finditer(r"^BR2_PACKAGE_([A-Z0-9_]+)=y", f.read(), re.M)}


def main():
    lists = {b: rows(b) for b in BOARDS}
    known = set().union(*lists.values())
    bad = 0
    for b in BOARDS:
        missing = sorted((enabled(BOARDS[b]) & known) - lists[b])
        for name in missing:
            bad += 1
            print(f"  FAIL: board/{b}/third-party-packages.md has no row for {name}, "
                  f"which configs/{b}_defconfig turns on")
    if bad:
        print("run plastron's make third-party after a release build, or add the row as the other boards have it")
        return 1
    print(f"ok: each board's package list credits every listed package its defconfig turns on ({len(known)} names)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
