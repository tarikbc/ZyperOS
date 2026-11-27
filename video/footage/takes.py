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


# The handheld a take wears (src/device/profile.h): the host build draws any
# of them, and wears the desktop's own without --device, so every take names
# one, the Pixel 2's unless it says another (1.0.6's Brick takes)
DEVICE = 'pixel2'


def live(name, last, steps, tail=900, settings=None, start=LEAD, gap=420, card='demo', device=DEVICE):
    script, t = keys(steps, start=start, gap=gap)
    TAKES[name] = dict(kind='live', last=last, settings=settings or {}, script=script, end=t + tail,
                       card=card, device=device)


def shots(name, segments, card='demo', env=None, device=DEVICE):
    """segments: (last, args, hold ms) a still, or (last, args with {t}, [ms, ...]) a motion,
    and a fourth item for what to set on the card before each shot (run_shots)"""
    TAKES[name] = dict(kind='shots', card=card, segments=segments, env=env or {}, device=device)


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
# has no MP4 to decode it, so rec/snapsub.c gives --snap's turn a frame of
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

# ---- 1.0.6: the video for the Discord (src/v106) ----------------------------
# On the fuller card (card.py's MORE), the Pixel 2's unless a take says the
# Brick's. The Brick's profile draws its own keys and its 1024x768 screen.
SFC = ('SFC', 'Chrono Trigger (USA).sfc')
SG = ('SG1000', 'Flicky (Japan).sg')
GB = ('GB', "The Legend of Zelda - Link's Awakening (USA, Europe).gb")
GB_SNAP = "{art}/Nintendo_-_Game_Boy/Named_Snaps/Legend of Zelda, The - Link's Awakening (USA, Europe).png"
GBA6 = ('GBA', 'Metroid Fusion (USA).gba')
GBA_SNAP = '{art}/Nintendo_-_Game_Boy_Advance/Named_Snaps/Metroid Fusion (USA).png'
SONIC_GM = ['--screen', 'games', '--backdrop', SONIC_SNAP]
# The home moving between systems, on each handheld
live('home6', PS1, start=700, steps=[1500, 'right', 800, 'right', 800, 'left', 700, 'left', 700, 'left', 1400], card='more')
# (the Brick's home has no N64, PSP or Dreamcast yet: two to the right of
# the PlayStation is the SNES)
live('brick-home', PS1, start=700, steps=[1500, 'right', 800, 'right', 1000, 'a', 1300, 'right', 600,
                                          'right', 600, 'right', 1400], card='more', device='brick')
# MENU > View > Grid: into the SNES shelf (16 games), START from Covers to
# Grid, along a row, down a row and a page, back, START again for the List
GRID_KEYS = ['a', 1000, 'start', 1400, 'right', 450, 'right', 450, 'down', 700, 'down', 1100, 'down', 1000, 'up', 900, 'start', 1400]
live('grid', SFC, GRID_KEYS, card='more')
live('brick-grid', SFC, GRID_KEYS, card='more', device='brick')
# The home's layouts, START after START, to the Grid: 18 systems at once
live('homegrid', PS1, ['start', 1400, 'start', 1600, 'right', 450, 'right', 450, 'down', 700, 'left', 450, 1300], card='more')
# The SG-1000's shelf: a card for each game with no art, the light crossing
# the one in focus
live('namecard', SG, ['a', 1300, 'right', 1100, 'right', 1300], card='more')
# MENU > Display > Home Systems: down its rows, Dreamcast turned off
shots('homesys', [(PS1, ['--homesys-page', str(r)], ms) for r, ms in ((0, 500), (1, 300), (2, 260), (3, 260), (4, 600))] +
      [(PS1, ['--homesys-page', '4'], 1400, {'settings': {'home.hidden': 'DC'}})], card='more')
# Screen Colors: its page, then the Look's list, each look under the cursor
shots('screencolors', [(PS1, ['--look-page', 'screen', '0'], 700)] +
      [(PS1, ['--look-page', 'screen-looks', '0', '--pick-at', str(k)], 480 if k < 5 else 900) for k in range(6)], card='more')
# Night Mode's page, then Night Warmth's list stepped warmer
shots('night', [(PS1, ['--look-page', 'night', str(r)], 400, {'settings': {'night.mode': 'hours'}}) for r in range(2)] +
      [(PS1, ['--look-page', 'night-list', '1', '--pick-at', str(k)], 260 if k < 9 else 900, {'settings': {'night.mode': 'hours'}})
       for k in range(3, 10)], card='more')
