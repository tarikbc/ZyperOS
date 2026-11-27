#!/usr/bin/env python3
"""The launcher's screens in every language, and what does not fit in them.

    make native
    python3 tools/i18n-shots.py                 every screen in each language,
                                                in light; fails on what does
                                                not fit (make check-i18n-shots)
    python3 tools/i18n-shots.py --keep DIR      and keeps the frames in DIR/<code>/
    python3 tools/i18n-shots.py --lang fr menu  one language, some screens
    python3 tools/i18n-shots.py --device brick --keep DIR
                                                every screen as that handheld
                                                alone (src/device/profile.h)
    python3 tools/i18n-shots.py --part 2/4      the second of four parts of the
                                                languages and handhelds (CI's
                                                macOS job, one part a runner)

Every screen is shot as each handheld ZyperOS runs on (DEVICES: the Pixel 2,
the README's, first, then the Brick and the Smart Pro), with its own keys,
rows and words: the Brick's MENU is the word on its key, wider than the
Pixel 2's three dots, and it has rows the Pixel 2 has not (Bluetooth) and
the other way round (Side Lights); the Smart Pro's canvas is 1368 units wide
(src/canvas.h). A string must fit on each, and is drawn if any draws it.

A layout drawn for English meets words up to half again as long in French or
Portuguese, and Chinese's taller face. The launcher, built for the host, says
on stderr when a string from res/strings had to be cut, broken past its
lines, slid in a marquee or drawn off the screen ("i18n: cut: ...", src/ui.c), in the language
it is drawn in. This shoots the README's screens (tools/readme-shots.py's
cards and SHOTS) and the rest of the main ones in every language, and fails on any such line, or a shot that fails
twice (one that fails is taken once more: a screen fails every time, and one
that passes the second time is said, "flaky ...", with its first reason).

The pseudo-locale, xx (src/i18n.h: every string accented, 40% longer and in
brackets), is shot too, and what it does not fit is listed: a warning of
what a longer translation would meet, which fails only with --strict.

Shots, not pictures held to a reference: what is drawn is the launcher's own,
and English's frames are held by the other shot checks. Each language has its
own scratch cards, cloned from one set made once a run, so they run at once,
one launcher a language and handheld (--jobs says otherwise). Stopped
(Ctrl-C, SIGTERM, a closed terminal), each language ends after its shot and
every scratch card goes. A pass is kept by the launcher's bytes and the
inputs its verdict is made of, and the same again prints it and passes at
once (ZYPEROS_SHOTS_CACHE=0 shoots anyway). Needs the launcher built (SDL2);
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
import threading
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import i18n  # noqa: E402

_spec = importlib.util.spec_from_file_location("readme_shots", ROOT / "tools" / "readme-shots.py")
R = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(R)

# The handhelds every screen is shot as: the README's first, whose every row
# a shot finds (a missing one is a renamed row), then the rest, which may lack
# one (the Brick's Strength row, with no GPU shaders); the Smart Pro for its
# wide canvas
DEVICES = ["pixel2", "brick", "smartpro"]

# The string IDs of a text the launcher drew: src/ui.c says the text, not the
# ID (a label is copied into a row's buffer, or formatted around a name, and
# is not the table's pointer by then), so it is found in the language's own
# strings: the whole string, one of its lines, or a format's pattern
class Strings:
    def __init__(self, code):
        errors = []
        self.names, self.whole, self.formats, self.leads = [], {}, [], []
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
                    # A format whose own words come first ("auto (%s)"): what
                    # it begins with is the string's, not a name put in it
                    if i18n.FMT_RE.match(piece) is None:
                        self.leads.append((self.formats[-1][0], sid))

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

    def small_first(self, text):
        """The IDs whose words begin `text` with a small letter: a row's value
        begins with a capital in every language that has them
        (GLOSSARY.md), and a name in it (a core's, a network's, a song's)
        keeps its own case, so only the table's own first word is held"""
        if not text[:1].islower():
            return []
        if text in self.whole:
            return self.whole[text]
        return [sid for pattern, sid in self.leads if pattern.match(text)]

    def label(self, text):
        found = self.ids(text)
        found = list(dict.fromkeys(found))
        return "/".join(found[:3]) if found else "(not a string)"


# The strings that are drawn as a part of another's text: a page's name in a
# heading, a mix's in Audio Channels' "auto (mono)" or a place's in Audio
# Output's "auto (speaker)", and the reason a scraper's run passed a system
# over in "NES: no such folder" (art.problem), and a month's short name and
# AM or PM in MENU's Date & Time row ("Jan 15, 10:08", clock.label_24h) and
# in a save slot's moment ("Aug 20 1:00:00 PM", slots.when)
COMPOSED = ("controls.page.", "audio.", "art.why_", "clock.short_", "clock.am", "clock.pm", "slots.jan",
            "slots.feb", "slots.mar", "slots.apr", "slots.may", "slots.jun", "slots.jul", "slots.aug",
            "slots.sep", "slots.oct", "slots.nov", "slots.dec", "slots.am", "slots.pm")

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


def read_unshot_why():
    """Each listed string's own reason, the words after its ID's #"""
    why = {}
    for line in UNSHOT.read_text(encoding="utf-8").splitlines() if UNSHOT.is_file() else []:
        name, sep, reason = line.partition("#")
        if sep and name.strip():
            why[name.strip()] = reason.strip()
    return why


def write_unshot(ids):
    # A string's own reason stays with it, as the header says
    kept = read_unshot_why()
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
        out.append(name + ("  # " + kept[name] if kept.get(name) else ""))
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
    # Date & Time's page (src/clock_screen.c's clock_page_rows): Time Zone on Auto
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
    "system-menu-end": ("demo", "PS1", ["--screen", "games", "--menu", "Downloads"]),
    # Favorites First under Sort By, On (DESIGN.md 11.70), its longer value
    # in most languages, over a shelf of its favorites first
    "system-menu-favfirst": ("demo", "PS1", ["--screen", "games", "--menu", "Favorites First"]),
    "storage-art": ("demo", "PS1", ["--storage", "art"]),
    "storage-states": ("demo", "PS1", ["--storage", "states"]),
    "storage-ds": ("demo", "PS1", ["--storage", "ds"]),
    # ...its row focused, the line under the list saying what it is
    "storage-ds-row": ("demo", "PS1", ["--storage", "main", "0"]),
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
    "about-3": ("demo", "PS1", ["--about", "3"]),
    "controls-0": ("demo", "PS1", ["--controls", "0"]),
    "controls-1": ("demo", "PS1", ["--controls", "1"]),
    "controls-2": ("demo", "PS1", ["--controls", "2"]),
    "gamemenu": ("demo", "PS1", ["--gamemenu", "0"]),
    # A game on more than one disc: the Disc row with the core's name for
    # the disc (it fits), and with one too long, which reads "Disc 2 of 3"
    "gamemenu-disc": ("demo", "PS1", ["--gamemenu", "4", "--gm-discs", "3", "FFIX (Disc 2)"]),
    # Undo Load, under Load after a load (1.0.6)
    "gamemenu-undo": ("demo", "PS1", ["--gamemenu", "3", "--gm-undo"]),
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
    # Every display mode's name on the Display row (1.0.6; what each does is
    # in its list, beside its picture: gamemenu-display-list-*)
    "gamemenu-dmode-aspect": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--dmode", "aspect"]),
    "gamemenu-dmode-square": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--dmode", "square"]),
    "gamemenu-dmode-integer": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--dmode", "integer"]),
    "gamemenu-dmode-tall": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--dmode", "integer-vertical"]),
    "gamemenu-dmode-overscale": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--dmode", "overscale"]),
    "gamemenu-dmode-fill": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--dmode", "fill"]),
    "gamemenu-dmode-native": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--dmode", "native"]),
    "system-menu-dmode-fill": ("demo", "PS1", ["--screen", "games", "--menu", "Display", "--dmode", "fill"]),
    # The menu over a port still starting, and its loading screen after a
    # minute (1.0.6)
    "gamemenu-port-waiting": ("demo", "PS1", ["--gamemenu", "0", "port", "--port-waiting"]),
    "gamemenu-controls": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "2"]),
    # ...and of a game the launcher has no hash for: a PSP game ("system"),
    # a PlayStation .chd ("file"), one in each screen; the Achievements row
    # is on the in-game menu's Game tab
    "gamemenu-nohash": ("demo", "PS1", ["--gamemenu", "0", "--cheevos-no-hash", "system"]),
    # The Achievements tab (1.0.5), in every state it has: the list (with
    # unlocks waiting too), every one earned, and the sentence where a game
    # has no list (src/achtab.h)
    "achtab-list": ("demo", "PS1", ["--gamemenu", "1", "--gm-tab", "3", "--achtab", "list"]),
    "achtab-waiting-send": ("demo", "PS1", ["--gamemenu", "4", "--gm-tab", "3", "--achtab", "list",
                                            "--cheevos-waiting", "2"]),
    "achtab-all": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "all"]),
    "achtab-not-system": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "not-system"]),
    "achtab-not-file": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "not-file"]),
    "achtab-signed-out": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "signed-out"]),
    "achtab-waiting": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "waiting"]),
    "achtab-unchecked": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "unchecked"]),
    "achtab-none": ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "none"]),
    # An achievement's card, opened with A from its list (1.1.0): its badge
    # under the heading's rule, its description whole. Earned; not earned,
    # the longest description there is, which scrolls itself (and the same
    # moved on); earned offline, waiting to send; none, with the plain badge
    "cheevo-card": ("demo", "PS1", ["--cheevo-card", "0", "--cheevos-badge",
                                    "{card}/Roms/PlayStation/.cheevos/Hagane.set", "b2"]),
    "cheevo-card-long": ("demo", "PS1", ["--cheevo-card", "1", "--phase", "0", "--cheevos-badge",
                                         "{card}/Roms/PlayStation/.cheevos/Hagane.set", "b4"]),
    "cheevo-card-long-moved": ("demo", "PS1", ["--cheevo-card", "1", "--phase", "9000", "--cheevos-badge",
                                               "{card}/Roms/PlayStation/.cheevos/Hagane.set", "b4"]),
    "cheevo-card-waiting": ("demo", "PS1", ["--cheevo-card", "2", "--cheevos-badge",
                                            "{card}/Roms/PlayStation/.cheevos/Hagane.set", "b3"]),
    "cheevo-card-plain": ("demo", "PS1", ["--cheevo-card", "3"]),
    "info-nohash": ("demo", "PS1", ["--screen", "games", "--info", "--cheevos-no-hash", "file"]),
    "gamecontrols": ("demo", "PS1", ["--gamecontrols", "1", "MD"]),
    "slots": ("demo", "PS1", ["--slots", "0"]),
    "synopsis": ("demo", "PS1", ["--screen", "games", "--info", "--synopsis"]),
    "keyboard": ("demo", "PS1", ["--keyboard"]),
    # The ScreenScraper sign-in's title, the longest the keyboard has
    "keyboard-ss-user": ("demo", "PS1", ["--keyboard", "--kb-ss-user"]),
    # Rename on an arcade set or a port: the name shown only, its title and
    # its note before the hints; emptied, the original name dimmed in the
    # field (src/gamern_screen.c)
    "keyboard-rename-shown": ("demo", "PS1", ["--screen", "games", "--kb-rename-shown"]),
    "keyboard-rename-shown-empty": ("demo", "PS1", ["--screen", "games", "--kb-rename-shown", "empty"]),
    "video-controls": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "controls"]),
    "video-full": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "hidden"]),
    # A video this CPU cannot carry, its notice: too big (AV1 at 60 fps), with
    # the controls it brings up; no picture at all, alone in the black
    "video-too-big": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "controls",
                                       "--video-size", "1920x1080", "--video-trouble", "too-big"]),
    "video-no-picture": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "hidden",
                                          "--video-size", "1920x1080", "--video-trouble", "no-picture"]),
    # MP4's lock screen, its panel with the bar a hold fills: over music, and
    # over a video, where the panel is the same and the picture is under it
    # MP4's menu on its shelf, which shows All (albums and videos mixed)
    "mp4-menu": ("media", "MUSE", ["--screen", "games", "--menu"]),
    # MP4's shelf in its other two Views, START's turns (DESIGN.md 11.67);
    # the README's mp4 is Covers
    "mp4-grid": ("media", "MUSE", ["--screen", "games"]),
    "mp4-list": ("media", "MUSE", ["--screen", "games"]),
    # MP4's Player Looks (DESIGN.md 11.64): each on Now Playing, Classic
    # paused too (its panel says so where the time's words would), and
    # MENU's Player Look list with its picture, the look under the cursor
    "np-cassette": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "cassette"]),
    "np-vinyl": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "vinyl"]),
    "np-classic": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "classic"]),
    "np-classic-paused": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "classic", "--paused"]),
    "np-spectrum": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "spectrum"]),
    # ...its list with the cursor on each look, so every look's note is
    # drawn in its picture's narrow column
    **{"mp4-look-list-%s" % _l: ("media", "MUSE", ["--screen", "games", "--menu", "Player Look", "--press",
                                                   "a", "--pick-at", str(_k)])
       for _k, _l in enumerate(("cover", "cassette", "vinyl", "classic", "spectrum"))},
    # MP4's Tape Styles (DESIGN.md 11.64): the Clear card, Worn paused (its
    # pause's word where the time left was), a style's name in its pill as
    # Up or Down leaves it, and MENU's Tape Style list on each style, its
    # note in the picture's narrow column
    "np-tape-clear": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "cassette", "--np-tape", "clear"]),
    "np-tape-worn-paused": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "cassette",
                                             "--np-tape", "worn", "--paused"]),
    "np-tape-said": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--np-look", "cassette",
                                      "--np-tape", "band", "--np-tape-said"]),
    **{"mp4-tape-list-%s" % _l: ("media", "MUSE", ["--screen", "games", "--np-look", "cassette", "--menu",
                                                   "Tape Style", "--press", "a", "--pick-at", str(_k)])
       for _k, _l in enumerate(("pinstripe", "band", "ferric", "metal", "clear", "red", "worn"))},
    # its Repeat row on One, the value no other shot has
    "mp4-menu-repeat-one": ("media", "MUSE", ["--screen", "games", "--menu", "Repeat", "--mode", "repeat one"]),
    # MP4's menu with its cursor on the Equalizer row, the same as MENU's
    "mp4-menu-eq": ("media", "MUSE", ["--screen", "games", "--menu", "Equalizer"]),
    # its Album Art row, under the fold since Repeat and Equalizer (opened, it
    # asks MusicBrainz, which no shot can hold still)
    "mp4-menu-art": ("media", "MUSE", ["--screen", "games", "--menu", "Album Art"]),
    "mp4-locked": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--locked"]),
    # MENU+L2's lock on any screen (src/menukeys_screen.h), its unlock held
    # half way; and MENU+L1's notice over the shelf
    "menu-locked": ("demo", "PS1", ["--menu-lock", ".5"]),
    "menu-shot-notice": ("demo", "PS1", ["--screen", "games", "--menu-shot-notice"]),
    "mp4-locked-video": ("demo", "MUSE", ["--nowplaying", "0", "0", "83", "--video", "hidden", "--locked"]),
    "tour-lang": ("demo", "PS1", ["--tour", "lang", "3500"]),
    "tour-lang-ja": ("demo", "PS1", ["--tour", "lang", "3000", "--tour-pick", "6"]),
    "tour-welcome": ("demo", "PS1", ["--tour", "welcome", "3000"]),
    "tour-move": ("demo", "PS1", ["--tour", "move", "2000"]),
    "tour-a": ("demo", "PS1", ["--tour", "a", "4000", "300"]),
    "tour-wrong": ("demo", "PS1", ["--tour", "vol", "3000", "--tour-wrong", "select", "420"]),
    "tour-menu": ("demo", "PS1", ["--tour", "menu", "2000"]),
    # Leave a Game: the in-game menu open over the game, X pressing next;
    # and X before MENU, his answer that MENU comes first
    "tour-quit": ("demo", "PS1", ["--tour", "quit", "1900"]),
    "tour-quit-wrong": ("demo", "PS1", ["--tour", "quit", "3000", "--tour-wrong", "x", "420"]),
    # ...and the player's MENU counted: its check on, the menu waiting for X
    "tour-quit-held": ("demo", "PS1", ["--tour", "quit", "2000", "--tour-held", "400"]),
    # Lock the Screen: the lock's panel on the dark stage; L2 alone (hold
    # MENU too) and MENU alone (how the chord goes)
    "tour-lock": ("demo", "PS1", ["--tour", "lock", "2600"]),
    "tour-lock-wrong": ("demo", "PS1", ["--tour", "lock", "3000", "--tour-wrong", "l2", "420"]),
    "tour-lock-wrong-menu": ("demo", "PS1", ["--tour", "lock", "3000", "--tour-wrong", "menu", "420"]),
    # Unlock the Screen: SELECT and START held, the lock's bar filling, and
    # a wrong press's answer
    "tour-unlock": ("demo", "PS1", ["--tour", "unlock", "2000", "--tour-held", "1000"]),
    "tour-unlock-wrong": ("demo", "PS1", ["--tour", "unlock", "3000", "--tour-wrong", "a", "420"]),
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
    # SSH over Wi-Fi (src/sshlink.h, sshlink_want): the Developer page over
    # Wi-Fi (keys only, no password), the message as it is turned on, with
    # no key, with Wi-Fi up and the switch off, and the cursor on Developer Mode
    "dev-page-wifi": ("demo", "PS1", ["--dev-page", "4"]),
    "dev-wifi-said": ("demo", "PS1", ["--dev-page", "5"]),
    "dev-page-wifi-nokey": ("demo", "PS1", ["--dev-page", "6"]),
    "dev-page-wifi-off": ("demo", "PS1", ["--dev-page", "7"]),
    "dev-page-mode-row": ("demo", "PS1", ["--dev-page", "8"]),
    "empty-ways": ("empty", "", ["--ways", "none,brick"]),
    # MENU > Scraper's page (DESIGN.md 11.19): the plan with videos off, on
    # for three systems and for one (the plurals' one), Systems with a long
    # name in focus too, Videos for, Settings, the run, done (and with one
    # system's videos), offline, and a system's own Box Art
    "radl-page-plan": ("demo", "PS1", ["--radl-page", "0"]),
    "radl-page-plan-checked": ("demo", "PS1", ["--radl-page", "1"]),
    "radl-page-signed-out": ("demo", "PS1", ["--radl-page", "2"]),
    "radl-page-no-wifi": ("demo", "PS1", ["--radl-page", "3"]),
    "radl-page-run": ("demo", "PS1", ["--radl-page", "4"]),
    "radl-page-run-no-wifi": ("demo", "PS1", ["--radl-page", "5"]),
    "radl-page-run-paused": ("demo", "PS1", ["--radl-page", "6"]),
    "radl-page-run-identifying": ("demo", "PS1", ["--radl-page", "7"]),
    "radl-page-done": ("demo", "PS1", ["--radl-page", "8"]),
    "radl-page-done-sign-in": ("demo", "PS1", ["--radl-page", "9"]),
    "radl-page-stopped": ("demo", "PS1", ["--radl-page", "10"]),
    "radl-page-stopped-no-answer": ("demo", "PS1", ["--radl-page", "11"]),
    "radl-page-run-retrying": ("demo", "PS1", ["--radl-page", "12"]),
    "radl-page-plan-one": ("demo", "PS1", ["--radl-page", "13"]),
    "radl-page-done-one": ("demo", "PS1", ["--radl-page", "14"]),
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
    # MENU > Downloads (DESIGN.md 11.53), every screen and state of the
    # downloads lab's picked design: the hub (its plan, a run going on, every
    # kind done, no network and nobody signed in, counting, on a kind, a kind
    # out), Settings, the kinds' pages, Systems, a system's games and a
    # game's panel, the run (going, waiting for Wi-Fi, paused), done and
    # stopped, On the Card, RetroAchievements' account, a console's page,
    # Videos for and Cheats' page
    "downloads": ("demo", "PS1", ["--downloads", "0"]),
    "downloads-running": ("demo", "PS1", ["--downloads", "1"]),
    "downloads-done": ("demo", "PS1", ["--downloads", "2"]),
    "downloads-offline": ("demo", "PS1", ["--downloads", "3"]),
    "downloads-settings": ("demo", "PS1", ["--downloads", "4"]),
    "downloads-counting": ("demo", "PS1", ["--downloads", "5"]),
    "downloads-on-covers": ("demo", "PS1", ["--downloads", "6"]),
    "downloads-covers-out": ("demo", "PS1", ["--downloads", "7"]),
    "downloads-covers": ("demo", "PS1", ["--downloads", "8"]),
    "downloads-videos": ("demo", "PS1", ["--downloads", "9"]),
    "downloads-names": ("demo", "PS1", ["--downloads", "10"]),
    "downloads-systems": ("demo", "PS1", ["--downloads", "11"]),
    "downloads-games": ("demo", "PS1", ["--downloads", "12"]),
    "downloads-game": ("demo", "PS1", ["--downloads", "13"]),
    "downloads-run": ("demo", "PS1", ["--downloads", "14"]),
    "downloads-run-no-wifi": ("demo", "PS1", ["--downloads", "15"]),
    "downloads-run-paused": ("demo", "PS1", ["--downloads", "16"]),
    "downloads-all-done": ("demo", "PS1", ["--downloads", "17"]),
    "downloads-stopped": ("demo", "PS1", ["--downloads", "18"]),
    "downloads-on-the-card": ("demo", "PS1", ["--downloads", "19"]),
    "downloads-ra-account": ("demo", "PS1", ["--downloads", "20"]),
    "downloads-console": ("demo", "PS1", ["--downloads", "21"]),
    "downloads-videos-for": ("demo", "PS1", ["--downloads", "22"]),
    "downloads-cheats": ("demo", "PS1", ["--downloads", "23"]),
    "downloads-ra-signed-out": ("demo", "PS1", ["--downloads", "24"]),
    "downloads-on-names": ("demo", "PS1", ["--downloads", "25"]),
    "downloads-covers-complete": ("demo", "PS1", ["--downloads", "26"]),
    "downloads-no-accounts": ("demo", "PS1", ["--downloads", "27"]),
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
    # Nothing found with ScreenScraper not linked; offline with no video
    "media-nothing-off": ("demo", "PS1", ["--screen", "games", "--media", "nothing-off", "--media-art", "{card}/media-art"]),
    "media-offline-novideo": ("demo", "PS1", ["--screen", "games", "--media", "offline-novideo", "--media-art", "{card}/media-art"]),
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
    EXTRA.setdefault("about-3-scroll-" + _scroll, ("demo", "PS1", ["--about", "3", "--about-scroll", _scroll]))
