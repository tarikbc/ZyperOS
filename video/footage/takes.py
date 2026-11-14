#!/usr/bin/env python3
"""The tour video's footage: the host launcher's own frames, as video clips.

make native
python3 video/footage/takes.py [TAKE ...]

A "live" take runs build-native/zyperos on the card video/footage/card.py
builds, headless, with video/rec/rec.dylib loaded: a script of button presses
drives it, and every frame it draws goes into video/public/footage/TAKE.mp4 at
60 frames a second, on the launcher's own clock (video/rec/rec.c says how).

A "shots" take is the launcher's own --shot pictures, for what the host cannot
reach by pressing buttons (a game's in-game menu, Now Playing, the charging
screen, the first start): a screen's motion where the shot takes a time (the
first start's hello, the charging screen, a song name sliding), a frame for
each sixtieth of a second, and otherwise one picture held, as a cut from one
row to the next. They go in the same kind of clip.

TAKE.json beside each clip has its length and, for a live take, its presses.
Nothing here changes the launcher, and no window opens.
"""
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import card as cardlib  # noqa: E402

BINARY = ROOT / 'build-native/zyperos'
REC = ROOT / 'video/rec/rec.dylib'
OUT = ROOT / 'video/public/footage'
WORK = Path(os.environ.get('VIDEO_WORK', Path(tempfile.gettempdir()) / 'zyperos-video'))
FPS = 60
# The corner on the host: no radio, so no Wi-Fi mark, in the shots too
STATUS = 'batt=82'
SHOT_STATUS = '10:08 24h batt=82'
CLOCK = '10:08'
MGS = 'Metal Gear Solid (USA).chd'

TAKES = {}


def keys(steps, start=700, gap=420):
    """Presses one after another: a step is a key name, (key, hold ms), or a
    pause in ms (a number)"""
    out, t = [], start
    for s in steps:
        if isinstance(s, (int, float)):
            t += s
            continue
        k, hold = (s if isinstance(s, tuple) else (s, 90))
        out.append((t, k, hold))
        t += gap if hold <= 90 else hold + 200
    return out, t


# Zyper names the menu music's song on the home as a take starts; the presses
# wait until his pop-up has gone, but on the home take, which shows it
LEAD = 2600


def live(name, last, steps, tail=900, settings=None, start=LEAD, gap=420):
    script, t = keys(steps, start=start, gap=gap)
    TAKES[name] = dict(kind='live', last=last, settings=settings or {}, script=script, end=t + tail)


def shots(name, segments, card='demo', env=None):
    """segments: (last, args, hold ms) a still, or (last, args with {t}, [ms, ...]) a motion,
    and a fourth item for what to set on the card before each shot (run_shots)"""
    TAKES[name] = dict(kind='shots', card=card, segments=segments, env=env or {})


def motion(t0, t1):
    """The times of a motion from t0 to t1 ms, a sixtieth of a second apart"""
    return [round(t0 + i * 1000 / FPS) for i in range(int((t1 - t0) * FPS / 1000) + 1)]


PS1 = ('PS1', MGS)

# ---- live ---------------------------------------------------------------
# The home moving between systems, with the consoles' own animation
live('home', PS1, start=700, steps=[1500, 'left', 800, 'left', 800, 'right', 700, 'right', 700, 'right', 800, 'right', 1400])
# The same home as a Gallery
live('gallery', PS1, [1100, 'right', 700, 'right', 700, 'right', 1200], settings={'home': 'gallery'})
# The PlayStation shelf: in, along the covers and back
live('shelf', ('PS1', 'Crash Bandicoot (USA).chd'),
     [1000, 'a', 1400, 'right', 750, 'right', 750, 'right', 1100, 'left', 700, 'left', 1400])
# MENU > Search Games: "son", three Sonics, and the second one started
live('search', PS1,
     [800, 'menu', 700, 'a', 900, 'down', 'right', 'a', 'up'] + ['right'] * 7 +
     ['a', 'down', 'down', 'left', 'left', 'left', 'a', 700, 'start', 1300, 'down', 450, 'down', 700, 'a', 1800],
     gap=240)
# X on Metal Gear Solid: its details
live('details', PS1, [800, 'a', 1100, 'x', 2200, 'down', 600, 'down', 600, 'down', 600, 'down', 600, 'down', 1500, 'b', 1000])
# MENU > Appearance to Dark, then the home and a shelf in the dark
live('dark', PS1,
     [800, 'menu', 600] + ['down'] * 16 + [700, 'a', 1500, 'b', 1200, 'right', 900, 'left', 900, 'a', 1300,
                                         'right', 750, 'right', 1400], gap=150, settings={'appearance': 'light'})
# MENU > Language: 日本語, and the menu in it
live('language', PS1,
     [800, 'menu', 600] + ['down'] * 11 + [700, 'a', 1100] + ['down', 300] * 6 + [700, 'a', 2400, 'b', 1200],
     gap=170, settings={'language': 'en'})

