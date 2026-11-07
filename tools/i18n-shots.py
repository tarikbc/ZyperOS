#!/usr/bin/env python3
"""The launcher's screens in every language, and what does not fit in them.

    make native
    python3 tools/i18n-shots.py                 every screen in each language,
                                                in light; fails on what does
                                                not fit (make check-i18n-shots)
    python3 tools/i18n-shots.py --keep DIR      and keeps the frames in DIR/<code>/
    python3 tools/i18n-shots.py --lang fr menu  one language, some screens

A layout drawn for English meets words up to half again as long in French or
Portuguese, and Chinese's taller face. The launcher, built for the host, says
on stderr when a string from res/strings had to be cut, broken past its
lines, slid in a marquee or drawn off the screen ("i18n: cut: ...", src/ui.c), in the language
it is drawn in. This shoots the README's screens (tools/readme-shots.py's
cards and SHOTS) and the rest of the main ones in every language, and fails on any such line, or a shot that fails.

The pseudo-locale, xx (src/i18n.h: every string accented, 40% longer and in
brackets), is shot too, and what it does not fit is listed: a warning of
what a longer translation would meet, which fails only with --strict.

Shots, not pictures held to a reference: what is drawn is the launcher's own,
and English's frames are held by the other shot checks. Each language has its
own scratch cards, so the five run at once. Needs the launcher built (SDL2);
skips cleanly without SDL2.
"""
import argparse
import concurrent.futures
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import i18n  # noqa: E402

_spec = importlib.util.spec_from_file_location("readme_shots", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)

# The README's, but its page shots (a browser's), and these: MENU through its
# rows, the panels it opens, the in-game menu, the tutorial, the battery's
# and the power-off's screens, the keyboard, the save slots
EXTRA = {
    "menu-network": ("demo", "PS1", ["--menu", "Audio Output"]),
    "menu-clock": ("demo", "PS1", ["--menu", "Language"]),
    "menu-look": ("demo", "PS1", ["--menu", "Appearance"]),
    "menu-end": ("demo", "PS1", ["--menu", "About ZyperOS"]),
    "language": ("demo", "PS1", ["--language"]),
    "system-menu": ("demo", "PS1", ["--screen", "games", "--menu"]),
    "about-0": ("demo", "PS1", ["--about", "0"]),
    "about-1": ("demo", "PS1", ["--about", "1"]),
    "about-2": ("demo", "PS1", ["--about", "2"]),
    "controls-0": ("demo", "PS1", ["--controls", "0"]),
    "controls-1": ("demo", "PS1", ["--controls", "1"]),
    "controls-2": ("demo", "PS1", ["--controls", "2"]),
    "gamemenu": ("demo", "PS1", ["--gamemenu", "0"]),
    "gamemenu-display": ("demo", "PS1", ["--gamemenu", "3"]),
    "gamecontrols": ("demo", "PS1", ["--gamecontrols", "1", "MD"]),
    "slots": ("demo", "PS1", ["--slots", "0"]),
    "synopsis": ("demo", "PS1", ["--screen", "games", "--info", "--synopsis"]),
    "keyboard": ("demo", "PS1", ["--keyboard"]),
    "video-controls": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "controls"]),
    "video-full": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "full"]),
    "tour-lang": ("demo", "PS1", ["--tour", "lang", "3500"]),
    "tour-lang-ja": ("demo", "PS1", ["--tour", "lang", "3000", "--tour-pick", "6"]),
    "tour-welcome": ("demo", "PS1", ["--tour", "welcome", "3000"]),
    "tour-move": ("demo", "PS1", ["--tour", "move", "2000"]),
    "tour-a": ("demo", "PS1", ["--tour", "a", "4000", "300"]),
    "tour-wrong": ("demo", "PS1", ["--tour", "vol", "3000", "--tour-wrong", "select", "420"]),
    "tour-menu": ("demo", "PS1", ["--tour", "menu", "2000"]),
    "tour-bright": ("demo", "PS1", ["--tour", "bright", "2000"]),
    "tour-power": ("demo", "PS1", ["--tour", "power", "1800"]),
    "tour-finish": ("demo", "PS1", ["--tour", "finish", "500"]),
    "lowbatt": ("demo", "PS1", ["--lowbatt", "1500"]),
    "lowbatt-charging": ("demo", "PS1", ["--lowbatt", "1500", "charging"]),
    "charging-hold": ("demo", "PS1", ["--charging", "4000", "hold"]),
    "charging-limit": ("demo", "PS1", ["--charging", "4000", "limit"]),
    "charging-kept": ("demo", "PS1", ["--charging", "4000", "kept"]),
    "charging-keptfull": ("demo", "PS1", ["--charging", "4000", "keptfull"]),
    "xfer-ask": ("demo", "PS1", ["--xfer-ask", "0"]),
    "usb-ask-1": ("demo", "PS1", ["--usb-ask", "1"]),
    "empty-ways": ("empty", "", ["--ways", "none,brick"]),
    # A row whose value is "needs Wi-Fi", the longest it has in most languages
    "scraper-offline": ("demo", "PS1", ["--scraper", "5"]),
}