# The Scraper page's run saying what it is doing past a game's name (the
# catalog, a game with no art, the checksums), and done with the reason it
# passed a system over or stopped a source (art_status's lines), and three
# systems passed over (art.problems)
for _k, _w in enumerate(("catalog", "no-art", "checksums")):
    EXTRA.setdefault("scraper-page-run-" + _w, ("demo", "PS1", ["--scraper-page", "17", str(_k)]))
for _k, _w in enumerate(("no-folder", "not-in-table", "no-curl", "index-failed", "index-refused", "index-bad",
                         "ss-quota", "ss-too-many", "ss-busy", "ss-no-answer", "sg-refused", "sg-busy",
                         "sg-no-answer", "many")):
    EXTRA.setdefault("scraper-page-done-" + _w, ("demo", "PS1", ["--scraper-page", "16", str(_k)]))
# Every month: Date & Time's page on the 3rd of each (its name in full), and
# MENU's Date & Time row on the 15th (its short name, --status's date=), in
# 12 hours in July (AM) and December (PM)
_MONTHS = ("jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec")
for _k, _m in enumerate(_MONTHS):
    if _m != "oct":
        EXTRA.setdefault("datetime-" + _m, ("demo", "PS1", ["--datetime", "3", str(_k + 1)]))
    # its own --status after the shots' one, which it takes the place of
    _st = "%s %s wifi=off batt=82 date=2026-%02d-15" % ("18:05" if _m == "dec" else "10:08",
                                                       "12h" if _m in ("jul", "dec") else "24h", _k + 1)
    EXTRA.setdefault("menu-clock-" + _m, ("demo", "PS1", ["--menu", "Language", "--status", _st]))
# The save slots: the slot in view saved in each other month (August is the
# slots shot's), in 12 hours in January (AM) and February (PM); an empty
# slot; and the strip as Save asks where to
for _k, _m in enumerate(_MONTHS):
    if _m != "aug":
        _f = ["--slots", "1", "--slot-when", str(_k + 1), "9" if _m == "jan" else "15"]
        if _m in ("jan", "feb"):
            _f += ["--status", "10:08 12h wifi=off batt=82"]
        EXTRA.setdefault("slots-" + _m, ("demo", "PS1", _f))
EXTRA.setdefault("slots-empty", ("demo", "PS1", ["--slots", "5"]))
EXTRA.setdefault("slots-save", ("demo", "PS1", ["--slots", "1", "--slot-save"]))
EXTRA.setdefault("hare-cable", ("demo", "PS1", ["--hare", "10.42.0.1", "1 signed in", "Tobu Tobu Girl.gb"]))
# File Transfer's news on Flash Drive's page (DESIGN.md 11.23): the tiles in
# each state, over Wi-Fi and over the cable (SSH beside the browsers)
for _move in ("idle", "in", "out", "both", "closing", "finish", "gone", "paused"):
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
# Every question with two answers (src/confirmq.h, DESIGN.md 11.3), each
# over the shelf with the inventory's own words (src/shots.c's
# shot_ask_text): Delete Game and Delete Screenshot with "Castlevania -
# Symphony of the Night", the longest name the confirm panels lab found hard
CONFIRM_QUESTIONS = (
    "wifi-forget", "wifi-forget-joined", "pico8-restart-display", "pico8-restart-buttons",
    "pico8-reset", "game-reset", "splore-wifi", "cheats-crash", "cheats-first", "hotkeys-swap",
    "hotkeys-reset-game", "hotkeys-reset-system", "hotkeys-reset-every", "storage-unused-art",
    "storage-old-states", "storage-ds-copies", "storage-system-snaps", "storage-media-folder", "storage-media-file",
    "storage-orphan-port", "game-delete", "port-delete", "update-rollback", "collection-delete",
    "screenshot-delete", "media-video-delete", "sgdb-unlink", "ss-unlink", "snaps-delete-all",
    "reset-buttons-game", "reset-buttons-all", "usb-offer", "dl-sign-out", "dl-delete-videos",
    "dl-delete-cheats", "dl-delete-sets", "dl-sys-videos", "dl-sys-sets", "dl-sys-cheats", "bt-forget")
for _q in CONFIRM_QUESTIONS:
    EXTRA.setdefault("ask-" + _q, ("demo", "PS1", ["--ask", _q]))
# System Update (DESIGN.md 11.17, 2026-11-11): What's New scrolled to its
# end, the corner's mark of a download behind the shelf, and Zyper's news
EXTRA.setdefault("update-notes-end", ("demo", "PS1", ["--update", "--update-scroll", "99"]))
EXTRA.setdefault("update-mark", ("demo", "PS1", ["--status", "10:08 24h wifi=3 batt=82 update=42"]))
for _k in ("available", "ready"):
    EXTRA.setdefault("update-news-" + _k, ("demo", "PS1", ["--upnews", _k]))
# The last start's restart (DESIGN.md 11.70): Zyper's bubble with the
# longest code of a start that did not finish and of a freeze while playing,
# and About ZyperOS's Last Restart row, from the card's copy said yesterday
EXTRA.setdefault("lastboot-start", ("demo", "PS1", ["--lastboot", "stage=L1;reason=W;kernel_time=118.4"]))
EXTRA.setdefault("lastboot-froze", ("demo", "PS1", ["--lastboot", "stage=H;reason=P;kernel_time=18342.5"]))
EXTRA.setdefault("about-0-restart", ("demo", "PS1", ["--about", "0"]))
EXTRA.setdefault("hotspot-waiting", ("demo", "PS1", ["--hotspot", "ZyperOS-7A2B", "q7mx-3kpa", "0 joined", "waiting"]))
EXTRA.setdefault("gamemenu-dual", ("demo", "PS1", ["--gamemenu", "0", "dual"]))
# ...and its Display tab, where the screens' three rows are, on the Stylus row
EXTRA.setdefault("gamemenu-dual-display", ("demo", "PS1", ["--gamemenu", "0", "dual", "--gm-tab", "1"]))
EXTRA.setdefault("gamemenu-dual-stylus", ("demo", "PS1", ["--gamemenu", "4", "dual", "--gm-tab", "1"]))
# Rename's refusal of a name, the longest of its and Delete Game's messages
# (src/shots.c's GM_SAID): four lines in English, German, French and
# Portuguese, which three cut ("* ?...")
EXTRA.setdefault("gm-said-7", ("demo", "PS1", ["--screen", "games", "--gm-said", "7"]))
# MENU's Shader, ZyperOS's own screens' (src/screenfx.h, SHOT_SETTINGS
# below): CRT Curved on the menu itself, a system's own shader beside it
# changing nothing
EXTRA.setdefault("menu-shader", ("demo", "PS1", ["--menu", "Shader"]))
# The screens' shader on the home, a shelf and a menu, one old screen each
# (the proof sheet has every one on each)
EXTRA.setdefault("screens-crt-curved-home", ("demo", "PS1", ["--home", "spotlight"]))
EXTRA.setdefault("screens-scanlines-shelf", ("demo", "PS1", ["--screen", "games"]))
EXTRA.setdefault("screens-lcd-menu", ("demo", "PS1", ["--menu", "Search Games"]))

# The README's shots' own settings, and these shots'
SHOT_SETTINGS = {**R.SHOT_SETTINGS,
                 "menu-shader": {"launcher_shader": "crt-curved", "shader.PS1": "shimmerless"},
                 "screens-crt-curved-home": {"launcher_shader": "crt-curved"},
                 "screens-scanlines-shelf": {"launcher_shader": "scanlines"},
                 "screens-lcd-menu": {"launcher_shader": "lcd", "launcher_strength": "50"},
                 "menu-saver-off": {"saver": "0"},
                 "mp4-grid": {"view.MUSE": "grid"},
                 "mp4-list": {"view.MUSE": "list"}}

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
# A Famicom Disk System game with no disksys.rom (config/systems.cfg's
# bios|NES|disksys.rom|fds), first on the NES shelf: the shelf says it under
# the name, in **warning**, in each view
for _name, _view in (("shelf-fds-needs-bios", None), ("shelf-fds-needs-bios-list", "list"),
                     ("shelf-fds-needs-bios-grid", "grid")):
    EXTRA.setdefault(_name, ("demo", "NES", ["--screen", "games"]))
    SHOT_FILES[_name] = ["Roms/NES/Bio Miracle Bokutte Upa (Japan).fds"]
    if _view:
        SHOT_SETTINGS[_name] = {"view.NES": _view}


