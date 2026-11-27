#!/usr/bin/env python3
"""ZyperOS's design tokens: the one place a value is typed.

    python3 tools/designdef.py           write everything below (make design)
    python3 tools/designdef.py --check   fail if anything below is stale, if a
                                         color misses its contrast, or if a role
                                         the launcher keeps differs by theme

Writes, from the tables in this file alone:

  DESIGN.md                    the tables between <!-- generated:NAME --> marks
  src/design.h                 the tokens for C: the launcher, src/osdbar.c,
                               tools/portkeys.c, and Diatom's copy of osdbar
  docs/design/mock/tokens.js   the same for the mock kit's HTML screens
  res/web/app.css              Wi-Fi File Transfer's colors and panel lift, between
                               its generated:tokens marks, Beard and Salt &
                               Pepper by the browser's prefers-color-scheme;
                               --check also holds the page's text pairs
  res/web/play/play.css        the same block, for NES player 2's play page,
                               held to the same pairs

DESIGN.md says what each token is for and why; this file only says what it
is. Standard library only, so `make check` runs it anywhere, as
tools/checkmark.py does for the mark.

Contrast is WCAG 2's relative-luminance ratio. Text is owed 4.5:1 against
every ground it is drawn on; a mark (a segment, a heart, a sun) 3:1.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# ---- color ---------------------------------------------------------------------
#
# role: (Beard, the light theme; Salt & Pepper, the dark theme). Zyper's own
# colors, from tools/zyperdef.py, are where these start: the dark ground is
# his ink (17,19,16), its surfaces lean on his cap (82,86,88), the dark
# theme's text is his beard (228,226,216) and its dim text his coat
# (150,153,152); the light theme is the beard as ground.

THEMES = ("light", "dark")
THEME_NAMES = {"light": "Beard", "dark": "Salt & Pepper"}

COLORS = [
    # role          light       dark
    ("ground",     "e6e4da",   "111310"),
    ("surface",    "f5f4ee",   "1d201e"),
    ("raised",     "d9d7cc",   "303433"),
    ("edge",       "c9c7bc",   "3a3e3c"),
    ("track",      "cfcdc2",   "3f4341"),
    ("text",       "1a1c1a",   "e4e2d8"),
    ("text-soft",  "3d403e",   "c3c3ba"),
    ("text-dim",   "5a5d5a",   "969998"),
    ("accent",     "bef23c",   "bef23c"),
    ("on-accent",  "111310",   "111310"),
    ("accent-ink", "476b00",   "bef23c"),
    ("light",      "9a6200",   "f2d27a"),
    ("favorite",   "c2557a",   "de929e"),
    ("warning",    "b23f28",   "e0705a"),
    ("ink",        "111310",   "111310"),
    # MENU > Storage's stacked bar (DESIGN.md 11.32): the three parts that
    # belong to games, a hue each, none of lime, warm, pink or red-orange,
    # whose meanings are taken. Media, the rest and free space are roles
    # above (text-soft, text-dim, track) and ZyperOS is accent-ink.
    ("store-games", "2c64c4",  "6b9ef5"),
    ("store-saves", "7a4fc4",  "ae8ef2"),
    ("store-art",   "0e8a86",  "4fc8c0"),
]

# Text roles are owed 4.5:1 on these grounds; marks 3:1 on theirs.
TEXT_ON = {
    "text": ("ground", "surface"),
    "text-dim": ("ground", "surface"), "accent-ink": ("ground", "surface"),
    "warning": ("surface",), "on-accent": ("accent",),
    # a two-answer panel's answers at rest, each on its **raised** slab
    # (DESIGN.md 11.3, "A question with two answers")
    "text-soft": ("ground", "surface", "raised"),
}
# Text at **menu** (49 units, 31 px on the Pixel 2) and larger, owed large
# text's 3:1: the answer that loses something, in **warning** on its
# **raised** slab (4.0:1 in Beard, 3.9:1 in Salt & Pepper)
LARGE_TEXT_ON = {
    "warning": ("raised",),
}
MARK_ON = {
    "accent-ink": ("track",), "light": ("surface", "track"),
    "favorite": ("surface",), "accent": ("ink",),
    # the status corner's low battery, outline and fill (DESIGN.md 11.12)
    "warning": ("ground",),
    # Storage's bar and its legend's dots, on the page's ground
    "store-games": ("ground",), "store-saves": ("ground",), "store-art": ("ground",),
}

# Wi-Fi File Transfer's page (res/web/app.css) draws text on more grounds than the
# launcher's screens: a button is **raised** and **edge** while it is pressed,
# a field **ground**. Every text pair it draws is here, read off its rules
# with their cascade, and owed TEXT_ON's 4.5:1 in both themes. check-design
# also reads the page and fails when it sets a text color or a background in
# a role named nowhere below, so a new role cannot come in unlisted; an old
# role on a ground it was not on before is the one thing it cannot see, and
# is listed here when it is written.
PAGE_TEXT_ON = {
    "text":       ("ground", "surface", "raised", "edge"),
    # rows and menu items at rest, and a row's size while the pointer is on
    # it (text-dim misses 4.5:1 on Salt & Pepper's raised, so it steps up)
    "text-soft":  ("ground", "surface", "raised"),
    "text-dim":   ("ground", "surface"),
    "accent-ink": ("ground", "surface"),
    "warning":    ("ground", "surface"),
    "on-accent":  ("accent",),
}
# Backgrounds on the page that carry no text: the transfer bar's track, and
# its fill, going or failed; ink, the scrim under a menu or a dialog; sys, no
# role but a console's own color from systems.cfg, the tint of the tile under
# its folder's pixel art (DESIGN.md 11.15), set on the row by the page; and
# text-dim, the card's room meter's fill and the tint of a tile no system
# names a color for, under a picture, never under text.
PAGE_FILLS = ("track", "accent-ink", "warning", "ink", "sys", "text-dim")

# The page's sizes (DESIGN.md 11.14). Its shapes are DESIGN.md 6's radii at
# the Pixel 2's own 0.625, so a row on a phone has the corners of a row on the
# handheld. Its type keeps DESIGN.md 4's roles and their ratio to **menu**,
# with **menu** at the web's 16 px; **meta** would come out at 12 and is held
# at 13.5, the least a phone reads comfortably.
PAGE_SCALE = 0.625
PAGE_TYPE = {"title": 20, "label": 18, "menu": 16, "meta": 13.5}
# DESIGN.md 8.1's curves as CSS: the quintic and cubic as cubic-beziers (the
# closest a cubic Bezier gets to t^5 and t^3), smoothstep as its own.
PAGE_CURVES = {"out": "cubic-bezier(0.22, 1, 0.36, 1)",
               "in": "cubic-bezier(0.32, 0, 0.67, 0)",
               "turn": "cubic-bezier(0.45, 0, 0.55, 1)",
               "fade": "linear"}

# Roles that must be one value in both themes, and why. A theme change reaches
# whatever is drawn from theme() on the next frame, but not a value the
# launcher has kept: src/main.c keeps the accent as the color of Favorites
# and MP4 (ZYPEROS_ACCENT, in their systems' accents and the covers drawn
# from it) for as long as it runs, and src/ui.c's ui_sys_mark knows that
# lime, and draws it as accent-ink, by its value.
SAME_IN_BOTH = {
    "accent": "src/main.c keeps it as Favorites' and MP4's color "
              "(ZYPEROS_ACCENT) and src/ui.c's ui_sys_mark knows it by value",
}

# The player's look (DESIGN.md 3.5): MENU > Display > Theme, my picks of
# 2026-11-12. Ten accents round the hue circle, each a fill with dark text and
# the same in both themes, lime first because it is the accent above; and
# three background tones for each theme, the first each theme's ground above.
# Everything else in a theme is worked out from the two by src/look.c, and
# tools/look-check.c holds every pair of them to TEXT_ON and MARK_ON.
ACCENTS = [
    # id        name       fill
    ("lime",    "Lime",    "bef23c"),
    ("green",   "Green",   "5ee08a"),
    ("teal",    "Teal",    "3fd4c0"),
    ("blue",    "Blue",    "74b0ff"),
    ("purple",  "Purple",  "b59bff"),
    ("pink",    "Pink",    "ff94c8"),
    ("red",     "Red",     "ff7a70"),
    ("orange",  "Orange",  "ffa04d"),
    ("yellow",  "Yellow",  "ffd84d"),
    ("silver",  "Silver",  "d3d5cf"),
]
TONES = {
    "light": [("warm", "Warm", "e6e4da"), ("neutral", "Neutral", "e3e3e1"),
              ("cool", "Cool", "dde2e7")],
    "dark":  [("black", "Black", "111310"), ("deepblue", "Deep Blue", "0e1526"),
              ("darkgrey", "Dark Grey", "1e2120")],
}

# Scrims: ink at this opacity (0-1).
SCRIMS = [
    # name      light  dark
    ("menu",    0.28,  0.47),
    ("game",    0.73,  0.73),
]

# ---- type --------------------------------------------------------------------

FONT = "Lexend"
FONT_FILES = {"Regular": "menu.ttf", "SemiBold": "title.ttf",
              # 简体中文's (tools/cjkfont.py): Noto Sans SC, Regular and Bold
              "CJK": "zh-menu.otf", "CJK title": "zh-title.otf",
              # 日本語's: Noto Sans JP, Regular and Bold
              "JA": "ja-menu.otf", "JA title": "ja-title.otf"}
TYPE = [
    # role    px   weight      for
    ("title", 60, "SemiBold", "the name under the cursor; screen titles"),
    ("label", 55, "Regular",  "menu headings, the keyboard's title, a notice's body"),
    ("menu",  49, "Regular",  "menu rows, keys, a game's details"),
    ("meta",  37, "Regular",  "menu captions and secondary text on detailed screens"),
    ("chrome", 24, "Regular", "home and games headers, status and footer legends"),
    ("support", 28, "Regular", "home counts, compact game metadata, and the action beside its button glyph (action 27 and support 29 until 1.0.6)"),
    ("game-title", 37, "SemiBold", "the full game name below its cover row: meta's size, title's weight; its lines 40 apart (38 and 44 until 1.0.6)"),
    ("card",  71, "Regular",  "the title drawn into a generated card (512 px wide)"),
    ("clock", 120, "SemiBold", "MP4's Classic look: the time played, large; digits only, so Lexend in every language"),
]

# ---- space and shape -----------------------------------------------------------

SPACE = [
    ("grid",        8,   "the step for anything new; 5 panel pixels"),
    ("inset",       24,  "text and marks from every screen edge"),
    ("panel-w",     800, "the standard menu panel's width"),
    ("bar",         8,   "the rail, a progress bar: on the grid (6 until 1.0.6)"),
    ("rail-inset",  23,  "the rail from the bottom edge"),
    ("rail-ends",   90,  "the rail's inset at both ends"),
    ("rail-min",    18,  "the rail marker's least length"),
    ("level-mark",  42,  "the volume and brightness bar's mark: its middle, in from the pill's left end"),
    ("boost-gap",   12,  "the bar's gap at the top, its thin mark in the middle, before the boost cells"),
    ("boost-charge-w", 36, "the bar's charging cell before the boost opens, wide..."),
    ("boost-charge-h", 30, "...and tall: big, so its fill is easy to see"),
]

SHAPE = [
    ("radius-card",  22, "a cover, a generated card"),
    ("radius-panel", 28, "a menu panel, the keyboard, a notice, the tutorial stage"),
    ("radius-row",   14, "the focus fill on a row"),
    ("radius-key",   12, "a keyboard key, the text field"),
    ("radius-chip",  8,  "a chip, a hint, a save slot's dot"),
    ("frame",        3,  "the edge around a surface"),
    ("rule",         2,  "a heading's rule, the keyboard field's edge"),
    ("caret",        3,  "the keyboard's caret"),
    ("ring",         4,  "a button glyph's ring"),
    ("radius-bar",   4,  "MP4's bars (Player Look); a bar's cap is a pill"),
]

# ---- layout --------------------------------------------------------------------
#
# Where the launcher's own parts sit, in units of the 1024 x 768 design space:
# a page's frame (Flash Drive, File Transfer's page, the Scraper), a panel of
# tiles or a list, the marks, and the button hints. The launcher's alone, so
# they go into src/ds/layout.h and not design.h, which Diatom and portkeys
# carry too. A new screen takes its places from here (DESIGN.md, "Where each
# part lives"); one that types a number of its own says why beside it.

LAYOUT = [
    # the page frame (src/ds/page.h)
    ("page-zyper-x",    123, "Zyper's middle beside a page's heading"),
    ("page-zyper-y",    168, "his middle's height"),
    ("page-zyper-h",    124, "his height there"),
    ("page-head-x",     208, "the heading's left beside him (with him off: the inset)"),
    ("page-head-y",     168, "a heading on one line: its middle"),
    ("page-head-step",  34,  "a heading on two lines: each line this far above and under head-y"),
    ("page-title-y",    150, "a heading with a smaller line under it: the heading's middle"),
    ("page-sub-y",      208, "that smaller line's middle"),
    ("page-sub-mark",   32,  "a mark before the smaller line"),
    ("page-body-y",     256, "where a page's content starts: the heading's band, Zyper in it, is this tall"),
    ("page-news-drop",  56,  "the first line of words under a page's two tiles: its middle this far under their foot (592)"),
    ("page-news-step",  48,  "a second line of other words: its middle this far under the first's (File Transfer's Browsers, 640); a sentence on two lines keeps meta's line, 40"),
    ("page-line-over",  40,  "the line of marks and words over the keys: its box, a glyph tall, this far over theirs (its middle at 648)"),
    ("page-line-gap",   48,  "between the line's marks and words"),
    ("page-line-gap-tight", 24, "...where they are wider than the page"),
    ("foot-y",          720, "every screen's keys: the last row's middle, 32 from the screen's foot to the glyph's (one foot since 1.0.6; pages had 704, Cover and Video 724)"),
    # the Video view (src/videoview.h, DESIGN.md 11.57)
    ("video-band-top",  524, "the Video view's band of covers and keys: its top while it is up"),
    ("video-step",      108, "its covers a step apart"),
    ("video-cover-w",   90,  "a cover's box in the band"),
    ("video-cover-h",   120, "...its height"),
    ("video-focus-w",   108, "the focused cover's box"),
    ("video-focus-h",   144, "...its height"),
    ("video-card-y",    96,  "the name's card at the top left: its top, clear of the header row"),
    ("video-card-pad-x", 28, "the card's padding at its sides"),
    ("video-card-pad-y", 18, "...at its top and foot"),
    ("page-foot-gap",   40,  "between the hints at a page's foot"),
    ("page-foot-gap-tight", 20, "...where they are wider than the page"),
    ("page-tile-w",     472, "a page's tile, two side by side from the insets"),
    ("page-tile-h",     280, "its height"),
    ("page-tile-pad",   32,  "its words from its sides"),
    # a menu panel (src/menu_panel.c)
    ("menu-row",        72,  "a menu's row: on the grid, 45 whole pixels on the Pixel 2 (1.5 menu lines, 73, until 1.0.6)"),
    ("menu-pad",        56,  "a menu panel's padding: 35 whole pixels on the Pixel 2 (3/4 of a row, 54, until 1.0.6)"),
    # a tab row's turn (src/menu_panel.c) and the in-game menu's Controls tab (src/ctltab_screen.c)
    ("tab-move-name",   96,  "a tab's turn (L1, R1): the tab names move this far the press's way"),
    ("tab-move-in",     64,  "...and the new tab's rows come in from this far, the Controls tab's handheld with them"),
    ("ctl-side",        280, "the Controls tab's handheld column: its rows and their focus fill start this far in from the content's left"),
    ("sc-side",         192, "Screen Colors' color chart column: its six rows and their focus fill start this far in from the content's left (the design session's mock, 2026-11-16)"),
    ("ctl-device-x",    99,  "the handheld's middle from the content's left: half the Pixel 2's drawing at its tab pose, so its left edge is on the heading rule's left end"),
    ("ctl-device-y",    458, "the handheld's middle on the screen: one place on every tab and page that has it, as their panel is as tall as the screen allows (the mock's, 2026-11-13)"),
    ("hud-band",        152, "On-Screen Stats' band: the screen's strip where the line goes, the line at Far (48) under the level bar (94 + 49) and at the foot; on the grid, 95 whole pixels on the Pixel 2 (150 until 1.0.6)"),
    # a row's scale, the graded bar (src/ds/scale.h)
    ("scale-track",     16,  "the bar's track, its colors the scale's"),
    ("scale-knob",      32,  "the value's knob, across"),
    ("scale-mark",      8,   "a mark under the track at an end or a half"),
    ("scale-zero",      12,  "zero's mark, through the track and this much past it"),
    ("scale-min-w",     192, "the least a bar is, so the words beside it stay at most about 313 units"),
    # a panel of tiles, or a list (src/ds/tiles.h)
    ("tile-w",          272, "a tile, three in a row"),
    ("tile-w-wide",     408, "a tile, two in a row or the grid's"),
    ("tile-h",          232, "a tile's height: the mark, a name on two lines"),
    ("tile-h-grid",     164, "a grid's tile: the mark, a name on one line"),
    ("tile-gap",        24,  "between tiles"),
    ("tile-pad",        40,  "a tiles panel's sides, and the screen's either side of the widest"),
    ("tile-sub-top",    92,  "a name's top in a tile with a smaller line under it"),
    ("tile-mark",       72,  "a tile's mark"),
    ("tile-mark-sub",   64,  "a tile's mark over a name and a smaller line"),
    ("list-mark",       64,  "a list row's mark"),
    ("list-row-gap",    12,  "between a list's rows"),
    ("list-pad-y",      14,  "a list row's words from its top and foot"),
    # a question with two answers (src/ds/confirm.h, DESIGN.md 11.3)
    ("confirm-inset",   26,  "an answer's slab from the panel's edge"),
    ("confirm-gap",     24,  "between the two answers side by side"),
    ("confirm-word-pad", 24, "an answer's words from its slab's ends, at the least"),
    ("confirm-glyph",   40,  "B's glyph in the answer B gives"),
    ("confirm-glyph-gap", 14, "between that glyph and the answer's word"),
    ("confirm-press",   94,  "a pressed answer's slab, percent of its size"),
    # marks (src/ds/mark.h)
    ("badge",           40,  "a disc with a mark on a cover: its source, a video"),
    ("badge-small",     30,  "the same on a small cover"),
    ("badge-ring",      2,   "the edge round a badge, and a switch mark's ring"),
    ("badge-inset",     12,  "a badge from its cover's corner"),
    ("badge-inset-small", 9, "the same on a small cover"),
    ("more-w",          24,  "the more-above or more-below triangle's width"),
    ("more-h",          12,  "its height"),
    ("mark-line",       36,  "a mark beside its words over the keys"),
    ("mark-line-pad",   14,  "that mark to its words"),
    ("mark-stat",       44,  "a count's mark"),
    # button hints (src/keyhints.h, page_hint)
    ("hint-glyph",      32,  "a hint's button glyph"),
    ("hint-glyph-focus", 40, "a hint's button glyph where the focus is"),
    ("hint-gap",        12,  "a button glyph to its word"),
    ("hints-gap",       32,  "one hint to the next"),
    ("hints-gap-spread", 16, "the least between hints spread across a foot"),
    ("hints-row",       44,  "one row of hints to the next"),
    # the system (DESIGN.md "The system", round 3): one 8-unit grid (5 panel
    # pixels on the Pixel 2) for every position and every distance between
    # parts, these roles for what a screen keeps everywhere, and each type's
    # line as a box, all for the layout engine (src/ds/flex.h). Screens move
    # onto them one by one, each through the pixel gate.
    ("margin",          64,  "content from the screen's sides"),
    ("header",          72,  "the top bar's band: the clock, a title, the marks; content starts a section under it"),
    ("section",         24,  "between the parts of a screen"),
    ("related",         8,   "between things that go together: a bar and its times"),
    ("foot-inset",      32,  "the keys' foot from the screen's foot: one row's middle at 720"),
    ("foot-gap",        24,  "content over the keys keeps at least this"),
    ("caption-inset",   40,  "a caption at the foot: its last line's box from the screen's foot"),
    ("list-row",        72,  "a list's row"),
    ("line-title",      72,  "a title's line, as a box"),
    ("line-label",      64,  "a label's line"),
    ("line-menu",       56,  "a menu row's line"),
    ("line-meta",       40,  "a meta line: times, a caption's line"),
    ("line-support",    36,  "a support line"),
    ("line-chrome",     32,  "a chrome line: a key's word, the header"),
    # the dense grid (src/gamegrid.c)
    ("grid-gap",        24,  "between a grid's cells"),
    ("grid-band-gap",   24,  "a grid's foot to the band under it: the name and its facts"),
    # Highlight's marks (src/ds/focus.h), each drawn as the whole panel
    # pixels it comes to (src/focusgeo.h): 2 on the Pixel 2, 3 on the Brick
    ("focus-ring",      3,   "Border's ring round the item in focus, as whole panel pixels"),
    ("focus-gap",       3,   "the ground between that ring and the item, as whole panel pixels"),
    ("focus-bar-w",     40,  "Bar's bar under the item in focus"),
    ("focus-bar-h",     6,   "...its height, a pill"),
    ("focus-bar-under", 8,   "...under the item's foot (a cover's, a still's name)"),
    ("radius-cell",     14,  "a grid cell's corners; its ring's are the cell's, the gap's and the ring's added"),
    # a choice's picture beside its list (src/ds/preview.h)
    ("preview-col-w",   336, "the picture column at an option list's left: the screen in small and its note"),
    ("preview-col-min", 200, "...narrower where the list's choices run long, to this"),
    ("preview-gap",     32,  "the column to the list's rows"),
    ("preview-note-gap", 16, "the small screen's foot to its note's first line"),
    # MP4's Player Looks (src/nowplaying_looks.c, DESIGN.md 11.64): every
    # edge on the grid, so whole pixels on the Pixel 2 as on the Brick
    ("look-bar-w",      16,  "a bar: 10 whole pixels on the Pixel 2"),
    ("look-bar-gap",    8,   "between two bars: 5"),
    ("look-cap",        4,   "a bar's cap: its height, and its gap over the bar"),
    ("look-classic-w",  488, "Classic: the clock's panel, 18 bars and their pad wide, and the words under it"),
    ("look-panel-pad",  32,  "Classic: the clock's panel's words and bars from its edges"),
    ("look-list-row",   56,  "Classic: a row of the track list"),
    ("look-list-num",   48,  "Classic: a row's number, tabular, before its name"),
    ("line-clock",      128, "Classic: the clock's line, as a box"),
    ("look-cover",      192, "Spectrum: the cover beside the names"),
    ("look-record",     448, "Vinyl: the platter, a 12-inch record and its rim, 424 and 12 each side"),
    ("look-cassette-w", 592, "Cassette: a C-cassette, 100.4 x 63.8 mm, 74 x 47 grid steps (592 x 376), the nearest whole steps to its proportions"),
]

# ---- elevation -----------------------------------------------------------------
#
# Salt & Pepper lifts with an additive glow (alpha 0-255, spread x the rect);
# Beard with a shadow (y offset, blur, opacity of ink).

GLOWS = [
    ("focus",       105, 2.3, "a card on the shelf"),
    ("focus-small", 85,  1.5, "a thumbnail, MP4's cover"),
    ("panel",       60,  1.5, "a menu panel, the keyboard"),
    ("wash",        34,  1.7, "the system's light along the bottom of the shelf"),
]
SHADOWS = [
    ("focus",       18, 48, 0.22),
    ("focus-small", 8,  20, 0.18),
    ("panel",       24, 64, 0.18),
    ("bar",         4,  8,  0.18),
]
BAR_SHADOW_DARK = 0.40      # the bar's shadow in Salt & Pepper
WASH_LIGHT = 0.16           # Beard's wash: the system's color, normal blend

# ---- motion ----------------------------------------------------------------------

MOTION = [
    # name          ms     curve    for
    ("press",       80,   "out",   "a button glyph pressing in, a key"),
    ("quick",       120,  "out",   "the focus fill between rows, the bar coming in, a check"),
    ("move",        160,  "out",   "a card step, a panel opening"),
    ("exit",        140,  "in",    "a panel closing"),
    ("turn",        300,  "turn",  "the tutorial details panel"),
    ("spin",        900,  "turn",  "the 3D handheld turning from its front to its back or back again, where the keys lit are on both"),
    ("hold-spin",   1800, "-",     "the 3D handheld held on its front or its back before it turns to the other"),
    ("launch",      190,  "in",    "a card zooming into its game"),
    ("fade-out",    200,  "fade",  "the bar, a notice going away"),
    ("hold-bar",    1500, "-",     "the bar after the last press"),
    ("hold-short",  1600, "-",     "a one-line message"),
    ("hold-long",   2200, "-",     "a two-line message"),
    ("zyper-hop",   280,  "out",   "Zyper's hop"),
    ("zyper-tilt",  160,  "out",   "Zyper's head cocking"),
    ("zyper-tongue", 160, "out",   "Zyper's tongue"),
    ("zyper-breathe", 2400, "-",   "Zyper's idle bob, one loop"),
    ("zyper-pant",  1000, "-",     "Zyper's pant, the tongue in and out three times"),
    ("zyper-blink", 134,  "turn",  "Zyper's blink: shut in 3/8 of it, held to half, open by the end"),
    ("zyper-blink-gap", 3000, "-", "the least time between Zyper's blinks at rest; the most is twice it"),
    ("hold-zyper-open", 80, "-",   "a menu's first frame, before Zyper's opening hop"),
    ("hold-zyper-fail", 860, "-",  "Zyper's cocked head after a failure, before he settles"),
    ("zyper-pop",   360,  "-",     "Zyper popping in by scale for the tutorial's hello, with his overshoot"),
    ("zyper-ears",  420,  "-",     "Zyper's ears swinging out and back as he says hello"),
    ("tour-reward", 1100, "-",     "a tutorial step's reward, before the next"),
    ("tour-pulse",  440,  "-",     "the right glyph's two pulses after a press of the wrong button"),
    ("hold-unlock", 2000, "-",     "MP4's lock: SELECT and START held together this long unlock it"),
    ("hold-power",  1000, "-",     "POWER held this long turns the handheld off with Power Button on Hold, and starts the charging screen's handheld"),
    ("hold-boost",  1000, "-",     "a level key held this long on the top opens the boost past it; the charging cell fills over it, linearly, and drains back the same way when let go early"),
    ("tap-gap",     1000, "-",     "the most time between two presses on the top that count toward the three that open the boost; the charge drains after it"),
    ("boost-open",  240,  "out",   "the bar opening to its boost cells while the charging cell settles to a cell's size"),
    ("boost-flash", 360,  "fade",  "the bar's rim in the boost color as the boost opens, going back to its edge"),
    ("hold-lock-lit", 4000, "-",   "MP4's lock screen, lit as it locks"),
    ("hold-controls", 3000, "-",   "MP4's video controls after the last press"),
    ("peak-hold",   400,  "-",     "MP4's bars (Player Look): a bar's cap held where the bar last reached, before it drops with gravity"),
    ("bar-fall",    140,  "follow", "MP4's bars falling to the sound: follow's tau, where a bar rises at once"),
    ("spin-up",     360,  "out",   "MP4's reels (Player Look) coming up to speed as the music plays"),
    ("spin-down",   520,  "out",   "...and coming to a stop as it pauses; the sound stops at once"),
    ("rewind",      700,  "turn",  "MP4's cassette winding back to the start for the next song"),
    ("wind",        180,  "follow", "MP4's cassette winding to a seek's place: follow's tau"),
    ("arm-glide",   180,  "follow", "MP4's tonearm gliding to the song's place on the record: follow's tau"),
    ("hold-snap",   1000, "-",     "a game focused on the shelf before its cover turns into its video snap"),
    ("hold-band",   2000, "-",     "the Video view's band of covers and keys, and the List view's details over the media, after the last press on a game whose snap plays, before they go down"),
    ("slide-hold",  1400, "-",     "a name or a line too long for its place, still before it slides (at 70 units a second, its ends faded over 36: src/ui.c's marquee)"),
    ("slide-end",   900,  "-",     "...and still at its far end before it slides back"),
    ("sheen",       480,  "turn",  "one light crossing a game's card with no art, once, as the cover row stops on it"),
    ("sharpen",     200,  "fade",  "a played game's card with no art turning from its blurred frame of play to the sharp one, as the cover row stops on it"),
    ("hold-low-battery", 6000, "-", "the low-battery start screen before the handheld powers off"),
    ("hold-heat",   4000, "-",     "\"Getting hot\", on the home and over a game"),
    ("hold-charge-lit", 10000, "-", "the charging screen lit after the last button, before it goes dark"),
]

# ---- sound -----------------------------------------------------------------------
#
# Synthesized by tools/gensounds.py into res/sounds/<voice>/<sound>.wav, which
# is what src/uisound.c plays. A tone is (wave, Hz, ms, peak 0-1, start ms),
# its Hz one pitch or a (from, to) pair that glides over the tone's first
# third; a chord is several tones, and a "noise" is a band of white noise at
# the pitch, for what is struck rather than rung. Three voices of the same
# seven sounds: Wood is a new device's, and MENU > UI Sounds picks another or
# none. my, 2026-11-04, in the sound lab (docs/design/mock/sound-lab.html).

SOUNDS = [
    ("move",   "a step on a shelf or a list"),
    ("edge",   "a step blocked at the end of a list"),
    ("accept", "A, a menu opening"),
    ("back",   "B, a menu closing"),
    ("deny",   "a press nothing can act on"),
    ("reward", "something good"),
    ("shutter", "the Screenshot hotkey's shot, over a game or your PICO-8"),
]

# The shutter, the same in every voice (2026-11-20, on the PICO-8 hotkeys
# test: "Can we have the screenshot make a photo sound and vibrate?"): a
# camera's two clicks, the shutter opening and closing 115 ms apart, each a
# bright snap of noise, a darker body under it and a soft thump; the second a
# little softer and lower, as it falls back. 175 ms in all. Louder than a
# tick, as it is heard over a game's own sound.
SHUTTER = [
    ("noise", 5200, 12, 0.32, 0), ("noise", (2600, 1800), 40, 0.16, 0),
    ("sine", (220, 160), 45, 0.08, 2),
    ("noise", 4400, 12, 0.22, 115), ("noise", (2200, 1500), 60, 0.12, 115),
    ("sine", (190, 140), 55, 0.06, 117),
]

VOICES = [
    # A soft mallet on a wooden bar. Each note settles from 5% sharp as it is
    # struck, and a quiet partial four times up is the strike, gone in 6 ms.
    # Forward is up the scale (C6, D6), back and the end down it (A5, G5);
    # deny is a falling second, "uh-uh"; a reward is two bars a fifth apart.
    ("wood", "Wood", {
        "move":   [("sine", (1098.825, 1046.5), 30, 0.055, 0), ("sine", 4186.0, 6, 0.012, 0)],
        "edge":   [("sine", (823.2, 784.0), 38, 0.055, 0), ("sine", 3136.0, 6, 0.010, 0)],
        "accept": [("sine", (1233.435, 1174.7), 55, 0.080, 0), ("sine", 2349.4, 24, 0.020, 0),
                   ("sine", 4698.8, 6, 0.012, 0)],
        "back":   [("sine", (924.0, 880.0), 60, 0.075, 0), ("sine", 1760.0, 24, 0.018, 0)],
        "deny":   [("triangle", 466.2, 50, 0.080, 0), ("triangle", 440.0, 60, 0.080, 85)],
        "reward": [("sine", (1098.825, 1046.5), 220, 0.080, 0), ("sine", 4186.0, 8, 0.015, 0),
                   ("sine", (1646.4, 1568.0), 380, 0.070, 100), ("sine", 6272.0, 8, 0.012, 100)],
        "shutter": SHUTTER,
    }),
    # Pure high ticks and clicks: what the design lab played and DESIGN.md
    # first wrote down.
    ("glass", "Glass", {
        "move":   [("sine", 1900, 25, 0.05, 0)],
        "edge":   [("sine", 1200, 30, 0.05, 0)],
        "accept": [("triangle", 1250, 40, 0.09, 0)],
        "back":   [("triangle", 820, 50, 0.08, 0)],
        "deny":   [("square", 330, 40, 0.04, 0), ("square", 330, 40, 0.04, 70)],
        "reward": [("triangle", 1318.5, 160, 0.08, 0), ("triangle", 1975.5, 260, 0.07, 90)],
        "shutter": SHUTTER,
    }),
    # Drops that pop: forward runs up, back runs down, the end and deny are
    # short falls.
    ("bubble", "Bubble", {
        "move":   [("sine", (700, 1150), 28, 0.050, 0)],
        "edge":   [("sine", (600, 480), 34, 0.050, 0)],
        "accept": [("sine", (800, 1600), 45, 0.080, 0)],
        "back":   [("sine", (1300, 650), 50, 0.070, 0)],
        "deny":   [("triangle", (520, 400), 45, 0.070, 0), ("triangle", (520, 400), 45, 0.070, 80)],
        "reward": [("sine", (900, 1318.5), 160, 0.075, 0), ("sine", (1300, 1975.5), 280, 0.065, 90)],
        "shutter": SHUTTER,
    }),
]
VOICE_DEFAULT = "wood"

# A step repeated by a held direction plays at this level of its press: the
# shelf is heard running, and eleven steps a second do not fill the room.
SOUND_HELD = 0.5


# ---- contrast -----------------------------------------------------------------

def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def lum(h):
    def ch(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(v) for v in rgb(h))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def contrast_failures():
    col = {r: dict(zip(THEMES, v)) for r, *v in COLORS}
    bad = []
    for need, table, where in ((4.5, TEXT_ON, ""), (3.0, LARGE_TEXT_ON, ""), (3.0, MARK_ON, ""),
                               (4.5, PAGE_TEXT_ON, "Wi-Fi File Transfer: ")):
        for role, grounds in table.items():
            for t in THEMES:
                for g in grounds:
                    c = ratio(col[role][t], col[g][t])
                    if c < need:
                        bad.append("%s%s %s on %s: %.2f, owed %.1f"
                                   % (where, THEME_NAMES[t], role, g, c, need))
    return bad


# The pages that wear the tokens: Wi-Fi File Transfer's, and NES player 2's
# play page in its clothes. The panel lift is for the File Transfer page's
# panel (fill_css), the play page's too-old panel being as wide.
PAGE_CSS = ("res/web/app.css", "res/web/play/play.css")


def page_role_failures(css, name):
    """The roles a page's CSS sets as a text color or a background, outside
    its generated block, that PAGE_TEXT_ON and PAGE_FILLS do not name."""
    css = re.sub(r"/\* generated:tokens \*/.*?/\* /generated:tokens \*/", "", css, flags=re.S)
    css = re.sub(r"/\*.*?\*/", "", css, flags=re.S)
    texts = set(re.findall(r"(?<![-\w])color:\s*var\(--([a-z-]+)\)", css))
    grounds = set(re.findall(r"background(?:-color)?:\s*var\(--([a-z-]+)\)", css))
    grounds |= set(re.findall(r"background(?:-color)?:\s*rgba\(var\(--([a-z-]+)-rgb\)", css))
    named = {g for gs in PAGE_TEXT_ON.values() for g in gs} | set(PAGE_FILLS)
    bad = ["%s sets text in %s, which PAGE_TEXT_ON does not hold to its grounds" % (name, r)
           for r in sorted(texts - set(PAGE_TEXT_ON))]
    bad += ["%s fills a background with %s, which PAGE_TEXT_ON and PAGE_FILLS do not name" % (name, r)
            for r in sorted(grounds - named)]
    return bad


def same_failures():
    col = {r: dict(zip(THEMES, v)) for r, *v in COLORS}
    bad = ["%s is %s in Beard and %s in Salt & Pepper; it must be one value: %s"
           % (role, col[role]["light"], col[role]["dark"], why)
           for role, why in SAME_IN_BOTH.items()
           if col[role]["light"] != col[role]["dark"]]
    # The look's first accent and first tones are the themes as they ship
    if ACCENTS[0][2] != col["accent"]["light"]:
        bad.append("the first accent, %s, is not the accent, %s" % (ACCENTS[0][2], col["accent"]["light"]))
    for t in THEMES:
        if TONES[t][0][2] != col["ground"][t]:
            bad.append("%s's first tone, %s, is not its ground, %s" % (THEME_NAMES[t], TONES[t][0][2], col["ground"][t]))
        if len(TONES[t]) != len(TONES[THEMES[0]]):
            bad.append("%s has %d tones and %s %d" % (t, len(TONES[t]), THEMES[0], len(TONES[THEMES[0]])))
    return bad


# ---- outputs ---------------------------------------------------------------------

def cname(s):
    return s.upper().replace("-", "_")


def layout_h():
    L = []
    L.append("/* SPDX-License-Identifier: GPL-3.0-only */")
    L.append("/* Where the launcher's parts sit, in units of the 1024 x 768 design space.")
    L.append(" * GENERATED by tools/designdef.py from its LAYOUT table: edit that, run")
    L.append(" * `make design`. DESIGN.md says what each is for (section 5, Layout).")
    L.append(" *")
    L.append(" * The launcher's alone, beside src/design.h's tokens, which Diatom and")
    L.append(" * portkeys carry too. */")
    L.append("#ifndef ZYPEROS_DS_LAYOUT_H")
    L.append("#define ZYPEROS_DS_LAYOUT_H")
    L.append("")
    w = max(len(cname(n)) for n, _, _ in LAYOUT) + 4
    for name, v, _ in LAYOUT:
        L.append("#define %s %d" % (("DS_" + cname(name)).ljust(w), v))
    L.append("")
    L.append("#endif")
    return "\n".join(L) + "\n"


def design_h():
    L = []
    L.append("/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */")
    L.append("/* ZyperOS's design tokens. GENERATED by tools/designdef.py from its tables:")
    L.append(" * edit those, run `make design`. DESIGN.md says what each is for.")
    L.append(" *")
    L.append(" * Plain C, no SDL, so the launcher, src/osdbar.c, tools/portkeys.c and")
    L.append(" * Diatom's copy of the bar all read the same numbers. Either license, for")
    L.append(" * the same reason as src/osdbar.c. */")
    L.append("#ifndef ZYPEROS_DESIGN_H")
    L.append("#define ZYPEROS_DESIGN_H")
    L.append("")
    L.append("#include <stdint.h>")
    L.append("")
    L.append("typedef struct { uint8_t r, g, b; } ds_rgb;")
    L.append("")
    L.append("/* A theme: every color role, and the scrims as ink opacity 0-255. */")
    L.append("typedef struct {")
    L.append("\tconst char *name;")
    for role, *_ in COLORS:
        L.append("\tds_rgb %s;" % role.replace("-", "_"))
    for name, *_ in SCRIMS:
        L.append("\tuint8_t scrim_%s;" % name)
    L.append("\tuint8_t bar_shadow;   /* the bar's shadow, ink opacity 0-255 */")
    L.append("} ds_theme;")
    L.append("")
    L.append("enum { DS_LIGHT, DS_DARK, DS_THEMES };")
    L.append("")
    L.append("static const ds_theme DS_THEME[DS_THEMES] = {")
    for ti, t in enumerate(THEMES):
        L.append("\t{ \"%s\"," % THEME_NAMES[t])
        for role, *v in COLORS:
            r, g, b = rgb(v[ti])
            L.append("\t  { %3d, %3d, %3d },   /* %s */" % (r, g, b, role))
        scr = ", ".join(str(int(round(s[1 + ti] * 255))) for s in SCRIMS)
        bar = int(round((BAR_SHADOW_DARK if t == "dark" else dict((s[0], s[3]) for s in SHADOWS)["bar"]) * 255))
        L.append("\t  %s, %d }," % (scr, bar))
    L.append("};")
    L.append("")
    L.append("/* The player's look (DESIGN.md 3.5, src/look.h): the accents and each")
    L.append(" * theme's background tones, the first of each as the themes ship. */")
    L.append("enum { DS_ACCENTS = %d, DS_TONES = %d };" % (len(ACCENTS), len(TONES["light"])))
    L.append("static const ds_rgb DS_ACCENT[DS_ACCENTS] = {")
    for aid, _, hx in ACCENTS:
        r, g, b = rgb(hx)
        L.append("\t{ %3d, %3d, %3d },   /* %s */" % (r, g, b, aid))
    L.append("};")
    L.append("static const char *const DS_ACCENT_ID[DS_ACCENTS] = { %s };"
             % ", ".join('"%s"' % a for a, _, _ in ACCENTS))
    L.append("static const ds_rgb DS_TONE[DS_THEMES][DS_TONES] = {")
    for t in THEMES:
        L.append("\t{ %s }," % ", ".join("{ %d, %d, %d }" % rgb(hx) for _, _, hx in TONES[t]))
    L.append("};")
    L.append("static const char *const DS_TONE_ID[DS_THEMES][DS_TONES] = {")
    for t in THEMES:
        L.append("\t{ %s }," % ", ".join('"%s"' % i for i, _, _ in TONES[t]))
    L.append("};")
    L.append("")
    L.append("/* Type: pixels in the 1024x768 design space. */")
    for role, px, w, _ in TYPE:
        L.append("#define DS_TYPE_%s %d" % (cname(role), px))
    L.append('#define DS_FONT_REGULAR "%s"' % FONT_FILES["Regular"])
    L.append('#define DS_FONT_SEMIBOLD "%s"' % FONT_FILES["SemiBold"])
    L.append('#define DS_FONT_CJK "%s"' % FONT_FILES["CJK"])
    L.append('#define DS_FONT_CJK_TITLE "%s"' % FONT_FILES["CJK title"])
    L.append('#define DS_FONT_JA "%s"' % FONT_FILES["JA"])
    L.append('#define DS_FONT_JA_TITLE "%s"' % FONT_FILES["JA title"])
    L.append("")
    L.append("/* Space and shape, in units. */")
    for name, v, _ in SPACE + SHAPE:
        L.append("#define DS_%s %d" % (cname(name), v))
    L.append("")
    L.append("/* Elevation. Glows: alpha 0-255 and spread. Shadows: y, blur, ink 0-255. */")
    for name, a, s, _ in GLOWS:
        L.append("#define DS_GLOW_%s_A %d" % (cname(name), a))
        L.append("#define DS_GLOW_%s_SPREAD %.2ff" % (cname(name), s))
    L.append("#define DS_WASH_LIGHT_A %d   /* Beard: the system's color, normal blend */" % int(round(WASH_LIGHT * 255)))
    for name, y, blur, a in SHADOWS:
        L.append("#define DS_SHADOW_%s_Y %d" % (cname(name), y))
        L.append("#define DS_SHADOW_%s_BLUR %d" % (cname(name), blur))
        L.append("#define DS_SHADOW_%s_A %d" % (cname(name), int(round(a * 255))))
    L.append("")
    L.append("/* Motion: milliseconds. The curves are below. */")
    for name, ms, _, _ in MOTION:
        L.append("#define DS_MS_%s %d" % (cname(name), ms))
    L.append("")
    L.append("/* out: arriving and moving. Quintic ease-out. */")
    L.append("static inline float ds_out(float t)")
    L.append("{")
    L.append("\tt = t < 0 ? 0 : (t > 1 ? 1 : t);")
    L.append("\tt = 1 - t;")
    L.append("\treturn 1 - t * t * t * t * t;")
    L.append("}")
    L.append("")
    L.append("/* in: leaving. Cubic ease-in. */")
    L.append("static inline float ds_in(float t)")
    L.append("{")
    L.append("\tt = t < 0 ? 0 : (t > 1 ? 1 : t);")
    L.append("\treturn t * t * t;")
    L.append("}")
    L.append("")
    L.append("/* turn: big spatial changes. Smoothstep. */")
    L.append("static inline float ds_turn(float t)")
    L.append("{")
    L.append("\tt = t < 0 ? 0 : (t > 1 ? 1 : t);")
    L.append("\treturn t * t * (3 - 2 * t);")
    L.append("}")
    L.append("")
    L.append("/* Sound (DESIGN.md 9): seven sounds in three voices, each sound a WAV,")
    L.append(" * res/sounds/<voice id>/<sound id>.wav, that tools/gensounds.py")
    L.append(" * synthesizes from the tones in tools/designdef.py. */")
    L.append("typedef enum {")
    for name, _ in SOUNDS:
        L.append("\tDS_SOUND_%s," % cname(name))
    L.append("\tDS_SOUNDS")
    L.append("} ds_sound;")
    L.append("static const char *const DS_SOUND_ID[DS_SOUNDS] = { %s };"
             % ", ".join('"%s"' % n for n, _ in SOUNDS))
    L.append("typedef enum {")
    for vid, _, _ in VOICES:
        L.append("\tDS_VOICE_%s," % cname(vid))
    L.append("\tDS_VOICES")
    L.append("} ds_voice;")
    L.append("static const char *const DS_VOICE_ID[DS_VOICES] = { %s };"
             % ", ".join('"%s"' % v for v, _, _ in VOICES))
    L.append("static const char *const DS_VOICE_NAME[DS_VOICES] = { %s };"
             % ", ".join('"%s"' % n for _, n, _ in VOICES))
    L.append("#define DS_VOICE_DEFAULT DS_VOICE_%s" % cname(VOICE_DEFAULT))
    L.append("/* A step repeated by a held direction plays at this level of its press. */")
    L.append("#define DS_SOUND_HELD_LEVEL %.2ff" % SOUND_HELD)
    L.append("")
    L.append("#endif")
    return "\n".join(L) + "\n"


def tokens_js():
    import json
    col = {t: {} for t in THEMES}
    for role, *v in COLORS:
        for ti, t in enumerate(THEMES):
            col[t][role] = "#" + v[ti]
    out = {
        "themes": {t: {"name": THEME_NAMES[t], "color": col[t],
                       "scrim": {s[0]: s[1 + ti] for s in SCRIMS}}
                   for ti, t in enumerate(THEMES)},
        "font": FONT,
        "type": {r: {"px": px, "weight": w} for r, px, w, _ in TYPE},
        "space": {n: v for n, v, _ in SPACE},
        "shape": {n: v for n, v, _ in SHAPE},
        "layout": {n: v for n, v, _ in LAYOUT},
        "motion": {n: {"ms": ms, "curve": c} for n, ms, c, _ in MOTION},
        "curves": {"out": "cubic-bezier(.23,1,.32,1)", "in": "cubic-bezier(.32,0,.67,0)",
                   "turn": "cubic-bezier(.45,0,.55,1)", "fade": "linear"},
        "sound": {"voices": {vid: {"name": vn, "sounds": {
                      n: [{"wave": w, "hz": list(hz) if isinstance(hz, tuple) else hz,
                           "ms": ms, "peak": pk, "at": at} for w, hz, ms, pk, at in tones[n]]
                      for n, _ in SOUNDS}} for vid, vn, tones in VOICES},
                  "default": VOICE_DEFAULT, "held": SOUND_HELD},
        "look": {"accents": [{"id": i, "name": n, "fill": "#" + h} for i, n, h in ACCENTS],
                 "tones": {t: [{"id": i, "name": n, "ground": "#" + h} for i, n, h in TONES[t]]
                           for t in THEMES}},
    }
    return ("/* ZyperOS's design tokens for the mock kit. GENERATED by\n"
            " * tools/designdef.py: edit there, run `make design`. */\n"
            "window.ZYPER_TOKENS = " + json.dumps(out, indent=2) + ";\n")


def page_panel_lift(panel_w):
    """DESIGN.md 7's **panel** lift as a CSS box-shadow for Wi-Fi File Transfer's
    panel, `panel_w` wide, in each theme. Beard's is the shadow as it stands:
    the launcher draws its shadows as CSS draws a box-shadow of the same
    numbers. Salt & Pepper's is the glow, which the launcher draws as a soft
    disc (alpha falling as the cube of the distance) stretched to `spread`
    times the panel, in **text-dim** for a panel that is no system's. Only
    what shows past the panel matters, so the shadow matches that: as wide as
    the glow reaches past the panel's side, (spread - 1) / 2 of its width, and
    as strong at the panel's edge, a box-shadow's half there being the glow's
    alpha times (1 - 1 / spread) cubed."""
    sh = {n: (y, b, a) for n, y, b, a in SHADOWS}["panel"]
    gl = {n: (a, sp) for n, a, sp, _ in GLOWS}["panel"]
    edge = gl[0] / 255.0 * (1.0 - 1.0 / gl[1]) ** 3
    return {
        "light": "0 %dpx %dpx rgba(var(--ink-rgb), %.2f)" % sh,
        "dark": "0 0 %dpx rgba(var(--text-dim-rgb), %.3f)"
                % (round((gl[1] - 1.0) / 2.0 * panel_w), 2.0 * edge),
    }


def css_tokens(panel_w):
    """The colors as CSS custom properties for Wi-Fi File Transfer's page: each
    role as --role, and as --role-rgb ("r, g, b") for an rgba() at an opacity
    of its own, and --panel-lift (page_panel_lift). Beard on :root, since a
    browser that says nothing is a light one, and Salt & Pepper where it asks
    for dark."""
    lift = page_panel_lift(panel_w)

    def props(t, ind):
        L = []
        for role, *v in COLORS:
            h = v[THEMES.index(t)]
            L.append("%s--%s: #%s;" % (ind, role, h))
            L.append("%s--%s-rgb: %d, %d, %d;" % ((ind, role) + rgb(h)))
        L.append("%s--panel-lift: %s;" % (ind, lift[t]))
        # focus-small for a lifted control (the upload button, a sheet's
        # handle of the page); Salt & Pepper lifts nothing that small
        if t == "light":
            sh = {n: (y, b, a) for n, y, b, a in SHADOWS}["focus-small"]
            L.append("%s--small-lift: 0 %dpx %dpx rgba(var(--ink-rgb), %.2f);" % ((ind,) + sh))
        else:
            L.append("%s--small-lift: none;" % ind)
        scrim = {n: (l, d) for n, l, d in SCRIMS}["menu"]
        L.append("%s--scrim-menu: %.2f;" % (ind, scrim[THEMES.index(t)]))
        return L

    def sizes(ind):
        L = []
        for name, units, _ in SHAPE:
            if name.startswith("radius-"):
                L.append("%s--%s: %dpx;" % (ind, name, round(units * PAGE_SCALE)))
        for role, px in PAGE_TYPE.items():
            L.append("%s--fs-%s: %spx;" % (ind, role, ("%g" % px)))
        for curve, css in PAGE_CURVES.items():
            L.append("%s--ease-%s: %s;" % (ind, curve, css))
        for name, ms, _, _ in MOTION:
            L.append("%s--t-%s: %dms;" % (ind, name, ms))
        return L
    L = ["/* Generated by tools/designdef.py (make design) from DESIGN.md 3's roles,",
         " * 6's radii, 4's type, 7's lifts and 8's motion: edit there, never here. */",
         ":root {", "\tcolor-scheme: light dark;"]
    L += props("light", "\t")
    L += sizes("\t")
    L += ["}", "@media (prefers-color-scheme: dark) {", "\t:root {"]
    L += props("dark", "\t\t")
    L += ["\t}", "}"]
    return "\n".join(L)


def fill_css(text, app_css, name):
    """A page's token block, for a panel as wide as the File Transfer page's
    own .gate .panel says (its max-width, in app_css), so the two cannot
    drift."""
    pat = re.compile(r"(/\* generated:tokens \*/\n).*?(/\* /generated:tokens \*/)", re.S)
    if not pat.search(text):
        raise SystemExit("%s has no generated:tokens block" % name)
    w = re.search(r"\.gate \.panel \{[^}]*?max-width:\s*(\d+)px", app_css, re.S)
    if not w:
        raise SystemExit("res/web/app.css's .gate .panel has no max-width in px")
    return pat.sub(lambda m: m.group(1) + css_tokens(int(w.group(1))) + "\n" + m.group(2), text)


def md_tables():
    T = {}
    rows = ["| Role | Beard (light) | Salt & Pepper (dark) |", "|---|---|---|"]
    for role, l, d in COLORS:
        rows.append("| **%s** | `#%s` | `#%s` |" % (role, l, d))
    T["colors"] = rows
    rows = ["| Accent | Fill | Tone | Beard (light) | Salt & Pepper (dark) |", "|---|---|---|---|---|"]
    for i, (aid, name, hx) in enumerate(ACCENTS):
        tone = ("| **%d** | %s `#%s` | %s `#%s` |" % (i + 1, TONES["light"][i][1], TONES["light"][i][2],
                                                       TONES["dark"][i][1], TONES["dark"][i][2])
                if i < len(TONES["light"]) else "| | | |")
        rows.append("| **%s**%s | `#%s` %s" % (name, " (as it ships)" if i == 0 else "", hx, tone))
    T["look"] = rows
    rows = ["| Role | Size | Weight | For |", "|---|---|---|---|"]
    for r, px, w, f in TYPE:
        rows.append("| **%s** | %d (%.0f px on the Pixel 2) | %s | %s |" % (r, px, px * 0.625, w, f))
    T["type"] = rows
    rows = ["| Token | Units | For |", "|---|---|---|"]
    for n, v, f in SPACE:
        rows.append("| **%s** | %d | %s |" % (n, v, f))
    T["space"] = rows
    rows = ["| Token | Units | For |", "|---|---|---|"]
    for n, v, f in SHAPE:
        rows.append("| **%s** | %d | %s |" % (n, v, f))
    T["shape"] = rows
    rows = ["| Token | Units | For |", "|---|---|---|"]
    for n, v, f in LAYOUT:
        rows.append("| **%s** | %d | %s |" % (n, v, f))
    T["layout"] = rows
    rows = ["| Token | Salt & Pepper glow (alpha / spread) | Beard shadow (y / blur / opacity) | For |", "|---|---|---|---|"]
    sh = {s[0]: s for s in SHADOWS}
    for n, a, s, f in GLOWS:
        x = sh.get(n)
        light = ("%d / %d / %.0f%%" % (x[1], x[2], x[3] * 100) if x
                 else "the system's color at %.0f%%, normal blend" % (WASH_LIGHT * 100) if n == "wash" else "none")
        rows.append("| **%s** | %d / %.1f | %s | %s |" % (n, a, s, light, f))
    rows.append("| **bar** | shadow 4 / 8 / %.0f%% | 4 / 8 / %.0f%% | the volume and brightness bar |" % (BAR_SHADOW_DARK * 100, sh["bar"][3] * 100))
    T["elevation"] = rows
    rows = ["| Token | ms | Curve | For |", "|---|---|---|---|"]
    for n, ms, c, f in MOTION:
        rows.append("| **%s** | %d | %s | %s |" % (n, ms, c, f))
    T["motion"] = rows
    def tone(t):
        w, hz, ms, pk, at = t
        pitch = "%g-%g" % hz if isinstance(hz, tuple) else "%g" % hz
        return "%s %s Hz %d ms %g at %d" % (w, pitch, ms, pk, at)
    rows = ["| Sound | For | %s |" % " | ".join(
                "%s%s" % (vn, " (default)" if vid == VOICE_DEFAULT else "") for vid, vn, _ in VOICES),
            "|---|---|" + "---|" * len(VOICES)]
    for n, f in SOUNDS:
        rows.append("| **%s** | %s | %s |" % (n, f, " | ".join("; ".join(tone(t) for t in tones[n])
                                                            for _, _, tones in VOICES)))
    T["sound"] = rows
    return {k: "\n".join(v) for k, v in T.items()}


def fill_md(text):
    for name, table in md_tables().items():
        pat = re.compile(r"(<!-- generated:%s -->\n).*?(<!-- /generated:%s -->)" % (name, name), re.S)
        if not pat.search(text):
            raise SystemExit("DESIGN.md has no generated:%s block" % name)
        text = pat.sub(lambda m: m.group(1) + "*Generated by tools/designdef.py.*\n\n" + table + "\n" + m.group(2), text)
    return text


def outputs():
    md_path = os.path.join(ROOT, "DESIGN.md")
    with open(md_path) as f:
        md = f.read()
    css = {}
    for name in PAGE_CSS:
        with open(os.path.join(ROOT, name)) as f:
            css[name] = f.read()
    out = {
        md_path: fill_md(md),
        os.path.join(ROOT, "src", "design.h"): design_h(),
        os.path.join(ROOT, "src", "ds", "layout.h"): layout_h(),
        os.path.join(ROOT, "docs", "design", "mock", "tokens.js"): tokens_js(),
    }
    for name, text in css.items():
        out[os.path.join(ROOT, name)] = fill_css(text, css[PAGE_CSS[0]], name)
    return out


def main():
    check = "--check" in sys.argv[1:]
    bad = contrast_failures()
    unlisted = []
    for name in PAGE_CSS:
        with open(os.path.join(ROOT, name)) as f:
            unlisted += page_role_failures(f.read(), name)
    same = same_failures()
    stale = []
    for path, want in outputs().items():
        have = None
        if os.path.exists(path):
            with open(path) as f:
                have = f.read()
        if have == want:
            continue
        if check:
            stale.append(os.path.relpath(path, ROOT))
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w") as f:
                f.write(want)
            print("wrote", os.path.relpath(path, ROOT))
    for b in bad:
        print("  FAIL contrast:", b)
    for b in unlisted:
        print("  FAIL page:", b)
    for b in same:
        print("  FAIL one value:", b)
    for s in stale:
        print("  FAIL %s is not what tools/designdef.py writes: run `make design`" % s)
    if bad or unlisted or same or stale:
        sys.exit(1)
    print("design: tokens, DESIGN.md, src/design.h, src/ds/layout.h, the mock kit, Wi-Fi File Transfer's page and the play page agree;"
          " every color meets its contrast, the page's text pairs too; the accent is one value in both themes")


if __name__ == "__main__":
    main()