# Theme's Accent list repainting the whole screen, then the dark background's
# list showing the dark theme while it is open
shots('hover', [(PS1, ['--look-page', 'theme', '0'], 500)] +
      [(PS1, ['--look-page', 'theme-list', '0', '--pick-at', str(k)], 330) for k in range(1, 10)] +
      [(PS1, ['--look-page', 'theme-list', '2', '--pick-at', str(k)], 450) for k in range(0, 4)], card='more')
# Folder Color: its list, each choice coloring the shelf's folders beside it
shots('foldercolor', [(PS1, ['--look-page', 'theme', '3'], 500)] +
      [(PS1, ['--look-page', 'theme-list', '3', '--pick-at', str(k)], 330) for k in range(0, 11)], card='more')
# Undo Load: the Game tab down to it, under Load
shots('undo', [(GEN, SONIC_GM + ['--gamemenu', str(r), '--gm-undo'], ms) for r, ms in ((0, 400), (1, 260), (2, 400), (3, 1800))],
      card='more')
# On-Screen Stats: its page over the game, down its rows
shots('stats', [(GEN, SONIC_GM + ['--gamemenu', '1', '--hud-page', str(r), '--hud-shows', 'fps,temp,batt,time'], ms)
                for r, ms in ((0, 600), (1, 400), (2, 400), (3, 400), (4, 400), (5, 400), (6, 800))], card='more')
# The Display list: each mode on the game's own picture, with what it does
shots('dmode', [(GEN, SONIC_GM + ['--gamemenu', '0', '--gm-tab', '1', '--gm-choose', '--pick-at', str(k)], 480 if k < 5 else 900)
                for k in range(6)], card='more')
# LCD No Backlight: a Game Boy game's Shader list, down to it
shots('lcd', [(GB, ['--screen', 'games', '--backdrop', GB_SNAP, '--gamemenu', '1', '--gm-tab', '1', '--gm-choose', '--pick-at', str(k)],
               300 if k < 6 else 1800) for k in range(7)], card='more')
# Timing: the list on the GBA shelf, Smooth's demo and Exact's, moving
shots('timing', [(GBA6, ['--screen', 'games', '--menu', 'Timing', '--press', 'a', '--pick-at', str(k), '--shot-at', '{t}'],
                  motion(0, 1800)) for k in (0, 1)], card='more')
# MENU > Achievements: the plan, a run, done
shots('radl', [(PS1, ['--radl-page', '0'], 1100), (PS1, ['--radl-page', '4'], 1500), (PS1, ['--radl-page', '8'], 1300)],
      card='more', env={'ZYPEROS_LOOK_BADGES': '{card}/Roms/PlayStation/.cheevos'})
# MENU's Shader, ZyperOS's own screens': its list showing each one on the
# whole screen, then the home on CRT Curved
shots('lshader', [(PS1, ['--menu', 'Shader', '--press', 'a', '--pick-at', str(k)], 450) for k in range(5)] +
      [(PS1, ['--home', 'spotlight', '--phase', '{t}'], motion(2000, 3400), {'settings': {'launcher_shader': 'crt-curved'}})],
      card='more')
# A port's loading screen, PortMaster's last line at its foot, then a minute on
shots('portline', [(('PRT', 'Star Farm.sh'), ['--portload', 'Star Farm', '--shot-at', '{t}'], motion(0, 2200)),
                   (('PRT', 'Star Farm.sh'), ['--portload-later', '--portload', 'Star Farm'], 1600)], card='portdocs')
# MENU > Power Button on Hold: POWER held, the bar filling for a second, and off
shots('powerhold', [(PS1, ['--home', 'spotlight', '--phase', '3200', '--osd', 'power', str(k)], 50) for k in range(0, 21)] +
      [(PS1, ['--screen', 'games', '--poweroff', '--shot-at', '{t}'], motion(0, 1500))], card='more')
# The handheld while a computer's browser is on File Transfer (the page's
# pictures are footage/portal-pages.py's)
shots('xfer-hand', [(PS1, ['--hare', 'http://192.168.1.42', '1 connected'], 9300)], card='more')
# The PSP's home, its console's own start, beside the numbers
shots('psp-home', [(('PSP', 'God of War - Chains of Olympus (USA).iso'), ['--home', 'spotlight', '--phase', '{t}'],
                    motion(0, 6300))], card='more')


