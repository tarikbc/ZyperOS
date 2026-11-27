#!/usr/bin/env python3
"""What an option list's cursor shows writes nothing (src/ds/hover.h).

    python3 tools/hover-sweep-check.py        (make check-hover-sweep)

Each card below is shot with --hover-sweep: every list whose cursor shows a
choice (MENU's, a system's, MP4's, Theme's, Night Mode's, Screen Colors' Look, Time
Zone's, the game menu's, Menu Music's, On-Screen Stats') shows every choice
and puts the kept one back, as its list would, and three frames of the shelf
behind are drawn after each show. The launcher says, a line a list, how many
it showed, how many database rows were written meanwhile, whether the page
reads the same after, and the slowest show and frame (src/hover_sweep.h).
This fails on a row written, a page that reads otherwise after, or a list
that should show and is missing; and on any file the sweep left on the card
that the same shot without it does not (the theme file, the boot screen's
words, the night's hours). It says the slowest shows and frame here, a
Mac's, not the Pixel 2's.
"""
import hashlib
import importlib.util
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("rs", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(spec)
spec.loader.exec_module(R)

# Theme's lists, on every card they are shown on
THEME = {("theme", "Accent"), ("theme", "Light Background"), ("theme", "Dark Background"),
         ("theme", "Folder Color")}
# (name, card, shelf, focus, device, settings, extra flags, lists that must show)
CASES = [
    ("pixel2", "demo", "PS1", True, "pixel2", {}, [],
     {("menu", "Appearance"), ("menu", "Home Screen"), ("menu", "Battery Icon"), ("night", "Night Mode"),
      ("menu", "Language"),
      ("menu", "UI Sounds"), ("menu", "Audio Channels"),
      ("screen", "Look"), ("system", "Sort By"), ("system", "View"), ("datetime", "Time Zone"),
      ("music", "Volume"), ("music", "Track"),
      ("hud", "Corner"), ("hud", "Edge Distance"), ("hud", "Opacity"), ("hud", "Text Color")} | THEME),
    ("night on", "demo", "PS1", True, "pixel2", {"night.mode": "on"}, [],
     {("night", "Night Warmth"), ("night", "Night Mode")}),
    ("dark", "demo", "PS1", True, "pixel2", {"appearance": "dark"}, [],
     THEME),
    ("brick", "demo", "PS1", True, "brick", {}, [],
     {("menu", "Home Screen"), ("menu", "Battery Icon"), ("menu", "Language")} | THEME),
    ("mp4", "media", "MUSE", False, "pixel2", {}, [],
     {("system", "Show")}),
]


def tree(card):
    """Every file on the card but the databases and logs: its path and hash"""
    out = {}
    for p in card.rglob("*"):
        if not p.is_file():
            continue
        name = p.name
        if ".db" in name or name.endswith(".log") or "/logs/" in p.as_posix():
            continue
        out[p.relative_to(card).as_posix()] = hashlib.sha1(p.read_bytes()).hexdigest()
    return out


def shoot(card_kind, shelf, focus, device, settings, extra, sweep, tmp):
    card = Path(tmp) / ("sweep" if sweep else "plain")
    if card_kind == "media":
        R.media_card(card)
    else:
        R.demo_card(card, None, {"artwork": {}})
    (card / "ZyperOS/.last").write_text(shelf + "\n" + (R.FOCUS + ".chd" if focus else "") + "\n")
    R.set_settings(card, settings, True)
    cmd = [str(ROOT / "build-native/zyperos"), "--shot", str(Path(tmp) / "s.png"), "--lang", "en",
           "--screen", "games" if shelf == "MUSE" else "systems", "--status", "10:08 24h wifi=off batt=82",
           "--device", device, *extra]
    if sweep:
        cmd.append("--hover-sweep")
    r = subprocess.run(cmd, cwd=ROOT, env=R.env_for(card), text=True, errors="replace",
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    return r, tree(card)


def main():
    binary = ROOT / "build-native" / "zyperos"
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    if not binary.is_file():
        print("FAIL no build-native/zyperos: make native first")
        return 1
    fails, lists, slow, frame_ms = [], 0, [], 0.0
    for name, card_kind, shelf, focus, device, settings, extra, want in CASES:
        with tempfile.TemporaryDirectory() as tmp:
            plain, plain_tree = shoot(card_kind, shelf, focus, device, settings, extra, False, tmp)
            r, sweep_tree = shoot(card_kind, shelf, focus, device, settings, extra, True, tmp)
        if r.returncode:
            fails.append("%s: the launcher ended with %d: %s" % (name, r.returncode, r.stdout[-400:]))
            continue
        seen = set()
        for line in r.stdout.splitlines():
            if not line.startswith("hover: "):
                continue
            page, label, shown, written, same, ms, f_ms = line[7:].split("\t")
            slow.append((float(ms), "%s > %s" % (page, label)))
            frame_ms = max(frame_ms, float(f_ms))
            seen.add((page, label))
            lists += 1
            if int(written):
                fails.append("%s: %s > %s wrote %s database row(s) while its cursor showed" %
                             (name, page, label, written))
            if same != "same":
                fails.append("%s: %s > %s left its page reading otherwise" % (name, page, label))
            if int(shown) <= 0:
                fails.append("%s: %s > %s showed nothing" % (name, page, label))
        for page, label in sorted(want - seen):
            fails.append("%s: %s > %s did not show its choices" % (name, page, label))
        for path in sorted(set(sweep_tree) - set(plain_tree)):
            fails.append("%s: the sweep wrote %s" % (name, path))
        for path in sorted(p for p in set(sweep_tree) & set(plain_tree) if sweep_tree[p] != plain_tree[p]):
            fails.append("%s: the sweep changed %s" % (name, path))
    for f in fails:
        print("FAIL " + f)
    if fails:
        return 1
    slow.sort(reverse=True)
    print("ok: %d lists in %d cases show every choice and write nothing, and their pages read the same after;"
          " slowest shows here: %s; the slowest frame after a show %.2f ms"
          % (lists, len(CASES), ", ".join("%s %.1f ms" % (n, m) for m, n in slow[:4]), frame_ms))
    return 0


if __name__ == "__main__":
    sys.exit(main())
