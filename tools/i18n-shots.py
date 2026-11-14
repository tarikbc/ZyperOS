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

# The string IDs of a text the launcher drew: src/ui.c says the text, not the
# ID (a label is copied into a row's buffer, or formatted around a name, and
# is not the table's pointer by then), so it is found in the language's own
# strings: the whole string, one of its lines, or a format's pattern
class Strings:
    def __init__(self, code):
        errors = []
        self.names, self.whole, self.formats = [], {}, []
        for name, s, form in i18n.flat(i18n.parse(errors)):
            sid = s.id + ("" if not s.plural else (".one" if form == 0 else ".other"))
            en = i18n.get(s, "en", form)
            text = i18n.pseudo(en) if code == i18n.PSEUDO else i18n.get(s, code, form)
            self.names.append(sid)
            for piece in [text] + text.split("\n"):
                if piece.strip():
                    self.whole.setdefault(piece, []).append(sid)
            for piece in text.split("\n"):
                if i18n.FMT_RE.search(piece):
                    pattern, pos, literal = "", 0, 0
                    for m in i18n.FMT_RE.finditer(piece):
                        pattern += re.escape(piece[pos:m.start()]) + (r"%" if m.group(0) == "%%" else r"(.+?)")
                        literal += m.start() - pos
                        pos = m.end()
                    pattern += re.escape(piece[pos:])
                    literal += len(piece) - pos
                    self.formats.append((re.compile("^" + pattern.replace("%%", "%") + "$", re.S), sid, literal))

    def ids(self, text):
        """The IDs a drawn text may be: [] when it is no string's (a name, a number)"""
        if text in self.whole:
            return self.whole[text]
        # The formats it fits, the one with the most words of its own first:
        # "MENU > %s: %s" is what a note of the Pak is, and "%s: %s" fits it too
        found = [(literal, sid, m) for pattern, sid, literal in self.formats
                 for m in [pattern.match(text)] if m]
        best = max((literal for literal, _, _ in found), default=0)
        ids = [sid for literal, sid, _ in found if literal == best]
        # A page's name is drawn inside the heading ("Controls: Hotkeys"), the
        # heading's own string being the format that fits: the name is drawn
        # too, when what stands in the heading's place is the name
        for literal, sid, m in found:
            if literal == best:
                for piece in m.groups():
                    ids += [n for n in self.whole.get(piece, []) if n.startswith(COMPOSED)]
        return ids

    def label(self, text):
        found = self.ids(text)
        found = list(dict.fromkeys(found))
        return "/".join(found[:3]) if found else "(not a string)"


# The strings that are drawn as a part of another's text: a page's name in a
# heading, and a mix's in Audio Channels' "auto (mono)" or a place's in Audio
# Output's "auto (speaker)"
COMPOSED = ("controls.page.", "audio.")

STRINGS = {}


def strings_of(code):
    if code not in STRINGS:
        STRINGS[code] = Strings(code)
    return STRINGS[code]


def unescape(text):
    return text.replace("\\n", "\n")


# The strings no shot draws yet, held as a list that may only shrink: a new
# string must come with a shot of it, or be listed here with the reason
UNSHOT = ROOT / "tools" / "i18n-unshot.txt"
UNSHOT_WHY = {
    "hare": "answers sent to the File Transfer page, which draws them, not the launcher",
    "media": "drawn composed with another word (\"Box Front · USA\", \"libretro · no match\") or "
             "wrapped onto balanced lines (the question, the line under \"Connect to Wi-Fi first.\", "
             "the video's tile), so no drawn line is the string; the media-* shots draw every one "
             "and fail on any cut (src/mediapage.c, src/main.c's media_shot)",
    "storage": "drawn wrapped to their lines, in pieces of a line (the used number as a title, "
               "a part of the line under the list), with tabular digits, or a plural's one form "
               "the stand-in card has none of (src/storage_page.c, src/main.c's storage_shot)",
}
UNSHOT_DEFAULT = "no shot draws it yet"


def read_unshot():
    ids = set()
    for line in UNSHOT.read_text(encoding="utf-8").splitlines() if UNSHOT.is_file() else []:
        line = line.split("#")[0].strip()
        if line:
            ids.add(line)
    return ids


def write_unshot(ids):
    out = ["# Strings of res/strings that no shot of tools/i18n-shots.py draws, so that",
           "# check-i18n-shots cannot say whether they fit where they are drawn.",
           "#",
           "# The list may only shrink: a string not here that no shot draws fails the",
           "# check. Add a shot (a screen's flags, or one of EXTRA's loops) and run",
           "#     python3 tools/i18n-shots.py --write-unshot",
           "# to drop what it draws, or keep a string here with the reason after its ID.",
           ""]
    last = None
    for name in ids:
        group = name.split(".")[0]
        if group != last:
            out.append("")
            out.append("# " + group + ": " + UNSHOT_WHY.get(group, UNSHOT_DEFAULT))
            last = group
        out.append(name)
    UNSHOT.write_text("\n".join(out) + "\n", encoding="utf-8")


