#!/usr/bin/env python3
"""Fetch RetroAchievements sets for a ROM library and write them beside it.

    tools/ra-sets.py [--roms DIR] [--system NAME] [--force] [--dry-run]

Writes `Roms/<System>/.cheevos/<name>.set` - the same shape as box art's
`.media/<name>.png`, so a set travels with the ROM folder and a card reflash
loses nothing that was not already lost.

A CONVENIENCE, NOT THE MECHANISM. The device fetches its own sets the normal
way - src/rafetch.c, on first launch of a game - because it turns out to have
curl and OpenSSL and only lacked a trust store. This exists to seed a whole
library in one pass, or to work from a card that will never see a network. It
writes the identical file, and `make check-raset` requires the two converters
to keep producing byte-identical output.

An earlier version of this comment said the device could not do HTTPS. That
was never measured, only assumed from the launcher not using it, and it was
wrong.

The file is read by two programs and is deliberately one file:

    #! zyperos-cheevos 2  game=1459  console=7  title=Blaster Master
    #: 76195  5  Blast Off  Complete the first area  85741
    76195     0xH06f3=0_0xH0400=3_0xH00ba=0_0xH06f0<d0xH06f0

Version 2 (2026-11-05) adds the last field of a `#:` line, the badge's name
on media.retroachievements.org; a version 1 line has no badge. Since
2026-11-12 the header ends with `hash=<md5>`, the ROM's RetroAchievements hash,
which an unlock earned offline is sent with; a set without it still loads.

Diatom reads the bare `<id>\\t<condition>` lines and skips every `#` line, which
its format says it will. ZyperOS reads the `#` lines for the menu. One file
means the set being evaluated and the list being shown cannot drift apart,
which two files would eventually do.

CREDENTIALS come from RA_USER and RA_PASS, or --user/--password, or a prompt.
Nothing is written to disk and nothing is stored here.

`r=patch` returns the whole set including things that must not be shown:

  - **Flags 5 is "unofficial"** - achievements in development, not part of the
    set anyone is playing. Skipped.
  - **Two notices RA sends as if they were achievements**, both with condition
    `1=1.300.` - true after 300 frames. Neither is something a player earned,
    and writing either puts an entry in a game's list that unlocks itself five
    seconds in. Both skipped, and counted in the summary so the facts they
    carry stay visible rather than being papered over:

      "Warning: Unknown Emulator"  this client is not registered with RA.
      "Unsupported Game Version"   this ROM is a dump RA has not verified.
                                   It arrives as the ONLY achievement under a
                                   synthetic game id, so the whole set is a
                                   placeholder and the game ends up with none.

    Eight of the 180 ROMs in the test library are in the second case: Sonic 2,
    Alex Kidd, Phantasy Star, Castle of Illusion, Monster World IV, Super Mario
    Bros. Deluxe, Final Fantasy VI and Super Mario Advance 4. A different dump
    of each would have achievements.
"""
import argparse
import getpass
import importlib.util
import json
import os
import sys
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UA = "ZyperOS/1.0 (+achievement sets)"


def _ra_check():
    """The hash rules live in ra-check.py and are imported, not copied. Two
    tables of per-console header offsets would agree right up until one of them
    was corrected."""
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ra-check.py")
    spec = importlib.util.spec_from_file_location("ra_check", p)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


rac = _ra_check()


def post(**kw):
    body = urllib.parse.urlencode(kw).encode()
    req = urllib.request.Request("https://retroachievements.org/dorequest.php",
                                 data=body, headers={"User-Agent": UA})
    return json.load(urllib.request.urlopen(req, timeout=25))


def login(user, password):
    for verb in ("login2", "login"):
        try:
            d = post(r=verb, u=user, p=password)
            if d.get("Success") and d.get("Token"):
                return d["Token"]
        except Exception as e:
            print(f"  login ({verb}): {e}", file=sys.stderr)
    return None


def patch(user, token, gid, tries=3):
    for k in range(tries):
        try:
            return post(r="patch", u=user, t=token, g=gid)
        except Exception:
            time.sleep(1.5 * (k + 1))
    return None


def tsv_safe(s):
    """A tab or a newline in a title would become a field. RA titles have
    neither today; this is here so the day one does, the file stays readable
    rather than becoming subtly wrong."""
    return " ".join(str(s or "").split())


def badge_safe(s):
    """A badge name as a file name: letters, digits, '-' and '_', ending at
    anything else, as src/rafetch.c's badge_safe does."""
    out = ""
    for ch in str(s or ""):
        if not (ch.isascii() and (ch.isalnum() or ch in "-_")):
            break
        out += ch
    return out


