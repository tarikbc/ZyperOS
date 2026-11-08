#!/usr/bin/env python3
"""MENU > Storage's page and a panel opening on it, at the motion's ends.

    make check-storage

A on a game on a system's page asks Delete Game's question over the page.
The page is a base, as Muse's shelf is (src/main.c, store_base_open): MENU's
panel leaves over it when it opens, and a panel opened on it comes from
nothing. Without that, the question opened as a swap from the last panel
drawn - MENU's, or the question before - which left over the page first:
a panel there, gone, and back (seen on the Pixel 2, 2026-11-08).

--storage delete-open --shot-at T is that open as the handheld does it:
MENU's panel at rest and drawn, Storage's page a base, a second on it, then
the question opening (src/main.c, shot_motion). Held to the pictures it
must be, byte for byte, in both themes:

  - at 0, the page with nothing on it (--storage system);
  - at quick, the question at rest (--storage delete);
  - halfway, neither.

The host build's --shot, drawn by SDL's software renderer, so it needs SDL2
and skips cleanly without it. The card is tools/readme-shots.py's
demonstration card, whose PlayStation shelf the page shows.
"""
import importlib.util
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("readme_shots", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)


def main():
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    binary = ROOT / "build-native" / "zyperos"
    if not binary.is_file():
        print("FAIL no build-native/zyperos: make native first", file=sys.stderr)
        return 1
    design = (ROOT / "src" / "design.h").read_text()
    quick = int(re.search(r"#define DS_MS_QUICK (\d+)", design).group(1))
    fails = 0
    print("a panel opened on Storage's page comes from nothing, and comes once")
    with tempfile.TemporaryDirectory(prefix="zyperos-storage-motion-") as t:
        card = Path(t) / "demo"
        R.demo_card(card, None, {"artwork": {}})
        (card / "ZyperOS/.last").write_text("PS1\n" + R.FOCUS + ".chd\n")

        def shot(name, theme, *flags):
            out = Path(t) / ("%s-%s.png" % (name, theme))
            r = subprocess.run([str(binary), "--shot", str(out), "--theme", theme, "--screen", "systems",
                                "--status", "10:08 24h wifi=off batt=82", *flags],
                               cwd=ROOT, env=R.env_for(card), text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if r.returncode or not out.is_file():
                print("  FAIL %s/%s: the shot failed: %s" % (theme, name, r.stdout.strip()[-300:]))
                return None
            return out.read_bytes()

        for theme in ("light", "dark"):
            page = shot("page", theme, "--storage", "system")
            asked = shot("asked", theme, "--storage", "delete")
            at0 = shot("open-0", theme, "--storage", "delete-open", "--shot-at", "0")
            half = shot("open-half", theme, "--storage", "delete-open", "--shot-at", str(quick // 2))
            end = shot("open-end", theme, "--storage", "delete-open", "--shot-at", str(quick))
            if None in (page, asked, at0, half, end):
                fails += 1
                continue
            for ok, what in ((at0 == page, "the open starts from the page, with nothing on it"),
                             (end == asked, "the open is the question at rest at quick"),
                             (half not in (page, asked), "and is neither halfway")):
                if not ok:
                    print("  FAIL %s: %s" % (theme, what))
                    fails += 1
    if fails:
        print("storage-motion: %d FAILED" % fails)
        return 1
    print("ok: Delete Game's question opens on Storage's page from nothing to at rest, in both themes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