# The README's, but its page shots (a browser's), and these: MENU through its
# rows, the panels it opens, the in-game menu, the tutorial, the battery's
# and the power-off's screens, the keyboard, the save slots
EXTRA = {
    "menu-network": ("demo", "PS1", ["--menu", "Audio Output"]),
    # Audio Output's value not focused, under the row that is: on the device
    # "auto (alto-falante)" was cut there and nowhere a shot looked
    "menu-audio-under": ("demo", "PS1", ["--menu", "Audio Channels"]),
    "menu-clock": ("demo", "PS1", ["--menu", "Language"]),
    # Date & Time's page (src/main.c's clock_page_rows): Time Zone on Auto
    # before it has found a zone, with the zone it found and with a long
    # city; a zone of the list; and what Auto says as it is chosen
    "datetime-auto": ("demo", "PS1", ["--datetime", "0"]),
    "datetime-found": ("demo", "PS1", ["--datetime", "1"]),
    "datetime-long": ("demo", "PS1", ["--datetime", "2"]),
    "datetime": ("demo", "PS1", ["--datetime", "3"]),
    "datetime-auto-said": ("demo", "PS1", ["--datetime", "4"]),
    # Time Zone's list, A on its row: open on its first zone, on its longest
    # city in the middle, and on Auto with the zone it found, short and long
    "timezone-list": ("demo", "PS1", ["--datetime", "5"]),
    "timezone-list-middle": ("demo", "PS1", ["--datetime", "6"]),
    "timezone-list-auto": ("demo", "PS1", ["--datetime", "7"]),
    "timezone-list-auto-long": ("demo", "PS1", ["--datetime", "8"]),
    "menu-look": ("demo", "PS1", ["--menu", "Appearance"]),
    "menu-end": ("demo", "PS1", ["--menu", "About ZyperOS"]),
    "language": ("demo", "PS1", ["--language"]),
    "system-menu": ("demo", "PS1", ["--screen", "games", "--menu"]),
    # View's other value, List, over the list it shows (DESIGN.md 11.39)
    "system-menu-list": ("demo", "GBA", ["--screen", "games", "--view", "list", "--menu", "View"]),
    # ...and at its end: with View and Shader (1.0.5), Box Art is below the
    # first shot's rows
    "system-menu-end": ("demo", "PS1", ["--screen", "games", "--menu", "Box Art"]),
    "storage-art": ("demo", "PS1", ["--storage", "art"]),
    "storage-states": ("demo", "PS1", ["--storage", "states"]),
    "storage-delete": ("demo", "PS1", ["--storage", "delete"]),
    "storage-done": ("demo", "PS1", ["--storage", "done"]),
    "storage-first": ("demo", "PS1", ["--storage", "first"]),
    "storage-zyperos": ("demo", "PS1", ["--storage", "main", "zyperos"]),
    "storage-videos": ("demo", "PS1", ["--storage", "main", "videos"]),
    "storage-music": ("demo", "PS1", ["--storage", "main", "music"]),
    "storage-books": ("demo", "PS1", ["--storage", "main", "books"]),
    "storage-bios": ("demo", "PS1", ["--storage", "main", "bios"]),
    "storage-other": ("demo", "PS1", ["--storage", "main", "other"]),
    "storage-snaps": ("demo", "PS1", ["--storage", "system", "0"]),
    "storage-rest": ("demo", "PS1", ["--storage", "system", "rest"]),
    "storage-never": ("demo", "PS1", ["--storage", "system", "5"]),
    # The media pages, an album's question, and X's orders with their hint
    "storage-page-music": ("demo", "PS1", ["--storage", "music"]),
    "storage-page-videos": ("demo", "PS1", ["--storage", "videos"]),
    "storage-page-books": ("demo", "PS1", ["--storage", "books"]),
    "storage-music-rest": ("demo", "PS1", ["--storage", "music", "rest"]),
    "storage-music-bgm": ("demo", "PS1", ["--storage", "music", "bgm"]),
    "storage-media-delete": ("demo", "PS1", ["--storage", "media-delete"]),
    "storage-videos-delete": ("demo", "PS1", ["--storage", "videos-delete"]),
    "storage-main-az": ("demo", "PS1", ["--storage", "main", "--storage-sort", "az"]),
    "storage-main-last": ("demo", "PS1", ["--storage", "main", "--storage-sort", "last"]),
    "storage-system-last": ("demo", "PS1", ["--storage", "system", "--storage-sort", "last"]),
    "about-0": ("demo", "PS1", ["--about", "0"]),
    "about-1": ("demo", "PS1", ["--about", "1"]),
    "about-2": ("demo", "PS1", ["--about", "2"]),
    "controls-0": ("demo", "PS1", ["--controls", "0"]),
    "controls-1": ("demo", "PS1", ["--controls", "1"]),
    "controls-2": ("demo", "PS1", ["--controls", "2"]),
    "gamemenu": ("demo", "PS1", ["--gamemenu", "0"]),
    # A game on more than one disc: the Disc row with the core's name for
    # the disc (it fits), and with one too long, which reads "Disc 2 of 3"
    "gamemenu-disc": ("demo", "PS1", ["--gamemenu", "4", "--gm-discs", "3", "FFIX (Disc 2)"]),
    "gamemenu-disc-long": ("demo", "PS1", ["--gamemenu", "4", "--gm-discs", "3",
                                           "Final Fantasy IX (USA) (Rev 1) (Disc 2)"]),
    # Achievements offline: the list with unlocks waiting, Zyper saying they
    # went, and the rows of a game never played with a network
    "cheevos-waiting": ("demo", "PS1", ["--cheevos-screen", "0", "--cheevos-waiting", "2"]),
    "ranews": ("demo", "PS1", ["--ranews", "3"]),
    "ranews-one": ("demo", "PS1", ["--ranews", "1"]),
    "gamemenu-unchecked": ("demo", "PS1", ["--gamemenu", "0", "--cheevos-unchecked"]),
    "info-unchecked": ("demo", "PS1", ["--screen", "games", "--info", "--cheevos-unchecked"]),
    "gamemenu-display": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1"]),
    "gamemenu-controls": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "2"]),
    # ...and of a game the launcher has no hash for: a PSP game ("system"),
    # a PlayStation .chd ("file"), one in each screen; the Achievements row
    # is on the in-game menu's Game tab
    "gamemenu-nohash": ("demo", "PS1", ["--gamemenu", "0", "--cheevos-no-hash", "system"]),
    "info-nohash": ("demo", "PS1", ["--screen", "games", "--info", "--cheevos-no-hash", "file"]),
    "gamecontrols": ("demo", "PS1", ["--gamecontrols", "1", "MD"]),
    "slots": ("demo", "PS1", ["--slots", "0"]),
    "synopsis": ("demo", "PS1", ["--screen", "games", "--info", "--synopsis"]),
    "keyboard": ("demo", "PS1", ["--keyboard"]),
    "video-controls": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "controls"]),
    "video-full": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "full"]),
    # MP4's lock screen, its panel with the bar a hold fills: over music, and
    # over a video, where the panel is the same and the picture is under it
    # MP4's menu on its shelf, which shows All (albums and videos mixed)
    "mp4-menu": ("media", "MUSE", ["--screen", "games", "--menu"]),
    "mp4-locked": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--locked"]),
    "mp4-locked-video": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "controls", "--locked"]),
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
    # Y pressed under a limit: to 100% this time (2026-11-11)
    "charging-tofull": ("demo", "PS1", ["--charging", "4000", "tofull"]),
    "xfer-ask": ("demo", "PS1", ["--xfer-ask", "0"]),
    # The game's question, its B hint "Not Now" (src/keyhints.h)
    "xfer-ask-game": ("demo", "PS1", ["--xfer-ask", "0", "game"]),
    "usb-ask": ("demo", "PS1", ["--usb-ask", "1"]),
    "usb-ask-plain": ("demo", "PS1", ["--usb-ask", "1", "plain"]),
    # Computer Connected with Flash Drive (an image with usbdrive), two to a
    # row: three tiles and, on a development image, four; and its pictures
    "usb-ask-drive": ("demo", "PS1", ["--usb-ask", "2", "plain", "drive"]),
    "usb-ask-drive-0": ("demo", "PS1", ["--usb-ask", "0", "plain", "drive"]),
    "usb-ask-drive-dev": ("demo", "PS1", ["--usb-ask", "3", "drive"]),
    "usb-drive-0": ("demo", "PS1", ["--usb-drive", "0"]),
    "usb-drive-1": ("demo", "PS1", ["--usb-drive", "1"]),
    "usb-drive-2": ("demo", "PS1", ["--usb-drive", "2"]),
    "usb-drive-in": ("demo", "PS1", ["--usb-drive", "0", "in"]),
    "usb-drive-out": ("demo", "PS1", ["--usb-drive", "0", "out"]),
    "usb-drive-both": ("demo", "PS1", ["--usb-drive", "0", "both"]),
    "usb-drive-wide": ("demo", "PS1", ["--usb-drive", "0", "wide"]),
    "usb-drive-3": ("demo", "PS1", ["--usb-drive", "3"]),
    "usb-drive-bright": ("demo", "PS1", ["--usb-drive", "0", "bright"]),
    "usb-drive-atlas": ("demo", "PS1", ["--usb-drive", "4"]),
    # Developer Mode (src/devmode.h): its page with one key, a count of two
    # figures (the plural's other), and none, and the message of the unlock
    "dev-page": ("demo", "PS1", ["--dev-page", "0"]),
    "dev-page-keys": ("demo", "PS1", ["--dev-page", "1"]),
    "dev-page-none": ("demo", "PS1", ["--dev-page", "2"]),
    "dev-on": ("demo", "PS1", ["--dev-page", "3"]),
    "empty-ways": ("empty", "", ["--ways", "none,brick"]),
    # MENU > Scraper's page (DESIGN.md 11.19): the plan with videos off, on
    # for three systems and for one (the plurals' one), Systems with a long
    # name in focus too, Videos for, Settings, the run, done (and with one
    # system's videos), offline, and a system's own Box Art
    "scraper-page-plan-off": ("demo", "PS1", ["--scraper-page", "0"]),
    "scraper-page-plan": ("demo", "PS1", ["--scraper-page", "1"]),
    "scraper-page-plan-one": ("demo", "PS1", ["--scraper-page", "2"]),
    "scraper-page-systems": ("demo", "PS1", ["--scraper-page", "3"]),
    "scraper-page-systems-long": ("demo", "PS1", ["--scraper-page", "4"]),
    "scraper-page-videos-for": ("demo", "PS1", ["--scraper-page", "5"]),
    "scraper-page-settings": ("demo", "PS1", ["--scraper-page", "6"]),
    "scraper-page-run": ("demo", "PS1", ["--scraper-page", "7", "7180"]),
    "scraper-page-done": ("demo", "PS1", ["--scraper-page", "8"]),
    "scraper-page-done-one": ("demo", "PS1", ["--scraper-page", "9"]),
    "scraper-page-offline": ("demo", "PS1", ["--scraper-page", "10"]),
    "scraper-page-boxart": ("demo", "PS1", ["--scraper-page", "11"]),
    "scraper-page-done-no-account": ("demo", "PS1", ["--scraper-page", "12"]),
    "scraper-page-plan-short": ("demo", "PS1", ["--scraper-page", "13"]),
    "scraper-page-videos-for-one": ("demo", "PS1", ["--scraper-page", "14"]),
    "scraper-page-done-full": ("demo", "PS1", ["--scraper-page", "15"]),
    # and the Video Snaps page it opens: how the shelf plays them, the room
    "scraper-video-snaps": ("demo", "PS1", ["--scraper", "3"]),
    # A game's Cover and Video (DESIGN.md 11.34) in every state, a tile of
    # each kind and region focused, Delete's question and the message of a
    # download that did not come; and the details row's other value
    "info-art": ("demo", "PS1", ["--screen", "games", "--info", "--info-art"]),
    "media-fetching": ("demo", "PS1", ["--screen", "games", "--media", "fetching", "--media-art", "{card}/media-art"]),
    "media-offline": ("demo", "PS1", ["--screen", "games", "--media", "offline", "--media-art", "{card}/media-art"]),
    "media-nothing": ("demo", "PS1", ["--screen", "games", "--media", "nothing", "--media-art", "{card}/media-art"]),
    "media-after": ("demo", "PS1", ["--screen", "games", "--media", "after", "--media-art", "{card}/media-art"]),
    "media-videoget": ("demo", "PS1", ["--screen", "games", "--media", "videoget", "--media-art", "{card}/media-art"]),
    "media-delete": ("demo", "PS1", ["--screen", "games", "--media", "delete", "--media-art", "{card}/media-art"]),
    "media-failed": ("demo", "PS1", ["--screen", "games", "--media", "failed", "--media-art", "{card}/media-art"]),
    "media-novideo": ("demo", "PS1", ["--screen", "games", "--media", "novideo", "--media-art", "{card}/media-art"]),
    "media-tile-1": ("demo", "PS1", ["--screen", "games", "--media", "page", "1", "--media-art", "{card}/media-art"]),
    "media-tile-2": ("demo", "PS1", ["--screen", "games", "--media", "page", "2", "--media-art", "{card}/media-art"]),
    "media-tile-3": ("demo", "PS1", ["--screen", "games", "--media", "page", "3", "--media-art", "{card}/media-art"]),
    "media-tile-4": ("demo", "PS1", ["--screen", "games", "--media", "page", "4", "--media-art", "{card}/media-art"]),
    "media-tile-5": ("demo", "PS1", ["--screen", "games", "--media", "page", "5", "--media-art", "{card}/media-art"]),
    "media-tile-6": ("demo", "PS1", ["--screen", "games", "--media", "page", "6", "--media-art", "{card}/media-art"]),
    "media-tile-8": ("demo", "PS1", ["--screen", "games", "--media", "page", "8", "--media-art", "{card}/media-art"]),
    "media-tile-9": ("demo", "PS1", ["--screen", "games", "--media", "page", "9", "--media-art", "{card}/media-art"]),
    "media-tile-10": ("demo", "PS1", ["--screen", "games", "--media", "page", "10", "--media-art", "{card}/media-art"]),
    "media-look-after": ("demo", "PS1", ["--screen", "games", "--media", "after", "0", "--media-art", "{card}/media-art"]),
}