def border_png(path):
    """A picture of the player's own for Border's Your Own (src/border.h):
    ours, diagonal bands in two blues round a plain middle, 320x240"""
    rows = []
    for y in range(240):
        row = []
        for x in range(320):
            if 40 <= x < 280 and 24 <= y < 216:
                row.append((28, 36, 64))
            elif ((x + y) // 12) % 2:
                row.append((60, 98, 168))
            else:
                row.append((44, 72, 132))
        rows.append(row)
    R.png_rgb(path, 320, 240, rows)


# The folders a shot's files made on a card, by the card, taken away with
# them: a disc set's folder left empty would be a folder the next shot's scan
# reads (and counts as left out)
MADE_DIRS = {}


def make_parent(card, path):
    """path's folder on the card, made with each folder above it it lacks,
    which are kept to go after the shot (drop_parents)"""
    missing = []
    d = path.parent
    while d != card and not d.exists():
        missing.append(d)
        d = d.parent
    path.parent.mkdir(parents=True, exist_ok=True)
    MADE_DIRS.setdefault(card, []).extend(missing)


def drop_parents(card):
    """The folders make_parent made on the card, the deepest first, each once
    it is empty; one still holding a file (SHOT_TEXT's, taken after
    SHOT_FILES') is kept for the next call"""
    kept = []
    for d in sorted(set(MADE_DIRS.pop(card, [])), key=lambda p: len(p.parts), reverse=True):
        try:
            d.rmdir()
        except FileNotFoundError:
            pass
        except OSError:
            kept.append(d)
    if kept:
        MADE_DIRS[card] = kept


def set_files(card, files, on):
    """A shot's files (SHOT_FILES) on the card, empty, or off it; a .png
    under Borders/ a real picture (border_png)"""
    for f in files or []:
        path = card / f
        if on:
            make_parent(card, path)
            if f.startswith("Borders/") and f.endswith(".png"):
                border_png(path)
                continue
            path.touch()
        elif path.exists():
            path.unlink()
    if not on:
        drop_parents(card)


# The in-game menu's Shader at each old screen, so each name is drawn and fit
for _s in ("shimmerless", "crt", "crt-curved", "scanlines", "lcd"):
    EXTRA.setdefault("gamemenu-shader-" + _s, ("demo", "PS1", ["--screen", "games", "--gamemenu", "1", "--gm-tab", "1"]))
    SHOT_SETTINGS["gamemenu-shader-" + _s] = {"shader.PS1": _s}

# The Shader list, each shader under its cursor beside its picture and its
# note (src/shaderdemo.h); the Strength list beside its picture; MENU's
# Shader list, the screens' own, its choice on the whole screen and its note
# under it (the Pixel 2 offers Off and the four old screens; the Brick has no
# such row, its list's last row standing in)
for _k, _s in enumerate(["off", "shimmerless", "crt", "crt-curved", "scanlines", "lcd"]):
    EXTRA.setdefault("gamemenu-shader-list-" + _s,
                     ("demo", "PS1", ["--gamemenu", "1", "--gm-tab", "1", "--gm-choose", "--pick-at", str(_k)]))
EXTRA.setdefault("gamemenu-strength-list", ("demo", "PS1", ["--gamemenu", "2", "--gm-tab", "1", "--gm-choose",
                                                           "--pick-at", "4"]))
SHOT_SETTINGS["gamemenu-strength-list"] = {"shader.PS1": "crt", "shader_strength.PS1": "50"}
EXTRA.setdefault("menu-shader-list", ("demo", "PS1", ["--menu", "Shader", "--press", "a"]))
SHOT_SETTINGS["menu-shader-list"] = {"launcher_shader": "scanlines"}
SHOT_SETTINGS["system-menu-favfirst"] = {"favfirst.PS1": "on"}

# LCD No Backlight (src/app.h SHADERS' only), the Game Boy's three's alone:
# in the in-game menu of a Game Boy Advance game, its row and its list under
# the cursor; the GBA shelf's own Shader list with it; and where it is not
# offered, with a card edited to name it: the SNES shelf's list (Off, the
# SNES's default, and no such row) and MENU's, the screens' own (Off)
# The Advance's Display tab has Border under Display (its frames are offered
# in every mode), so Shader is its third row
EXTRA.setdefault("gamemenu-shader-lcd-unlit", ("demo", "GBA", ["--screen", "games", "--gamemenu", "2",
                                                               "--gm-tab", "1"]))
SHOT_SETTINGS["gamemenu-shader-lcd-unlit"] = {"shader.GBA": "lcd-unlit"}
EXTRA.setdefault("gamemenu-shader-list-lcd-unlit",
                 ("demo", "GBA", ["--gamemenu", "2", "--gm-tab", "1", "--gm-choose", "--pick-at", "6"]))
EXTRA.setdefault("system-menu-shader-list-gba", ("demo", "GBA", ["--screen", "games", "--menu", "Shader",
                                                                 "--press", "a"]))
SHOT_SETTINGS["system-menu-shader-list-gba"] = {"shader.GBA": "lcd-unlit"}
EXTRA.setdefault("system-menu-shader-list-snes", ("demo", "SNES", ["--screen", "games", "--menu", "Shader",
                                                                   "--press", "a"]))
SHOT_SETTINGS["system-menu-shader-list-snes"] = {"shader.SNES": "lcd-unlit"}
EXTRA.setdefault("menu-shader-list-unlit", ("demo", "PS1", ["--menu", "Shader", "--press", "a"]))
SHOT_SETTINGS["menu-shader-list-unlit"] = {"launcher_shader": "lcd-unlit"}

# The Strength row under each Shader row while the shader is an old screen
# (src/strength.h): in the game on it, in the system menu, and MENU's, the
# screens' own, a system's own beside it changing nothing
EXTRA.setdefault("gamemenu-strength", ("demo", "PS1", ["--screen", "games", "--gamemenu", "2", "--gm-tab", "1"]))
SHOT_SETTINGS["gamemenu-strength"] = {"shader.PS1": "lcd", "shader_strength.PS1": "50"}
EXTRA.setdefault("system-menu-strength", ("demo", "PS1", ["--screen", "games", "--menu", "Strength"]))
SHOT_SETTINGS["system-menu-strength"] = {"shader.PS1": "scanlines", "shader_strength.PS1": "20"}
EXTRA.setdefault("menu-strength", ("demo", "PS1", ["--menu", "Strength"]))
SHOT_SETTINGS["menu-strength"] = {"launcher_shader": "crt", "launcher_strength": "60", "shader_strength.PS1": "60"}
SHOT_SETTINGS["screen-colors-vivid"] = {"screen_contrast": "3", "screen_saturation": "3", "screen_gamma": "-2"}
SHOT_SETTINGS["screen-colors-custom"] = {"screen_temp": "4", "screen_tint": "-1", "screen_contrast": "3",
                                         "screen_saturation": "5", "screen_gamma": "-2"}
SHOT_SETTINGS["screen-colors-look-custom"] = dict(SHOT_SETTINGS["screen-colors-custom"])

# Timing (src/pacing.h) on the GBA's system menu, at its other value, Exact:
# Smooth, the default, is drawn in every GBA system menu shot
EXTRA.setdefault("system-menu-timing-exact", ("demo", "GBA", ["--screen", "games", "--menu", "Timing"]))
SHOT_SETTINGS["system-menu-timing-exact"] = {"pace.GBA": "exact"}

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
        ("dc-place", "DC", ["--gamemenu", "2"]), ("dc-mixed", "DC", ["--gamemenu", "2"]),
        ("stick", "PS1", ["--gamemenu", "9"]),
        ("pick-share", "PS1", ["--gamemenu", "5", "--gm-pick", "--pick-at", "1"]),
        ("shared", "PS1", ["--gamemenu", "5"])):
    EXTRA.setdefault("gamemenu-controls-" + _name,
                     ("demo", _tag, ["--screen", "games", "--gm-tab", "2"] + _flags))
# A Nintendo 64 game's Pak row, back in the Controls tab
EXTRA.setdefault("gamemenu-pak", ("demo", "N64", ["--screen", "games", "--gm-tab", "2", "--gamemenu", "2"]))
# Rumble, an N64 game's whose core asked for the motor: the row under the
# Pak, and the list A opens there; and MENU > Sound's row and its list
EXTRA.setdefault("gamemenu-rumble", ("demo", "N64", ["--screen", "games", "--gm-tab", "2",
                                                     "--gamemenu", "3", "--gm-rumble"]))
EXTRA.setdefault("gamemenu-rumble-list", ("demo", "N64", ["--screen", "games", "--gm-tab", "2",
                                                          "--gamemenu", "3", "--gm-rumble", "--gm-pick"]))
# The game's own level, Low: the row in accent-ink, the list's tick on Low and
# Default on MENU's Full
EXTRA.setdefault("gamemenu-rumble-own-list", ("demo", "N64", ["--screen", "games", "--gm-tab", "2",
                                                              "--gamemenu", "3", "--gm-rumble", "--gm-pick"]))
EXTRA.setdefault("menu-rumble", ("demo", "PS1", ["--menu", "Rumble"]))
EXTRA.setdefault("menu-rumble-list", ("demo", "PS1", ["--menu", "Rumble", "--press", "a"]))
# MENU's level at Low: the list opens on it, with the share's note
EXTRA.setdefault("menu-rumble-list-low", ("demo", "PS1", ["--menu", "Rumble", "--press", "a"]))
SHOT_SETTINGS["menu-rumble-list-low"] = {"rumble": "low"}
# And at Off: the note that says so
EXTRA.setdefault("menu-rumble-list-off", ("demo", "PS1", ["--menu", "Rumble", "--press", "a"]))
SHOT_SETTINGS["menu-rumble-list-off"] = {"rumble": "off"}
CTL_FOCUS = {"gamemenu-pak": "Mario Kart 64 (USA).z64",
             "gamemenu-rumble": "Mario Kart 64 (USA).z64",
             "gamemenu-rumble-list": "Mario Kart 64 (USA).z64",
             "gamemenu-rumble-own-list": "Mario Kart 64 (USA).z64",
             "gamemenu-controls-n64-end": "Mario Kart 64 (USA).z64",
             **{"gamemenu-controls-" + _n: _SONIC for _n in ("dc", "dc-end", "dc-place", "dc-mixed")}}
# A game's details with its own buttons (Buttons, This Game), the cursor on
# the row (below the first screenful since File, DESIGN.md 11.69), and the
# page that row opens
EXTRA.setdefault("info-buttons", ("demo", "PS1", ["--screen", "games", "--info-on-buttons"]))
EXTRA.setdefault("info-buttons-page", ("demo", "PS1", ["--screen", "games", "--info-buttons"]))
SHOT_LIB = {"gamemenu-rumble-own-list": {"rumble.N64.Mario Kart 64 (USA).z64": "low"},
            "info-buttons": {_MGS: "1;b:y,y:b"},
            "info-buttons-page": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-all": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-pick": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-warn": {_MGS: "1;b:none"},
            "gamemenu-controls-ask": {_MGS: "1;b:y,y:b"},
            "gamemenu-controls-ask-all": {"map.PS1": "1;b:y,y:b"},
            "gamemenu-controls-dc-end": {"stick.DC." + _SONIC: "1"},
            "gamemenu-controls-dc-place": {"map.DC." + _SONIC: "1;a:a,b:b,x:x,y:y"},
            "gamemenu-controls-dc-mixed": {"map.DC." + _SONIC: "1;a:a"},
            # L2 Stick While Held and R2 Stick On/Off, a button's two stick
            # choices (DESIGN.md 11.45), the cursor on R2
            "gamemenu-controls-stick": {_MGS: "1;l2:stickhold,r2:stick"},
            # Y on cross, which B gives, in Y's list: the foot's A Swap and Y
            # Both, and what each does; and Y given cross as well, the two
            # rows each with the other's glyph (issue #305)
            "gamemenu-controls-shared": {_MGS: "1;y:b"}}


# MENU > Display > Home Systems (src/homesys.h, DESIGN.md 11.48): its row with
# two systems off ("8 of 10"), its page at the top, and on the one-system card
# with both its notes; and Recents, the games played last, on the home and as
# a shelf, from play sessions put in the card's library for the shot alone.
# Each game's line says when it was last played (1.1.0-rc.8: "the recents
# page should show when was it last played"), so the sessions are this
# morning, last night at 21:40 and three days ago, on the card's clock (its
# zone as it ships), as the Screenshots shelf's shots are of days near now


def _recent_times():
    import time as _time
    from datetime import datetime, timedelta
    try:
        from zoneinfo import ZoneInfo
        zone = ZoneInfo("America/New_York")
    except Exception:          # no zone data: the C library's own
        zone = None
    now = datetime.now(zone)
    at = lambda d, h, m: datetime(d.year, d.month, d.day, h, m, tzinfo=zone).timestamp()
    today = min(at(now, 9, 12), _time.time() - 60)
    return (today, at(now - timedelta(days=1), 21, 40), at(now - timedelta(days=3), 18, 5))


_HS_OFF = {"home.hidden": "GB,DC"}
_RECENT_LIB = {"sess.%d.%d.%s\t%s" % (_w, 900 + _i, _t, _f): "600\tquit"
               for _i, (_w, (_t, _f)) in enumerate(zip(_recent_times(),
                                                       (("GBA", "The Legend of Zelda - The Minish Cap (USA).gba"),
                                                        ("SFC", "Super Metroid (USA).sfc"),
                                                        ("PS1", "Metal Gear Solid (USA).chd"))))}
EXTRA.setdefault("menu-homesys", ("demo", "PS1", ["--menu-tab", "1", "--menu", "Home Systems"]))
SHOT_SETTINGS["menu-homesys"] = _HS_OFF
EXTRA.setdefault("homesys-page", ("demo", "PS1", ["--homesys-page", "4"]))
SHOT_SETTINGS["homesys-page"] = _HS_OFF
EXTRA.setdefault("homesys-last", ("openbor", "OPENBOR", ["--homesys-page", "3"]))
# MENU > Side Lights (DESIGN.md 11.27), where the handheld has the lights (the
# Pixel 2; nothing over the shelf on the Brick): its page, the cursor on
# Battery Level, Off as on a new card
EXTRA.setdefault("lights-page", ("demo", "PS1", ["--lights-page", "2"]))
for _name, _flags in (("home-recents", []), ("shelf-recents", ["--screen", "games"]),
                      # when each was last played, under its name, in every View
                      ("shelf-recents-grid", ["--screen", "games", "--view", "grid"]),
                      ("shelf-recents-list", ["--screen", "games", "--view", "list"]),
                      ("shelf-recents-video", ["--screen", "games", "--view", "video"]),
                      ("shelf-recents-yesterday", ["--screen", "games"]),
                      ("shelf-recents-days", ["--screen", "games", "--view", "list"])):
    EXTRA.setdefault(_name, ("demo", "RECENT", _flags))
    SHOT_LIB[_name] = _RECENT_LIB
    # The list's panel is narrow: a system's name as long as "Game Boy
    # Advance" takes the line to itself, the time under it (gl_fact_origin)
    # and Played one line, which Spanish's words pass; a SNES game's line
    # leaves Played the two lines Spanish needs
    CTL_FOCUS[_name] = {"shelf-recents-yesterday": "Super Metroid (USA).sfc",
                        "shelf-recents-list": "Super Metroid (USA).sfc",
                        "shelf-recents-days": "Metal Gear Solid (USA).chd"}.get(
        _name, "The Legend of Zelda - The Minish Cap (USA).gba")


# Collections (src/collect.h, DESIGN.md 11.56): three lists on the card, one
# naming a game in a folder of games and one with a note line, as the files
# a player keeps; the home on its group, its shelf's top, one opened, a
# game's details on Add to Collection and its list, the shelf's menu at its
# top and in one, and Delete Collection's question
SHOT_TEXT = {}
SHOT_TEXT["about-0-restart"] = {
    "userdata/lastboot.txt": "reason=W\nstage=L1\nkernel_time=118.4\ntime=2026-11-23 22:14:05\n"
                             "version=1.1.0\ndevice=px2\nlast=-\nseen=%d\n" % (int(__import__("time").time()) - 86400)}