# ---- shots ----------------------------------------------------------------
GEN = ('MD', 'Sonic The Hedgehog 2 (USA).md')
SONIC_SNAP = '{art}/Sega_-_Mega_Drive_-_Genesis/Named_Snaps/Sonic The Hedgehog 2 (World).png'
# The in-game menu over Sonic 2, a row at a time, then its Controls and Hotkeys
shots('ingame', [(GEN, ['--screen', 'games', '--gamemenu', str(r), '--backdrop', SONIC_SNAP], 330) for r in range(6)] +
      [(GEN, ['--screen', 'games', '--gamecontrols', '0', 'MD', '--backdrop', SONIC_SNAP], 1700),
       (GEN, ['--screen', 'games', '--gamecontrols', '2', 'MD', '--backdrop', SONIC_SNAP], 1700)])
# PlayStation's buttons drawn as their shapes
shots('controls-ps1', [(PS1, ['--screen', 'games', '--gamecontrols', '0', 'PS1',
                              '--backdrop', '{card}/media-art/c6.png'], 2200)])
# Cover and Video: what the sources offer, focus moving, one looked at closely
shots('media', [(PS1, ['--screen', 'games', '--media', 'page', str(k), '--media-art', '{card}/media-art'], 420)
                for k in range(0, 5)] +
      [(PS1, ['--screen', 'games', '--media', 'look', '4', '--media-art', '{card}/media-art'], 1500)])
# MENU > Storage, its PlayStation page and its Music page
shots('storage', [(PS1, ['--storage', 'main'], 2200), (PS1, ['--storage', 'system'], 2000),
                  (PS1, ['--storage', 'music'], 1800)])
# Achievements: the list, then an unlock with Zyper
shots('cheevos', [(PS1, ['--cheevos-screen', str(r), '--cheevos-waiting', '2',
                         '--cheevos-badge', '{card}/Roms/PlayStation/.cheevos/Hagane.set', 'b#'], 330)
                  for r in range(4)] +
      [(PS1, ['--notice-kind', 'unlock', '--notice', 'Violated Heavens', 'Unlocked · 10 points',
              '--notice-badge', '{card}/Roms/PlayStation/.cheevos/badges/b3.png'], 4700)])
# MP4: albums, the film and the book on one shelf; Now Playing; a video's controls
shots('mp4', [(('MUSE', 'Videos'), ['--screen', 'games'], 2500),
              (PS1, ['--nowplaying', '0', '0', '{t}'], [f'{83 + t / 1000:.3f}' for t in motion(0, 3500)])])
# Ports: PortMaster offered, installing, and a port paused under the in-game menu
shots('ports', [(('PRT', 'PortMaster.sh'), ['--screen', 'games'], 2000),
                (PS1, ['--portmaster', 'running'], 2000)])
shots('portmenu', [(('PRT', 'Star Farm.sh'), ['--screen', 'games', '--gamemenu', '0', 'port'], 2000),
                   (('PRT', 'Star Farm.sh'), ['--screen', 'games', '--gamecontrols', '0', 'PRT'], 2000)],
      card='portdocs')
# The first start: Zyper says hello in each language
shots('firststart', [(PS1, ['--tour', 'lang', '{t}'], motion(400, 7000)),
                     (PS1, ['--tour', 'welcome', '{t}'], motion(0, 2200))])
# The charging screen of a handheld that is off, and MENU > Battery Icon's Time Left
shots('battery', [(PS1, ['--charging', '{t}'], motion(0, 3000)),
                  (PS1, ['--menu', 'Battery Icon', '--status', '10:08 24h batt=62 eta=205 icon=time'], 3000)])
# A song's name too long for the top bar, sliding
shots('topbar', [(PS1, ['--home', 'spotlight', '--song', 'Night Drive (Extended Mix) - The Midnight Rider Collective',
                        '--phase', '{t}'], motion(0, 4200))])
# Zyper cheering the first games on an empty card
# Zyper's cheer for the first games: in, held at its height so it can be read,
# and out (the launcher's own frames, the middle one held)
shots('cheer', [(PS1, ['--cheer', '{t}'], motion(0, 700)), (PS1, ['--cheer', '700'], 1300),
                (PS1, ['--cheer', '{t}'], motion(700, 1900)), (PS1, ['--cheer', '1900'], 400)])
# The empty card: the two ways to add games
shots('empty', [(('', ''), ['--ways', 'both'], 3200)], card='empty')

# ---- v2: quicker takes and the features v1 left out ------------------------
# The PlayStation shelf flown along, then N64's and SNES's (the opening)
live('fast-shelf', ('PS1', 'Castlevania - Symphony of the Night (USA).chd'),
     ['a', 700] + ['right'] * 5 + [500] + ['left'] * 5 + [400, 'b', 500, 'left', 600, 'a', 700] + ['right'] * 3 +
     [400, 'b', 500, 'right', 300, 'right', 600, 'a', 700] + ['right'] * 3 + [600], gap=260)