# What the first-start tour says, step by step, and what the in-game Controls
# page says for every system: the same words in each language on a screen of
# each, since the longest of a system's is the one that overflows (the tour
# is read from src/main.c's TOUR_STEP_NAMES, the systems from config/systems.cfg)
def tour_steps():
    text = (ROOT / "src" / "main.c").read_text(encoding="utf-8")
    block = re.search(r"TOUR_STEP_NAMES\[TOUR_STEPS\] = \{(.*?)\};", text, re.S).group(1)
    return re.findall(r'"([a-z]+)"', block)


def system_tags():
    tags = []
    for line in (ROOT / "config" / "systems.cfg").read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#") and line.count("|") >= 4:
            tags.append(line.split("|")[4].strip())
    return tags


for _step in tour_steps():
    EXTRA.setdefault("tour-" + _step, ("demo", "PS1", ["--tour", _step, "2500"]))
# On the card with a game in every system: a shelf with no game is not on
# the shelves, and the Controls page asked for a system that is not there is
# the cursor's own (the PlayStation's), whatever the system said
for _tag in system_tags():
    for _page in range(3):
        EXTRA.setdefault("gamecontrols-%s-%d" % (_tag, _page),
                         ("all", "PS1", ["--gamecontrols", str(_page), _tag]))