# ---- 1.1: the Discord video and the Reddit cut (src/v110, src/reddit110) ----
# On card.py's v110 card: the fuller card, and your PICO-8's place, carts,
# Collections, Screenshots, Recents with their last screens, and a game's
# cheats. The Pixel 2's unless a take says the Brick's.
V11 = 'v110'
SOTN = ('PS1', 'Castlevania - Symphony of the Night (USA).chd')
SOTN_SNAP = '{art}/Sony_-_PlayStation/Named_Snaps/Castlevania - Symphony of the Night (USA).png'
THPS2 = ('PS1', "Tony Hawk's Pro Skater 2 (USA).chd")
THPS2_SNAP = "{art}/Sony_-_PlayStation/Named_Snaps/Tony Hawk's Pro Skater 2 (USA).png"
GB_KEPT = {'display.GB': 'integer', 'display_source.GB': '160x144', 'display_shape.GB': '1.11111'}
P8 = ('PICO8', 'Celeste Classic.p8')
# The home moving between systems, on each handheld, still on the first
# system until after Zyper's pop-up (the posters' frame)
live('v11-home', ('PICO8', 'Splore.splore'), [900, 'right', 800, 'right', 800, 'left', 700, 'left', 700, 'left', 1400],
     card=V11)
live('v11-brick-home', PS1, [900, 'right', 800, 'right', 800, 'right', 1000, 'a', 1300, 'right', 600, 'right', 600, 'right', 1400],
     card=V11, device='brick')
# Grid view on the SNES shelf, on each (1.0.6's, for the Reddit cut and the pair)
live('v11-grid', SFC, GRID_KEYS, card=V11)
live('v11-brick-grid', SFC, GRID_KEYS, card=V11, device='brick')
# Your PICO-8: the home on its shelf, in, Splore first, then along the carts
live('v11-p8', ('PICO8', 'Splore.splore'), ['a', 1300, 'right', 900, 'right', 700, 'right', 700, 'right', 1200],
     card=V11)
# Collections: the home on its group, in, along the lists, into one and along it
live('v11-coll', ('COLL', 'Couch Co-op'), ['a', 1200, 'right', 900, 'a', 1200, 'right', 600, 'right', 600, 'right', 1200],
     card=V11)
# Screenshots: the home on its group, in, A for the newest on the whole
# screen, Right and Right through them, R1 to another game's
live('v11-shots', ('SHOT', ''), ['a', 1200, 'a', 1400, 'right', 1000, 'right', 1000, 'r1', 1400], card=V11)
# Louder than the top, over MP4 playing (the boost opens in MP4, games and
# ports, not over the menus alone): the level at the top, then the boost's
# cells, one each press, up to six
NP_SPEC = ['--nowplaying', '0', '0', '{t}', '--np-look', 'spectrum']
BOOST = [(17, 0, 450), (18, 0, 350), (19, 0, 350), (20, 0, 600)] + [(20, b, 450) for b in range(1, 6)] + [(20, 6, 1400)]
shots('v11-boost', [(('MUSE', ''), NP_SPEC + ['--osd', 'volume', str(v)] + (['+%d' % b] if b else []),
                     [f'{90 + t / 1000:.3f}' for t in motion(a, a + ms)])
                    for (v, b, ms), a in zip(BOOST, [sum(x[2] for x in BOOST[:k]) for k in range(len(BOOST))])],
      card=V11)
# MENU > Downloads: the plan, A and the run, its stream, all done
shots('v11-dl', [(PS1, ['--downloads', '0'], 1100), (PS1, ['--downloads', '1'], 800),
                 (PS1, ['--downloads', '14'], 1700), (PS1, ['--downloads', '17'], 1600)], card=V11)
# Cheats: the Game tab's row, two on, then the game's list, down it
shots('v11-cheats', [(THPS2, ['--gamemenu', '4', '--cheats', '--backdrop', THPS2_SNAP], 900)] +
      [(THPS2, ['--gamemenu', '0', '--cheats-page', str(r), '--backdrop', THPS2_SNAP], ms)
       for r, ms in ((0, 600), (1, 400), (2, 400), (3, 400), (4, 1200))], card=V11)
# MENU > System > Hotkeys: down to Screenshot, its capture with SELECT and Y down
shots('v11-hotkeys', [(PS1, ['--hotkeys', str(r)], ms) for r, ms in ((0, 500), (1, 250), (2, 250), (3, 250), (4, 700))] +
      [(PS1, ['--hotkeys', '4', '--hk-capture', '1'], 600), (PS1, ['--hotkeys', '4', '--hk-capture', '2', '--hk-key', '3'], 1400)],
      card=V11)