# The Scraper fetching covers: its states (src/main.c, --scraper)
shots('scraper', [(PS1, ['--scraper', str(k)], 600) for k in range(6)] +
      [(PS1, ['--art', 'Super Metroid (USA)'], 1400)])
# SELECT+R1: the quick save's pop-up over the game, and the load
shots('quicksave', [(GEN, ['--screen', 'games', '--popup', 'saved', '--backdrop', SONIC_SNAP], 1400),
                    (GEN, ['--screen', 'games', '--popup', 'loaded', '--backdrop', SONIC_SNAP], 1400)])
# File Transfer: over Wi-Fi, a computer on the cable, and over the cable
shots('wifi-xfer', [(PS1, ['--hare', 'http://192.168.1.42', '1 connected', 'Tobu Tobu Girl.gb'], 5200)])
# 1.0.5's way in: the address, zyperos.local and the PIN at full size until a
# browser signs in (six beats of the tour's music), then the files moving
shots('wifi-way', [(PS1, ['--hare-steps', 'wifi', 'browser'], 2310),
                   (PS1, ['--hare', 'http://192.168.1.42', '1 connected', 'Tobu Tobu Girl.gb'], 3900)])
# The cable: "What did you plug in?" on Computer and on Phone, how a phone
# connects (USB tethering, 1.0.5) and its File Transfer, then a computer's choices as a release has them (plain, with
# Flash Drive) and Flash Drive's speeds
shots('cable', [(PS1, ['--usb-power', '1'], 450), (PS1, ['--usb-power', '2'], 450),
                (PS1, ['--usb-power', 'phone'], 1200), (PS1, ['--hare-steps', 'phone', 'browser'], 900),
                (PS1, ['--usb-ask', '0', 'plain', 'drive'], 400), (PS1, ['--usb-ask', '1', 'plain', 'drive'], 400),
                (PS1, ['--usb-ask', '2', 'plain', 'drive'], 500), (PS1, ['--usb-drive', '0', 'both'], 1900)])
shots('hotspot', [(PS1, ['--hotspot', 'ZyperOS-7A2B', 'q7mx-3kpa', '1 joined', 'waiting'], 2800)])
# Files still moving while a game plays: the ring in the in-game menu's corner, over Sonic 2
shots('xfer-ring', [(GEN, ['--screen', 'games', '--gamemenu', '0', '--backdrop', SONIC_SNAP,
                           '--status', f'10:08 24h batt=82 xfer={p}'], 80) for p in range(30, 92, 2)])
# System Update
shots('update', [(PS1, ['--update'], 2400)])
# Battery: held at the charge limit, charging toward it, MENU > Charge Limit
shots('charge', [(PS1, ['--charging', '{t}', 'kept'], motion(0, 1600)),
                 (PS1, ['--charging', '{t}', 'limit'], motion(0, 1200))])
# MP4's lock screen
shots('lock', [(PS1, ['--nowplaying', '0', '0', '83', '--locked'], 1800)])
# Nintendo DS: the in-game menu's screen rows (its Display tab), and N64's shelf
shots('ds', [(('NDS', 'Mario Kart DS (USA).nds'), ['--screen', 'games', '--gamemenu', str(r), 'dual', '--gm-tab', '1',
                                                   '--backdrop', '{art}/Nintendo_-_Nintendo_DS/Named_Snaps/Mario Kart DS (USA) (En,Fr,De,Es,It).png'], 500)
             for r in (2, 3, 4)] +
     [(('NDS', 'Mario Kart DS (USA).nds'), ['--screen', 'games', '--popup', 'dslang',
                                           '--backdrop', '{art}/Nintendo_-_Nintendo_DS/Named_Snaps/Mario Kart DS (USA) (En,Fr,De,Es,It).png'], 1400)])