for _row in range(8):
    EXTRA.setdefault("gamemenu-%d" % _row, ("demo", "PS1", ["--gamemenu", str(_row)]))
for _kind in ("unplugged", "off", "pending", "soon"):
    EXTRA.setdefault("charging-" + _kind, ("demo", "PS1", ["--charging", "4000", _kind]))
for _layer in (1, 2):
    EXTRA.setdefault("keyboard-%d" % _layer, ("demo", "PS1", ["--keyboard", str(_layer)]))
for _page in (3, 4, 5):
    EXTRA.setdefault("controls-%d" % _page, ("demo", "PS1", ["--controls", str(_page)]))
for _scroll in ("0.5", "1"):
    EXTRA.setdefault("about-2-scroll-" + _scroll, ("demo", "PS1", ["--about", "2", "--about-scroll", _scroll]))
EXTRA.setdefault("hare-cable", ("demo", "PS1", ["--hare", "10.42.0.1", "1 signed in", "Tobu Tobu Girl.gb"]))
# File Transfer's news on Flash Drive's page (DESIGN.md 11.23): the tiles in
# each state, over Wi-Fi and over the cable (SSH beside the browsers)
for _move in ("idle", "in", "out", "both", "closing", "finish", "gone"):
    EXTRA.setdefault("hare-move-" + _move, ("demo", "PS1", ["--hare", "http://192.168.1.42", "1 connected",
                                                           "Tobu Tobu Girl.gb", "--hare-move", _move]))
    EXTRA.setdefault("hare-cable-move-" + _move, ("demo", "PS1", ["--hare", "10.42.0.1", "1 connected",
                                                                 "Tobu Tobu Girl.gb", "--hare-move", _move]))
# File Transfer's steps (src/linksteps.h) in every state, with each hint
for _way, _states in (("usb", ("unplugged", "taking", "opening", "nocomputer", "noanswer", "nodriver", "address",
                               "noaddress", "browser", "nobrowser", "waits", "replug", "pin")),
                      ("wifi", ("nonetwork", "browser", "nobrowser", "pin")),
                      ("phone", ("browser", "pin"))):
    for _state in _states:
        EXTRA.setdefault("steps-%s-%s" % (_way, _state), ("demo", "PS1", ["--hare-steps", _way, _state]))
    # and the browser's hint with no name on the network (mDNS did not start)
    EXTRA.setdefault("steps-%s-nobrowser-noname" % _way, ("demo", "PS1", ["--hare-steps", _way, "nobrowser", "noname"]))
    # the way in with a long address, for the fit (DESIGN.md 11.35)
    EXTRA.setdefault("steps-%s-longaddr" % _way, ("demo", "PS1", ["--hare-steps", _way, "browser", "--hare-addr", "192.168.100.200"]))
# the hotspot once someone came in: the news, network and PIN in the heading
EXTRA.setdefault("hotspot-in", ("demo", "PS1", ["--hotspot", "ZyperOS-7A2B", "q7mx-3kpa", "1 signed in", "Tobu Tobu Girl.gb", "--hotspot-in"]))
# What did you plug in? (DESIGN.md 11.36): each row focused, and the
# messages after Computer, after Phone and for an iPhone
for _k in ("0", "1", "2", "said", "phone", "iphone"):
    EXTRA.setdefault("usb-power-" + _k, ("demo", "PS1", ["--usb-power", _k]))
# MENU's USB rows (DESIGN.md 11.36): USB Device while a plug waits, with
# the corner's USB mark; USB Accessories on Ask and on Automatic, and the
# message Automatic says as it is chosen
_USB_WAIT = ["--status", "10:08 24h wifi=off batt=82 usb=on"]
EXTRA.setdefault("usb-menu-wait", ("demo", "PS1", ["--usb-menu", "--menu", "USB Device", *_USB_WAIT]))
EXTRA.setdefault("usb-menu-ask", ("demo", "PS1", ["--usb-menu", "--menu", "USB Accessories"]))
EXTRA.setdefault("usb-menu-auto", ("demo", "PS1", ["--usb-menu", "auto", "--menu", "USB Accessories"]))
EXTRA.setdefault("usb-menu-said", ("demo", "PS1", ["--usb-menu", "said"]))
# Zyper's one-time offer of Automatic, after a second USB Accessory
EXTRA.setdefault("usb-offer", ("demo", "PS1", ["--usb-offer"]))
EXTRA.setdefault("usb-mark", ("demo", "PS1", _USB_WAIT))
# System Update (DESIGN.md 11.17, 2026-11-11): What's New scrolled to its
# end, the corner's mark of a download behind the shelf, and Zyper's news
EXTRA.setdefault("update-notes-end", ("demo", "PS1", ["--update", "--update-scroll", "99"]))
EXTRA.setdefault("update-mark", ("demo", "PS1", ["--status", "10:08 24h wifi=3 batt=82 update=42"]))
for _k in ("available", "ready"):
    EXTRA.setdefault("update-news-" + _k, ("demo", "PS1", ["--upnews", _k]))
