#!/usr/bin/env python3
"""Every question with two answers fits, in every language, on every handheld
(src/confirmq.h, src/ds/confirm.h, DESIGN.md 11.3).

    python3 tools/confirm-fit-check.py          (make check-confirm)

The host launcher, as the Pixel 2 and as the Brick, lays out each of the 39
questions in each of the eight languages with the inventory's own words
(--confirm-fit; src/shots.c's shot_ask_text: Delete Game and Delete
Screenshot with "Castlevania - Symphony of the Night") and says, a line a
question, how it laid it out. This fails on a question whose answers are not
side by side, whose words are not all shown whole (a heading or an answer cut),
or whose question scrolls; and on a question or a language missing. The panel
is as wide on the Smart Pro's wide canvas as on the 4:3 screens (menu_most_w,
Tarik's rule for the 4:3 room); with its profile in the tree it is laid out
there too.

Then the fallbacks, with a name made long on purpose: Delete Game's question in
**meta** when **menu** does not fit, and scrolling in its room only past that,
the answers side by side all the while. It prints the table: per language and
handheld, the most lines a question takes and the questions in **meta**.
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

# The Smart Pro too, once its profile is in the tree (its wide canvas, #398):
# the panel is its 4:3 width in the middle of it
DEVICES = ("pixel2", "brick") + (("smartpro",) if (ROOT / "src/device/smartpro_profile.c").exists() else ())
LANGS = ("en", "zh-Hans", "es", "pt-BR", "fr", "de", "ja", "it")
QUESTIONS = 39
LINE = re.compile(r"confirm-fit: (\S+) (\S+) side=(\d) small=(\d) meta=(\d) lines=(\d+) scroll=(\d) "
                  r"whole=(\d) heading=(\d) rule=(\d) w=(\d+) h=(\d+)")


def lay(device, tmp, item=None):
    card = Path(tmp) / "card"
    if not card.exists():
        R.demo_card(card, None, {"artwork": {}})
        (card / "ZyperOS/.last").write_text("PS1\n" + R.FOCUS + ".chd\n")
    cmd = [str(ROOT / "build-native/zyperos"), "--shot", str(Path(tmp) / "s.png"), "--lang", "en",
           "--screen", "systems", "--status", "10:08 24h wifi=off batt=82", "--device", device,
           "--confirm-fit"]
    if item:
        cmd += ["--ask", "game-delete", item]
    r = subprocess.run(cmd, cwd=ROOT, env=R.env_for(card), text=True, errors="replace",
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    rows = {}
    for m in LINE.finditer(r.stdout):
        code, name, *v = m.groups()
        rows[(code, name)] = dict(zip(("side", "small", "meta", "lines", "scroll", "whole", "heading",
                                       "rule", "w", "h"), map(int, v)))
    return r, rows


def main():
    binary = ROOT / "build-native" / "zyperos"
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    if not binary.is_file():
        print("FAIL no build-native/zyperos: make native first")
        return 1
    fails = []
    for device in DEVICES:
        with tempfile.TemporaryDirectory() as tmp:
            r, rows = lay(device, tmp)
            if r.returncode:
                fails.append("%s: the launcher ended with %d: %s" % (device, r.returncode, r.stdout[-400:]))
                continue
            names = {n for (_, n) in rows}
            if len(names) != QUESTIONS:
                fails.append("%s: %d questions laid out, not %d" % (device, len(names), QUESTIONS))
            print("  %s: the most lines a question takes, and the questions in meta" % device)
            for code in LANGS:
                mine = {n: v for (c, n), v in rows.items() if c == code}
                if len(mine) != QUESTIONS:
                    fails.append("%s %s: %d questions, not %d" % (device, code, len(mine), QUESTIONS))
                for n, v in sorted(mine.items()):
                    if not v["side"]:
                        fails.append("%s %s %s: the answers stacked" % (device, code, n))
                    if v["scroll"]:
                        fails.append("%s %s %s: the question scrolls (%d lines in meta)" % (device, code, n, v["lines"]))
                    elif not v["whole"]:
                        fails.append("%s %s %s: a word cut (the heading or an answer)" % (device, code, n))
                    if v["heading"] > 1:
                        print("    note: %s %s %s: the heading on two lines" % (device, code, n))
                most = max(mine.values(), key=lambda v: (v["meta"], v["lines"]), default=None)
                meta = sorted(n for n, v in mine.items() if v["meta"])
                worst = [n for n, v in mine.items() if most and v["lines"] == most["lines"] and v["meta"] == most["meta"]]
                print("    %-7s %s, %s; in meta: %s; small answers: %s" % (
                    code, "%d lines%s" % (most["lines"], " (meta)" if most["meta"] else "") if most else "-",
                    ", ".join(sorted(worst)[:3]), ", ".join(meta) or "none",
                    ", ".join(sorted(n for n, v in mine.items() if v["small"])) or "none"))
            gd = rows.get(("de", "game-delete"))
            if gd:
                print("    Delete Game, Castlevania - Symphony of the Night, de: %d lines in %s" % (
                    gd["lines"], "meta" if gd["meta"] else "menu"))
            # The fallbacks, on a name made long on purpose
            long_name = "Castlevania - Symphony of the Night " * 3
            r, rows = lay(device, tmp, long_name.strip())
            v = rows.get(("en", "game-delete"))
            if not v or not v["meta"] or v["scroll"] or not v["side"] or not v["whole"]:
                fails.append("%s: a long name does not take Delete Game's question to meta, whole: %s" % (device, v))
            r, rows = lay(device, tmp, (long_name * 4).strip())
            v = rows.get(("en", "game-delete"))
            if not v or not v["meta"] or not v["scroll"] or not v["side"]:
                fails.append("%s: a name too long even for meta does not scroll: %s" % (device, v))
    for f in fails:
        print("FAIL " + f)
    if not fails:
        print("check-confirm: 39 questions in 8 languages on %s: side by side, nothing cut, nothing "
              "sliding" % ", ".join(DEVICES))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