shots('n64', [(('N64', 'Super Mario 64 (USA).z64'), ['--screen', 'games'], 1500)])
# Light and dark, the same home; MENU in each of the eight languages
shots('home-light', [(PS1, ['--home', 'spotlight', '--phase', '3200'], 100)])
shots('home-dark', [(PS1, ['--home', 'spotlight', '--phase', '3200', '--theme', 'dark'], 100)])
shots('shelf-dark', [(PS1, ['--screen', 'games', '--theme', 'dark'], 100)])
# v3: the power-off animation, and a video snap: the bar under the resting
# cover fills (DESIGN.md 11.19), then the cover turns into the snap. The host
# has no Muse to decode it, so rec/snapsub.c gives --snap's turn a frame of
# Spyro's snap (its libretro screenshot) in place of the test picture. It
# rests 600 ms first, so the video can start the wait where its beat needs.
shots('poweroff', [(PS1, ['--screen', 'games', '--poweroff', '--shot-at', '{t}'], motion(0, 1600))])
# The snap: SNAP_SOURCE, a video snap (.mp4, as ScreenScraper's on a card) or a
# picture, of SNAP_GAME ("TAG|file on the card"), Spyro's libretro snap clip
# by default. Its frames go to snapsub.dylib one a shot, at its own rate.
SNAP_GAME = tuple(os.environ.get('SNAP_GAME', 'PS1|Spyro the Dragon (USA).chd').split('|'))
SNAP_SOURCE = os.environ.get('SNAP_SOURCE', str(cardlib.ART_CACHE / 'Sony_-_PlayStation/Videos/Spyro the Dragon (USA).mp4'))
SNAP_FPS = 30
# Where in the snap the beat starts, in seconds: a lively stretch of a long clip
SNAP_START = os.environ.get('SNAP_START', '0')
SNAP_ENV = {'SNAP_I420': '{work}/snap.yuv', 'DYLD_INSERT_LIBRARIES': str(ROOT / 'video/rec/snapsub.dylib')}
shots('snap', [(SNAP_GAME, ['--screen', 'games', '--snap-wait', '0'], 600),
               (SNAP_GAME, ['--screen', 'games', '--snap-wait', '{t}'], [f'{t / 1000:.4f}' for t in motion(0, 1000)]),
               (SNAP_GAME, ['--screen', 'games', '--snap', '{t}', '--snap-frame', '{i}'], [f'{t / 300:.4f}' for t in motion(0, 300)]),
               (SNAP_GAME, ['--screen', 'games', '--snap', '1', '--snap-frame', '{i}'], motion(300, 3000))],
      card='snaps', env=SNAP_ENV)

# ---- 1.0.5: the new systems and features -----------------------------------
# The four new systems on the home, each console's own start (--phase is the
# home's clock since it was focused): PSP, Doom, ColecoVision, TIC-80
shots('newsys', [(sys_game, ['--home', 'spotlight', '--phase', '{t}'], motion(0, 1540))
                 for sys_game in [('PSP', 'LocoRoco (USA).iso'), ('DOOM', 'Doom.wad'),
                                  ('COLECO', 'Donkey Kong (USA).col'), ('TIC80', '8 Bit Panda.tic')]])
# Folders on the Game Boy Advance shelf: in from the home on the first folder,
# into it and along it, back up; then START for the list, down it, START for
# the covers again
live('folders', ('GBA', 'Commercial Owned'),
     ['a', 700, 'a', 800, 'right', 500, 'b', 900, 'start', 1100, 'down', 350, 'down', 700, 'start', 1100])
# MENU in its tabs: R1 round them and back to Display, Appearance to Dark, then
# Display > Theme and its Accent stepped round, the whole screen in it
live('look', PS1,
     ['menu', 500, 'r1', 400, 'r1', 400, 'l1', 500, 'right', 800, 'down', 'down', 300, 'a', 700,
      'right', 450, 'right', 450, 'right', 900], gap=300)
# NES Player 2 over Super Mario Bros. 3: the in-game menu down to Player 2, the
# hotspot starting, its code waiting, and the friend's phone in
NES_SNAP = '{art}/Nintendo_-_Nintendo_Entertainment_System/Named_Snaps/Super Mario Bros. 3 (USA).png'
NES = ('NES', 'Super Mario Bros. 3 (USA).nes')
shots('player2', [(NES, ['--screen', 'games', '--gamemenu', str(r), '--player2', 'off', '--backdrop', NES_SNAP], 200)
                  for r in range(5)] +
      [(NES, ['--screen', 'games', '--player2', 'starting', '--backdrop', NES_SNAP], 600),
       (NES, ['--screen', 'games', '--player2', 'hotspot', '--backdrop', NES_SNAP], 1600),
       (NES, ['--screen', 'games', '--player2', 'connected', '--backdrop', NES_SNAP], 3300)])


# ---- 1.0.5's last features: the in-game menu's tabs, the Scraper page -------
# The in-game menu over Metal Gear Solid, a game on two discs, in its tabs
# (Game, Display, Controls, Achievements), a tab's motion and the Controls
# tab's presses drawn by the launcher a sixtieth of a second at a time
# (--gm-tab-from, --gm-press with --shot-at). A segment's fourth item is set
# on the card before each of its shots: settings, and the button remaps
# cleared, since every press a shot makes is kept, as the handheld keeps it.
MGS_GM = ['--screen', 'games', '--backdrop', '{card}/media-art/c6.png']
REMAP = {'remap': True}


def scanlines(pct):
    return {'settings': {'shader.PS1': 'scanlines', 'shader_strength.PS1': pct}}


def shader(name):
    return {'settings': {'shader.PS1': name}}