# The Video view: Crash with no snap, its card whole, then Symphony of the
# Night's snap (Tarik's ScreenScraper clip, SNAP_SOURCE) on the whole screen
VVIEW_ENV = {'SNAP_I420': '{work}/snap.yuv', 'DYLD_INSERT_LIBRARIES': str(ROOT / 'video/rec/snapsub.dylib')}
shots('v11-vview', [(('PS1', 'Crash Bandicoot (USA).chd'), ['--screen', 'games', '--view', 'video'], 900),
                    (SOTN, ['--screen', 'games', '--view', 'video', '--snap', '1', '--snap-frame', '{i}'], motion(0, 4200))],
      card=V11, env=VVIEW_ENV)
# Recents with each game's last screen, the cursor along them
shots('v11-recents', [(('RECENT', f), ['--screen', 'games'], ms, {'settings': {'recents.picture': 'screen'}})
                      for f, ms in (('Super Metroid (USA).sfc', 1000), ('Metroid Fusion (USA).gba', 800),
                                    ('Castlevania - Symphony of the Night (USA).chd', 1300))], card=V11)
# MENU > Sound > Equalizer: the presets, each its curve, then five bands of your own
shots('v11-eq', [(PS1, ['--look-page', 'eq-presets', '0', '--pick-at', str(k)], 500 if k < 3 else 700) for k in range(4)] +
      [(PS1, ['--look-page', 'eq', '1'], 1500, {'settings': {'eq.speaker': 'custom', 'eq.speaker.bands': '4,1,-2,3,5'}})],
      card=V11)
# MP4's Player Looks, each playing: Cassette's reels, Vinyl's record and arm, Classic, Spectrum
shots('v11-looks', [(('MUSE', ''), ['--nowplaying', '0', '0', '{t}', '--np-look', look], [f'{83 + t / 1000:.3f}' for t in motion(a, a + ms)])
                    for look, a, ms in (('cassette', 0, 1500), ('vinyl', 1500, 1500), ('classic', 3000, 1000),
                                        ('spectrum', 4000, 1300))], card=V11)
# Border on a Game Boy game: its Display tab, the row, its list with each
# kind's picture, my drawing of the Game Boy round the game
GB_GM = ['--backdrop', GB_SNAP, '--gm-source', '160x144@1.11111']
shots('v11-border', [(GB, ['--gamemenu', str(r), '--gm-tab', '1'] + GB_GM, ms, {'settings': GB_KEPT})
                     for r, ms in ((0, 500), (1, 700))] +
      [(GB, ['--gamemenu', '1', '--gm-tab', '1', '--gm-choose', '--pick-en', w] + GB_GM, ms, {'settings': GB_KEPT})
       for w, ms in (('Off', 400), ('Blur', 500), ('Handheld', 1300), ('Handheld, Whole', 1300))], card=V11)
# Screen Colors in a game: the Display tab down to it, its page over the
# paused game, the Look list
SOTN_GM = ['--backdrop', SOTN_SNAP]
shots('v11-gmcolors', [(SOTN, ['--gamemenu', str(r), '--gm-tab', '1'] + SOTN_GM, ms) for r, ms in ((0, 400), (1, 300), (2, 700))] +
      [(SOTN, ['--gamemenu', '0', '--look-page', 'screen', '0'] + SOTN_GM, 700)] +
      [(SOTN, ['--gamemenu', '0', '--look-page', 'screen-looks', '0', '--pick-at', str(k)] + SOTN_GM, 420 if k < 4 else 900)
       for k in range(5)], card=V11)
# The game's file in its details: X on Final Fantasy VII, its File row
# sliding to the end of its whole name
FF7 = ('PS1', cardlib.FF7 + '.chd')
shots('v11-details', [(FF7, ['--screen', 'games'], 600),
                      (FF7, ['--screen', 'games', '--info', '--phase', '{t}'], motion(0, 3600))], card=V11)
# The in-game menu over your PICO-8: its Game tab, R1 to Display, down it
# (Border, Shader, its Strength and Screen Colors drawn over PICO-8)
P8_SET = {'settings': {'display.PICO8': 'integer', 'shader.PICO8': 'crt', 'shader_strength.PICO8': '70', 'border.PICO8': 'blur'}}
shots('v11-p8menu', [(P8, ['--screen', 'games', '--gamemenu', '0', 'port'], 800, P8_SET)] +
      [(P8, ['--screen', 'games', '--gamemenu', str(r), 'port', '--gm-tab', '1'], ms, P8_SET)
       for r, ms in ((0, 500), (1, 350), (2, 350), (3, 350), (4, 900))], card=V11)