EXTRA.setdefault("hotspot-waiting", ("demo", "PS1", ["--hotspot", "ZyperOS-7A2B", "q7mx-3kpa", "0 joined", "waiting"]))
EXTRA.setdefault("gamemenu-dual", ("demo", "PS1", ["--gamemenu", "0", "dual"]))
# ...and its Display tab, where the screens' three rows are, on the Stylus row
EXTRA.setdefault("gamemenu-dual-display", ("demo", "PS1", ["--gamemenu", "0", "dual", "--gm-tab", "1"]))
EXTRA.setdefault("gamemenu-dual-stylus", ("demo", "PS1", ["--gamemenu", "4", "dual", "--gm-tab", "1"]))
# MENU's Shader when a system has its own (SHOT_SETTINGS below): "per system"
EXTRA.setdefault("menu-shader", ("demo", "PS1", ["--menu", "Shader"]))

# The README's shots' own settings, and these shots'
SHOT_SETTINGS = {**R.SHOT_SETTINGS, "menu-shader": {"shader.PS1": "shimmerless"},
                 "menu-saver-off": {"saver": "0"}}

# MENU > Menu Music with the card's own tracks in its Menu Music folder, at
# the top of the card (src/bgm.h): ZyperOS Tracks and the note in each of its counts,
# drawn with no cursor so the note is pinned under the rows; and with none,
# the note that says where they go. The files are made for the shot and
# taken away after it.
SHOT_FILES = {}
_THREE = ["Menu Music/01 Title Theme.mp3", "Menu Music/Pixel Quest/02 Green Fields.mp3",
          "Menu Music/Pixel Quest/03 Boss.ogg"]
for _name, _files, _off in (("music-page-mine", _THREE, False), ("music-page-mine-one", _THREE[:1], False),
                            ("music-page-mine-off", _THREE, True), ("music-page-mine-off-one", _THREE[:1], True)):
    EXTRA.setdefault(_name, ("demo", "PS1", ["--music-page", "foot"]))
    SHOT_FILES[_name] = _files
    if _off:
        SHOT_SETTINGS[_name] = {"bgm.builtin": "off"}
EXTRA.setdefault("music-page-none", ("demo", "PS1", ["--music-page", "foot"]))


def set_files(card, files, on):
    """A shot's files (SHOT_FILES) on the card, empty, or off it"""
    for f in files or []:
        path = card / f
        if on:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        elif path.exists():
            path.unlink()


# The in-game menu's Shader at each old screen, so each name is drawn and fit
for _s in ("shimmerless", "crt", "crt-curved", "scanlines", "lcd"):
    EXTRA.setdefault("gamemenu-shader-" + _s, ("demo", "PS1", ["--screen", "games", "--gamemenu", "1", "--gm-tab", "1"]))
    SHOT_SETTINGS["gamemenu-shader-" + _s] = {"shader.PS1": _s}

# The in-game menu's Controls tab (src/ctltab.c), beside the handheld: its
# end (Reset Buttons, Hotkeys), a button's choices, the system over a game
# with buttons of its own and the note under it, a game button left with
# none, Reset Buttons' two questions, the Hotkeys row's pages (at their own
# width, as the Controls pages are); the Nintendo 64's
# (Pak, SELECT held for the C buttons) and the Dreamcast's (Face Buttons in
# each of its three values, the stick on and SELECT held for its d-pad).
# Their buttons and sticks are the card's library's (SHOT_LIB), the shelf on
# CTL_FOCUS's game.
_MGS = "map.PS1." + R.FOCUS + ".chd"
_SONIC = "Sonic Adventure (USA).chd"
for _name, _tag, _flags in (
        ("end", "PS1", ["--gamemenu", "13"]), ("pick", "PS1", ["--gamemenu", "5", "--gm-pick"]),
        ("all", "PS1", ["--gamemenu", "0", "--gm-all"]), ("warn", "PS1", ["--gamemenu", "3"]),
        ("ask", "PS1", ["--gamemenu", "12", "--gm-ask"]),
        ("ask-all", "PS1", ["--gamemenu", "12", "--gm-ask", "--gm-all"]),
        ("keys", "PS1", ["--gamemenu", "13", "--gm-keys", "0"]),
        ("keys-ingame", "PS1", ["--gamemenu", "13", "--gm-keys", "1"]),
        ("n64-end", "N64", ["--gamemenu", "13"]),
        ("dc", "DC", ["--gamemenu", "2"]), ("dc-end", "DC", ["--gamemenu", "13"]),
        ("dc-place", "DC", ["--gamemenu", "2"]), ("dc-mixed", "DC", ["--gamemenu", "2"])):
    EXTRA.setdefault("gamemenu-controls-" + _name,
                     ("demo", _tag, ["--screen", "games", "--gm-tab", "2"] + _flags))
# A Nintendo 64 game's Pak row, back in the Controls tab
EXTRA.setdefault("gamemenu-pak", ("demo", "N64", ["--screen", "games", "--gm-tab", "2", "--gamemenu", "2"]))
CTL_FOCUS = {"gamemenu-pak": "Mario Kart 64 (USA).z64",
             "gamemenu-controls-n64-end": "Mario Kart 64 (USA).z64",
             **{"gamemenu-controls-" + _n: _SONIC for _n in ("dc", "dc-end", "dc-place", "dc-mixed")}}