# Disc swapping: the Game tab down to its Disc row, the first disc in the
# tray, then the second. The shot puts disc 2 in the tray, so the first's
# value is given in the row's own words.
shots('gm-disc', [(PS1, MGS_GM + ['--gamemenu', str(r), '--gm-tab', '0', '--gm-discs', '2', 'Disc 1 of 2'], ms)
                  for r, ms in ((0, 450), (1, 200), (2, 200), (3, 200), (4, 1000))] +
      [(PS1, MGS_GM + ['--gamemenu', '4', '--gm-tab', '0', '--gm-discs', '2'], 1500)])
# R1 to Display, the Shader row stepped to Scanlines (the Strength row comes
# with the first old screen), then Strength down from 100% to 40%
shots('gm-strength', [(PS1, MGS_GM + ['--gamemenu', '1', '--gm-tab', '1', '--gm-tab-from', '0', '--shot-at', '{t}'],
                       motion(0, 500), shader('nearest'))] +
      [(PS1, MGS_GM + ['--gamemenu', '1', '--gm-tab', '1'], ms, shader(name))
       for name, ms in (('shimmerless', 240), ('crt', 240), ('crt-curved', 240), ('scanlines', 500))] +
      [(PS1, MGS_GM + ['--gamemenu', '2', '--gm-tab', '1'], 1100 if pct == 40 else 200 if pct < 100 else 450, scanlines(pct))
       for pct in range(100, 30, -10)])
# Button remapping: R1 to Controls, the handheld coming in beside its rows;
# down them, the handheld leaning toward each button; Left twice on Y, which
# swaps its button with the one that had it; down to L1, and it tilts back
shots('gm-remap', [(PS1, MGS_GM + ['--gamemenu', '0', '--gm-tab', '2', '--gm-tab-from', '1', '--shot-at', '{t}'],
                    motion(0, 700), REMAP)] +
      [(PS1, MGS_GM + ['--gamemenu', str(r), '--gm-tab', '2', '--gm-press', 'down', '--shot-at', '{t}'], motion(0, 340), REMAP)
       for r in range(5)] +
      [(PS1, MGS_GM + ['--gamemenu', '5', '--gm-tab', '2', '--gm-press', press, '--shot-at', '{t}'], motion(0, ms), REMAP)
       for press, ms in (('left', 650), ('left,left', 750), ('left,left,down', 1000))] +
      [(PS1, MGS_GM + ['--gamemenu', '5', '--gm-tab', '2', '--gm-press', 'left,left,down', '--shot-at', '2000'], 900, REMAP)])
# The Achievements tab: R1 from Controls, the game's list with two unlocks
# waiting to be sent, and down it to one counting its progress
ACH = ['--achtab', 'list', '--cheevos-waiting', '2', '--cheevos-badge', '{card}/Roms/PlayStation/.cheevos/Hagane.set', 'b#']
shots('gm-cheevos', [(PS1, MGS_GM + ['--gamemenu', '0', '--gm-tab', '3', '--gm-tab-from', '2', '--shot-at', '{t}'] + ACH,
                      motion(0, 600), REMAP)] +
      [(PS1, MGS_GM + ['--gamemenu', str(r), '--gm-tab', '3'] + ACH, ms) for r, ms in ((1, 300), (2, 300), (3, 300), (4, 1800))])
# MENU > Scraper as one page: the plan, the Systems grid, a run with the
# covers landing (one each 1.1 s, the shot's run clock) and done. The page's
# own stand-in systems, with covers of the card's games.
SCRAPED = [('NES', 'Super Mario Bros. 3 (USA)', 0), ('SNES', 'Super Metroid (USA)', 1),
           ('Game Boy', "The Legend of Zelda - Link's Awakening (USA, Europe)", 2),
           ('Game Boy Advance', 'Metroid Fusion (USA)', 3), ('Genesis', 'Sonic The Hedgehog 2 (USA)', 4),
           ('PlayStation', 'Castlevania - Symphony of the Night (USA)', 5), ('Nintendo 64', 'Super Mario 64 (USA)', 6)]
shots('scraper-page', [(PS1, ['--scraper-page', '1'], 1000), (PS1, ['--scraper-page', '3'], 900),
                       (PS1, ['--scraper-page', '7', '{t}'], motion(0, 7600)), (PS1, ['--scraper-page', '8'], 1600)],
      env={'ZYPEROS_LOOK_COVERS': ':'.join(f'{{card}}/Roms/{f}/.media/{n}.png' for f, n, _ in SCRAPED),
           'ZYPEROS_LOOK_SYS': ','.join(str(k) for *_, k in SCRAPED)})
# Internet from an Android phone (USB tethering): Phone in "What did you plug
# in?", the phone's step, and USB File Transfer in the phone's browser
shots('phone', [(PS1, ['--usb-power', str(k)], ms) for k, ms in ((0, 350), (1, 350), (2, 700))] +
      [(PS1, ['--usb-power', 'phone'], 1900), (PS1, ['--hare-steps', 'phone', 'browser'], 1600)])