_COLL = {"Collections/Couch Co-op.txt": "/Roms/SNES/Super Mario World (USA).sfc\n"
                                        "/Roms/Game Boy Advance/Mario Kart - Super Circuit (USA).gba\n"
                                        "/Roms/PlayStation/Tekken 3 (USA).chd\n",
         "Collections/RPGs to Finish.txt": "/Roms/SNES/Chrono Trigger (USA).sfc\n"
                                           "/Roms/Game Boy Advance/Commercial Owned/Golden Sun (USA).gba\n"
                                           "# after Golden Sun\n"
                                           "/Roms/Game Boy/Pokemon - Red Version (USA, Europe).gb\n",
         "Collections/For the Kids.txt": "/Roms/NES/Kirby's Adventure (USA).nes\n"}
for _name, _flags in (("home-collections", []),
                      ("shelf-collections", ["--screen", "games"]),
                      ("shelf-collection-in", ["--screen", "games", "--folder", "RPGs to Finish"]),
                      ("shelf-collection-list", ["--screen", "games", "--folder", "RPGs to Finish",
                                                 "--view", "list"]),
                      ("collect-menu-top", ["--screen", "games", "--menu", "Delete Collection"]),
                      ("collect-menu-in", ["--screen", "games", "--folder", "RPGs to Finish",
                                           "--menu", "Remove from Collection"]),
                      ("collect-menu-sort", ["--screen", "games", "--folder", "RPGs to Finish",
                                             "--menu", "Sort By", "--press", "a"]),
                      ("collect-delete", ["--screen", "games", "--collect-delete"])):
    EXTRA.setdefault(_name, ("demo", "COLL", _flags))
    SHOT_TEXT[_name] = _COLL
# Home Systems with a collection on the card: Collections' row after Recents
EXTRA.setdefault("homesys-page-coll", ("demo", "PS1", ["--homesys-page", "2"]))
SHOT_TEXT["homesys-page-coll"] = _COLL
SHOT_SETTINGS["homesys-page-coll"] = {"home.hidden": "COLL"}
for _name, _flags in (("collect-said-long", ["--screen", "games", "--collect-said", "0"]),
                      ("collect-said-taken", ["--screen", "games", "--collect-said", "1"]),
                      ("collect-said-failed", ["--screen", "games", "--collect-said", "2"]),
                      ("info-collect", ["--screen", "games", "--info-collect"]),
                      ("collect-page", ["--screen", "games", "--collect-page", "1"]),
                      ("collect-page-none", ["--screen", "games", "--collect-page", "0"])):
    EXTRA.setdefault(_name, ("demo", "SFC", _flags))
    SHOT_TEXT[_name] = {} if _name == "collect-page-none" else _COLL
    CTL_FOCUS[_name] = "Chrono Trigger (USA).sfc"


def set_text(card, files, on):
    """A shot's files with their text or bytes (SHOT_TEXT) on the card, or off it"""
    for f, text in (files or {}).items():
        path = card / f
        if on:
            make_parent(card, path)
            if isinstance(text, bytes):
                path.write_bytes(text)
            else:
                path.write_text(text)
        elif path.exists():
            path.unlink()
    if not on:
        drop_parents(card)


def scene_rgb(x, y, w, h, sky, ground, hue):
    """A made-up frame of play's pixel at x, y (y from the top): a sky of
    `sky` fading to white, hills, `ground` at the foot and a block of `hue`
    in the middle"""
    if y > h * 3 // 4:
        return ground
    if y > h * 3 // 4 - 2 - ((x // 8) % 5 == 0) * 6 and y > h // 2 + abs((x % 64) - 32) // 2:
        return tuple(max(0, v - 40) for v in ground)
    if w * 2 // 5 <= x < w * 3 // 5 and h * 2 // 5 <= y < h * 3 // 5:
        return hue
    k = y / h
    return tuple(int(v + (255 - v) * k * 0.6) for v in sky)


def scene_bmp(w, h, sky, ground, hue):
    """The frame of play, a 24-bit BMP of w x h drawn here (no Pillow), as a
    game's resume picture stands in a shot"""
    import struct
    row = (w * 3 + 3) & ~3
    out = bytearray()
    for y in range(h - 1, -1, -1):       # a BMP's rows from the bottom
        line = bytearray()
        for x in range(w):
            c = scene_rgb(x, y, w, h, sky, ground, hue)
            line += bytes((c[2], c[1], c[0]))
        out += line + b"\0" * (row - w * 3)
    head = struct.pack("<2sIHHI", b"BM", 54 + len(out), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(out), 2835, 2835, 0, 0)
    return head + info + bytes(out)


def scene_png(w, h, sky, ground, hue):
    """The same frame as an 8-bit RGB PNG (no Pillow), as the Screenshot
    hotkey writes a game's frame (diatom/src/main.c's png_encode)"""
    import struct
    import zlib
    raw = bytearray()
    for y in range(h):
        raw.append(0)                    # every row's filter none
        for x in range(w):
            raw += bytes(scene_rgb(x, y, w, h, sky, ground, hue))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


# Recents' Card Picture (DESIGN.md 11.48): the three games with a resume
# point, each its state and the picture Diatom writes beside it, made up
# here; Last Screen on the cover row and the grid, the row in its menu, and
# its list with the picture of each choice
_RECENT_SCREENS = {}
for (_folder, _file, _wh, _sky, _ground, _hue) in (
        ("Game Boy Advance", "The Legend of Zelda - The Minish Cap (USA).gba", (240, 160),
         (64, 140, 220), (72, 150, 60), (230, 200, 60)),
        ("SNES", "Super Metroid (USA).sfc", (256, 224), (40, 30, 90), (110, 70, 50), (220, 80, 60)),
        ("PlayStation", "Metal Gear Solid (USA).chd", (320, 240), (60, 80, 90), (70, 80, 70), (200, 200, 210))):
    _RECENT_SCREENS["shared/.zyperos/%s/%s.auto.state" % (_folder, _file)] = "state"
    _RECENT_SCREENS["shared/.zyperos/%s/%s.auto.bmp" % (_folder, _file)] = scene_bmp(*_wh, _sky, _ground, _hue)
for _name, _flags in (("shelf-recents-screen", ["--screen", "games"]),
                      ("shelf-recents-screen-grid", ["--screen", "games", "--view", "grid"]),
                      ("recents-menu-picture", ["--screen", "games", "--menu", "Card Picture"]),
                      ("recents-picture-list", ["--screen", "games", "--menu", "Card Picture", "--press", "a",
                                                "--expect-en", "Cover|Last Screen"]),
                      ("recents-picture-list-cover", ["--screen", "games", "--menu", "Card Picture", "--press", "a",
                                                      "--pick-at", "0", "--expect-en", "Cover|Last Screen"])):
    EXTRA.setdefault(_name, ("demo", "RECENT", _flags))
    SHOT_LIB[_name] = _RECENT_LIB
    SHOT_TEXT[_name] = _RECENT_SCREENS
    # The grid's band has the name and Resume on one line: a name short
    # enough for German's word there too
    CTL_FOCUS[_name] = "Super Metroid (USA).sfc" if _name.endswith("-grid") else \
        "The Legend of Zelda - The Minish Cap (USA).gba"
    if _name != "recents-picture-list-cover":
        SHOT_SETTINGS[_name] = {"recents.picture": "screen"}


# The Video view (DESIGN.md 11.57): a game with no snap, its card whole
# above the band; its snap playing (the test picture), the band up; at rest,
# the band and the name's card away, and halfway there; MENU's View on Video
# and its list
for _name, _flags in (("shelf-video", ["--screen", "games", "--view", "video"]),
                      ("shelf-video-snap", ["--screen", "games", "--view", "video", "--snap", "1"]),
                      ("shelf-video-rest", ["--screen", "games", "--view", "video", "--snap", "1", "--band", "1"]),
                      ("shelf-video-half", ["--screen", "games", "--view", "video", "--snap", "1", "--band", ".5"]),
                      ("system-menu-view-video", ["--screen", "games", "--view", "video", "--menu", "View"]),
                      ("system-menu-view-video-list", ["--screen", "games", "--view", "video", "--menu", "View",
                                                       "--press", "a"])):
    EXTRA.setdefault(_name, ("demo", "PS1", _flags))


# The List view (DESIGN.md 11.39): the snap at the height of the rows (the
# test picture) with the details over its foot, and halfway down at rest
for _name, _flags in (("shelf-list-snap", ["--screen", "games", "--view", "list", "--snap", "1"]),
                      ("shelf-list-rest", ["--screen", "games", "--view", "list", "--snap", "1", "--band", ".5"])):
    EXTRA.setdefault(_name, ("demo", "PS1", _flags))


# Screenshots (src/shotname.h, DESIGN.md 11.68): the Screenshot hotkey's
# pictures on the card, each a PNG made up here as Diatom names them, at its
# game's own size, and one of a game no longer on the card. The home on the
# group, its shelf in Covers, Grid and List, a shot opened with A (the Video
# view) and at rest, its band and card away, Delete Screenshot's question and its
# message, MENU's Sort By list, and Home Systems with the group's row Off
_SHOTS = {}
for (_folder, _game, _when, _wh, _sky, _ground, _hue) in (
        ("SNES", "Super Metroid (USA)", "2026-11-20 09.12.40", (256, 224), (40, 30, 90), (110, 70, 50), (220, 80, 60)),
        ("Game Boy Advance", "The Legend of Zelda - The Minish Cap (USA)", "2026-11-19 22.05.11", (240, 160),
         (64, 140, 220), (72, 150, 60), (230, 200, 60)),
        ("PlayStation", "Metal Gear Solid (USA)", "2026-11-19 12.44.09", (320, 240),
         (60, 80, 90), (70, 80, 70), (200, 200, 210)),
        ("SNES", "Super Metroid (USA)", "2026-11-18 21.40.05", (256, 224), (90, 40, 30), (60, 50, 110), (80, 220, 90)),
        ("SNES", "A Game Since Deleted (USA)", "2026-11-16 17.20.48", (256, 224), (30, 90, 60), (90, 90, 40), (60, 120, 220))):
    _SHOTS["Screenshots/%s/%s %s.png" % (_folder, _game, _when)] = scene_png(*_wh, _sky, _ground, _hue)
for _name, _flags in (("home-screenshots", []),
                      ("shelf-screenshots", ["--screen", "games"]),
                      ("shelf-screenshots-grid", ["--screen", "games", "--view", "grid"]),
                      ("shelf-screenshots-list", ["--screen", "games", "--view", "list"]),
                      ("screenshots-viewer", ["--screen", "games", "--shelf-press", "a"]),
                      ("screenshots-viewer-rest", ["--screen", "games", "--shelf-press", "a", "--band", "1"]),
                      ("screenshots-delete", ["--screen", "games", "--shots-delete"]),
                      ("screenshots-said", ["--screen", "games", "--shots-said"]),
                      ("screenshots-menu", ["--screen", "games", "--menu", "Sort By"]),
                      ("screenshots-menu-sort", ["--screen", "games", "--menu", "Sort By", "--press", "a"])):
    EXTRA.setdefault(_name, ("demo", "SHOT", _flags))
    SHOT_TEXT[_name] = _SHOTS
    CTL_FOCUS[_name] = "Super Metroid (USA).sfc"       # the newest shot, first
# OpenBOR's own (my pick: under OpenBOR, by their file names and times), on
# the card with an OpenBOR module: one of its game and one of its menu
_OBOR_PNG = scene_png(320, 240, (200, 90, 40), (60, 50, 70), (240, 220, 80))
EXTRA.setdefault("shelf-screenshots-openbor", ("openbor", "SHOT", ["--screen", "games"]))
SHOT_TEXT["shelf-screenshots-openbor"] = {"Saves/openbor/ScreenShots/Beats of Rage - 0000.png": _OBOR_PNG,
                                          "Saves/openbor/ScreenShots/Menu - 0000.png": _OBOR_PNG}
# and one shot alone, its count said in the singular
EXTRA.setdefault("home-screenshots-one", ("demo", "SHOT", []))
SHOT_TEXT["home-screenshots-one"] = {"Screenshots/SNES/Super Metroid (USA) 2026-11-20 09.12.40.png":
                                     _SHOTS["Screenshots/SNES/Super Metroid (USA) 2026-11-20 09.12.40.png"]}
EXTRA.setdefault("homesys-page-shots", ("demo", "PS1", ["--homesys-page", "2"]))
SHOT_TEXT["homesys-page-shots"] = _SHOTS
SHOT_SETTINGS["homesys-page-shots"] = {"home.hidden": "SHOT"}


# A zip whose game Diatom cannot unpack (DESIGN.md 11.63): its message
EXTRA.setdefault("launch-zip-method", ("demo", "SFC", ["--screen", "games", "--zip-said"]))
# Two cores on the Game Boy shelves (DESIGN.md 11.62): the GB shelf's menu
# on Core, and a game's details with its Core fact
EXTRA.setdefault("system-menu-core-gb", ("demo", "GB", ["--screen", "games", "--menu", "Core"]))
EXTRA.setdefault("info-core-gb", ("demo", "GB", ["--screen", "games", "--info"]))
# A game's file on its details (DESIGN.md 11.69), the brackets its title
# leaves out: a long tagged name, sliding in every language (as it is drawn
# at --phase 3200, and at rest at its start); a disc set in a folder of its
# own, its folder and first disc and how many discs; and an .m3u beside its
# discs. The files are made for the shot and taken away after it.
_PSX = "Roms/PlayStation/"
_LONG = "Final Fantasy VII (USA) (Rev 1) (Disc 1).chd"
_SET = "Final Fantasy IX (USA)"
_M3U = ["Final Fantasy VIII (USA) (Disc %d).chd" % k for k in range(1, 5)]
for _name, _flags in (("info-file-long", []), ("info-file-long-start", ["--phase", "0"])):
    EXTRA.setdefault(_name, ("demo", "PS1", ["--screen", "games", "--info", *_flags]))
    SHOT_FILES[_name] = [_PSX + _LONG]
    CTL_FOCUS[_name] = _LONG
EXTRA.setdefault("info-file-discs", ("demo", "PS1", ["--screen", "games", "--info"]))
SHOT_FILES["info-file-discs"] = [_PSX + "%s/%s (Disc %d).chd" % (_SET, _SET, k) for k in range(1, 5)]
CTL_FOCUS["info-file-discs"] = "%s/%s (Disc 1).chd" % (_SET, _SET)
EXTRA.setdefault("info-file-m3u", ("demo", "PS1", ["--screen", "games", "--info"]))
SHOT_FILES["info-file-m3u"] = [_PSX + _d for _d in _M3U]
SHOT_TEXT["info-file-m3u"] = {_PSX + "Final Fantasy VIII (USA).m3u": "".join(_d + "\n" for _d in _M3U)}
CTL_FOCUS["info-file-m3u"] = "Final Fantasy VIII (USA).m3u"


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
# Game Names' two (DESIGN.md 11.71), each with its note and its example name
# under the list, in every language: Short and File Name
for _k in range(2):
    EXTRA.setdefault("menu-game-names-%d" % _k, ("demo", "PS1", ["--menu-tab", "1", "--menu", "5", "--press", "a",
                                                                "--pick-at", str(_k)]))