# A game's details with its own buttons (Buttons, This Game), and the page that
# row opens
EXTRA.setdefault("info-buttons", ("demo", "PS1", ["--screen", "games", "--info"]))
EXTRA.setdefault("info-buttons-page", ("demo", "PS1", ["--screen", "games", "--info-buttons"]))
SHOT_LIB = {"info-buttons": {_MGS: "1;b:y,y:b"},
            "info-buttons-page": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-all": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-pick": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-warn": {_MGS: "1;b:none"},
            "gamemenu-controls-ask": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-ask-all": {"map.PS1": "1;b:y,y:b"},
            "gamemenu-controls-dc-end": {"stick.DC." + _SONIC: "1"},
            "gamemenu-controls-dc-place": {"map.DC." + _SONIC: "1;a:a,b:b,x:x,y:y"},
            "gamemenu-controls-dc-mixed": {"map.DC." + _SONIC: "1;a:a"}}


def set_lib(card, settings, on):
    """A shot's keys (SHOT_LIB) into the card's library, or out of it"""
    if not settings:
        return
    import sqlite3
    with sqlite3.connect(card / "shared/.zyperos/library.db") as db:
        db.execute("CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL)")
        for key, value in settings.items():
            if on:
                db.execute("INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)", (key, value))
            else:
                db.execute("DELETE FROM settings WHERE key=?", (key,))


# MENU's tabs (DESIGN.md 11.3), each the one that is on, its name centered
# and its neighbours' beside it; Display on its Theme row, with its chips
for _tab in range(6):
    EXTRA.setdefault("menu-tab-%d" % _tab, ("demo", "PS1", ["--menu-tab", str(_tab), "--menu", "0"]))
EXTRA.setdefault("menu-display-theme", ("demo", "PS1", ["--menu-tab", "1", "--menu", "Theme"]))
# Display's Screensaver at off: "off", a status's lowercase, now that the
# switches say On and Off (menu.off, and scraper.off and ssh.off, its words)
EXTRA.setdefault("menu-saver-off", ("demo", "PS1", ["--menu-tab", "1", "--menu", "Screensaver"]))
# Display's Theme page (DESIGN.md 11.42) in each of the ten accents, the
# tones going round in both themes and the corners turning over, so every
# accent's, tone's and value's name is drawn; its foot, Zyper and Reset to
# Default, both ways; and Auto Hours
_TONES = (("warm", "black"), ("neutral", "deepblue"), ("cool", "darkgrey"))
for _i, _acc in enumerate(("lime", "green", "teal", "blue", "purple", "pink", "red", "orange",
                           "yellow", "silver")):
    _lt, _dk = _TONES[_i % 3]
    EXTRA.setdefault("theme-" + _acc, ("demo", "PS1", [
        "--look", "%s,%s,%s,%s,on" % (_acc, _lt, _dk, "square" if _i % 2 else "rounded"),
        "--look-page", "theme", "0"]))
EXTRA.setdefault("theme-foot-on", ("demo", "PS1", ["--look-page", "theme", "5"]))
EXTRA.setdefault("theme-foot-off", ("demo", "PS1", ["--look", "lime,warm,black,square,off",
                                                    "--look-page", "theme", "5"]))
EXTRA.setdefault("auto-hours", ("demo", "PS1", ["--look-page", "hours", "0"]))

# The pop-ups the SELECT hotkeys put over a game (src/main.c, on_quick and
# on_speed), each as it says: the slot saved, loaded, empty or failed, a game
# whose core keeps no states, and the speed at a half step and a whole one;
# the ones over a game started fresh: a DS game in a new Game Language, and
# a game whose resume the shelf's other core left; and NES Player 2's, a
# friend's browser joining and leaving
for _kind in ("saved", "loaded", "save_failed", "load_failed", "slot_empty", "no_states", "dslang",
              "corestate", "stick_on", "stick_off", "player2_joined", "player2_left",
              "player2_stopped"):
    EXTRA.setdefault("popup-" + _kind, ("demo", "PS1", ["--popup", _kind]))
EXTRA.setdefault("popup-speed-half", ("demo", "PS1", ["--popup", "speed", "3"]))
EXTRA.setdefault("popup-speed-whole", ("demo", "PS1", ["--popup", "speed", "4"]))
# NES Player 2 (src/p2.h): its page over a paused NES game in every state
# (--player2), the code on the hotspot with nobody yet and its address under
# it, a player connected, the cable, before A on each link, with neither
# plugged in, after a failure, the hotspot's start, and the stop on each
# link; and the in-game menu on its row, which says connected
for _kind in ("hotspot", "connected", "cable", "off", "off-cable", "none", "failed", "starting",
              "closing", "closing-cable"):
    EXTRA.setdefault("player2-" + _kind, ("demo", "NES", ["--screen", "games", "--player2", _kind]))
EXTRA.setdefault("gamemenu-player2", ("demo", "NES", ["--screen", "games", "--gamemenu", "4",
                                                       "--player2", "connected"]))

# MENU > System Update in the states whose lines are longest, each as the
# status file the update program writes it (src/sysupdate.c); the second is
# whether a network is there (a test release's source on the card counts as
# one). The lines wrap onto a second line, and fail here if they still do not fit.
UPDATE_STATES = {
    "update-unsupported": ("state unsupported\n", True),
    "update-unsupported-offline": ("state unsupported\n", False),
    "update-uptodate": ("state uptodate\nversion 0.1.0\ntime {now}\n", True),
    "update-idle-offline": ("state idle\n", False),
    # A download cut short, Wi-Fi gone: it says it goes on by itself
    "update-stopped-offline": ("state stopped\ninstalled 0.1.0\nversion 0.1.1\nkind system\n"
                               "done 31000000\ntotal 83000000\ndownload 83000000\ntime {now}\n", False),
    "update-error": ("state error\ntime {now}\n", True),
    "update-error-download": ("state error\nversion 0.1.1\ndownload 83000000\nkind system\ntime {now}\n", True),
    "update-needs-system": ("state needs-system\nversion 0.3.0\ntime {now}\n", True),
    "update-too-big": ("state too-big\nversion 0.3.0\nkind system\ntime {now}\n", True),
    "update-space": ("state space\nversion 0.2.0\ndownload 14200000\ntime {now}\n", True),
    "update-available-offline": ("state available\nversion 0.1.1\ndownload 83000000\nkind system\ntime {now}\n", False),
    "update-ready": ("state ready\nversion 0.1.1\nkind system\ntime {now}\n", True),
}
EXTRA.update({name: ("demo", "PS1", ["--update"]) for name in UPDATE_STATES})