# ---- the 1.0.5 Reddit cut (src/reddit) ---------------------------------------
# The screensaver: the snap's game on its shelf, then its video snap fading up
# over the whole screen with its name. --saver draws one picture, so it is
# given the snap a frame at a time (SAVER_FRAMES of them, made from
# SNAP_SOURCE at its own rate in main()), each held a thirtieth of a second.
SAVER_FRAMES = 100
shots('saver', [(SNAP_GAME, ['--screen', 'games'], 700)] +
      [(SNAP_GAME, ['--saver', '--saver-alpha', f'{min(1, i / 15):.3f}', '--saver-pic', f'{{work}}/saver-pics/{i:04d}.png'],
        1000 / SNAP_FPS) for i in range(SAVER_FRAMES)], card='snaps')

# Rename: Freedoom's WAD on the Doom shelf, named as its file, X > Rename, the
# old name deleted (B) and the new one typed, the focus on each key it types
# and X for the capitals (src/keyboard.c's layers; the action row is row 4)
KB_ROWS = ['1234567890', 'qwertyuiop', 'asdfghjkl-', 'zxcvbnm_.@']


def typing(old, new, ms=80):
    """The keyboard's pictures from `old` to `new`: (text, layer, row, col, ms)"""
    steps, row, col, layer = [(old, 0, 1, 0, 500)], 1, 0, 0
    for n in range(len(old) - 1, -1, -1):
        steps.append((old[:n], 0, row, col, ms * 0.7))
    text = ''
    for c in new:
        want = 1 if c.isupper() else 0
        if want != layer:
            layer = want
            steps.append((text, layer, row, col, ms))
        if c == ' ':
            row, col = 4, 2
        else:
            row = next(r for r, keys in enumerate(KB_ROWS) if c.lower() in keys)
            col = KB_ROWS[row].index(c.lower())
        text += c
        steps.append((text, layer, row, col, ms))
    steps.append((text, layer, 4, 4, 500))
    return steps


DOOM_FREE = ('DOOM', 'freedoom1.wad')
RENAMED = 'Freedoom Phase 1'
shots('rename', [(DOOM_FREE, ['--screen', 'games'], 500), (DOOM_FREE, ['--screen', 'games', '--info-rename'], 700)] +
      [(DOOM_FREE, ['--screen', 'games', '--keyboard', str(layer), text, 'Rename', '--kbsel', str(r), str(c)], ms)
       for text, layer, r, c, ms in typing('freedoom1', RENAMED)])
# ...and the shelf after, on a card where the file has the new name
shots('renamed', [(('DOOM', RENAMED + '.wad'), ['--screen', 'games'], 1500)], card='renamed')

# 1.0.4 and 1.0.3, for the same cut. A computer on the cable as a release has
# it (plain): Charge Only, File Transfer, Flash Drive in focus, then Flash
# Drive's screen as a copy starts, runs one way and both ways
shots('flashdrive', [(PS1, ['--usb-ask', str(k), 'plain', 'drive'], ms) for k, ms in ((0, 400), (1, 400), (2, 600))] +
      [(PS1, ['--usb-drive', '0', v], ms) for v, ms in (('idle', 500), ('in', 800), ('both', 1300))])
# USB File Transfer at zyperos.local, step by step: the computer taking the
# handheld, the loading bar, the address, the browser, then the page open
shots('usbsteps', [(PS1, ['--hare-steps', 'usb', st], ms) for st, ms in
                   (('taking', 600), ('opening', 600), ('address', 700), ('browser', 800))] +
      [(PS1, ['--hare', 'http://10.42.0.1', '1 connected', 'Super Metroid (USA).sfc'], 1300)])
# System Update in the background: Download on the update's page, then the
# shelf with the download's mark filling at the top
shots('update-bg', [(PS1, ['--update'], 900)] +
      [(PS1, ['--screen', 'games', '--status', f'10:08 24h batt=82 update={p}'], 60) for p in range(3, 100, 3)])
# Charge to 100% this once: the charging screen held at the limit, Y's hint,
# then charging to full this one time
shots('charge-full', [(PS1, ['--charging', '{t}', 'kept'], motion(0, 1400)),
                      (PS1, ['--charging', '{t}', 'tofull'], motion(0, 1800))])

for code in ['en', 'zh-Hans', 'es', 'pt-BR', 'fr', 'de', 'ja', 'it']:
    shots(f'lang-{code}', [(PS1, ['--lang', code, '--menu', 'Language'], 100)])


# ---- running them --------------------------------------------------------------
def write_last(card, tag, game):
    (card / 'ZyperOS/.last').write_text(f'{tag}\n{game}\n')


# MENU > Display > Theme as it ships: a live take that changes it leaves it on the card
LOOK = {'look.accent': 'lime', 'look.light': 'warm', 'look.dark': 'black', 'look.corners': 'rounded', 'look.zyper': 'on'}