# MENU > System Update in the states whose lines are longest, each as the
# status file the update program writes it (src/sysupdate.c); the second is
# whether a network is there (a test release's source on the card counts as
# one). The lines wrap onto a second line, and fail here if they still do not fit.
UPDATE_STATES = {
    "update-unsupported": ("state unsupported\n", True),
    "update-unsupported-offline": ("state unsupported\n", False),
    "update-uptodate": ("state uptodate\nversion 0.1.0\ntime {now}\n", True),
    "update-idle-offline": ("state idle\n", False),
    "update-error": ("state error\ntime {now}\n", True),
    "update-error-download": ("state error\nversion 0.1.1\ndownload 83000000\nkind system\ntime {now}\n", True),
    "update-needs-system": ("state needs-system\nversion 0.3.0\ntime {now}\n", True),
    "update-too-big": ("state too-big\nversion 0.3.0\nkind system\ntime {now}\n", True),
    "update-space": ("state space\nversion 0.2.0\ndownload 14200000\ntime {now}\n", True),
    "update-available-offline": ("state available\nversion 0.1.1\ndownload 83000000\nkind system\ntime {now}\n", False),
    "update-ready": ("state ready\nversion 0.1.1\nkind system\ntime {now}\n", True),
}
EXTRA.update({name: ("demo", "PS1", ["--update"]) for name in UPDATE_STATES})