# The Install PortMaster page (src/pminstall.c) in its states with the
# longest words: its status as res/portmaster/pm-install.py writes it, and
# --portmaster's view of the device. And the Ports shelf with PortMaster not
# on the card, A saying Install, on a card of its own that has the installer
PM_STATES = {
    "portmaster-offer": ("", "none"),
    "portmaster-offline": ("", "nonet"),
    "portmaster-old-system": ("", "nopython"),
    "portmaster-looking": ("state checking\n", "running"),
    "portmaster-downloading": ("state downloading\nversion 2026.06.23-0015\ntotal 24121702\ndone 6100000\n", "running"),
    "portmaster-verifying": ("state verifying\nversion 2026.06.23-0015\n", "running"),
    "portmaster-unpacking": ("state unpacking\nversion 2026.06.23-0015\ntotal 41198857\ndone 18000000\n", "running"),
    "portmaster-stopped": ("state unpacking\nversion 2026.06.23-0015\ntotal 41198857\ndone 18000000\n", "none"),
    "portmaster-done": ("state done\nversion 2026.06.23-0015\n", "installed"),
    "portmaster-network": ("state error\nerror network\n", "none"),
    "portmaster-checksum": ("state error\nversion 2026.06.23-0015\nerror checksum\n", "none"),
    "portmaster-space": ("state error\nversion 2026.06.23-0015\nerror space\nneed 108000000\n", "none"),
    "portmaster-unpack": ("state error\nversion 2026.06.23-0015\nerror unpack\n", "nonet"),
    "portmaster-release": ("state error\nerror release\n", "none"),
    # Reinstall PortMaster: the offer from its details page, the page that
    # says it closed with an error, at work, done, failed
    "portmaster-reinstall": ("", "installed,repair"),
    "portmaster-reinstall-failed": ("", "installed,failed"),
    "portmaster-reinstalling": ("state downloading\nkind reinstall\nversion 2026.06.23-0015\n"
                                "total 24121702\ndone 6100000\n", "installed,repair,running"),
    "portmaster-reinstalled": ("state done\nkind reinstall\nversion 2026.06.23-0015\n", "installed,repair"),
    "portmaster-reinstall-error": ("state error\nkind reinstall\nversion 2026.06.23-0015\nerror checksum\n",
                                   "installed,repair"),
}
EXTRA.update({name: ("demo", "PS1", ["--portmaster", view]) for name, (_, view) in PM_STATES.items()})
# PortMaster's details page (X on its entry) with PortMaster on the card:
# its Reinstall PortMaster row, on a card of its own that has PortMaster
EXTRA["portmaster-details"] = ("pmcard", "PRT", ["--screen", "games", "--info"])
# (The Ports shelf with PortMaster's Install, and the page downloading, are
# the README's, tools/readme-shots.py's ports and portmaster.)

# Every Controls page of the made-up ports (tools/fixtures/ports) and of
# PortMaster's own app, on the card that has them (readme-shots.py's
# portdocs_card): a port's README's words, its keys, In a game and its About,
# each with the shelf on that port (PORT_FOCUS)
PORT_FOCUS = {}
for _script, _pages in (("Star Farm.sh", 5), ("Lone Key.sh", 3), ("Odd Keys.sh", 3), ("Plain.sh", 1),
                        ("PortMaster.sh", 2)):
    for _page in range(_pages):
        _name = "port-%s-%d" % (_script[:-3].replace(" ", "").lower(), _page)
        EXTRA.setdefault(_name, ("portdocs", "PRT", ["--screen", "games", "--gamecontrols", str(_page), "PRT"]))
        PORT_FOCUS[_name] = _script


# The demonstration card with one game in each system that has none (a name
# and its first extension, from config/systems.cfg), so that every system is
# a shelf, and its in-game menu and Controls are the ones of its games
def all_systems_card(card):
    for line in (ROOT / "config" / "systems.cfg").read_text(encoding="utf-8").splitlines():
        f = line.split("|")
        if f[0] != "sys" or len(f) < 8:
            continue
        folder = card / "Roms" / f[2]
        if folder.exists():
            continue
        if f[4] == "SCUMMVM":
            (folder / "Beneath a Steel Sky").mkdir(parents=True)
            (folder / "Beneath a Steel Sky" / "readme.txt").touch()
            continue
        exts = [e for e in f[7].split(",") if e != "zip"] or ["zip"]
        folder.mkdir(parents=True)
        (folder / ("Stand In." + exts[0])).touch()