# Display's Screensaver at off: "Off", a status begun with a capital as every
# row's value is (menu.off, and scraper.off and ssh.off, its words)
EXTRA.setdefault("menu-saver-off", ("demo", "PS1", ["--menu-tab", "1", "--menu", "Screensaver"]))
# An option row's list (DESIGN.md 11.3), A on the row: Auto Off's every
# value (its seconds and minutes); Display's, with what the mode under the
# cursor does under it, from a system's menu and from the in-game menu
EXTRA.setdefault("menu-autooff-list", ("demo", "PS1", ["--menu", "Auto Off", "--press", "a"]))
# MENU's values off their defaults, each on its row: the clock in 12 hours
# and the battery's icon as a percentage and as time left (the shot's own
# --status, SHOT_STATUS), Auto Off off, and the systems in release order
SHOT_STATUS = {}
EXTRA.setdefault("menu-hours-12h", ("demo", "PS1", ["--menu", "Language"]))
# On a day of two digits, as the Date & Time row's widest: with no date= it
# was the day the check ran, so the row fitted on the 19th and was tight on
# the 20th (German on the Brick, 2026-11-20)
SHOT_STATUS["menu-hours-12h"] = "10:08 12h wifi=off batt=82 date=2026-11-28"
for _icon in ("percentage", "time"):
    EXTRA.setdefault("menu-battery-" + _icon, ("demo", "PS1", ["--menu-tab", "1", "--menu", "Battery Icon"]))
    SHOT_STATUS["menu-battery-" + _icon] = "10:08 24h wifi=off batt=82 eta=95 icon=" + _icon
EXTRA.setdefault("menu-autooff-never", ("demo", "PS1", ["--menu", "Auto Off"]))
SHOT_SETTINGS["menu-autooff-never"] = {"autooff": "0"}
EXTRA.setdefault("menu-order-release", ("demo", "PS1", ["--menu-tab", "1", "--menu", "System Order"]))
SHOT_SETTINGS["menu-order-release"] = {"systems.order": "release"}
EXTRA.setdefault("system-menu-display-list", ("demo", "PS1", ["--screen", "games", "--menu", "Display", "--press", "a"]))
# View's three (Covers, Grid, List) over a shelf in the grid
EXTRA.setdefault("system-menu-view-list", ("demo", "NES", ["--screen", "games", "--view", "grid", "--menu", "View", "--press", "a"]))
# Home Screen's three (Spotlight, Gallery, Grid) over the Grid home
EXTRA.setdefault("menu-home-list", ("demo", "PS1", ["--home", "grid", "--menu-tab", "1", "--menu", "Home Screen", "--press", "a"]))
EXTRA.setdefault("gamemenu-display-list", ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--gm-choose"]))
# Cheats (src/cheats_screen.h, DESIGN.md 11.62): the Game tab's row with two
# on, the game's list (the player's file, libretro's two under their seams,
# Turn All Off), its end, the first cheat's question, the Achievements tab
# while paused, and the two lists with none: libretro has none, not checked
EXTRA.setdefault("cheats-row", ("demo", "PS1", ["--gamemenu", "4", "--cheats"]))
EXTRA.setdefault("cheats-page", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "2"]))
EXTRA.setdefault("cheats-page-end", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "99"]))
EXTRA.setdefault("cheats-ask", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "4", "--cheats-ask"]))
EXTRA.setdefault("cheats-achtab", ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "3", "--achtab", "paused"]))
EXTRA.setdefault("cheats-none-libretro", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "0"]))
EXTRA.setdefault("cheats-not-checked", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "0"]))
EXTRA.setdefault("cheats-note", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "1", "--cheats-note"]))
EXTRA.setdefault("cheats-note-end", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "99", "--cheats-note"]))
EXTRA.setdefault("cheats-on-first", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "0", "--cheats-on-first"]))
EXTRA.setdefault("cheats-search", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "0", "--cheats-query", "infinite"]))
EXTRA.setdefault("cheats-search-none", ("demo", "PS1", ["--gamemenu", "0", "--cheats-page", "0", "--cheats-query", "zzz"]))
EXTRA.setdefault("cheats-crash", ("demo", "PS1", ["--cheats-crash"]))
# The shelf's line under the game's name (the lab's): "2 cheats on" on the
# covers and in the list, and after a crash with them on
EXTRA.setdefault("cheats-shelf", ("demo", "PS1", ["--screen", "games"]))
EXTRA.setdefault("cheats-shelf-crashed", ("demo", "PS1", ["--screen", "games", "--cheats-crashed"]))
EXTRA.setdefault("cheats-shelf-list", ("demo", "PS1", ["--screen", "games", "--view", "list", "--cheats-crashed"]))
for _n in ("cheats-row", "cheats-page", "cheats-page-end", "cheats-ask", "cheats-note", "cheats-note-end",
           "cheats-on-first", "cheats-search", "cheats-search-none", "cheats-shelf", "cheats-shelf-crashed",
           "cheats-shelf-list"):
    R.SHOT_FOCUS.setdefault(_n, R.CHEATS_GAME + ".chd")
R.SHOT_FOCUS.setdefault("cheats-none-libretro", "Crash Bandicoot (USA).chd")
R.SHOT_FOCUS.setdefault("cheats-not-checked", "Spyro the Dragon (USA).chd")
# A name long enough to reach the screen's middle under the focused cover,
# where Highlight's bar is (src/games.c): the bar clear of it, and the snap's
# wait bar in the bar's place while it fills
EXTRA.setdefault("games-long-title", ("demo", "PS1", ["--screen", "games"]))
EXTRA.setdefault("games-long-title-wait", ("demo", "PS1", ["--screen", "games", "--snap-wait", "0.5"]))
for _n in ("games-long-title", "games-long-title-wait"):
    R.SHOT_FOCUS.setdefault(_n, "Tony Hawk's Pro Skater 2 (USA).chd")
# On-Screen Stats' page (src/hudmenu_screen.h): its rows with three items
# shown ("3 Items"), the Shows list (every item's name), and each row's list
# of more than two (every corner, distance, opacity and color); Background
# and Size, of two, are the page's own values and the Pill/Small shots
EXTRA.setdefault("hud-page", ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--hud-page", "1",
                                               "--hud-shows", "fps,speed,temp"]))