def write_set(path, game, console, title, achievements, md5=None):
    # The ROM's hash last, and only when given: the device's converter writes
    # the same line (ra_set_from_json_hash), and every unlock earned from the
    # set carries it, offline ones too (src/cheevos.h, chv_game_hash).
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        f.write(f"#! zyperos-cheevos 2\tgame={game}\tconsole={console}"
                f"\ttitle={tsv_safe(title)}"
                + (f"\thash={md5}" if md5 and len(md5) == 32 else "") + "\n")
        for a in achievements:
            f.write("#:\t{}\t{}\t{}\t{}\t{}\n".format(
                a["ID"], a.get("Points", 0),
                tsv_safe(a.get("Title")), tsv_safe(a.get("Description")),
                badge_safe(a.get("BadgeName"))))
            f.write("{}\t{}\n".format(a["ID"], a["MemAddr"]))
    os.replace(tmp, path)


BADGE_URL = "https://media.retroachievements.org/Badge/{}.png"
PNG = b"\x89PNG\r\n\x1a\n"


def fetch_badges(out_dir, achievements):
    """Each achievement's badge beside the set, badges/<name>.png, for a
    handheld that is never online (src/rabadge.h). Only the missing ones, and
    only what comes back as a PNG; the locked look is drawn on the device."""
    bdir = os.path.join(out_dir, "badges")
    got = 0
    for a in achievements:
        name = badge_safe(a.get("BadgeName"))
        if not name:
            continue
        path = os.path.join(bdir, name + ".png")
        if os.path.exists(path):
            continue
        try:
            req = urllib.request.Request(BADGE_URL.format(name), headers={"User-Agent": UA})
            data = urllib.request.urlopen(req, timeout=25).read()
        except Exception:
            continue
        if not data.startswith(PNG):
            continue
        os.makedirs(bdir, exist_ok=True)
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
        got += 1
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", default=os.path.join(ROOT, "ZyperOS-Test-Set", "Roms"))
    ap.add_argument("--system")
    ap.add_argument("--user", default=os.environ.get("RA_USER"))
    ap.add_argument("--password", default=os.environ.get("RA_PASS"))
    ap.add_argument("--force", action="store_true",
                    help="refetch sets that are already on disk")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-badges", action="store_true",
                    help="write the sets only, not each achievement's badge")
    a = ap.parse_args()

    user = a.user or input("RetroAchievements user: ").strip()
    pw = a.password or getpass.getpass("password: ")
    token = login(user, pw)
    if not token:
        sys.exit("ra-sets: login failed")
    del pw

    systems = rac.systems_from_cfg(os.path.join(ROOT, "config", "systems.cfg"))
    if a.system:
        systems = [s for s in systems if s[0] == a.system]
        if not systems:
            sys.exit(f"ra-sets: {a.system} is not in systems.cfg")

    wrote = skipped = unknown = empty = failed = badges = 0
    warned = 0
    for folder, tag, exts in systems:
        d = os.path.join(a.roms, folder)
        if not os.path.isdir(d):
            continue
        out_dir = os.path.join(d, ".cheevos")
        n = k = 0
        for fn in sorted(os.listdir(d)):
            p = os.path.join(d, fn)
            if fn.startswith(".") or not os.path.isfile(p):
                continue
            if exts and os.path.splitext(fn)[1][1:].lower() not in exts:
                continue
            # By the same rule the device hashes with, discs and named
            # arcade sets included: ra-check.py's ra_md5
            h = rac.ra_md5(tag, p)
            if h is None:
                continue
            n += 1
            out = os.path.join(out_dir, os.path.splitext(fn)[0] + ".set")
            if os.path.exists(out) and not a.force:
                skipped += 1
                k += 1
                continue

            gid = rac.gameid(h)
            if gid < 0:
                failed += 1
                print(f"    ASK FAILED: {folder}/{fn}")
                continue
            if gid == 0:
                unknown += 1
                continue

            d_ = patch(user, token, gid)
            pd = (d_ or {}).get("PatchData") or {}
            ach = pd.get("Achievements") or []
            keep = []
            for x in ach:
                if int(x.get("Flags", 3)) != 3:
                    continue
                t = str(x.get("Title", ""))
                if t.startswith("Warning: Unknown Emulator") or \
                   t.startswith("Unsupported Game Version"):
                    warned += 1
                    continue
                if not x.get("MemAddr"):
                    continue
                keep.append(x)
            if not keep:
                empty += 1
                continue

            if not a.dry_run:
                os.makedirs(out_dir, exist_ok=True)
                write_set(out, gid, pd.get("ConsoleID", 0),
                          pd.get("Title", ""), keep, md5=h)
                if not a.no_badges:
                    badges += fetch_badges(out_dir, keep)
            wrote += 1
            k += 1
            time.sleep(0.15)          # civility, and it is what keeps this honest
        if n:
            print(f"  {folder:<20} {k:>3}/{n}")

    print(f"\n  wrote {wrote}, already had {skipped}, "
          f"RA does not know {unknown}, no achievements {empty}, "
          f"could not ask {failed}, badges fetched {badges}")
    if warned:
        print(f"  {warned} of RA's notice entries were dropped - the unregistered-client\n"
              f"  warning, and unverified-ROM placeholders. A game whose ONLY entry was a\n"
              f"  placeholder is counted above as having no achievements, which is what it\n"
              f"  has: a different dump of it would have a real set.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