# Each language's own run of every shot, on its own cards
def run_language(code, shots, binary, keep, theme="light"):
    notes, failures, data, drawn = [], [], {}, set()
    with tempfile.TemporaryDirectory(prefix="zyperos-i18n-%s-" % code) as tmp:
        cards = {"demo": Path(tmp) / "demo", "empty": Path(tmp) / "empty", "scummvm": Path(tmp) / "scummvm",
                 "openbor": Path(tmp) / "openbor", "ports": Path(tmp) / "ports", "all": Path(tmp) / "all",
                 "portdocs": Path(tmp) / "portdocs", "media": Path(tmp) / "media",
                 "pmcard": Path(tmp) / "pmcard", "psp": Path(tmp) / "psp", "es": Path(tmp) / "es"}
        R.demo_card(cards["demo"], None, {"artwork": {}})
        R.media_card(cards["media"])
        R.es_card(cards["es"])
        R.ports_card(cards["ports"])
        R.portdocs_card(cards["portdocs"])
        R.portdocs_card(cards["pmcard"])
        (cards["pmcard"] / "Roms/Ports/PortMaster").mkdir()
        (cards["pmcard"] / "Roms/Ports/PortMaster/pugwash").touch()
        (cards["pmcard"] / "Roms/Ports/PortMaster/version").write_text("2026.11.12-1059\n")
        R.demo_card(cards["all"], None, {"artwork": {}})
        all_systems_card(cards["all"])
        R.scummvm_card(cards["scummvm"])
        R.openbor_card(cards["openbor"])
        R.psp_card(cards["psp"])
        R.card_dirs(cards["empty"])
        out = (keep / code) if keep else Path(tmp) / "out"
        out.mkdir(parents=True, exist_ok=True)
        for name, (card_name, tag, flags) in shots.items():
            card = cards[card_name]
            focus = PORT_FOCUS.get(name) or CTL_FOCUS.get(name) or R.SHOT_FOCUS.get(name) or \
                {"PS1": R.FOCUS + ".chd", "SCUMMVM": R.SCUMMVM_FOCUS, "OPENBOR": R.OPENBOR[0],
                 "PRT": "PortMaster.sh"}.get(tag, "")
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
            command = [str(binary), "--shot", str(target), "--theme", theme, "--lang", code,
                       "--screen", "systems", "--phase", "3200", *status, "--fn-volume",
                       *[f.replace("{card}", str(card)) for f in flags]]
            # errors=replace: a log line may end mid-character (a cut name)
            env = R.pm_env(R.env_for(card), name, tmp)
            if name in PM_STATES:
                pm = Path(tmp) / ("pm-" + name)
                pm.mkdir(exist_ok=True)
                (pm / "status").write_text(PM_STATES[name][0])
                env["ZYPEROS_PM_STATUS"] = str(pm / "status")
            # A shot's own settings (readme-shots' SHOT_SETTINGS), only while it
            # is taken: the Shader row at its widest value
            R.set_settings(card, SHOT_SETTINGS.get(name), True)
            set_lib(card, SHOT_LIB.get(name), True)
            set_files(card, SHOT_FILES.get(name), True)
            r = subprocess.run(command, cwd=ROOT, env=env, text=True, errors="replace",
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            R.set_settings(card, SHOT_SETTINGS.get(name), False)
            set_lib(card, SHOT_LIB.get(name), False)
            set_files(card, SHOT_FILES.get(name), False)
            for line in r.stdout.splitlines():
                if line.startswith(("i18n: cut data: ", "i18n: slides data: ", "i18n: tight data: ")):
                    data.setdefault(name, []).append(line[6:])
                elif line.startswith("fit: drawn: "):
                    drawn.add(unescape(line[12:]))
                elif line.startswith("fit: overflow: "):
                    px, box, text = line[15:].split("|", 2)
                    kind, span, *beside = box.split(" ", 2)
                    what = {"screen": "past the screen's edge",
                            "meets": "into the text beside it (x %s, \"%s\")" % (span, beside[0] if beside else ""),
                            "box": "past its box (x %s)" % span}[kind]
                    text = unescape(text)
                    notes.append("%s/%s: %s: %s px %s: %s" % (
                        code, name, strings_of(code).label(text), px, what, text.replace("\n", " / ")[:60]))
                elif line.startswith("i18n: "):
                    notes.append("%s/%s: %s" % (code, name, line[6:]))
            ok_anyway = name in R.CORNER_OWN and R.CORNER_OWN[name] in r.stdout
            if (r.returncode and not ok_anyway) or not target.is_file():
                tail = " | ".join(r.stdout.strip().splitlines()[-3:])
                failures.append("%s/%s: the shot failed (exit %d): %s" % (code, name, r.returncode, tail))
    return code, notes, failures, data, drawn


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--keep", type=Path, help="keep the frames in DIR/<code>/")
    ap.add_argument("--lang", action="append", help="this language only (more than once for more)")
    ap.add_argument("--strict", action="store_true", help="fail on the pseudo-locale's too")
    ap.add_argument("--coverage", action="store_true", help="list the strings no shot draws")
    ap.add_argument("--write-unshot", action="store_true",
                    help="write tools/i18n-unshot.txt: every string no shot draws, as it is now")
    ap.add_argument("--theme", choices=["light", "dark"], default="light",
                    help="the theme the shots are drawn in (light; tools/pixel-gate.py takes both)")
    ap.add_argument("--binary", type=Path, help="this launcher, not build-native/zyperos (tools/pixel-gate.py's base)")
    ap.add_argument("--jobs", type=int, help="languages shot at once (all of them)")
    ap.add_argument("only", nargs="*", help="shot names (default: all)")
    args = ap.parse_args()
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    binary = args.binary.resolve() if args.binary else ROOT / "build-native" / "zyperos"
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
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(args.jobs or len(langs), len(langs)))) as pool:
        for code, notes, failures, data, drawn in pool.map(lambda c: run_language(c, shots, binary, keep, args.theme),
                                                           langs):
            # Data (a game's name, a formatted line) may be cut or slide in
            # English too; a screen where a language cuts more of it than
            # English does has a translation that made the cut ("vinculado
            # como t..." beside ScreenScraper)
            results[code] = (notes, failures, data, drawn)
    english = results["en"][2] if "en" in results else None
    for code in langs:
        if english is None or code == "en": continue
        for name, said in sorted(results[code][2].items()):
            if len(said) > len(english.get(name, [])):
                results[code][0].extend("%s/%s: %s (English: %d)" % (code, name, d, len(english.get(name, [])))
                                        for d in said)
    # Every string a shot draws, or says why it is none of the shots'
    full = not args.only and not args.lang and "en" in results
    gone = []
    if "en" in results:
        st = strings_of("en")
        seen = set()
        for text in results["en"][3]:
            seen.update(st.ids(text))
        gone = [n for n in st.names if n not in seen]
    if args.coverage:
        for name in gone:
            print("undrawn " + name)
        print("%d of %d strings are drawn in no shot" % (len(gone), len(st.names)))
    if args.write_unshot:
        write_unshot(gone)
        print("wrote %s: %d strings" % (UNSHOT.relative_to(ROOT), len(gone)))
        return 0
    unshot_bad = []
    if full:
        listed = read_unshot()
        unshot_bad = ["%s: drawn in no shot and not in tools/i18n-unshot.txt: add a shot to "
                      "tools/i18n-shots.py (a text of it must be drawn in each language), or list it "
                      "there with the reason" % n for n in gone if n not in listed]
        for n in sorted(listed - set(gone)):
            print("note %s is in tools/i18n-unshot.txt but is drawn (or gone): "
                  "python3 tools/i18n-shots.py --write-unshot" % n)
    bad = 0
    for code in langs:
        notes, failures = results[code][:2]
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
    for n in unshot_bad:
        print("FAIL " + n)
        bad += 1
    pseudo = len(results.get(i18n.PSEUDO, ([], [], {}, set()))[0])
    if bad:
        print("check-i18n-shots: %d problems in %d screens x %d languages" % (bad, len(shots), len(langs)))
        return 1
    print("check-i18n-shots: %d screens in %s, everything fits%s" % (
        len(shots), ", ".join(langs),
        "; the pseudo-locale cut or overflowed %d strings (notes above)" % pseudo if pseudo else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