EXTRA.setdefault("hud-page-two", ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--hud-page", "6"]))
EXTRA.setdefault("hud-page-alt", ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--hud-page", "6", "--hud-alt"]))
EXTRA.setdefault("hud-items", ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--hud-page", "0", "--hud-items"]))
EXTRA.setdefault("hud-items-end", ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--hud-page", "6", "--hud-items"]))
for _r, _n in ((2, "corner"), (3, "edge"), (5, "opacity"), (6, "color")):
    EXTRA.setdefault("hud-pick-" + _n, ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--hud-page", str(_r),
                                                       "--hud-pick", str(_r)]))
# Each mode under the list's cursor, its picture and its note beside the
# list (src/ds/preview.h): every note in its room in every language
for _k, _m in enumerate(["stretch", "aspect", "square", "integer", "integer-vertical", "overscale", "fill",
                        "native"]):
    EXTRA.setdefault("gamemenu-display-list-" + _m,
                     ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--gm-choose", "--pick-at", str(_k)]))
# Square Pixels over an NES game's 256x224, which fceumm asks to show wider
# (1.3061, diatom/docs/reference/core-facts.md): the picture 8:7, as tall as
# the screen, room at its sides (#346)
EXTRA.setdefault("gamemenu-display-list-square-nes",
                 ("demo", "PS1", ["--gamemenu", "0", "--gm-tab", "1", "--gm-choose", "--pick-at", "2",
                                  "--gm-source", "256x224@1.3061"]))
# Display's Theme page (DESIGN.md 11.42) in each of the ten accents, the
# tones going round in both themes and the corners turning over, so every
# accent's, tone's and value's name is drawn; its foot, Zyper and Reset to
# Default, both ways
_TONES = (("warm", "black"), ("neutral", "deepblue"), ("cool", "darkgrey"))
for _i, _acc in enumerate(("lime", "green", "teal", "blue", "purple", "pink", "red", "orange",
                           "yellow", "silver")):
    _lt, _dk = _TONES[_i % 3]
    EXTRA.setdefault("theme-" + _acc, ("demo", "PS1", [
        "--look", "%s,%s,%s,%s,on" % (_acc, _lt, _dk, "square" if _i % 2 else "rounded"),
        "--look-page", "theme", "0"]))
# Theme's three lists with the cursor off the choice kept, the whole screen
# showing the one under it and the tick on the kept one (src/ds/hover.h): a
# ground of the theme that is not on shows that theme while its list is open
EXTRA.setdefault("theme-accent-list-teal", ("demo", "PS1", ["--look-page", "theme-list", "0", "--pick-at", "2"]))
EXTRA.setdefault("theme-light-list-cool", ("demo", "PS1", ["--look-page", "theme-list", "1", "--pick-at", "2"]))
EXTRA.setdefault("theme-dark-list-deepblue", ("demo", "PS1", ["--look-page", "theme-list", "2", "--pick-at", "1"]))
# Folder Color (1.0.6 rc.3): its row in focus, System Colors and an accent,
# and its list with the picture of the shelf's folders beside it, on System
# Colors and on an accent under the cursor
EXTRA.setdefault("theme-folders-system", ("demo", "PS1", ["--look-page", "theme", "3"]))
EXTRA.setdefault("theme-folders-orange", ("demo", "PS1", ["--look", "lime,warm,black,rounded,on,orange",
                                                          "--look-page", "theme", "3"]))
EXTRA.setdefault("theme-folders-list-system", ("demo", "PS1", ["--look-page", "theme-list", "3", "--pick-at", "0"]))
EXTRA.setdefault("theme-folders-list-teal", ("demo", "PS1", ["--look-page", "theme-list", "3", "--pick-at", "3"]))
# Border (src/border.h): the Game Boy's system menu at Sharp Pixels, a
# Game Boy's 160x144 kept for it, at Blur; its list with each kind under the
# cursor (by its English words, --pick-en: the Brick has no Blur, so the
# places differ), a picture of the player's own on the card for Your Own;
# and the in-game Display tab's row and list over a paused Game Boy game.
# The handheld frames (src/handframe.h) on each of the three Game Boys,
# Handheld and Handheld, Whole under the cursor (the Advance has no Whole on
# the Pixel 2: its cursor then stays on the kept one), and the Game Boy
# Color's Handheld Color list, each of its six under the cursor
_BORDER_SET = {"display.GB": "integer", "display_source.GB": "160x144", "display_shape.GB": "1.11111"}
_BORDER_PIC = ["Borders/Game Boy/Waves.png"]
# Border's lists say only its own kinds (--expect-en), in their order and each
# once: Blur where the device draws it, the rest on both
_BORDER_EXPECT = "Off|Blur?|Handheld|Handheld, Whole|Your Own"
_BORDER_WORDS = (("off", "Off"), ("blur", "Blur"), ("handheld", "Handheld"), ("whole", "Handheld, Whole"),
                 ("own", "Your Own"))
EXTRA.setdefault("system-menu-border", ("demo", "GB", ["--screen", "games", "--menu", "Border"]))
SHOT_SETTINGS["system-menu-border"] = dict(_BORDER_SET, **{"border.GB": "blur"})
SHOT_FILES["system-menu-border"] = _BORDER_PIC
for _p, _w in _BORDER_WORDS:
    EXTRA.setdefault("system-menu-border-list-" + _p, ("demo", "GB", ["--screen", "games", "--menu", "Border",
                                                                     "--press", "a", "--pick-en", _w,
                                                                     "--expect-en", _BORDER_EXPECT]))
    SHOT_SETTINGS["system-menu-border-list-" + _p] = dict(_BORDER_SET)
    SHOT_FILES["system-menu-border-list-" + _p] = _BORDER_PIC
    EXTRA.setdefault("gamemenu-border-list-" + _p, ("demo", "GB", ["--gamemenu", "1", "--gm-tab", "1", "--gm-choose",
                                                                  "--pick-en", _w, "--gm-source",
                                                                  "160x144@1.11111", "--expect-en", _BORDER_EXPECT]))
    SHOT_SETTINGS["gamemenu-border-list-" + _p] = dict(_BORDER_SET)
    SHOT_FILES["gamemenu-border-list-" + _p] = _BORDER_PIC
EXTRA.setdefault("gamemenu-border", ("demo", "GB", ["--gamemenu", "1", "--gm-tab", "1", "--gm-source",
                                                    "160x144@1.11111"]))
SHOT_SETTINGS["gamemenu-border"] = dict(_BORDER_SET, **{"border.GB": "own"})
SHOT_FILES["gamemenu-border"] = _BORDER_PIC
_FRAME_GBC = ["Roms/Game Boy Color/Tobu Tobu Girl Deluxe.gbc"]
for _tag, _src, _shape, _files in (("GBC", "160x144", "1.11111", _FRAME_GBC), ("GBA", "240x160", "1.5", None)):
    _set = {"display." + _tag: "integer", "display_source." + _tag: _src, "display_shape." + _tag: _shape}
    _t = _tag.lower()
    EXTRA.setdefault("system-menu-border-" + _t, ("demo", _tag, ["--screen", "games", "--menu", "Border"]))
    SHOT_SETTINGS["system-menu-border-" + _t] = dict(_set, **{"border." + _tag: "handheld"})
    SHOT_FILES["system-menu-border-" + _t] = _files
    for _p, _w in (("handheld", "Handheld"), ("whole", "Handheld, Whole")):
        _n = "system-menu-border-%s-list-%s" % (_t, _p)
        EXTRA.setdefault(_n, ("demo", _tag, ["--screen", "games", "--menu", "Border", "--press", "a",
                                             "--pick-en", _w]))
        SHOT_SETTINGS[_n] = dict(_set)
        SHOT_FILES[_n] = _files
        _n = "gamemenu-border-%s-list-%s" % (_t, _p)
        EXTRA.setdefault(_n, ("demo", _tag, ["--gamemenu", "1", "--gm-tab", "1", "--gm-choose", "--pick-en", _w,
                                             "--gm-source", _src + "@" + _shape]))
        SHOT_SETTINGS[_n] = dict(_set)
        SHOT_FILES[_n] = _files
# A Game Boy at the mode a system starts in, Full Screen, with no size kept
# (a system never played): Border shows all the same and offers the frames
# (not Blur or Your Own, which would change nothing there), its list, the
# list in a game's Display tab, and Right on the row: Handheld chosen and
# Display turned to Sharp Pixels. Before 1.1.0 the row hid in that mode, so
# on a card left at Full Screen no frame could be found; the first of these
# fails then (--menu: no row called Border).
EXTRA.setdefault("system-menu-border-fullscreen", ("demo", "GB", ["--screen", "games", "--menu", "Border"]))
EXTRA.setdefault("system-menu-border-fullscreen-list", ("demo", "GB", ["--screen", "games", "--menu", "Border",
                                                                     "--press", "a", "--pick-en", "Handheld"]))
EXTRA.setdefault("system-menu-border-fullscreen-chosen", ("demo", "GB", ["--screen", "games", "--menu", "Border",
                                                                       "--press", "right"]))
EXTRA.setdefault("gamemenu-border-fullscreen-list", ("demo", "GB", ["--gamemenu", "1", "--gm-tab", "1", "--gm-choose",
                                                                  "--pick-en", "Handheld", "--gm-source",
                                                                  "160x144@1.11111"]))
_GBC_FRAME = {"display.GBC": "integer", "display_source.GBC": "160x144", "display_shape.GBC": "1.11111",
              "border.GBC": "handheld"}
EXTRA.setdefault("system-menu-border-color", ("demo", "GBC", ["--screen", "games", "--menu", "Handheld Color"]))
SHOT_SETTINGS["system-menu-border-color"] = dict(_GBC_FRAME, **{"border_color.GBC": "4"})
SHOT_FILES["system-menu-border-color"] = _FRAME_GBC
for _c in ("Yellow", "Red", "Purple", "Green", "Teal", "Clear Purple"):
    _n = "system-menu-border-color-list-" + _c.lower().replace(" ", "-")
    EXTRA.setdefault(_n, ("demo", "GBC", ["--screen", "games", "--menu", "Handheld Color", "--press", "a",
                                          "--pick-en", _c]))
    SHOT_SETTINGS[_n] = dict(_GBC_FRAME)
    SHOT_FILES[_n] = _FRAME_GBC
EXTRA.setdefault("gamemenu-border-color-list", ("demo", "GBC", ["--gamemenu", "2", "--gm-tab", "1", "--gm-choose",
                                                                "--pick-en",
                                                                "Clear Purple", "--gm-source", "160x144@1.11111"]))
SHOT_SETTINGS["gamemenu-border-color-list"] = dict(_GBC_FRAME, **{"border.GBC": "whole"})
SHOT_FILES["gamemenu-border-color-list"] = _FRAME_GBC
# Super Game Boy (src/sgbmode.h) on the Game Boy's system menu, at Its
# Games, and its list with each choice under the cursor: the picture of each
# at the shelf's display mode and what it does under it
EXTRA.setdefault("system-menu-sgb", ("demo", "GB", ["--screen", "games", "--menu", "Super Game Boy"]))
SHOT_SETTINGS["system-menu-sgb"] = {"sgb.GB": "games"}
for _k, _p in enumerate(("off", "games")):
    EXTRA.setdefault("system-menu-sgb-list-" + _p, ("demo", "GB", ["--screen", "games", "--menu", "Super Game Boy",
                                                                  "--press", "a", "--pick-at", str(_k)]))
# Highlight (the focus lab, 2026-11-19): its row in focus on each choice,
# the pictures over it showing the dock's mark as the choice has it, and its
# list with the home's three systems beside it marked as the choice under
# the cursor marks them, each choice's note under the picture
for _k, _f in enumerate(("bar", "border", "lift", "none")):
    EXTRA.setdefault("theme-highlight-" + _f, ("demo", "PS1", ["--look", "lime,warm,black,rounded,on,system," + _f,
                                                              "--look-page", "theme", "5"]))
    EXTRA.setdefault("theme-highlight-list-" + _f, ("demo", "PS1", ["--look-page", "theme-list", "5",
                                                                   "--pick-at", str(_k)]))
# Timing's list (src/pacing.h): two choices, opened on A all the same for
# the picture beside it (the two ways, slowed) and what each does under it
for _k, _p in enumerate(("smooth", "exact")):
    EXTRA.setdefault("system-menu-timing-list-" + _p, ("demo", "GBA", ["--screen", "games", "--menu", "Timing",
                                                                      "--press", "a", "--pick-at", str(_k)]))
EXTRA.setdefault("theme-foot-on", ("demo", "PS1", ["--look-page", "theme", "6"]))
# MENU > Display > Screen Colors (src/panelgamma.h): its row, and its page
# Standard as it ships, Vivid, and Custom with every row moved (the values
# from the database, as the page reads them); and the Look's list with each
# look under the cursor, its note in the picture column, and Custom's
EXTRA.setdefault("menu-screen", ("demo", "PS1", ["--menu", "Screen Colors"]))
# MENU > Sound > Equalizer (src/eq.h): the row; the page Off, as a new card
# has it, and with a curve of the player's own, the cursor on 60 Hz; the
# presets' list, each with its picture, the cursor on Bass
EXTRA.setdefault("menu-eq", ("demo", "PS1", ["--menu", "Equalizer"]))
EXTRA.setdefault("eq-page", ("demo", "PS1", ["--look-page", "eq", "0"]))
EXTRA.setdefault("eq-page-custom", ("demo", "PS1", ["--look-page", "eq", "1"]))
SHOT_SETTINGS["eq-page-custom"] = {"eq.speaker": "custom", "eq.speaker.bands": "4,1,-2,3,5"}
EXTRA.setdefault("eq-presets", ("demo", "PS1", ["--look-page", "eq-presets", "0", "--pick-at", "1"]))
# And the cursor on each other preset, for its note and its picture
for _k, _p in enumerate(["off", "bass", "voice", "treble", "custom"]):
    if _p != "bass":
        EXTRA.setdefault("eq-presets-" + _p, ("demo", "PS1", ["--look-page", "eq-presets", "0", "--pick-at", str(_k)]))
SHOT_SETTINGS["eq-presets-custom"] = {"eq.speaker.bands": "4,1,-2,3,5"}
# With headphones in: their own preset, Bass, and its curve on the bands
EXTRA.setdefault("eq-page-phones", ("demo", "PS1", ["--look-page", "eq", "0", "--eq-phones"]))
SHOT_SETTINGS["eq-page-phones"] = {"eq.phones": "bass"}
EXTRA.setdefault("screen-colors", ("demo", "PS1", ["--look-page", "screen", "0"]))
EXTRA.setdefault("screen-colors-vivid", ("demo", "PS1", ["--look-page", "screen", "0"]))
EXTRA.setdefault("screen-colors-custom", ("demo", "PS1", ["--look-page", "screen", "1"]))
for _k, _l in enumerate(["standard", "vivid", "intense", "warm", "cool", "muted"]):
    EXTRA.setdefault("screen-colors-look-" + _l,
                     ("demo", "PS1", ["--look-page", "screen-looks", "0", "--pick-at", str(_k)]))
EXTRA.setdefault("screen-colors-look-custom",
                 ("demo", "PS1", ["--look-page", "screen-looks", "0", "--pick-at", "6"]))
# Screen Colors in a game (1.1.0): the in-game menu's Display tab, its cursor
# on the row (right over On-Screen Stats), at Custom, its longest value; and
# A on it, the same page over the paused game, in Custom with its numbers,
# and its Look's list there
EXTRA.setdefault("gamemenu-screen-colors", ("demo", "PS1", ["--gamemenu", "2", "--gm-tab", "1"]))
SHOT_SETTINGS["gamemenu-screen-colors"] = dict(SHOT_SETTINGS["screen-colors-custom"])
EXTRA.setdefault("gamemenu-screen-colors-page", ("demo", "PS1", ["--gamemenu", "0", "--look-page", "screen", "1"]))
SHOT_SETTINGS["gamemenu-screen-colors-page"] = dict(SHOT_SETTINGS["screen-colors-custom"])
EXTRA.setdefault("gamemenu-screen-colors-looks",
                 ("demo", "PS1", ["--gamemenu", "0", "--look-page", "screen-looks", "0", "--pick-at", "1"]))
EXTRA.setdefault("theme-foot-off", ("demo", "PS1", ["--look", "lime,warm,black,square,off",
                                                    "--look-page", "theme", "6"]))
# MENU > Display > Night Mode (DESIGN.md 11.42), only where the display has a
# table (the Pixel 2 and the Brick): its one row, By Hours; and its page, A on the row:
# By Hours with every row live, Off and On with the rows they do not use
# quiet, the hours on the 12-hour clock at their longest (10:30 PM, 11:30
# AM), and the lists of Night Mode, Night Warmth and Night From
EXTRA.setdefault("menu-night", ("demo", "PS1", ["--menu", "Night Mode"]))
SHOT_SETTINGS["menu-night"] = {"night.mode": "hours"}
EXTRA.setdefault("night-page", ("demo", "PS1", ["--look-page", "night", "0"]))
SHOT_SETTINGS["night-page"] = {"night.mode": "hours", "night.from": "1260", "night.until": "420"}
EXTRA.setdefault("night-page-off", ("demo", "PS1", ["--look-page", "night", "0"]))
EXTRA.setdefault("night-page-on", ("demo", "PS1", ["--look-page", "night", "1"]))
SHOT_SETTINGS["night-page-on"] = {"night.mode": "on", "night.warmth": "10"}
EXTRA.setdefault("night-page-12h", ("demo", "PS1", ["--look-page", "night", "2"]))
SHOT_SETTINGS["night-page-12h"] = {"night.mode": "hours", "night.from": "1350", "night.until": "690"}
SHOT_STATUS["night-page-12h"] = "10:08 12h wifi=off batt=82"
EXTRA.setdefault("night-mode-list", ("demo", "PS1", ["--look-page", "night-list", "0"]))
SHOT_SETTINGS["night-mode-list"] = {"night.mode": "hours"}
EXTRA.setdefault("night-warmth-list", ("demo", "PS1", ["--look-page", "night-list", "1"]))
SHOT_SETTINGS["night-warmth-list"] = {"night.mode": "on"}
EXTRA.setdefault("night-from-list", ("demo", "PS1", ["--look-page", "night-list", "2"]))
SHOT_SETTINGS["night-from-list"] = {"night.mode": "hours", "night.from": "1260"}
# MENU > Display > Appearance's Auto (1.1.0, on the Night Hours): the row on
# Auto, and its list, A on the row, with each choice's note under it, and the
# list opened on Auto; and Night Mode's page Off with Appearance on Auto, its
# hours live, on the 12-hour clock at their longest (10:30 PM, 11:30 AM). A card on Auto is
# drawn in the shot's --theme at any hour (main.c), and is a 1.1.0 card
# (appearance.auto_106), so the start's migration leaves its hours as they are
_AUTO = {"appearance": "auto", "appearance.auto_106": "1"}
EXTRA.setdefault("menu-look-auto", ("demo", "PS1", ["--menu", "Appearance"]))
SHOT_SETTINGS["menu-look-auto"] = dict(_AUTO)
for _k, _n in ((0, "light"), (1, "dark"), (2, "auto")):
    EXTRA.setdefault("appearance-list-" + _n, ("demo", "PS1", ["--menu", "Appearance", "--press", "a",
                                                               "--pick-at", str(_k)]))
EXTRA.setdefault("appearance-list-on-auto", ("demo", "PS1", ["--menu", "Appearance", "--press", "a"]))
SHOT_SETTINGS["appearance-list-on-auto"] = dict(_AUTO)
EXTRA.setdefault("night-page-off-auto", ("demo", "PS1", ["--look-page", "night", "0"]))
SHOT_SETTINGS["night-page-off-auto"] = dict(_AUTO, **{"night.from": "1350", "night.until": "690"})
SHOT_STATUS["night-page-off-auto"] = "10:08 12h wifi=off batt=82"

# The pop-ups the SELECT hotkeys put over a game (src/main.c, on_quick and
# on_speed), each as it says: the slot saved, loaded, empty or failed, a game
# whose core keeps no states, and the speed at a half step and a whole one;
# the ones over a game started fresh: a DS game in a new Game Language, and
# a game whose resume the shelf's other core left; and NES Player 2's, a
# friend's browser joining and leaving
for _kind in ("saved", "loaded", "save_failed", "load_failed", "slot_empty", "no_states", "dslang",
              "corestate", "stick_on", "stick_off", "player2_joined", "player2_left",
              "player2_stopped", "shot", "shader", "display", "cheats", "cheats_one"):
    EXTRA.setdefault("popup-" + _kind, ("demo", "PS1", ["--popup", _kind]))
# A Famicom Disk System game's disk as L1 and R1 move it (src/diskpopup.h)
for _kind in ("disk_side", "disk_in", "disk_out", "disk_first"):
    EXTRA.setdefault("popup-" + _kind, ("demo", "NES", ["--popup", _kind]))
EXTRA.setdefault("popup-speed-half", ("demo", "PS1", ["--popup", "speed", "3"]))
EXTRA.setdefault("popup-speed-whole", ("demo", "PS1", ["--popup", "speed", "4"]))
# Max (0), and 1x, as Fast Forward turns it off
EXTRA.setdefault("popup-speed-max", ("demo", "PS1", ["--popup", "speed", "0"]))
EXTRA.setdefault("popup-speed-one", ("demo", "PS1", ["--popup", "speed", "2"]))
# The Hotkeys list (src/hotkeys_screen.h): MENU > System's, at its top and its
# end (Home, Reset Hotkeys, the footer for MENU's chords); a game's, Applies To on each
# of its three and All Controls at the end; the capture screen waiting and
# with SELECT and Y down; the swap question; Reset Hotkeys' question for each
# scope; and Reset Game's question over the game
for _name, _flags in (
        ("top", ["--hotkeys", "4"]), ("end", ["--hotkeys", "103"]),
        ("game", ["--screen", "games", "--hk-game", "--hotkeys", "100"]),
        ("game-every", ["--screen", "games", "--hk-game", "--hk-scope", "0", "--hotkeys", "100"]),
        ("game-system", ["--screen", "games", "--hk-game", "--hk-scope", "1", "--hotkeys", "100"]),
        ("game-end", ["--screen", "games", "--hk-game", "--hotkeys", "104"]),
        ("capture", ["--hotkeys", "4", "--hk-capture", "1"]),
        ("capture-keys", ["--hotkeys", "4", "--hk-capture", "2", "--hk-key", "3"]),
        ("swap", ["--hotkeys", "4", "--hk-ask", "1", "--hk-key", "7"]),
        ("reset-every", ["--hotkeys", "103", "--hk-ask", "2"]),
        ("reset-system", ["--screen", "games", "--hk-game", "--hk-scope", "1", "--hotkeys", "103", "--hk-ask", "2"]),
        ("reset-game", ["--screen", "games", "--hk-game", "--hk-scope", "2", "--hotkeys", "103", "--hk-ask", "2"]),
        ("reset-ask", ["--screen", "games", "--hk-game", "--hotkeys", "103", "--hk-ask", "3"]),
        # MENU's chords on every screen (src/menukeys.h): their rows, and
        # Screenshot's capture with MENU and L1 down
        ("menu-keys", ["--hotkeys", "109"]),
        ("menu-capture", ["--hotkeys", "107", "--hk-capture", "1"]),
        ("menu-capture-keys", ["--hotkeys", "107", "--hk-capture", "2", "--hk-key", "4"]),
        ("menu-swap", ["--hotkeys", "107", "--hk-ask", "1"])):
    EXTRA.setdefault("hotkeys-" + _name, ("demo", "PS1", _flags))
# Fast Forward and its speed under it (src/hotkeys.h): as they come, None and
# 2x; set, SELECT+Y and Max, both in the accent-ink, in MENU > System's and a
# game's; and its capture screen
EXTRA.setdefault("hotkeys-ffwd", ("demo", "PS1", ["--hotkeys", "9"]))
EXTRA.setdefault("hotkeys-ff-speed", ("demo", "PS1", ["--hotkeys", "120"]))
EXTRA.setdefault("hotkeys-ff-set", ("demo", "PS1", ["--hotkeys", "120"]))
SHOT_LIB["hotkeys-ff-set"] = {"hotkeys": "1;ffwd:y,ffspeed:max"}
EXTRA.setdefault("hotkeys-ff-game", ("demo", "PS1", ["--screen", "games", "--hk-game", "--hk-scope", "0",
                                                     "--hotkeys", "120"]))
SHOT_LIB["hotkeys-ff-game"] = {"hotkeys": "1;ffwd:y,ffspeed:max"}
EXTRA.setdefault("hotkeys-ff-capture", ("demo", "PS1", ["--hotkeys", "9", "--hk-capture", "2", "--hk-key", "3"]))
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
    # The card too full: what the update needs, beside the shot's stand-in
    # free room (src/shots.c)
    "update-space": ("state space\nversion 0.2.0\ndownload 14200000\nneed 410000000\ntime {now}\n", True),
    # ...and as a program from before need was written says it
    "update-space-old": ("state space\nversion 0.2.0\ndownload 14200000\ntime {now}\n", True),
    "update-available-offline": ("state available\nversion 0.1.1\ndownload 83000000\nkind system\ntime {now}\n", False),
    "update-ready": ("state ready\nversion 0.1.1\nkind system\ntime {now}\n", True),
}
EXTRA.update({name: ("demo", "PS1", ["--update"]) for name in UPDATE_STATES})

# About's Details with a battery (src/about.c): the full charge never measured
# (charge_full_learned 0, as the Pixel 2's kernel says until its first
# measure: "Not measured yet") and measured worn ("81% estimate"). The
# launcher reads these files for the gauge's folder (ZYPEROS_BATTERY_DIR,
# plat_battery_info_path), made for the shot.
BATTERY_DIRS = {
    "about-1-unmeasured": {"charge_full": "1730000\n", "charge_full_design": "1730000\n",
                           "charge_full_learned": "0\n"},
    "about-1-measured": {"charge_full": "1400000\n", "charge_full_design": "1730000\n",
                         "charge_full_learned": "3\n"},
}
for _name in BATTERY_DIRS:
    EXTRA.setdefault(_name, ("demo", "PS1", ["--about", "1"]))

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

# A port's loading screen (src/portload.c, DESIGN.md 11.11): the start's line,
# and the one after a minute that tools/portkeys.c shows, which says how long
# a first start can install and how to go back (check-portload holds its
# contrast and Zyper's frame, in English)
EXTRA["portload"] = ("portdocs", "PRT", ["--portload", "Star Farm"])
EXTRA["portload-later"] = ("portdocs", "PRT", ["--portload-later", "--portload", "Star Farm"])

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

# MENU > Power Button on Hold (src/powerhold.h, DESIGN.md 11.47): its row,
# the Controls pages that then say a hold (In a game, Anywhere, a port's In a
# game) and the tutorial's POWER step, the setting on for each shot alone
for _name, _shot in (("menu-power-hold", ("demo", "PS1", ["--menu-tab", "4", "--menu", "Power Button"])),
                     ("controls-2-hold", ("demo", "PS1", ["--controls", "2"])),
                     ("controls-5-hold", ("demo", "PS1", ["--controls", "5"])),
                     ("tour-power-hold", ("demo", "PS1", ["--tour", "power", "1800"])),
                     ("port-plain-0-hold", ("portdocs", "PRT", ["--screen", "games", "--gamecontrols", "0",
                                                                "PRT"]))):
    EXTRA.setdefault(_name, _shot)
    SHOT_SETTINGS[_name] = {"power.button": "hold"}
PORT_FOCUS["port-plain-0-hold"] = "Plain.sh"

# Your PICO-8 (src/pico8.h, DESIGN.md 11.55), on a card of its own with the
# payload's script and Splore's art and a cart, but not PICO-8's own files:
# Splore first on the shelf, Not installed, A How to Add; the page A opens
# there; and what it says over the shelf (src/launch_screen.c's
# p8_said_shot): a zip copied by hand taken, with its version and without,
# and not taken (another PICO-8's zip, one that could not be), Splore with
# no network joined, Delete Game on Splore, and Splore's question with no
# network
def pico8_card(card):
    R.card_dirs(card)
    (card / "ZyperOS/pico8").mkdir()
    shutil.copy(ROOT / "sd/pixel2/PICO-8.sh", card / "ZyperOS/pico8/PICO-8.sh")
    for art in ("cover-light.png", "cover-dark.png", "icon.png"):
        shutil.copy(ROOT / "res/pico8" / art, card / "ZyperOS/pico8" / art)
    # fake08, the shelf's other core, as the payload has it (a stand-in file)
    (card / "ZyperOS/cores").mkdir()
    (card / "ZyperOS/cores/fake08_libretro.so").write_bytes(b"")
    (card / "Roms/PICO-8").mkdir(parents=True)
    (card / "Roms/PICO-8/Cloud Hopper.p8").write_text("pico-8 cartridge // stand-in\n")


EXTRA["pico8-splore"] = ("pico8", "PICO8", ["--screen", "games"])
# Splore on the Video view (DESIGN.md 11.57): A How to Add and no Y, as on
# the other views
EXTRA["pico8-splore-video"] = ("pico8", "PICO8", ["--screen", "games", "--view", "video"])
EXTRA["pico8-page"] = ("pico8", "PICO8", ["--screen", "games", "--pico8-page"])
for _k in range(7):
    EXTRA["pico8-said-%d" % _k] = ("pico8", "PICO8", ["--screen", "games", "--pico8-said", str(_k)])
# The in-game menu over your PICO-8 (src/portmenu.h's port_menu_display): a
# game's tabs, Game, Display and Controls (gm_tab_count: no Achievements),
# with the rows a stopped PICO-8 answers to (1.1.0-rc.8). The Game tab, on
# Continue and on Reset, and the question Reset asks (the Reset Game
# hotkey's too), the cursor on Cancel. The Pixel 2's alone in effect: the
# Brick has no PICO-8 of its own yet
EXTRA["pico8-menu-game"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "0", "port"])
EXTRA["pico8-menu-reset"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "1", "port"])
EXTRA["pico8-reset-ask"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "1", "port", "--gm-reset"])
# The Display tab, on Sharp Pixels: its Display row; the Display list A
# opens there, the cursor on Actual Size; and the question a mode other than
# the one on asks before PICO-8 starts again in it, the cursor on Cancel,
# for each mode it can name
EXTRA["pico8-menu-display"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "0", "port", "--gm-tab", "1"])
EXTRA["pico8-display-list"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "0", "port", "--gm-tab", "1",
                                                  "--gm-choose", "--pick-at", "7"])
