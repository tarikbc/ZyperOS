#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""A game's panel in MENU > Downloads, looking at the card (src/dl_screen.c's
one_look): Videos says On card only when the game's video is there.

    python3 tools/dl-look-check.py        (make check-dl-look, after make native)
    python3 tools/dl-look-check.py --binary PATH    another launcher's look

The real launcher, headless, on readme-shots' demo card, plays
ZYPEROS_DOWNLOADS_TEST's script (dl_test): look=FOLDER|NAME is the panel's
look after a file lands or its Cover and Video page closes, on the game as
its system's list read it, each kind's mark said before and after.

The panel took the snap's path for the snap: snaps_path answers whether a
path fits, not whether a file is there, so after its first look every game's
Videos said On card, and Get What's Missing asked for no video
(one_wants_video). This fails unless, on PlayStation's shelf: a game with
its own snap has it; a game with none does not, and keeps the mark its list
gave it; a game whose snap is an empty file does not, as the shelf plays no
such snap (the list, one listing of .media with no stat a game, still
counts the file); a game with EmulationStation's video
(videos/<name>-video.mp4) has it; and a game whose snap is deleted after a
look (as the Cover and Video page's Delete does) is missing it at the next
look.
"""
import importlib.util
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rs", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)

OWN = "Crash Bandicoot (USA)"         # its own snap, deleted after the first look
NONE = "Spyro the Dragon (USA)"       # no video anywhere
EMPTY = "Tekken 3 (USA)"              # an empty snap
ES = "Metal Gear Solid (USA)"         # EmulationStation's video
# Between the two looks at OWN, the launcher waits until the check has
# deleted its snap (gone=PATH, at most 30 s), not a fixed 1500 ms on its own
# clock: on a loaded machine the check read the first look's line late, and
# the second look still found the snap (2026-11-24, make check's parallel
# run). A delete that never comes still fails: the wait ends and the look
# finds it.
def script(snap):
    return ",".join(["wait=300", f"look=PlayStation|{OWN}", f"gone={snap}", f"look=PlayStation|{NONE}",
                     f"look=PlayStation|{EMPTY}", f"look=PlayStation|{ES}", f"look=PlayStation|{OWN}"])


LOOK = re.compile(r"downloads-test: look PlayStation\|(.*) videos=(\d+) was(.*) now(.*)$")


def marks(text):
    return {int(k): v for k, v in (p.split("=") for p in text.split())}


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
    looks, out = [], []
    with tempfile.TemporaryDirectory(prefix="zyperos-dl-look-") as tmp:
        card = Path(tmp) / "card"
        R.demo_card(card, None, {"artwork": {}})
        R.set_settings(card, {"tour.done": "1"}, True)
        ps = card / "Roms" / "PlayStation"
        snap = ps / ".media" / (OWN + ".mp4")
        snap.parent.mkdir(parents=True, exist_ok=True)
        snap.write_bytes(b"\0\0\0\x18ftypmp42")
        (ps / ".media" / (EMPTY + ".mp4")).write_bytes(b"")
        (ps / "videos").mkdir()
        (ps / "videos" / (ES + "-video.mp4")).write_bytes(b"\0\0\0\x18ftypmp42")
        assert "," not in str(snap), snap      # the script's steps are split at commas
        env = dict(R.env_for(card), ZYPEROS_DOWNLOADS_TEST=script(snap))
        p = subprocess.Popen([str(binary), "--lang", "en"], cwd=ROOT, env=env, text=True, errors="replace",
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            for line in p.stdout:
                out.append(line)
                m = LOOK.match(line.rstrip("\n"))
                if not m:
                    continue
                k = int(m.group(2))
                looks.append((m.group(1), marks(m.group(3))[k], marks(m.group(4))[k]))
                # The Cover and Video page's Delete, between two looks
                if len(looks) == 1:
                    snap.unlink()
            code = p.wait(timeout=180)
        finally:
            if p.poll() is None:
                p.kill()
    log = ROOT / "build-native" / "dl-look-check.log"
    log.write_text("".join(out))
    fails = []

    def need(cond, what):
        if not cond:
            fails.append(what)

    need(code == 0, "the launcher ended with %d" % code)
    through = len(looks) == 5 and [n for n, _, _ in looks] == [OWN, NONE, EMPTY, ES, OWN]
    need(through, "the look did not get through: %s" % looks)
    if through:
        (_, _, own), (_, none_was, none), (_, empty_was, empty), (_, _, es), (_, _, gone) = looks
        need(own == "have", "a game with its own snap has a video (%s)" % own)
        need(none != "have" and none == none_was,
             "a game with no video has none, as its list read it (%s, listed %s)" % (none, none_was))
        need(empty != "have", "a game whose snap is an empty file has none (%s, listed %s)" % (empty, empty_was))
        need(es == "have", "a game with EmulationStation's video has a video (%s)" % es)
        need(gone in ("miss", "off"), "a game whose snap was deleted is missing it at the next look (%s)" % gone)
    for f in fails:
        print("FAIL " + f)
    if fails:
        print("(log: %s)" % log)
        return 1
    print("ok: the panel's Videos is On card for a game's own snap and for EmulationStation's video, and not for "
          "a game with none, an empty snap or a snap deleted after a look (%s)" % looks[1][2])
    return 0


if __name__ == "__main__":
    sys.exit(main())
