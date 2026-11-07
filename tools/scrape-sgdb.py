#!/usr/bin/env python3
"""The covers libretro does not have, from SteamGridDB.

    tools/scrape-sgdb.py --roms /Volumes/ZYPEROS/Roms [--system NAME] [--dry-run]

Runs after tools/scrape-art.py, for whatever it left without a cover:
homebrew, hacks, ports, and games libretro has never filed.
SteamGridDB's portrait "grids" (600x900) are box-shaped, which is what the
shelf draws. Each is converted to PNG and cut to 512 on its long side, the size
the README asks for, so the card holds no 2 MB covers.

Needs a SteamGridDB API key (steamgriddb.com, Preferences, API), from SGDB_KEY
or ~/.config/zyperos/steamgriddb.key. It is never written anywhere else.

SteamGridDB searches by name across every platform, so a match must be close:
the file's title and SteamGridDB's, both normalized, at a similarity of 0.85
or more. A pack's numbering ("036 ") and every bracketed tag are taken off
first. A miss is reported and left for art by hand rather than guessed.

Needs Pillow.
"""
import argparse
import concurrent.futures
import difflib
import importlib.util
import io
import json
import os
import re
import sys
import urllib.parse
import urllib.request

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
API = "https://www.steamgriddb.com/api/v2"
LONG_SIDE = 512

_spec = importlib.util.spec_from_file_location("scrape_art", os.path.join(HERE, "scrape-art.py"))
art = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(art)


def key():
    k = os.environ.get("SGDB_KEY")
    if not k:
        p = os.path.expanduser("~/.config/zyperos/steamgriddb.key")
        if os.path.exists(p):
            k = open(p).read().strip()
    if not k:
        sys.exit("scrape-sgdb: no key (SGDB_KEY or ~/.config/zyperos/steamgriddb.key)")
    return k


def api(path, k):
    req = urllib.request.Request(API + path, headers={
        "Authorization": "Bearer " + k, "User-Agent": art.UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def title(stem):
    """What to search for: no pack numbering, no tags, no file-name filler.

    The numbering stays when it is the whole title ("2048 (homebrew)"), since
    a search for nothing finds nothing. src/sgdb.c's sgdb_title is the same
    rule on the device, and tools/sgdb-check.py holds the two together."""
    def rest(t):
        t = re.sub(r"\[[^\]]*\]|\([^)]*\)", " ", t)
        t = re.sub(r"(?<=[a-z])(?=[A-Z])", " ", t) if " " not in t else t   # StardewValley
        return " ".join(t.replace("_", " ").replace("-", " ").split())
    return rest(re.sub(r"^\d{1,4}\s*-?\s+", "", stem)) or rest(stem)


def close(a, b):
    return difflib.SequenceMatcher(None, art.norm(a), art.norm(b)).ratio()


def find(stem, k):
    """(game name, image url) or None."""
    want = title(stem)
    if not want:
        return None
    games = api("/search/autocomplete/" + urllib.parse.quote(want), k).get("data") or []
    best = max(games, key=lambda g: close(want, g["name"]), default=None)
    if not best or close(want, best["name"]) < 0.85:
        return None
    grids = api(f"/grids/game/{best['id']}?dimensions=600x900&types=static&nsfw=false",
                k).get("data") or []
    return (best["name"], grids[0]["url"]) if grids else None


def save(url, dest):
    req = urllib.request.Request(url, headers={"User-Agent": art.UA})
    with urllib.request.urlopen(req, timeout=60) as r:
        im = Image.open(io.BytesIO(r.read())).convert("RGBA")
    scale = LONG_SIDE / max(im.size)
    if scale < 1:
        im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
    tmp = dest + ".part"
    im.save(tmp, "PNG")
    os.replace(tmp, dest)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", required=True)
    ap.add_argument("--system", help="only this system folder")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--jobs", type=int, default=4)
    a = ap.parse_args()
    k = key()

    for folder, exts in art.systems_from_cfg(os.path.join(ROOT, "config", "systems.cfg")):
        if a.system and folder != a.system:
            continue
        d = os.path.join(a.roms, folder)
        if not os.path.isdir(d):
            continue
        media = os.path.join(d, ".media")
        todo = []
        for f in sorted(os.listdir(d)):
            stem, ext = os.path.splitext(f)
            if f.startswith(".") or not os.path.isfile(os.path.join(d, f)):
                continue
            if exts and ext[1:].lower() not in exts:
                continue
            if not os.path.exists(os.path.join(media, stem + ".png")):
                todo.append(stem)
        if not todo:
            continue

        def one(stem):
            try:
                hit = find(stem, k)
                if not hit:
                    return stem, None
                if not a.dry_run:
                    os.makedirs(media, exist_ok=True)
                    save(hit[1], os.path.join(media, stem + ".png"))
                return stem, hit[0]
            except Exception as e:
                return stem, e

        got = 0
        with concurrent.futures.ThreadPoolExecutor(max(1, a.jobs)) as pool:
            for stem, r in pool.map(one, todo):
                if isinstance(r, str):
                    got += 1
                    print(f"    {folder}/{stem}  ->  {r}")
                elif r is not None:
                    print(f"    FAILED: {folder}/{stem}: {r}")
        print(f"  {folder:<20} {got:>3} of {len(todo)} missing covers found")
    if a.dry_run:
        print("  (dry run: nothing was written)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