for _k, _m in enumerate(("stretch", "aspect", "square", "integer", "tall", "overscale", "fill", "native")):
    EXTRA["pico8-restart-" + _m] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "0", "port",
                                                       "--gm-tab", "1", "--gm-restart", "--pick-at", str(_k)])
# ...its picture's rows, drawn by its shim (src/p8fx.h), in the Display tab's
# order: the cursor on Shader at CRT 70% with Blur round it, Strength under
# it; and Border's list, the cursor on Blur
EXTRA["pico8-menu-shader"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "2", "port", "--gm-tab", "1"])
EXTRA["pico8-border-list"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "1", "port", "--gm-tab", "1",
                                                 "--gm-choose", "--pick-at", "1"])
# ...its Screen Colors row (1.1.0, the whole panel's), at Custom, its longest
# value, right over On-Screen Stats, and the page A opens over the paused
# cart; and On-Screen Stats, the tab's last, Off and On
EXTRA["pico8-menu-colors"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "4", "port", "--gm-tab", "1"])
EXTRA["pico8-colors-page"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "0", "port",
                                                 "--look-page", "screen", "1"])
EXTRA["pico8-menu-hud"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "5", "port", "--gm-tab", "1"])
EXTRA["pico8-menu-hud-on"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "5", "port", "--gm-tab", "1"])
# ...and the Controls tab, a game's, the cursor on the A cap's row, the
# handheld beside it; and the start again new buttons ask for as the menu
# closes back to the cart
EXTRA["pico8-controls"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "1", "port", "--gm-tab", "2"])
EXTRA["pico8-restart-buttons"] = ("pico8", "PICO8", ["--screen", "games", "--gamemenu", "0", "port",
                                                     "--gm-restart", "buttons"])
for _name in [n for n in EXTRA if n.startswith("pico8-") and n not in (
        "pico8-splore", "pico8-splore-video", "pico8-page") and not n.startswith("pico8-said-")]:
    SHOT_SETTINGS[_name] = {"display.PICO8": "integer"}
for _name in ("pico8-menu-shader", "pico8-border-list", "pico8-menu-colors", "pico8-colors-page",
              "pico8-menu-hud", "pico8-menu-hud-on"):
    SHOT_SETTINGS[_name] = {"display.PICO8": "integer", "shader.PICO8": "crt", "shader_strength.PICO8": "70",
                            "border.PICO8": "blur"}
for _name in ("pico8-menu-colors", "pico8-colors-page"):
    SHOT_SETTINGS[_name].update(SHOT_SETTINGS["screen-colors-custom"])
SHOT_SETTINGS["pico8-menu-hud-on"]["hud"] = "1"


# The in-game menu over OpenBOR and over a port (src/portmenu.h), a game's
# tabs since 1.1.0-rc.8 (the README's 'openbor' and 'ports-menu' are their
# Game tabs): the Display tab's Screen Colors alone, at Custom, its longest
# value, and its page over the program; and the Controls tab's All
# Controls, the read-only pages behind it (port-<script>-<page>, above)
for _name, _shot in (("openbor-menu-display", ("openbor", "OPENBOR", ["--screen", "games", "--gamemenu", "0",
                                                                      "port", "--gm-tab", "1"])),
                     ("openbor-menu-controls", ("openbor", "OPENBOR", ["--screen", "games", "--gamemenu", "0",
                                                                       "port", "--gm-tab", "2"])),
                     ("openbor-colors-page", ("openbor", "OPENBOR", ["--screen", "games", "--gamemenu", "0",
                                                                     "port", "--look-page", "screen", "1"])),
                     ("ports-menu-display", ("portdocs", "PRT", ["--screen", "games", "--gamemenu", "0", "port",
                                                                 "--gm-tab", "1"])),
                     ("ports-menu-controls", ("portdocs", "PRT", ["--screen", "games", "--gamemenu", "0", "port",
                                                                  "--gm-tab", "2"]))):
    EXTRA[_name] = _shot
for _name in ("openbor-menu-display", "openbor-colors-page", "ports-menu-display"):
    SHOT_SETTINGS[_name] = dict(SHOT_SETTINGS["screen-colors-custom"])
for _name in ("ports-menu-display", "ports-menu-controls"):
    PORT_FOCUS[_name] = "Star Farm.sh"


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
# Screens whose words came within the margin of a key or a mark when the host
# began to check it (src/ui.c's ui_fit_mark, 1.0.6), each a layout to decide
# on its own (DESIGN.md section 14). None is left: Storage's caption under its
# sort key left with the layout by rules, the video controls' keys with the
# one-view player (both 1.0.6). Any screen whose words meet a key or a mark
# fails.
KNOWN_KEYS = set()


# The scratch cards every run shoots from, by name
CARDS = ["demo", "empty", "scummvm", "openbor", "ports", "all", "portdocs", "media", "pmcard", "psp",
         "es", "systems", "pico8"]


def make_cards(top):
    """Every scratch card under top/: made once a run (about 250 MB, 4 s on
    an idle M3 and 20 s on a loaded one), then cloned for each language and
    handheld (clone_cards), since no card says a language"""
    cards = {name: top / name for name in CARDS}
    R.demo_card(cards["demo"], None, {"artwork": {}})
    R.systems_card(cards["systems"])
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
    pico8_card(cards["pico8"])
    R.card_dirs(cards["empty"])


def clone(src, dst):
    """src's tree as dst: cloned where the file system can (APFS's
    clonefile, cp -c; a reflink on Btrfs or XFS), so copies share their
    blocks and cost next to nothing, and copied where it cannot"""
    if sys.platform == "darwin":
        cmd = ["cp", "-cR", str(src), str(dst)]
    else:
        cmd = ["cp", "-a", "--reflink=auto", str(src), str(dst)]
    if subprocess.call(cmd, stderr=subprocess.DEVNULL) != 0:
        shutil.rmtree(dst, ignore_errors=True)
        shutil.copytree(src, dst, symlinks=True)


def clone_cards(template, tmp):
    """The cards of make_cards into tmp/, a run's own to write: eighteen
    runs share one copy's blocks and take a quarter of a second each"""
    for name in CARDS:
        clone(template / name, Path(tmp) / name)
    return {name: Path(tmp) / name for name in CARDS}


# What the launcher writes on a card while it is shot: its settings database
# and boot.env (userdata/) and the library it scans (shared/.zyperos/), and
# nothing else (2026-11-18, every shot of a language against a copy of the
# cards). Each shot starts from the cards' own: a shot's frame was the
# shots' before it too (a per-game remap one left put a row on Game Info and
# swapped the PlayStation's keys on Controls), so the same screen drew
# otherwise in a run of some shots than in a run of all, and the pixel gate
# compared the two.
STATE = ("userdata", "shared/.zyperos")


def reset_state(template, card):
    for sub in STATE:
        shutil.rmtree(card / sub, ignore_errors=True)
        src = template / card.name / sub
        if src.exists():
            clone(src, card / sub)


# Set when the run is stopped (Ctrl-C, SIGTERM, a closed terminal): each
# language's run ends after the shot it is taking, and its scratch cards go
STOP = threading.Event()
# Every scratch folder this run made, deleted at its end whatever happens
SCRATCH = []


