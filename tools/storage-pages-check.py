#!/usr/bin/env python3
"""MENU > Storage's lists in X's orders, and the media folders' pages, in the
real launcher.

    make check-storage

Each list of the page - the card's, a system's, Music's, Videos' and
Audiobooks' - is shot on the stand-in card (src/storage_screen.c, storage_shot) in each
order X steps through (--storage-sort), in both themes, and the launcher says
the whole list it made ("storage: order Size: Unused Art|..."), not only the
five rows the window shows. Held to:

  - the recommendations first on the card, in every order;
  - Size the largest first, A to Z by name with case not counting, Last
    Played the newest first and then the rest by size;
  - a media folder with no Last Played: asked for it, its list is by size,
    and its hint says so;
  - the media pages' rows: albums, films and books, Music's Menu Music, and
    Other Files last where the rows do not come to the folder's whole;
  - nothing cut or past its box, in either theme (the launcher's "fit:" and
    "i18n:" lines).

src/storage.c's own orders and the media rows' bytes are held by
tools/storage-check.c; this is that they reach the page. Needs the host
build (SDL2), and skips cleanly without it.
"""
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
_spec = importlib.util.spec_from_file_location("readme_shots", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)

RECS = ["Unused Art", "Old Save States"]
GAMES_BY_SIZE = ["Metal Gear Solid", "Castlevania - Symphony of the Night", "Tekken 3", "Crash Bandicoot"]
GAMES_BY_LAST = ["Castlevania - Symphony of the Night", "Crash Bandicoot", "Tekken 3", "Metal Gear Solid"]
MUSIC_BY_SIZE = ["Neon Harbor", "Paper Satellites", "Slow Static", "Coastline Radio", "Afterglow Sessions",
                 "Quiet Machines", "Small Hours", "Singles", "Live at the Pier", "Menu Music", "Other Files"]
VIDEOS_BY_SIZE = ["Sintel", "Nature Walks", "Tears of Steel", "Big Buck Bunny", "Elephants Dream"]
BOOKS_BY_SIZE = ["Pride and Prejudice", "The Time Machine"]


def az(names):
    return sorted(names, key=str.casefold) == names


def main():
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    binary = ROOT / "build-native" / "zyperos"
    if not binary.is_file():
        print("FAIL no build-native/zyperos: make native first", file=sys.stderr)
        return 1
    fails = 0

    def fail(what):
        nonlocal fails
        print("  FAIL " + what)
        fails += 1

    print("Storage's lists in X's orders, and the media folders' pages, in the launcher")
    with tempfile.TemporaryDirectory(prefix="zyperos-storage-pages-") as t:
        card = Path(t) / "demo"
        R.demo_card(card, None, {"artwork": {}})
        (card / "ZyperOS/.last").write_text("PS1\n" + R.FOCUS + ".chd\n")

        def rows(theme, page, order):
            out = Path(t) / ("%s-%s-%s.png" % (page, order, theme))
            r = subprocess.run([str(binary), "--shot", str(out), "--theme", theme, "--lang", "en",
                                "--screen", "systems", "--status", "10:08 24h wifi=off batt=82",
                                "--storage", page, "--storage-sort", order],
                               cwd=ROOT, env=R.env_for(card), text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            where = "%s/%s/%s" % (theme, page, order)
            if r.returncode or not out.is_file():
                fail("%s: the shot failed: %s" % (where, r.stdout.strip()[-300:]))
                return None, None
            for line in r.stdout.splitlines():
                if line.startswith(("fit: overflow", "i18n: cut", "i18n: off screen")):
                    fail("%s: %s" % (where, line))
            said = [l for l in r.stdout.splitlines() if l.startswith("storage: order ")]
            if not said:
                fail("%s: the launcher said no order" % where)
                return None, None
            label, _, names = said[-1][len("storage: order "):].partition(":")
            return label, names.strip().split("|") if names.strip() else []

        for theme in ("light", "dark"):
            # The card: the recommendations first, then the order's
            for order, label in (("size", "Size"), ("az", "A to Z"), ("last", "Last Played")):
                said, names = rows(theme, "main", order)
                if names is None:
                    continue
                if said != label:
                    fail("%s/main/%s: the hint says %r, not %r" % (theme, order, said, label))
                if names[:2] != RECS:
                    fail("%s/main/%s: the recommendations are not first: %s" % (theme, order, names[:3]))
                rest = names[2:]
                if order == "size" and rest[:3] != ["PlayStation", "Videos", "Sega CD"]:
                    fail("%s/main/size: %s" % (theme, rest[:3]))
                if order == "az" and not az(rest):
                    fail("%s/main/az: not A to Z: %s" % (theme, rest))
                if order == "last" and rest[:3] != ["PlayStation", "Nintendo DS", "Game Boy Advance"]:
                    fail("%s/main/last: %s" % (theme, rest[:3]))

            # A system's page: Delete Video Snaps first, then its games
            for order, want in (("size", GAMES_BY_SIZE), ("last", GAMES_BY_LAST), ("az", None)):
                said, names = rows(theme, "system", order)
                if names is None:
                    continue
                if names[:1] != ["Delete Video Snaps"]:
                    fail("%s/system/%s: Delete Video Snaps is not first: %s" % (theme, order, names[:2]))
                games = [n for n in names[1:] if n != "Other Files"]
                if want and games[:len(want)] != want:
                    fail("%s/system/%s: %s" % (theme, order, games[:len(want)]))
                if not want and not az(games):
                    fail("%s/system/az: not A to Z: %s" % (theme, games))

            # The media pages, by size and A to Z; no Last Played there
            for page, want in (("music", MUSIC_BY_SIZE), ("videos", VIDEOS_BY_SIZE), ("books", BOOKS_BY_SIZE)):
                said, names = rows(theme, page, "size")
                if names is not None and names != want:
                    fail("%s/%s/size: %s" % (theme, page, names))
                said, names = rows(theme, page, "last")
                if names is not None and (said != "Size" or names != want):
                    fail("%s/%s/last: a media folder has no Last Played, yet: %s %s" % (theme, page, said, names))
                said, names = rows(theme, page, "az")
                if names is not None:
                    kept = [n for n in names if n != "Other Files"]
                    if said != "A to Z" or not az(kept) or sorted(names) != sorted(want):
                        fail("%s/%s/az: %s %s" % (theme, page, said, names))
    if fails:
        print("storage-pages: %d FAILED" % fails)
        return 1
    print("ok: every Storage list is in X's order, the media pages list their albums, films and books,\n"
          "and nothing is cut, in both themes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
