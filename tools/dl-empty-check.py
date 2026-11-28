#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""MENU > Downloads on a card with no games, one game, and PICO-8 carts alone
(src/dl_screen.c, src/dlcount.c): the page gets its count, and in a bounded
time.

    python3 tools/dl-empty-check.py        (make check-dl-empty, after make native)
    python3 tools/dl-empty-check.py --binary PATH    another launcher's walk

Tarik's R36S on 1.1.1-rc.1, an empty card: MENU > Downloads said Counting
and never anything else. A card with no system was never counted
(dl_count_again only counted when there was a system to step), and once that
count ended at once the page took its turn after it, so it waited for a turn
that had already come. This fails unless, with the real launcher headless
and ZYPEROS_DOWNLOADS_TEST's `counted` step (until the page has a count):

  - an empty card has its count within BOUND seconds of the launch, no
    system in it, and the hub says so: its second line No games yet and the
    empty state (no Download that gets nothing, no Off); A and Y leave it on
    the hub; in both themes, the shots kept in build-native/dl-empty-check/;
  - a card with one game has it counted, one system, the second line no
    longer Counting, Covers 1 missing;
  - a card of PICO-8 carts alone has them counted, one system, and Covers
    Complete: a .p8.png cart is its own cover (#496).
"""
import importlib.util
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rs", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)

BOUND = 10.0     # seconds from the launch to the page's count: an empty card's is at once
OUT = ROOT / "build-native" / "dl-empty-check"
STEP = re.compile(r"downloads-test: (\S+) view=(\d+) depth=(\d+) counted=(yes|no) systems=(\d+) counting=(\d) live=(\d)")
HUB = re.compile(r"downloads-test: hub empty=(\d) sub=(.*?)\|download=(.*?)\|0=(.*?)\|1=(.*?)\|2=(.*?)\|3=(.*?)\|4=(.*)$")


def card_empty(card):
    R.card_dirs(card)
    R.set_settings(card, {"tour.done": "1"}, True)


def card_one(card):
    card_empty(card)
    (card / "Roms" / "Game Boy").mkdir(parents=True)
    (card / "Roms" / "Game Boy" / "Tetris (World).gb").write_bytes(b"\0" * 32768)


def card_pico8(card):
    card_empty(card)
    d = card / "Roms" / "PICO-8"
    d.mkdir(parents=True)
    for name in ("Celeste Classic.p8.png", "Ember Keep.p8.png"):
        R.png_rgb(d / name, 160, 205, [[(41, 173, 255)] * 160] * 205)
    # fake08, the shelf's own PICO-8 core, as the payload has it (a stand-in)
    (card / "ZyperOS" / "cores").mkdir(parents=True, exist_ok=True)
    (card / "ZyperOS" / "cores" / "fake08_libretro.so").write_bytes(b"")


def walk(binary, make, script, theme):
    """The page on a card `make` made: each step's state, the hub's words,
    and how long the launch took to the page's count"""
    steps, hubs, out = [], [], []
    counted_at = None
    with tempfile.TemporaryDirectory(prefix="zyperos-dl-empty-") as tmp:
        card = Path(tmp) / "card"
        make(card)
        # the theme as the card's setting: --theme is a shot's alone
        R.set_settings(card, {"appearance": theme}, True)
        env = dict(R.env_for(card), ZYPEROS_DOWNLOADS_TEST=script)
        t0 = time.monotonic()
        p = subprocess.Popen([str(binary), "--lang", "en"], cwd=ROOT, env=env, text=True,
                             errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            for line in p.stdout:
                out.append(line)
                line = line.rstrip("\n")
                m = STEP.match(line)
                if m:
                    steps.append({"tok": m.group(1), "view": int(m.group(2)), "counted": m.group(4) == "yes",
                                  "systems": int(m.group(5))})
                    # a step's line is said as it is taken: the one after
                    # `counted` is the first once the wait is over
                    if len(steps) >= 2 and steps[-2]["tok"] == "counted" and counted_at is None:
                        counted_at = time.monotonic() - t0
                    continue
                m = HUB.match(line)
                if m:
                    hubs.append({"empty": m.group(1) == "1", "sub": m.group(2), "download": m.group(3),
                                 "kinds": [m.group(4 + k) for k in range(5)]})
            code = p.wait(timeout=60)
        finally:
            if p.poll() is None:
                p.kill()
                code = -9
    return code, steps, hubs, counted_at, "".join(out)


def main():
    binary = ROOT / "build-native" / "zyperos"
    if len(sys.argv) == 3 and sys.argv[1] == "--binary":
        binary = Path(sys.argv[2]).resolve()
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    if not binary.is_file():
        print("FAIL no build-native/zyperos: make native first")
        return 1
    OUT.mkdir(parents=True, exist_ok=True)
    fails, logs = [], []

    def need(cond, what):
        if not cond:
            fails.append(what)
        return cond

    def counted(name, code, steps, at):
        need(code == 0, "%s: the launcher ended with %d" % (name, code))
        got = [steps[i + 1] for i in range(len(steps) - 1) if steps[i]["tok"] == "counted"]
        if not need(got and got[0]["counted"], "%s: the page never had its count (still Counting)" % name):
            return None
        need(at is not None and at < BOUND, "%s: the count took %.1f s from the launch (at most %.0f)"
             % (name, at or -1, BOUND))
        return got[0]

    for theme in ("light", "dark"):
        shot = OUT / ("empty-%s.png" % theme)
        if shot.exists():
            shot.unlink()
        name = "an empty card (%s)" % theme
        code, steps, hubs, at, log = walk(binary, card_empty, "counted,hub,shot=%s,A,Y,LEFT,hub" % shot, theme)
        logs.append(log)
        c = counted(name, code, steps, at)
        if c:
            need(c["systems"] == 0, "%s: %d systems counted on a card with none" % (name, c["systems"]))
        if need(len(hubs) == 2, "%s: the walk did not get through (%d hubs)" % (name, len(hubs))):
            need(hubs[0]["empty"] and hubs[0]["sub"] == "No games yet",
                 "%s: the hub is not the empty state: %s" % (name, hubs[0]))
            need(hubs[1]["empty"], "%s: A, Y and LEFT left the empty state" % name)
            need(all(s["view"] == 0 for s in steps), "%s: a key opened another view: %s" % (name, steps))
        need(shot.is_file(), "%s: no shot at %s" % (name, shot))

    code, steps, hubs, at, log = walk(binary, card_one, "counted,hub", "dark")
    logs.append(log)
    c = counted("one game", code, steps, at)
    if c:
        need(c["systems"] == 1, "one game: %d systems counted, not 1" % c["systems"])
    if need(len(hubs) == 1, "one game: the walk did not get through"):
        h = hubs[0]
        need(not h["empty"] and h["sub"] != "Counting…" and h["sub"] != "No games yet",
             "one game: the hub's second line is %r" % h["sub"])
        need(h["kinds"][0] == "1 missing", "one game: Covers says %r, not 1 missing" % h["kinds"][0])

    code, steps, hubs, at, log = walk(binary, card_pico8, "counted,hub", "dark")
    logs.append(log)
    c = counted("PICO-8 carts alone", code, steps, at)
    if c:
        need(c["systems"] == 1, "PICO-8 carts alone: %d systems counted, not 1" % c["systems"])
    if need(len(hubs) == 1, "PICO-8 carts alone: the walk did not get through"):
        h = hubs[0]
        need(not h["empty"], "PICO-8 carts alone: the hub says the card has no games")
        need(h["kinds"][0] == "Complete", "PICO-8 carts alone: Covers says %r, not Complete (a cart is its cover)"
             % h["kinds"][0])

    (ROOT / "build-native" / "dl-empty-check.log").write_text("\n----\n".join(logs))
    if fails:
        for f in fails:
            print("FAIL", f)
        print("(the launcher's output: build-native/dl-empty-check.log)")
        return 1
    print("ok: MENU > Downloads counts an empty card, one game and PICO-8 carts alone, and says No games yet")
    return 0


if __name__ == "__main__":
    sys.exit(main())