def run_language(code, shots, binary, keep, theme="light", device="pixel2", first=True, template=None):
    notes, failures, data, drawn = [], [], {}, set()
    small_said = set()      # a value said once a language, on the first screen it is on
    with tempfile.TemporaryDirectory(prefix="zyperos-i18n-%s-" % code) as tmp:
        SCRATCH.append(tmp)
        if template is None:
            make_cards(Path(tmp) / "template")
            cards = clone_cards(Path(tmp) / "template", tmp)
        else:
            cards = clone_cards(template, tmp)
        out = keep if keep else Path(tmp) / "out"
        out.mkdir(parents=True, exist_ok=True)
        for name, (card_name, tag, flags) in shots.items():
            if STOP.is_set():
                break
            card = cards[card_name]
            reset_state(template if template is not None else Path(tmp) / "template", card)
            focus = PORT_FOCUS.get(name) or CTL_FOCUS.get(name) or R.SHOT_FOCUS.get(name) or \
                {"PS1": R.FOCUS + ".chd", "SCUMMVM": R.SCUMMVM_FOCUS, "OPENBOR": R.OPENBOR[0],
                 "PRT": "PortMaster.sh", "PICO8": "Splore.splore"}.get(tag, "")
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
                ["--status", SHOT_STATUS.get(name, "10:08 24h wifi=off batt=82")]
            command = [str(binary), "--shot", str(target), "--theme", theme, "--lang", code,
                       "--screen", "systems", "--phase", "3200", *status,
                       "--device", device, *[f.replace("{card}", str(card)) for f in flags]]
            # errors=replace: a log line may end mid-character (a cut name)
            env = R.pm_env(R.env_for(card), name, tmp)
            if name in PM_STATES:
                pm = Path(tmp) / ("pm-" + name)
                pm.mkdir(exist_ok=True)
                (pm / "status").write_text(PM_STATES[name][0])
                env["ZYPEROS_PM_STATUS"] = str(pm / "status")
            if name in BATTERY_DIRS:
                bat = Path(tmp) / ("battery-" + name)
                bat.mkdir(exist_ok=True)
                for _f, _text in BATTERY_DIRS[name].items():
                    (bat / _f).write_text(_text)
                env["ZYPEROS_BATTERY_DIR"] = str(bat)

            def shoot():
                # A shot's own settings (readme-shots' SHOT_SETTINGS), only while
                # it is taken: the Shader row at its widest value
                if target.exists():
                    target.unlink()
                R.set_settings(card, SHOT_SETTINGS.get(name), True)
                set_lib(card, SHOT_LIB.get(name), True)
                set_files(card, SHOT_FILES.get(name), True)
                set_text(card, SHOT_TEXT.get(name), True)
                r = subprocess.run(command, cwd=ROOT, env=env, text=True, errors="replace",
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                R.set_settings(card, SHOT_SETTINGS.get(name), False)
                set_lib(card, SHOT_LIB.get(name), False)
                set_files(card, SHOT_FILES.get(name), False)
                set_text(card, SHOT_TEXT.get(name), False)
                return r

            def failed(r):
                # A row this handheld has not (the Strength row where there are
                # no GPU shaders) is said, not failed: on the first handheld a
                # missing row is a renamed one, and fails
                ok_anyway = name in R.CORNER_OWN and R.CORNER_OWN[name] in r.stdout
                if not first and r.returncode and re.search(r"--menu: no row called", r.stdout):
                    return False
                return bool(r.returncode and not ok_anyway) or not target.is_file()

            def last_words(r):
                # Its last words but the strings it drew, which bury the reason
                # ("--menu: no row called Strength") under the rows it did draw
                return " | ".join([l for l in r.stdout.strip().splitlines()
                                   if not l.startswith("fit: ")][-4:])

            r = shoot()
            # A shot is the same frame from the same card every time, so one
            # that fails is taken once more: what fails twice is the screen's,
            # and fails; what passes the second time was the run's, and is said
            # with its first reason. On GitHub's runners, sixteen checks at once,
            # one in some thousand launches built MENU with the Shader Off
            # (pixel2:ja/system-menu-strength, pixel2:pt-BR/menu-strength,
            # 2026-11-16), which none of 1,700 here did.
            if failed(r):
                once = r
                r = shoot()
                if not failed(r):
                    print("flaky %s:%s/%s: failed once (exit %d), passed again: %s" % (
                        device, code, name, once.returncode, last_words(once)))
            for line in r.stdout.splitlines():
                if line.startswith(("i18n: cut data: ", "i18n: slides data: ", "i18n: tight data: ")):
                    data.setdefault(name, []).append(line[6:])
                elif line.startswith("fit: drawn: "):
                    drawn.add(unescape(line[12:]))
                elif line.startswith("fit: value: "):
                    value = unescape(line[12:])
                    small = strings_of(code).small_first(value)
                    if small and value not in small_said:
                        small_said.add(value)
                        failures.append("%s/%s: a row's value begins with a small letter: %r (%s)" % (
                            code, name, value, "/".join(list(dict.fromkeys(small))[:3])))
                elif line.startswith("fit: overflow: "):
                    px, box, text = line[15:].split("|", 2)
                    kind, span, *beside = box.split(" ", 2)
                    what = {"screen": "past the screen's edge",
                            "keys": "into a key or a more mark at x %s" % span,
                            "meets": "into the text beside it (x %s, \"%s\")" % (span, beside[0] if beside else ""),
                            "box": "past its box (x %s)" % span}[kind]
                    text = unescape(text)
                    notes.append("%s/%s: %s: %s px %s: %s" % (
                        code, name, strings_of(code).label(text), px, what, text.replace("\n", " / ")[:60]))
                elif line.startswith("i18n: "):
                    notes.append("%s/%s: %s" % (code, name, line[6:]))
            row = re.search(r"--menu: no row called (.*)", r.stdout)
            if not first and r.returncode and row:
                print("not on %s: %s/%s: no row %s" % (device, code, name, row.group(1).strip()))
                continue
            if failed(r):
                failures.append("%s/%s: the shot failed twice (exit %d): %s" % (
                    code, name, r.returncode, last_words(r)))
    return device, code, notes, failures, data, drawn


def shot_list(only=()):
    """{name: (card, tag, flags)}: every shot, or those named. The README's
    but those of one language (menu-pt is that language's own), and EXTRA's.
    tools/pixel-gate.py keys its kept base frames by it."""
    shots = {k: v for k, v in R.SHOTS.items() if "--lang" not in v[2]}
    shots.update(EXTRA)
    if only:
        shots = {k: v for k, v in shots.items() if k in only}
    return shots


def cache_dir():
    if os.environ.get("ZYPEROS_SHOTS_CACHE_DIR"):
        return Path(os.environ["ZYPEROS_SHOTS_CACHE_DIR"]).expanduser()
    if sys.platform == "darwin":
        return Path.home() / "Library" / "Caches" / "ZyperOS" / "i18n-shots"
    return Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "zyperos" / "i18n-shots"


# What a verdict is made of besides the launcher: the cards and their files
# (res/, the fixtures, the systems), the strings this reads the drawn text
# against (res/strings, tools/i18n.py), the list of strings no shot draws,
# and this tool and the one it takes its cards and shots from
VERDICT_INPUTS = ["res", "tools/i18n-shots.py", "tools/readme-shots.py", "tools/i18n.py",
                  "tools/i18n-unshot.txt", "tools/fixtures", "config/systems.cfg"]


def verdict_key(binary, argv):
    """A pass's key: the launcher's own bytes (rebuilt from the same source,
    the same bytes), VERDICT_INPUTS as this tree has them, committed or not
    (their git hashes, staged into a copy of this worktree's index), the
    host's SDL, and the arguments. None when git cannot say."""
    import hashlib
    h = hashlib.sha256()
    h.update(binary.read_bytes())
    paths = [p for p in VERDICT_INPUTS if (ROOT / p).exists()]
    with tempfile.TemporaryDirectory(prefix="zyperos-i18n-index-") as tmp:
        index = Path(tmp) / "index"
        real = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-path", "index"],
                              cwd=ROOT, text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.strip()
        if real and Path(real).is_file():
            shutil.copyfile(real, index)
        env = dict(os.environ, GIT_INDEX_FILE=str(index))
        if subprocess.call(["git", "add", "-A", "--", *paths], cwd=ROOT, env=env,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
            return None
        tree = subprocess.run(["git", "write-tree"], cwd=ROOT, env=env, text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout.strip()
        listing = subprocess.run(["git", "ls-tree", tree, "--", *paths], cwd=ROOT, text=True,
                                 stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout
    if not tree:
        return None
    libs = subprocess.run(["pkg-config", "--modversion", "sdl2", "SDL2_image", "SDL2_ttf"],
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL).stdout
    h.update(("\n".join([listing, libs, sys.platform, repr(argv)])).encode())
    return h.hexdigest()


class Tee:
    """What is printed, printed and kept: a pass's words are kept with it"""
    def __init__(self, out):
        self.out, self.kept = out, []

    def write(self, text):
        self.kept.append(text)
        return self.out.write(text)

    def flush(self):
        self.out.flush()


def part_arg(s):
    m = re.fullmatch(r"(\d+)/(\d+)", s)
    if not m or not 1 <= int(m.group(1)) <= int(m.group(2)):
        raise argparse.ArgumentTypeError("a part is I/N with 1 <= I <= N, not %r" % s)
    return int(m.group(1)), int(m.group(2))


# What one language's shots cost on each handheld, against the Pixel 2's:
# the Brick's panel has 2.6 times its pixels, and on CI's runners a part of
# five Brick runs took 932 s where five Pixel 2 runs took 402 (2026-11-21)
COST = {"pixel2": 1.0, "brick": 2.5}


def part_of(runs, devices, i, n):
    """The I-th of N parts of the runs (handheld, language), for CI's
    runners, as even as their cost (COST) lets them be: the languages other
    than English dealt out the dearest first, each to the part it adds the
    least to, which counts the English run of its handheld too when the
    part has none yet, as a language is read against English's on the same
    handheld. The first part has English on every handheld from the start,
    so the strings no shot draws (tools/i18n-unshot.txt) are still looked
    for, in that part. The same runs give every part the same deal."""
    cost = lambda d: COST.get(d, 1.0)
    english = {d for d, c in runs if c == "en"}
    parts = [set() for _ in range(n)]
    load = [0.0] * n
    for d in devices:
        if d in english:
            parts[0].add((d, "en"))
            load[0] += cost(d)
    order = {r: k for k, r in enumerate(runs)}
    for r in sorted((r for r in runs if r[1] != "en"), key=lambda r: (-cost(r[0]), order[r])):
        d = r[0]
        extra = lambda k: cost(d) + (cost(d) if d in english and (d, "en") not in parts[k] else 0.0)
        k = min(range(n), key=lambda k: (load[k] + extra(k), k))
        load[k] += extra(k)
        parts[k].add(r)
        if d in english:
            parts[k].add((d, "en"))
    return [r for r in runs if r in parts[i - 1]]


def main():
    """check() with the run's stops and its kept passes around it: a pass is
    kept by verdict_key (ZYPEROS_SHOTS_CACHE_DIR; ~/Library/Caches/ZyperOS/
    i18n-shots on a Mac), and the same launcher and inputs again print it
    and pass at once (make check run again after a change to a document,
    a check or the device's system). ZYPEROS_SHOTS_CACHE=0 shoots anyway.
    Kept only for a run that writes nothing (no --keep, --coverage or
    --write-unshot)."""
    import signal

    def stop(signum, frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGHUP, stop)
    argv = sys.argv[1:]
    writes = any(a.startswith(("--keep", "--coverage", "--write-unshot")) for a in argv)
    key = kept = None
    binary = ROOT / "build-native" / "zyperos"
    for i, a in enumerate(argv):
        if a == "--binary" and i + 1 < len(argv):
            binary = Path(argv[i + 1]).resolve()
        elif a.startswith("--binary="):
            binary = Path(a.split("=", 1)[1]).resolve()
    if not writes and os.environ.get("ZYPEROS_SHOTS_CACHE", "1") != "0" and binary.is_file():
        key = verdict_key(binary, argv)
        if key:
            kept = cache_dir() / (key + ".txt")
            if kept.is_file():
                sys.stdout.write(kept.read_text(encoding="utf-8"))
                print("(kept: the same launcher and inputs passed on %s; ZYPEROS_SHOTS_CACHE=0 "
                      "shoots them again)" % time.strftime("%Y-%m-%d %H:%M", time.localtime(kept.stat().st_mtime)))
                os.utime(kept)
                return 0
    tee = Tee(sys.stdout)
    sys.stdout = tee
    try:
        code = check()
    except KeyboardInterrupt:
        STOP.set()
        print("i18n-shots: stopped", file=sys.stderr)
        code = 130
    finally:
        sys.stdout = tee.out
        for d in SCRATCH:
            shutil.rmtree(d, ignore_errors=True)
    if code == 0 and kept is not None:
        kept.parent.mkdir(parents=True, exist_ok=True)
        tmp = kept.with_suffix(".%d.tmp" % os.getpid())
        tmp.write_text("".join(tee.kept), encoding="utf-8")
        os.replace(tmp, kept)
        # The 200 passes used last
        for old in sorted(kept.parent.glob("*.txt"), key=lambda p: p.stat().st_mtime)[:-200]:
            old.unlink(missing_ok=True)
    return code


def check():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--keep", type=Path, help="keep the frames in DIR/<code>/, or DIR/<device>/<code>/ "
                    "for more than one handheld")
    ap.add_argument("--lang", action="append", help="this language only (more than once for more)")
    ap.add_argument("--strict", action="store_true", help="fail on the pseudo-locale's too")
    ap.add_argument("--coverage", action="store_true", help="list the strings no shot draws")
    ap.add_argument("--write-unshot", action="store_true",
                    help="write tools/i18n-unshot.txt: every string no shot draws, as it is now")
    ap.add_argument("--theme", choices=["light", "dark"], default="light",
                    help="the theme the shots are drawn in (light; tools/pixel-gate.py takes both)")
    ap.add_argument("--binary", type=Path, help="this launcher, not build-native/zyperos (tools/pixel-gate.py's base)")
    ap.add_argument("--jobs", type=int, help="launchers at once (one a language a handheld)")
    ap.add_argument("--device", action="append",
                    help="as this handheld only (more than once for more; all of DEVICES by default)")
    ap.add_argument("--part", type=part_arg, help="I/N: the I-th of N parts of the languages and handhelds "
                    "(make check CHECK_SHARD=I/N; CI's parts)")
    ap.add_argument("--cards", type=Path, help="clone the cards from DIR, which make_cards made (tools/pixel-gate.py "
                    "makes them once a gate), and leave it")
    ap.add_argument("only", nargs="*", help="shot names (default: all)")
    args = ap.parse_args()
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    binary = args.binary.resolve() if args.binary else ROOT / "build-native" / "zyperos"
    devices = args.device or DEVICES
    if not binary.is_file():
        print("FAIL no build-native/zyperos: make native first", file=sys.stderr)
        return 1
    shots = shot_list(args.only)
    langs = args.lang or (i18n.CODES + [i18n.PSEUDO])
    keep = args.keep.resolve() if args.keep else None
    runs = [(d, c) for d in devices for c in langs]
    if args.part:
        runs = part_of(runs, devices, *args.part)

    # One launcher a language and handheld, as before 2026-11-18: fewer when
    # the computer was loaded (its free cores) left a make check's run at two
    # launchers under other sessions' load, 712.9 s where 18 at once took 166
    jobs = max(1, min(args.jobs or len(runs), len(runs)))
    results = {}
    if args.cards:
        template = args.cards.resolve()
    else:
        template = Path(tempfile.mkdtemp(prefix="zyperos-i18n-cards-"))
        SCRATCH.append(str(template))
        make_cards(template)

    def run(dc):
        d, c = dc
        out = None if not keep else keep / c if len(devices) == 1 else keep / d / c
        return run_language(c, shots, binary, out, args.theme, d, d == devices[0], template)
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(jobs, len(runs)))) as pool:
        try:
            for device, code, notes, failures, data, drawn in pool.map(run, runs):
                results[(device, code)] = (notes, failures, data, drawn)
        except KeyboardInterrupt:
            # Each run ends after its shot, and its cards go with it
            STOP.set()
            raise

    def texts(said):
        # the data each line names, whether it was cut or slides: a name
        # cut on a row and sliding in a panel's heading is one name
        return {line.split(": ", 1)[-1] for line in said}

    # Data (a game's name, a formatted line) may be cut or slide in English
    # too; a screen where a language cuts more of it than English does on the
    # same handheld has a translation that made the cut ("vinculado como
    # t..." beside ScreenScraper)
    for device, code in runs:
        english = results[(device, "en")][2] if (device, "en") in results else None
        if english is None or code == "en": continue
        for name, said in sorted(results[(device, code)][2].items()):
            if len(texts(said)) > len(texts(english.get(name, []))):
                results[(device, code)][0].extend("%s/%s: %s (English: %d)" % (
                    code, name, d, len(english.get(name, []))) for d in said)
    # Every string a shot draws on any handheld, or says why it is none of
    # the shots'
    english_runs = [results[(d, "en")] for d in devices if (d, "en") in results]
    full = (not args.only and not args.lang and len(english_runs) == len(devices)
            and sorted(devices) == sorted(DEVICES))
    gone = []
    if english_runs:
        st = strings_of("en")
        seen = set()
        for run_en in english_runs:
            for text in run_en[3]:
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
    for device, code in runs:
        notes, failures = results[(device, code)][:2]
        # Which handheld, where there is more than one
        on = device + ":" if len(devices) > 1 else ""
        notes, failures = [on + n for n in notes], [on + f for f in failures]
        # A screen whose words met a key or a mark before the check was there:
        # said, not failed, until its own fix (DESIGN.md section 14)
        known = [n for n in notes if " into a key or a more mark " in n and
                 n.split(": ")[0].split(":")[-1].split("/")[-1] in KNOWN_KEYS]
        for n in known:
            print("known " + n)
        notes = [n for n in notes if n not in known]
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
    pseudo = sum(len(results.get((d, i18n.PSEUDO), ([], [], {}, set()))[0]) for d in devices)
    said = "check-i18n-shots" + (" part %d/%d" % args.part if args.part else "")
    langs = [c for c in langs if any(r[1] == c for r in runs)]
    shot_as = [d for d in devices if any(r[0] == d for r in runs)]
    if bad:
        print("%s: %d problems in %d screens x %d languages as %s" % (
            said, bad, len(shots), len(langs), ", ".join(shot_as)))
        return 1
    print("%s: %d screens in %s as %s, everything fits%s" % (
        said, len(shots), ", ".join(langs), " and ".join(shot_as),
        "; the pseudo-locale cut or overflowed %d strings (notes above)" % pseudo if pseudo else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