def run_live(name, spec, card):
    write_last(card, *spec['last'])
    cardlib.device_settings(card, BINARY, **{'tour.done': 1, 'clock': '24h', 'home': 'spotlight',
                                            'appearance': 'light', 'language': 'en', **LOOK, **spec['settings']})
    script = WORK / f'{name}.keys'
    lines = [f'{t} {k} {h}' for t, k, h in spec['script']] + [f"{spec['end']} end"]
    script.write_text('# made by video/footage/takes.py\n' + '\n'.join(lines) + '\n')
    (WORK / 'batt').write_text('82\n')
    env = cardlib.env_for(card)
    env.update(REC_OUT=str(OUT / f'{name}.mp4'), REC_SCRIPT=str(script), REC_CLOCK=CLOCK,
               REC_BATT=str(WORK / 'batt'), REC_MENU_MUSIC='1', DYLD_INSERT_LIBRARIES=str(REC))
    log = WORK / f'{name}.log'
    with open(log, 'w') as f:
        r = subprocess.run([str(BINARY), '--status', STATUS], cwd=ROOT, env=env,
                           stdout=f, stderr=subprocess.STDOUT, timeout=600)
    if r.returncode:
        raise RuntimeError(f'{name}: exit {r.returncode}, see {log}')
    (OUT / f'{name}.json').write_text(json.dumps({'fps': FPS, 'ms': spec['end'],
                                                  'presses': spec['script']}) + '\n')


def clear_kept(card):
    """No shader, Strength or button remap a take before left on the card"""
    with sqlite3.connect(card / 'userdata/zyperos.db') as db:
        db.execute("DELETE FROM settings WHERE key LIKE 'shader%'")
    with sqlite3.connect(card / 'shared/.zyperos/library.db') as db:
        db.execute("DELETE FROM settings WHERE key LIKE 'map.%' OR key LIKE 'stick.%'")


def set_kept(card, pre):
    """A segment's settings, and its button remaps cleared, before a shot"""
    if pre.get('remap'):
        with sqlite3.connect(card / 'shared/.zyperos/library.db') as db:
            db.execute("DELETE FROM settings WHERE key LIKE 'map.%' OR key LIKE 'stick.%'")
    if pre.get('settings'):
        with sqlite3.connect(card / 'userdata/zyperos.db') as db:
            for k, v in pre['settings'].items():
                db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)', (k, str(v)))


def run_shots(name, spec, cards):
    card = cards[spec['card']]
    # A live take before may have left another language or theme on the card
    cardlib.device_settings(card, BINARY, **{'language': 'en', 'appearance': 'light', 'home': 'spotlight', **LOOK})
    frames = WORK / name
    if frames.exists():
        shutil.rmtree(frames)
    frames.mkdir(parents=True)
    listing, total, n = [], 0, 0
    # The first shot that shows the snap, for its frames' clock
    first_turn = sum(len(w) if isinstance(w, list) else 1 for _, ar, w, *_ in spec['segments']
                     if '--snap-frame' not in ar) if any('--snap-frame' in ar for _, ar, *_ in spec['segments']) else 0
    env = cardlib.env_for(card)
    env.update({k: v.replace('{work}', str(WORK)).replace('{card}', str(card)) for k, v in spec['env'].items()})
    # No shader, Strength or button remap left by a take before
    clear_kept(card)
    for last, args, when, *pre in spec['segments']:
        write_last(card, *last)
        times = when if isinstance(when, list) else [None]
        for t in times:
            if pre:
                set_kept(card, pre[0])
            a = [x.replace('{t}', str(t)).replace('{card}', str(card)).replace('{art}', str(cardlib.ART_CACHE))
                 .replace('{work}', str(WORK)) for x in args]
            # --snap-frame {i}: the snap's frame for this shot (snapsub.dylib),
            # counted from the turn's start at the snap's own rate
            if '--snap-frame' in a:
                k = a.index('--snap-frame')
                ms = (n - first_turn) * 1000 / FPS
                env['SNAP_FRAME'] = str(int(ms * SNAP_FPS / 1000))
                del a[k:k + 2]
            elif 'SNAP_FRAME' in env:
                del env['SNAP_FRAME']
            status = [] if '--status' in a or '--tour' in a or '--charging' in a else ['--status', SHOT_STATUS]
            png = frames / f'{n:05d}.png'
            theme = [] if '--theme' in a else ['--theme', 'light']
            r = subprocess.run([str(BINARY), '--shot', str(png), *theme, '--fn-volume', *status, *a],
                               cwd=ROOT, env=env, capture_output=True, text=True)
            if not png.is_file():
                raise RuntimeError(f'{name}: {a}: {r.stderr[-800:]}')
            ms = 1000 / FPS if t is not None else when
            listing.append((png, ms))
            total += ms
            n += 1
    # The concat demuxer's durations, the last picture given twice so its own counts
    lst = frames / 'list.txt'
    lst.write_text(''.join(f"file '{p}'\nduration {ms / 1000:.6f}\n" for p, ms in listing) +
                   f"file '{listing[-1][0]}'\n")
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'concat', '-safe', '0', '-i', str(lst),
                    '-vf', f'fps={FPS},scale=out_color_matrix=bt709:out_range=tv', '-c:v', 'libx264', '-preset', 'fast',
                    '-crf', '10', '-pix_fmt', 'yuv420p', '-color_range', 'tv', '-colorspace', 'bt709',
                    '-color_primaries', 'bt709', '-color_trc', 'bt709', '-movflags', '+faststart',
                    str(OUT / f'{name}.mp4')], check=True)
    (OUT / f'{name}.json').write_text(json.dumps({'fps': FPS, 'ms': round(total)}) + '\n')
    # A take of one picture is used as a picture too
    if n == 1:
        shutil.copyfile(frames / '00000.png', OUT / f'{name}.png')
    shutil.rmtree(frames)