# The PSP's home, its console's own start, beside the numbers
shots('v11-psp', [(('PSP', 'God of War - Chains of Olympus (USA).iso'), ['--home', 'spotlight', '--phase', '{t}'],
                   motion(0, 6300))], card=V11)
# New on the Brick: the consoles whose cores draw with its GPU (#446), each
# on its home with its console's own start: N64, Dreamcast, PSP (the card has
# no Saturn games)
shots('v11-brick-gpu', [(sys_game, ['--home', 'spotlight', '--phase', '{t}'], motion(0, ms))
                        for sys_game, ms in [(('N64', 'Super Mario 64 (USA).z64'), 1700),
                                             (('DC', 'Crazy Taxi (USA).chd'), 1700),
                                             (('PSP', 'God of War - Chains of Olympus (USA).iso'), 2400)]],
      card=V11, device='brick')
# New on the Brick: Screen Colors' looks, Night Mode's page, and a computer's
# cable asking what it is for, Flash Drive, and its screen
shots('v11-brick', [(PS1, ['--look-page', 'screen', '0'], 600)] +
      [(PS1, ['--look-page', 'screen-looks', '0', '--pick-at', str(k)], 380) for k in range(1, 5)] +
      [(PS1, ['--look-page', 'night', str(r)], ms, {'settings': {'night.mode': 'hours'}}) for r, ms in ((0, 600), (1, 700))] +
      [(PS1, ['--usb-ask', str(k), 'plain', 'drive'], ms) for k, ms in ((0, 400), (1, 400), (2, 700))] +
      [(PS1, ['--usb-drive', '0', v], ms) for v, ms in (('in', 700), ('both', 1300))], card=V11, device='brick')


for code in ['en', 'zh-Hans', 'es', 'pt-BR', 'fr', 'de', 'ja', 'it']:
    shots(f'lang-{code}', [(PS1, ['--lang', code, '--menu', 'Language'], 100)])


# ---- The Brick introduction (src/brick-intro) ----------------------------------
# Every scene on the TrimUI Brick alone: each take a copy of one above, the
# same card and presses, drawn on the Brick's profile. A take whose presses
# land elsewhere on the Brick's home gets a Brick-only live() of its own.
def brick(name):
    """TAKES['bi-NAME'] (without a 'v11-' prefix): TAKES[name] on the Brick"""
    new = 'bi-' + name.removeprefix('v11-')
    TAKES[new] = {**TAKES[name], 'device': 'brick'}
    return new


for _src in ['poweroff', 'homegrid', 'snap', 'v11-vview', 'v11-dl', 'quicksave', 'undo', 'gm-remap',
             'v11-hotkeys', 'v11-shots', 'v11-cheats', 'gm-cheevos', 'v11-coll', 'v11-recents', 'search',
             'wifi-xfer', 'v11-looks', 'v11-boost', 'v11-eq', 'look', 'firststart', 'folders']:
    brick(_src)
# System Update as a Brick on 1.0.6 saw 1.1.0 (card.py's brick_intro)
TAKES['bi-update'] = {**TAKES['update'], 'device': 'brick', 'card': 'bi'}
# Made as if the Brick has every feature the Pixel 2 has (Tarik, 2026-11-22:
# the video waits for them, so it shows them): the shader and border lists,
# the Ports shelf and PortMaster, your PICO-8 with Splore, Player 2, the
# charge limit, and the Rumble and lights pages, each on the Brick's profile
for _src in ['lshader', 'v11-border', 'ports', 'portline', 'v11-p8', 'player2', 'charge']:
    brick(_src)
# The Brick's profile has no Rumble row yet (draft #452): the Rumble list as
# the Pixel 2's profile draws it, until the Brick's lands. The Brick's lights
# page does not exist yet, so the video does not show one.
# Only the Rumble list itself: its rows and lines are the same on every
# handheld, where the Sound tab around it is the Pixel 2's (its mono speaker)
shots('bi-rumble', [(PS1, ['--menu', 'Rumble', '--press', 'a', '--pick-at', str(k)], ms)
                    for k, ms in ((0, 700), (1, 750), (2, 750), (3, 1300))])
# System Update in the background without its page (it names versions): the
# shelf's download mark filling, on the Brick introduction's card
TAKES['bi-update-bg'] = {**TAKES['update-bg'], 'device': 'brick', 'card': 'bi',
                         'segments': TAKES['update-bg']['segments'][1:]}