# Each language's own run of every shot, on its own cards
def run_language(code, shots, binary, keep):
    notes, failures, data = [], [], {}
    with tempfile.TemporaryDirectory(prefix="zyperos-i18n-%s-" % code) as tmp:
        cards = {"demo": Path(tmp) / "demo", "empty": Path(tmp) / "empty", "scummvm": Path(tmp) / "scummvm"}
        R.demo_card(cards["demo"], None, {"artwork": {}})
        R.scummvm_card(cards["scummvm"])
        R.card_dirs(cards["empty"])
        out = (keep / code) if keep else Path(tmp) / "out"
        out.mkdir(parents=True, exist_ok=True)
        for name, (card_name, tag, flags) in shots.items():
            card = cards[card_name]
            focus = {"PS1": R.FOCUS + ".chd", "SCUMMVM": R.SCUMMVM_FOCUS}.get(tag, "")
            (card / "ZyperOS/.last").write_text(tag + "\n" + focus + "\n")
            # A state of the update page: its status, with or without a network
            up = card / "userdata/update"
            source = up / "source"
            if name in UPDATE_STATES:
                text, net = UPDATE_STATES[name]
                (up / "status").write_text(text.format(now=int(time.time())))
                if net:
                    source.write_text("file:///nonexistent\n")
                elif source.exists():
                    source.unlink()
            elif up.is_dir():
                # Back to the demonstration card's own, for the shots after
                (up / "status").write_text(R.UPDATE_STATUS)
                source.write_text("file:///nonexistent\n")
            target = out / (name + ".png")
            # The tutorial and the battery's screens draw no status corner
            status = [] if name.startswith(("tour-", "lowbatt", "charging-")) else \
                ["--status", "10:08 24h wifi=off batt=82"]
            command = [str(binary), "--shot", str(target), "--theme", "light", "--lang", code,
                       "--screen", "systems", "--phase", "3200", *status, "--fn-volume", *flags]
            # errors=replace: a log line may end mid-character (a cut name)
            r = subprocess.run(command, cwd=ROOT, env=R.env_for(card), text=True, errors="replace",
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for line in r.stdout.splitlines():
                if line.startswith(("i18n: cut data: ", "i18n: slides data: ")):
                    data.setdefault(name, []).append(line[6:])
                elif line.startswith("i18n: "):
                    notes.append("%s/%s: %s" % (code, name, line[6:]))
            ok_anyway = name in R.CORNER_OWN and R.CORNER_OWN[name] in r.stdout
            if (r.returncode and not ok_anyway) or not target.is_file():
                tail = " | ".join(r.stdout.strip().splitlines()[-3:])
                failures.append("%s/%s: the shot failed (exit %d): %s" % (code, name, r.returncode, tail))
    return code, notes, failures, data


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--keep", type=Path, help="keep the frames in DIR/<code>/")
    ap.add_argument("--lang", action="append", help="this language only (more than once for more)")
    ap.add_argument("--strict", action="store_true", help="fail on the pseudo-locale's too")
    ap.add_argument("only", nargs="*", help="shot names (default: all)")
    args = ap.parse_args()
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    binary = ROOT / "build-native" / "zyperos"
    if not binary.is_file():
        print("FAIL no build-native/zyperos: make native first", file=sys.stderr)
        return 1
    # The README's shots of one language (menu-pt) are that language's own
    shots = {k: v for k, v in R.SHOTS.items() if "--lang" not in v[2]}
    shots.update(EXTRA)
    if args.only:
        shots = {k: v for k, v in shots.items() if k in args.only}
    langs = args.lang or (i18n.CODES + [i18n.PSEUDO])
    keep = args.keep.resolve() if args.keep else None
    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=len(langs)) as pool:
        for code, notes, failures, data in pool.map(lambda c: run_language(c, shots, binary, keep), langs):
            # Data (a game's name, a formatted line) may be cut or slide in
            # English too; a screen where a language cuts more of it than
            # English does has a translation that made the cut ("vinculado
            # como t..." beside ScreenScraper)
            results[code] = (notes, failures, data)
    english = results["en"][2] if "en" in results else None
    for code in langs:
        if english is None or code == "en": continue
        for name, said in sorted(results[code][2].items()):
            if len(said) > len(english.get(name, [])):
                results[code][0].extend("%s/%s: %s (English: %d)" % (code, name, d, len(english.get(name, [])))
                                        for d in said)
    bad = 0
    for code in langs:
        notes, failures, _ = results[code]
        for f in failures:
            print("FAIL " + f)
            bad += 1
        if code == i18n.PSEUDO and not args.strict:
            for n in notes:
                print("note " + n)
            continue
        for n in notes:
            print("FAIL " + n)
            bad += 1
    pseudo = len(results.get(i18n.PSEUDO, ([], [], {}))[0])
    if bad:
        print("check-i18n-shots: %d problems in %d screens x %d languages" % (bad, len(shots), len(langs)))
        return 1
    print("check-i18n-shots: %d screens in %s, everything fits%s" % (
        len(shots), ", ".join(langs),
        "; the pseudo-locale cut or overflowed %d strings (notes above)" % pseudo if pseudo else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