def build_rec():
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl2'], text=True).split()
    for name in ('rec', 'snapsub'):
        src, lib = REC.parent / f'{name}.c', REC.parent / f'{name}.dylib'
        if not lib.is_file() or lib.stat().st_mtime < src.stat().st_mtime:
            subprocess.run(['cc', '-dynamiclib', '-O2', '-Wall', '-Wextra', '-o', str(lib), str(src), *flags], check=True)


def main():
    if not BINARY.is_file():
        sys.exit('run make native first')
    build_rec()
    OUT.mkdir(parents=True, exist_ok=True)
    WORK.mkdir(parents=True, exist_ok=True)
    only = sys.argv[1:]
    unknown = [t for t in only if t not in TAKES]
    if unknown:
        sys.exit(f'no take called {", ".join(unknown)}; there are {", ".join(TAKES)}')
    cards = {'demo': cardlib.build(WORK / 'card')}
    cardlib.device_settings(cards['demo'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    # The demonstration card with the snap's game holding it in its .media,
    # for the play mark and the bar, and the snap's frames for snapsub.dylib
    cards['snaps'] = cardlib.build(WORK / 'card-snaps')
    cardlib.device_settings(cards['snaps'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    folder = next(line.split('|')[2] for line in (ROOT / 'config/systems.cfg').read_text().splitlines()
                  if line.startswith('sys|') and line.split('|')[4] == SNAP_GAME[0])
    stem = Path(SNAP_GAME[1]).stem
    media = cards['snaps'] / 'Roms' / folder / '.media' / (stem + '.mp4')
    if SNAP_SOURCE.endswith('.mp4'):
        shutil.copyfile(SNAP_SOURCE, media)
    else:
        media.touch()
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', SNAP_START, '-i', SNAP_SOURCE, '-vf', f'fps={SNAP_FPS},scale=320:240',
                    '-pix_fmt', 'yuv420p', '-f', 'rawvideo', str(WORK / 'snap.yuv')], check=True)
    # The screensaver's pictures: the same snap, from the same moment, a file a frame
    saver = WORK / 'saver-pics'
    if saver.exists():
        shutil.rmtree(saver)
    saver.mkdir()
    subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-ss', SNAP_START, '-i', SNAP_SOURCE, '-vf', f'fps={SNAP_FPS}',
                    '-frames:v', str(SAVER_FRAMES), '-start_number', '0', str(saver / '%04d.png')], check=True)
    # The demonstration card with Freedoom renamed, for the shelf after Rename
    if not only or 'renamed' in only:
        cards['renamed'] = cardlib.build(WORK / 'card-renamed')
        doom = cards['renamed'] / 'Roms/Doom'
        (doom / DOOM_FREE[1]).rename(doom / (RENAMED + '.wad'))
        cardlib.device_settings(cards['renamed'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    cards['portdocs'] = WORK / 'card-portdocs'
    cards['empty'] = WORK / 'card-empty'
    for extra, make in (('portdocs', cardlib.rs.portdocs_card), ('empty', cardlib.rs.card_dirs)):
        if cards[extra].exists():
            shutil.rmtree(cards[extra])
        make(cards[extra])
        cardlib.device_settings(cards[extra], BINARY, **{'tour.done': 1, 'clock': '24h'})
    for name, spec in TAKES.items():
        if only and name not in only:
            continue
        if spec['kind'] == 'live':
            run_live(name, spec, cards['demo'])
        else:
            run_shots(name, spec, cards)
        print(name, flush=True)
    # Every take's length and presses in one file, for the video
    index = {p.stem: json.loads(p.read_text()) for p in sorted(OUT.glob('*.json')) if p.stem != 'index'}
    (OUT / 'index.json').write_text(json.dumps(index) + '\n')


if __name__ == '__main__':
    main()