# ---- running them --------------------------------------------------------------
def write_last(card, tag, game):
    (card / 'ZyperOS/.last').write_text(f'{tag}\n{game}\n')


# MENU > Display > Theme as it ships: a live take that changes it leaves it on the card
LOOK = {'look.accent': 'lime', 'look.light': 'warm', 'look.dark': 'black', 'look.corners': 'rounded', 'look.zyper': 'on'}


def run_live(name, spec, card):
    write_last(card, *spec['last'])
    # Every shelf on Covers, as it ships: a take that pressed START leaves
    # its view on the card (view.TAG)
    for db in (card / 'userdata/zyperos.db', card / 'shared/.zyperos/library.db'):
        if db.is_file():
            with sqlite3.connect(db) as c:
                if c.execute("SELECT 1 FROM sqlite_master WHERE name='settings'").fetchone():
                    c.execute("DELETE FROM settings WHERE key LIKE 'view.%'")
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
        r = subprocess.run([str(BINARY), '--status', STATUS, '--device', spec['device']], cwd=ROOT, env=env,
                           stdout=f, stderr=subprocess.STDOUT, timeout=600)
    if r.returncode:
        raise RuntimeError(f'{name}: exit {r.returncode}, see {log}')
    (OUT / f'{name}.json').write_text(json.dumps({'fps': FPS, 'ms': spec['end'],
                                                  'presses': spec['script']}) + '\n')


def clear_kept(card):
    """No shader, Strength or button remap a take before left on the card"""
    with sqlite3.connect(card / 'userdata/zyperos.db') as db:
        db.execute("DELETE FROM settings WHERE key LIKE 'shader%' OR key LIKE 'launcher_s%' OR key LIKE 'night.%'"
                   " OR key LIKE 'screen_%' OR key = 'home.hidden'")
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
    # Where each held picture after the first starts, and each motion: the
    # moments a press is seen (src/sounds.ts plays a sound there)
    cuts = []
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
        if n:
            cuts.append(round(total))
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
            device = [] if '--device' in a else ['--device', spec['device']]
            r = subprocess.run([str(BINARY), '--shot', str(png), *theme, '--fn-volume', *status, *device, *a],
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
    (OUT / f'{name}.json').write_text(json.dumps({'fps': FPS, 'ms': round(total), 'cuts': cuts}) + '\n')
    # A take of one picture is used as a picture too
    if n == 1:
        shutil.copyfile(frames / '00000.png', OUT / f'{name}.png')
    shutil.rmtree(frames)


def spec_card(name):
    return TAKES[name]['card']


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
    # 1.0.6's: fuller shelves for the Grid, and the SG-1000's
    cards['more'] = cardlib.build(WORK / 'card-more', more=True)
    cardlib.device_settings(cards['more'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    # 1.1's: the fuller card with what 1.1 brings to a card (card.py's v110)
    if not only or any(spec_card(t) == V11 for t in only):
        cards[V11] = cardlib.v110(cardlib.build(WORK / 'card-v110', more=True))
        cardlib.device_settings(cards[V11], BINARY, **{'tour.done': 1, 'clock': '24h'})
    # The Brick introduction's: 1.1's card, a Brick on 1.0.6 offered 1.1.0
    if not only or any(spec_card(t) == 'bi' for t in only):
        cards['bi'] = cardlib.brick_intro(cardlib.v110(cardlib.build(WORK / 'card-bi', more=True)))
        cardlib.device_settings(cards['bi'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    # The demonstration card with the snap's game holding it in its .media,
    # for the play mark and the bar, and the snap's frames for snapsub.dylib
    cards['snaps'] = cardlib.build(WORK / 'card-snaps')
    cardlib.device_settings(cards['snaps'], BINARY, **{'tour.done': 1, 'clock': '24h'})
    # Its BIOS files as stand-ins, as the 1.1 card's, so a shelf shows no
    # "Needs scph5501.bin in Bios" (the Brick's PlayStation core asks for one)
    cardlib.rs.stand_in_bios(cards['snaps'])
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
            run_live(name, spec, cards[spec['card']])
        else:
            run_shots(name, spec, cards)
        print(name, flush=True)
    # Every take's length and presses in one file, for the video
    index = {p.stem: json.loads(p.read_text()) for p in sorted(OUT.glob('*.json')) if p.stem != 'index'}
    (OUT / 'index.json').write_text(json.dumps(index) + '\n')


if __name__ == '__main__':
    main()
