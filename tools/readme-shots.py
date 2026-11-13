#!/usr/bin/env python3
"""Capture current launcher frames for the README and website.

make native
python3 tools/readme-shots.py

Writes build/readme-shots/light (every screen, in Beard), build/readme-shots/dark
(the few screens the README's light-and-dark section shows in Salt & Pepper)
and a manifest. --out changes that output directory. No device is accessed.
Temporary demonstration cards use empty ROM and music files named after
well-known games and freeware; the launcher itself draws every pixel.

--art puts the games' real box art on the demonstration card, as the
launcher's own Scraper would: the covers, title screens and screenshots of
the libretro-thumbnails sets (github.com/libretro-thumbnails), fetched once
into a cache outside the repository (~/Library/Caches/zyperos-readme-art,
--art-cache moves it) and reused after. Only the composed screenshots go in
the repository; the games and their art belong to their owners. Without
--art, nothing is fetched and every game wears the launcher's generated card,
as the checks that load this file use it.
Achievements, playback timing, the Scraper's counts and the update page use
the launcher's built-in shot fixtures and the status file written below.

The File Transfer page (res/web) is shot in a browser: build-native/portal-serve
(make build-native/portal-serve) serves it over a scratch card of freeware
names, and headless Chrome draws it at a computer's width and a phone's, in
light, over the DevTools protocol. --chrome names the browser; --no-page
leaves those shots out. The server listens on a port the kernel picks while
it runs.

NES Player 2's play page (res/web/play) is shot the same way, on a phone held
upright and on its side: build-native/p2d serves it from res/web as
mk/payload.sh lays it out, over a share that build-native/p2d-feed fills with
its own drawn frames (stripes and a box, no game's picture), and the page
draws them as a friend's phone would. --no-page leaves these out too.
"""
import argparse
import base64
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import sqlite3
import struct
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import zlib

ROOT=Path(__file__).resolve().parents[1]
# The demonstration library: well-known games on the systems the home shows,
# as empty files, each with the libretro-thumbnails name its art is filed
# under (--art). folder: [(the file on the card, libretro's name)]
LIBRARY={
    'PlayStation':[
        ('Castlevania - Symphony of the Night (USA).chd','Castlevania - Symphony of the Night (USA)'),
        ('Crash Bandicoot (USA).chd','Crash Bandicoot (USA)'),
        ('Metal Gear Solid (USA).chd','Metal Gear Solid (USA)'),
        ('Spyro the Dragon (USA).chd','Spyro the Dragon (USA)'),
        ('Tekken 3 (USA).chd','Tekken 3 (USA)'),
        ("Tony Hawk's Pro Skater 2 (USA).chd","Tony Hawk's Pro Skater 2 (USA)")],
    'NES':[
        ("Kirby's Adventure (USA).nes","Kirby's Adventure (USA)"),
        ('Mega Man 2 (USA).nes','Mega Man 2 (USA)'),
        ('Metroid (USA).nes','Metroid (USA)'),
        ('Super Mario Bros. 3 (USA).nes','Super Mario Bros. 3 (USA)'),
        ('The Legend of Zelda (USA).nes','Legend of Zelda, The (USA)')],
    'SNES':[
        ('Chrono Trigger (USA).sfc','Chrono Trigger (USA)'),
        ('Donkey Kong Country (USA).sfc','Donkey Kong Country (USA)'),
        ('Super Mario World (USA).sfc','Super Mario World (USA)'),
        ('Super Metroid (USA).sfc','Super Metroid (Japan, USA) (En,Ja)'),
        ('The Legend of Zelda - A Link to the Past (USA).sfc','Legend of Zelda, The - A Link to the Past (USA)')],
    'Game Boy':[
        ('Pokemon - Red Version (USA, Europe).gb','Pokemon - Red Version (USA, Europe) (SGB Enhanced)'),
        ('Super Mario Land (World).gb','Super Mario Land (World)'),
        ('Tetris (World).gb','Tetris (World) (Rev 1)'),
        ("The Legend of Zelda - Link's Awakening (USA, Europe).gb","Legend of Zelda, The - Link's Awakening (USA, Europe)")],
    'Game Boy Advance':[
        ('Advance Wars (USA).gba','Advance Wars (USA)'),
        ('Mario Kart - Super Circuit (USA).gba','Mario Kart - Super Circuit (USA)'),
        ('Metroid Fusion (USA).gba','Metroid Fusion (USA)'),
        ('Pokemon - Emerald Version (USA, Europe).gba','Pokemon - Emerald Version (USA, Europe)'),
        ('The Legend of Zelda - The Minish Cap (USA).gba','Legend of Zelda, The - The Minish Cap (USA)'),
        # Folders of games (src/shelfdir.h), as someone sorts a shelf: the
        # folders shot and the list. A hack and a homebrew game with no art
        # of their own show the generated card.
        ('Commercial Owned/Castlevania - Aria of Sorrow (USA).gba','Castlevania - Aria of Sorrow (USA)'),
        ('Commercial Owned/Final Fantasy Tactics Advance (USA).gba','Final Fantasy Tactics Advance (USA)'),
        ('Commercial Owned/Golden Sun (USA).gba','Golden Sun (USA, Europe)'),
        ('Commercial Owned/Kirby & The Amazing Mirror (USA).gba','Kirby & The Amazing Mirror (USA)'),
        ('Friends Games/Fire Emblem (USA).gba','Fire Emblem (USA, Australia)'),
        ('Homebrew and Aftermarket/Goodboy Galaxy.gba',None),
        ('Rom Hacks/Metroid Zero Mission Redux.gba',None),
        ('Rom Hacks/Translations/Mother 3 (English).gba','Mother 3 (Japan)')],
    'Genesis':[
        ('Gunstar Heroes (USA).md','Gunstar Heroes (USA)'),
        ('Sonic The Hedgehog (USA).md','Sonic The Hedgehog (USA)'),
        ('Sonic The Hedgehog 2 (USA).md','Sonic The Hedgehog 2 (USA)'),
        ('Streets of Rage 2 (USA).md','Streets of Rage 2 (USA)')],
    'Nintendo 64':[
        ('GoldenEye 007 (USA).z64','GoldenEye 007 (USA)'),
        ('Mario Kart 64 (USA).z64','Mario Kart 64 (USA)'),
        ('Super Mario 64 (USA).z64','Super Mario 64 (USA)'),
        ('The Legend of Zelda - Ocarina of Time (USA).z64','Legend of Zelda, The - Ocarina of Time (USA)')],
    'Dreamcast':[
        ('Crazy Taxi (USA).chd','Crazy Taxi (USA)'),
        ('Jet Grind Radio (USA).chd','Jet Grind Radio (USA)'),
        ('Sonic Adventure (USA).chd','Sonic Adventure (USA)'),
        ('Soulcalibur (USA).chd','Soulcalibur (USA)')],
}
# Each folder's libretro-thumbnails repository
LR_REPO={'PlayStation':'Sony_-_PlayStation','NES':'Nintendo_-_Nintendo_Entertainment_System',
         'SNES':'Nintendo_-_Super_Nintendo_Entertainment_System','Game Boy':'Nintendo_-_Game_Boy',
         'Game Boy Advance':'Nintendo_-_Game_Boy_Advance','Genesis':'Sega_-_Mega_Drive_-_Genesis',
         'Nintendo 64':'Nintendo_-_Nintendo_64','Dreamcast':'Sega_-_Dreamcast','ScummVM':'ScummVM'}
# With --art, the in-game menu and its Controls pages stand over a screenshot
# of a PlayStation game (--backdrop) rather than a made-up frame
BACKDROP=['gamemenu','gamemenu-shader','controls-ps1','controls-hotkeys','controls-psp']
BACKDROP_GAME='Spyro the Dragon (USA)'   # bright, so the panel reads against it
ART_CACHE=Path.home()/'Library/Caches/zyperos-readme-art'
GAMES=[Path(f).stem for f,_ in LIBRARY['PlayStation']]
FOCUS=GAMES[2]   # the games page and its details open on Metal Gear Solid
# Its details' Synopsis, in my words
SYNOPSIS=('Solid Snake slips into a nuclear weapons base on a snowbound Alaskan island, '
          'alone, to stop a rogue unit that threatens to launch.')
# ScummVM's freeware games, as ScummVM's own site gives them away: a folder each
SCUMMVM=['Beneath a Steel Sky','Drascula - The Vampire Strikes Back',
         'Flight of the Amazon Queen','Lure of the Temptress','Soltys']
SCUMMVM_FOCUS=SCUMMVM[2]
# ...and their art in libretro-thumbnails' ScummVM set (--art)
SCUMMVM_ART={'Beneath a Steel Sky':'Beneath a Steel Sky','Drascula - The Vampire Strikes Back':'Drascula - The Vampire Strikes Back',
             'Flight of the Amazon Queen':'Flight of the Amazon Queen (CD_DOS_English)',
             'Lure of the Temptress':'Lure of the Temptress','Soltys':'Soltys'}
# OpenBOR's shelf: Senile Team's freeware game, as a stand-in module
OPENBOR=['Beats of Rage.pak']
# name: (card, the shelf .last names, flags). 'demo' is the library above,
# 'media' the same with a film in Videos too,
# 'empty' a card with no games (the empty home), 'scummvm' ScummVM's freeware
# games alone, 'openbor' a module on the OpenBOR shelf.
SHOTS={
    'systems':('demo','PS1',['--home','spotlight']),
    'gallery':('demo','PS1',['--home','gallery']),
    'shelf':('demo','PS1',['--screen','games']),
    # Other shelves of the demonstration card, and Favorites across systems
    'shelf-n64':('demo','N64',['--screen','games']),
    'shelf-snes':('demo','SFC',['--screen','games']),
    'favorites':('demo','FAV',['--screen','games']),
    'info':('demo','PS1',['--screen','games','--info']),
    'info-delete':('demo','PS1',['--screen','games','--info-delete']),
    # A card copied from Knulli (DESIGN.md 11.40): the PlayStation's games in
    # psx/, their covers and video ES's, and Rename's keyboard (11.41)
    'es-shelf':('es','PS1',['--screen','games']),
    'info-rename':('demo','PS1',['--screen','games','--info-rename']),
    'rename':('demo','PS1',['--screen','games','--keyboard','0','Metal Gear Solid (USA)','Rename']),
    'delete-ask':('demo','PS1',['--screen','games','--delete-ask']),
    'cheevos':('demo','PS1',['--cheevos-screen','0','--cheevos-badge','{card}/Roms/PlayStation/.cheevos/Hagane.set','b#']),
    # ...with two of its unlocks earned offline and waiting to be sent
    'cheevos-waiting':('demo','PS1',['--cheevos-screen','0','--cheevos-waiting','2','--cheevos-badge','{card}/Roms/PlayStation/.cheevos/Hagane.set','b#']),
    # An unlock over a game (DESIGN.md 11.7): its badge, and Zyper at the right
    'cheevo-notice':('demo','PS1',['--notice-kind','unlock','--notice',"Violated Heavens",'Unlocked \u00b7 10 points',
                                   '--notice-badge','{card}/Roms/PlayStation/.cheevos/badges/b3.png']),
    'menu':('demo','PS1',['--menu','Search Games']),
    # MENU's Display tab on Theme, its chips, and the Theme page with both
    # themes small (DESIGN.md 11.3, 11.42)
    'display':('demo','PS1',['--menu-tab','1','--menu','Theme']),
    'theme':('demo','PS1',['--look-page','theme','0']),
    'mp4':('media','MUSE',['--screen','games']),
    'nowplaying':('demo','MUSE',['--nowplaying','0','0','83']),
    'hare':('demo','PS1',['--hare','http://192.168.1.42','1 connected','Tobu Tobu Girl.gb']),
    'hotspot':('demo','PS1',['--hotspot','ZyperOS-7A2B','q7mx-3kpa','1 joined','waiting']),
    # NES Player 2's page over a paused NES game (src/p2panel.h), on the
    # handheld's own Wi-Fi: its code, nobody joined yet (the launcher's
    # made-up frame behind it, not a game's)
    'player2-hotspot':('demo','NES',['--screen','games','--player2','hotspot']),
    'usb-ask':('demo','PS1',['--usb-ask','0']),
    # USB File Transfer's steps (DESIGN.md 11.35): the cable and the address
    # ticked, the browser awaited
    'usb-steps':('demo','PS1',['--hare-steps','usb','browser']),
    'scummvm':('scummvm','SCUMMVM',['--screen','games']),
    # Folders of games on a shelf, and the list (DESIGN.md 11.39): the Game
    # Boy Advance shelf's top, its folders first with their games counted;
    # inside one; and MENU > View > List, its covers' panel beside the rows
    'folders':('demo','GBA',['--screen','games']),
    'folder-inside':('demo','GBA',['--screen','games']),
    'list':('demo','GBA',['--screen','games','--view','list']),
    'list-inside':('demo','GBA',['--screen','games','--view','list']),
    # The in-game menu over OpenBOR (src/portmenu.h), on the launcher's
    # made-up frame rather than a game's
    'openbor':('openbor','OPENBOR',['--screen','games','--gamemenu','0','port']),
    'scraper':('demo','PS1',['--scraper','2']),
    'update':('demo','PS1',['--update']),
    # The screensaver (DESIGN.md 11.37) over the focused game's screenshot,
    # which is the picture its video snap would be
    'screensaver':('demo','PS1',['--saver','--saver-pic','{art}/Sony_-_PlayStation/Named_Snaps/Metal Gear Solid (USA).png']),
    'ports':('ports','PRT',['--screen','games']),
    # The in-game menu over a port, and the first of its Controls pages: what
    # its README says the buttons do (the made-up Star Farm, SHOT_FOCUS)
    'ports-menu':('portdocs','PRT',['--screen','games','--gamemenu','0','port']),
    'ports-controls':('portdocs','PRT',['--screen','games','--gamecontrols','0','PRT']),
    'portmaster':('demo','PS1',['--portmaster','running']),
    # MENU > Storage (DESIGN.md 11.32) on its stand-in card of 128 GB nearly
    # full, and the demonstration card's PlayStation page
    'storage':('demo','PS1',['--storage','main']),
    'storage-system':('demo','PS1',['--storage','system']),
    # ...and its Music page: albums by size, X's order beside the list
    'storage-music':('demo','PS1',['--storage','music']),
    # A game's Cover and Video (DESIGN.md 11.34): what the sources offer
    # beside the cover there is, one looked at closely, and the video's rows.
    # Its pictures are the stand-ins media_art draws, not any game's box
    'media':('demo','PS1',['--screen','games','--media','page','--media-art','{card}/media-art']),
    'media-look':('demo','PS1',['--screen','games','--media','look','--media-art','{card}/media-art']),
    'media-video':('demo','PS1',['--screen','games','--media','video','--media-art','{card}/media-art']),
    'empty':('empty','',['--ways','usb']),
    'tour-theme':('demo','PS1',['--tour','theme','2000','--tour-music','--tour-pick','0']),
    'tour-home':('demo','PS1',['--tour','home','2000','--tour-music','--tour-pick','1']),
    'tour-sounds':('demo','PS1',['--tour','sounds','2000','--tour-music','--tour-pick','1']),
    'tour-music':('demo','PS1',['--tour','music','2000','--tour-music','--tour-pick','0']),
    # The blog's battery guide (site/blog/): the charging screen of a handheld
    # that is off, and Zyper's heat warning on the home
    'charging':('demo','PS1',['--charging','4000']),
    'heat':('demo','PS1',['--heatpop','1600']),
    # The README's languages (DESIGN.md 11.30): the first start's step on
    # 简体中文, and MENU in Portuguese and in Japanese
    'tour-language':('demo','PS1',['--tour','lang','3000','--tour-pick','1']),
    'menu-pt':('demo','PS1',['--lang','pt-BR','--menu','Language']),
    'menu-ja':('demo','PS1',['--lang','ja','--menu','Search Games']),
    # MENU > Battery Icon on Time Left: the corner writes the time left
    # (STATUS below), as 3 h 25 min beside the battery
    'battery':('demo','PS1',['--menu','Battery Icon']),
    # The top bar on the home while a song plays, with headphones in the jack:
    # the song's mark and the headphones' (src/status.h), in both themes, and
    # the mark of a song paused
    'topbar':('demo','PS1',['--home','spotlight','--song','Night Drive']),
    'topbar-paused':('demo','PS1',['--home','spotlight','--song','Night Drive','--song-paused']),
    # A song name too long for its room slides (ui_text_marquee's pace, holds and
    # faded ends): the same name at the start of its travel and a moment into it
    'topbar-long':('demo','PS1',['--home','spotlight','--song','Night Drive (Extended Mix) - The Midnight Rider Collective','--phase','0']),
    'topbar-long-moving':('demo','PS1',['--home','spotlight','--song','Night Drive (Extended Mix) - The Midnight Rider Collective','--phase','2600']),
    # The same short name with its clock at the start: the song's name that fits does not move
    'topbar-still':('demo','PS1',['--home','spotlight','--song','Night Drive','--phase','0']),
    # The video with its controls, the foot's hints (Now Playing's own is above)
    'video-controls':('demo','MUSE',['--nowplaying','0','0','83','--video','controls']),
    'video-full':('demo','MUSE',['--nowplaying','0','0','83','--video','full']),
    # The quick slot and the hotkeys (2026-11-07): the in-game menu on its
    # Quick Slot row, a PlayStation game's buttons drawn as their shapes, and
    # its Hotkeys page, SELECT with a shoulder
    'gamemenu':('demo','PS1',['--screen','games','--gamemenu','3']),
    # ...on its Shader row, Sharp-Shimmerless (SHOT_SETTINGS)
    'gamemenu-shader':('demo','PS1',['--screen','games','--gamemenu','5']),
    'controls-ps1':('demo','PS1',['--screen','games','--gamecontrols','0','PS1']),
    'controls-hotkeys':('demo','PS1',['--screen','games','--gamecontrols','2','PS1']),
    # The PSP's buttons, with R2, which turns the d-pad into its stick
    'controls-psp':('psp','PSP',['--screen','games','--gamecontrols','1','PSP']),
}
# Favorites for a shot alone, set before it is taken and cleared after: the
# games page with the focused game and the one before it kept, so the heart
# shows on a focused cover and on a side one (src/artbadge.h). Only there, so
# the other shots' homes have no Favorites shelf.
SHOT_FAVS={'shelf':[('PS1',FOCUS+'.chd'),('PS1',GAMES[1]+'.chd')],
           # Favorites: one game from each of seven systems
           'favorites':[('N64','Super Mario 64 (USA).z64'),('SFC','Super Metroid (USA).sfc'),('PS1',FOCUS+'.chd'),
                        ('GBA','Metroid Fusion (USA).gba'),('MD','Sonic The Hedgehog 2 (USA).md'),
                        ('NES','Super Mario Bros. 3 (USA).nes'),('DC','Jet Grind Radio (USA).chd')]}
# The game a shot's shelf opens on, where it is not its system's own (below)
# MP4's on the film, between an album and the book, so its three kinds show
SHOT_FOCUS={'ports-menu':'Star Farm.sh','ports-controls':'Star Farm.sh','mp4':'Videos',
            'folders':'Commercial Owned','list':'Commercial Owned',
            'folder-inside':'Commercial Owned/Golden Sun (USA).gba',
            'list-inside':'Commercial Owned/Golden Sun (USA).gba',
            'shelf-n64':'Super Mario 64 (USA).z64','shelf-snes':'Super Mario World (USA).sfc',
            'favorites':'Sonic The Hedgehog 2 (USA).md'}
# A shot's own --status where the README's default (no Wi-Fi, 82%) would hide
# what it shows: icon= is the setting for this shot, eta= the minutes left
STATUS={'battery':'10:08 24h wifi=3 batt=62 eta=205 icon=time',
        'topbar':'10:08 24h wifi=3 phones=on batt=62','topbar-paused':'10:08 24h wifi=3 phones=on batt=62',
        'topbar-still':'10:08 24h wifi=3 phones=on batt=62','topbar-long':'10:08 24h wifi=3 phones=on batt=62','topbar-long-moving':'10:08 24h wifi=3 phones=on batt=62'}
# The README's light-and-dark section: these, in both themes
DARK=['systems','menu','topbar','nowplaying','video-controls','video-full','media','shelf-n64',
      'folders','list']
# A shot whose screen draws its own clock, outside the status corner, so the
# launcher's corner check (shot_corner_clear in src/main.c) reports this after
# writing a right picture: the charging screen's clock is its own (DESIGN.md
# 11.25). Accepted for that shot alone, by its exact words.
CORNER_OWN={'charging':'no corner drawn, though --status gave it something to show'}
# MENU > System Update as a card with two system slots sees an update (83 MB,
# the size of the Pixel 2's two-slot test download, tarikbc/ZyperOS#59)
UPDATE_NOTES=[
    'http://zyperos.local: USB File Transfer now has a name you can type, next to its address. The page opens from any browser on the computer, and the address now wins over any other network that uses the same numbers, which fixes File Transfer on some Windows 11 computers.',
    'The connection, step by step: File Transfer shows Cable, Address and Browser as they connect, and if one stalls, a short hint says what to do. It counts browsers, not connections, so one browser shows as one.',
    '"What did you plug in?": when something on the USB port wants power, the Pixel 2 asks: a USB accessory (headphones, a Wi-Fi adapter, a keyboard) or a computer. It never powers a computer by itself. If a computer doesn\'t connect, the Pixel 2 tells you to plug the cable in again. Answered by mistake? The USB mark at the top, or MENU > USB Device, asks again.',
    'MENU > USB Accessories: keep it on Ask, or set Automatic to power accessories right away with no question.',
    'Cooler File Transfer: the File Transfer screen and the open MENU no longer redraw when nothing changes. On my Pixel 2, File Transfer left open on the cable now stays around 37 \u00b0C instead of passing 50 \u00b0C, and uses a sixth of the battery. Its screen turns off after Auto Off, and only dims while files are copying.',
    'Button hints in the new questions show each button\'s glyph, the same way everywhere.',
    'Fixes: the Pixel 2 is recognized by a Mac on the first plug, and longer words, like "auto (alto-falante)" in Audio Output, no longer get cut.',
]
UPDATE_STATUS='state available\ninstalled 0.1.0\nversion 0.1.1\ndownload 83000000\nkind system\n'

def png_rgb(path,w,h,rows):
    """An 8-bit RGB PNG from rows of (r,g,b), standard library only"""
    raw=b''.join(b'\x00'+bytes(c for px in row for c in px) for row in rows)
    def chunk(kind,body):
        out=struct.pack('>I',len(body))+kind+body
        return out+struct.pack('>I',zlib.crc32(kind+body)&0xFFFFFFFF)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))
                    +chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))

# Stand-ins for an achievement's badge (src/rabadge.h): a 64-pixel square, a
# diagonal blend of one color, a frame and a diamond. RetroAchievements' own
# are the games' art, and none is in this repository.
BADGE_COLORS=[(214,87,62),(61,142,201),(242,178,52),(120,186,102),(163,92,201),(40,168,160),(224,112,90),(90,100,220)]
def stand_in_badge(path,color):
    rows=[]
    for y in range(64):
        row=[]
        for x in range(64):
            t=(x+y)/126
            c=[round(v*(1.0-0.38*t)) for v in color]
            if min(x,y,63-x,63-y)<3: c=[round(v*0.55) for v in color]
            elif abs(x-31.5)+abs(y-31.5)<15: c=[min(255,round(v*0.35+255*0.65)) for v in color]
            row.append(tuple(c))
        rows.append(row)
    png_rgb(path,64,64,rows)

# Stand-ins for the pictures the Cover and Video page shows (--media-art):
# the shapes a source offers - a portrait box, a landscape box, a box in 3D,
# a title screen, a screenshot, a fan's tall cover - each a blend of one color
# with bands and a diamond, so a shot shows their shapes and none of any
# game's art. The names are the ones src/main.c's media_shot asks for.
MEDIA_ART=[  # file, width, height, color, kind
    ('now.png',512,364,(70,120,190),'box'),('c0.png',364,512,(200,90,70),'box'),
    ('c1.png',512,364,(70,120,190),'box'),('c2.png',360,512,(120,170,90),'box'),
    ('c3.png',512,360,(190,140,60),'box'),('c4.png',440,512,(150,90,190),'box3d'),
    ('c5.png',320,240,(40,40,70),'title'),('c6.png',320,240,(80,150,110),'shot'),
    ('c7.png',512,364,(70,120,190),'box'),('c8.png',320,240,(40,40,70),'title'),
    ('c9.png',320,240,(80,150,110),'shot'),('c10.png',400,600,(60,70,140),'fan'),
    ('snap.png',320,240,(90,160,120),'shot'),
]
# With --art, the page shows Metal Gear Solid's own pictures from the
# libretro-thumbnails sets in those places: its box from several regions,
# its title screen and screenshots.
MEDIA_REAL=[  # file, the set, libretro's name
    ('now.png','Named_Boxarts','Metal Gear Solid (USA)'),
    ('c0.png','Named_Boxarts','Metal Gear Solid (USA) (Disc 1)'),
    ('c1.png','Named_Boxarts','Metal Gear Solid (Japan)'),
    ('c2.png','Named_Boxarts','Metal Gear Solid (Europe)'),
    ('c3.png','Named_Boxarts','Metal Gear Solid (Spain) (Disc 1)'),
    ('c4.png','Named_Boxarts','Metal Gear Solid (Japan, Asia) (Disc 1) (Ichi) (Premium Package)'),
    ('c5.png','Named_Titles','Metal Gear Solid (USA)'),
    ('c6.png','Named_Snaps','Metal Gear Solid (USA)'),
    ('c7.png','Named_Boxarts','Metal Gear Solid (Japan) (Disc 1) (Ichi)'),
    ('c8.png','Named_Titles','Metal Gear Solid (Japan) (Disc 1) (Ichi)'),
    ('c9.png','Named_Snaps','Metal Gear Solid (USA) (Disc 1)'),
    ('c10.png','Named_Boxarts','Metal Gear Solid (Germany) (Disc 1)'),
    ('snap.png','Named_Snaps','Metal Gear Solid (USA)'),
]
def media_art(dest,art=None,manifest=None):
    dest.mkdir(parents=True,exist_ok=True)
    if art:
        for name,kind,lr in MEDIA_REAL:
            got=art_file(art,LR_REPO['PlayStation'],kind,lr)
            shutil.copyfile(got,dest/name)
            if manifest is not None: note_art(manifest,'media-art/'+name,'PlayStation',kind,lr,got)
        return
    for name,w,h,color,kind in MEDIA_ART:
        rows=[]
        for y in range(h):
            row=[]
            for x in range(w):
                t=(x/w+y/h)/2
                c=[round(v*(1.0-0.45*t)) for v in color]
                if kind=='box3d' and x<w*0.14: c=[round(v*0.45) for v in color]
                elif kind in ('title','shot') and abs(y-h*0.62)<h*0.06: c=[min(255,round(v*0.4+150)) for v in color]
                elif kind in ('box','fan') and y>h*0.8: c=[round(v*0.5) for v in color]
                elif abs(x-w/2)/w+abs(y-h*0.42)/h<0.16: c=[min(255,round(v*0.35+255*0.65)) for v in color]
                row.append(tuple(c))
            rows.append(row)
        png_rgb(dest/name,w,h,rows)

def art_file(cache,repo,kind,name):
    """One picture of a libretro-thumbnails set, from the cache, or fetched
    into it from GitHub the first time. libretro files a name's & * / : ` < >
    ? \\ | as _, and a set's link to another picture comes back as that
    picture's name, which is followed."""
    path=cache/repo/kind/(name+'.png')
    if path.is_file(): return path
    safe=''.join('_' if c in '&*/:`<>?\\|' else c for c in name)
    url=f'https://raw.githubusercontent.com/libretro-thumbnails/{repo}/master/{kind}/'+urllib.parse.quote(safe+'.png')
    for _ in range(4):
        with urllib.request.urlopen(url,timeout=60) as reply: data=reply.read()
        if data.startswith(b'\x89PNG'): break
        url=urllib.parse.urljoin(url,urllib.parse.quote(data.decode().strip()))
    else: raise RuntimeError(f'{repo}/{kind}/{name}: no picture')
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data)
    return path

def snap_video(cache,folder,name):
    """A game's video snap for the shelf's play mark and its details: two
    seconds of its libretro screenshot (Named_Snaps) as an MP4, made with
    ffmpeg into the cache. None without ffmpeg or a screenshot."""
    out=cache/LR_REPO[folder]/'Videos'/(name+'.mp4')
    if out.is_file(): return out
    if not shutil.which('ffmpeg'): return None
    try: still=art_file(cache,LR_REPO[folder],'Named_Snaps',name)
    except urllib.error.HTTPError: return None
    out.parent.mkdir(parents=True,exist_ok=True)
    subprocess.run(['ffmpeg','-v','error','-y','-loop','1','-i',str(still),'-t','2','-r','30',
                    '-vf','scale=320:-2,format=yuv420p','-c:v','libx264','-movflags','+faststart',str(out)],check=True)
    return out

def note_art(manifest,where,folder,kind,name,path):
    manifest['artwork'][where]={'set':f'libretro-thumbnails/{LR_REPO[folder]}/{kind}','name':name,
                                'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}

def card_dirs(card):
    for folder in ['ZyperOS','Roms','userdata','shared']: (card/folder).mkdir(parents=True)
    shutil.copyfile(ROOT/'config/systems.cfg',card/'ZyperOS/systems.cfg')
    shutil.copytree(ROOT/'res/system-icons',card/'ZyperOS/system-icons')
    (card/'ZyperOS/VERSION').write_text('0.1.0\n')

def demo_card(card,art,manifest):
    """The demonstration card: LIBRARY's games as empty files, and with art
    (the cache --art fills) each game's box art in its folder's .media, where
    the Scraper puts it"""
    card_dirs(card)
    for folder,games in LIBRARY.items():
        d=card/'Roms'/folder;(d/'.media').mkdir(parents=True)
        for file,lr in games:
            (d/file).parent.mkdir(parents=True,exist_ok=True)
            (d/file).touch()
            if art and lr:
                got=art_file(art,LR_REPO[folder],'Named_Boxarts',lr)
                shutil.copyfile(got,d/'.media'/(Path(file).stem+'.png'))
                note_art(manifest,f'{folder}/{file}',folder,'Named_Boxarts',lr,got)
                video=snap_video(art,folder,lr)
                if video: shutil.copyfile(video,d/'.media'/(Path(file).stem+'.mp4'))
    pm_art(card)   # the Install PortMaster page's icon (--portmaster)
    # Stand-in badges for the achievements list and notice shots, beside the
    # (absent) set of the PlayStation folder as src/rabadge.h keeps them
    # The Cover and Video page's pictures, beside the card's files
    media_art(card/'media-art',art,manifest)
    badges=card/'Roms/PlayStation/.cheevos/badges';badges.mkdir(parents=True)
    for i,color in enumerate(BADGE_COLORS): stand_in_badge(badges/f'b{i+1}.png',color)
    # Curated metadata for the demonstration's focused game. This is
    # library data, not a modified drawing path or a user's saved state.
    dbdir=card/'shared/.zyperos';dbdir.mkdir()
    with sqlite3.connect(dbdir/'library.db') as db:
        db.execute('CREATE TABLE games(folder TEXT NOT NULL,file TEXT NOT NULL,year TEXT,publisher TEXT,developer TEXT,players TEXT,genres TEXT,esrb TEXT,note TEXT,synopsis TEXT,scraped INTEGER,PRIMARY KEY(folder,file))')
        db.execute('INSERT INTO games VALUES(?,?,?,?,?,?,?,?,?,?,?)',('PlayStation',FOCUS+'.chd','1998','Konami','KCEJ','1','Action','','',SYNOPSIS,0))
    for album in ['First Light','Night Drive','Quiet Hours']:
        d=card/'Music/Demo Library'/album;d.mkdir(parents=True)
        for track in ['01 First Light.mp3','02 Open Road.mp3','03 Home Again.mp3']: (d/track).touch()
    # The tutorial's Menu Music step shows only on a card with tracks
    shutil.copytree(ROOT/'res/bgm',card/'ZyperOS/res/bgm',ignore=shutil.ignore_patterns('*.json'))
    shutil.copytree(ROOT/'res/tour',card/'ZyperOS/res/tour')
    up=card/'userdata/update';up.mkdir(parents=True)
    (up/'status').write_text(UPDATE_STATUS)
    # What's New as a release's list carries it: 1.0.3's points, whole, as
    # tools/update-pack.py takes them from its notes (the page wraps and
    # scrolls them, DESIGN.md 11.17)
    (up/'latest-system.txt').write_text('zyperos-update 1\nkind system\n'+''.join('note %s\n'%n for n in UPDATE_NOTES),encoding='utf-8')
    # A test release's source on the card counts as a network to look with
    # (docs/updates.md), so the page offers its download as on Wi-Fi
    (up/'source').write_text('file:///nonexistent\n')

def media_card(card,art=None,manifest=None):
    """The demonstration card, a film, Blender's freely licensed Sintel as a
    stand-in name, and a book, Treasure Island, which is in the public domain:
    MP4's shelf shows All, the albums, the book and the video mixed
    (src/muselib.h), each card with its kind's badge, and its menu has Show"""
    demo_card(card,art,manifest if manifest is not None else {'artwork':{}})
    (card/'Videos').mkdir();(card/'Videos/Sintel.mp4').touch()
    book=card/'Audiobooks/Treasure Island';book.mkdir(parents=True)
    (book/'01 The Old Sea-dog at the Admiral Benbow.mp3').touch()

# A shot's settings in the handheld's own database (userdata/zyperos.db, as
# the launcher's db_dev keeps them), there only while it is taken
SHOT_SETTINGS={'gamemenu-shader':{'shader.PS1':'shimmerless'}}

def set_settings(card,settings,on):
    """A shot's settings (SHOT_SETTINGS) into the handheld's database, or out"""
    if not settings: return
    (card/'userdata').mkdir(parents=True,exist_ok=True)
    with sqlite3.connect(card/'userdata/zyperos.db') as db:
        db.execute('CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL)')
        for key,value in settings.items():
            if on: db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)',(key,value))
            else: db.execute('DELETE FROM settings WHERE key=?',(key,))

def es_card(card,art=None,manifest=None):
    """A card copied from a system built on EmulationStation (DESIGN.md
    11.40): every ZyperOS folder empty, as a new card has them, and the
    PlayStation's games in Knulli's psx/, with what its scraper left beside
    them - images/<game>-thumb.png, videos/<game>-video.mp4 and gamelist.xml
    with their names and details. Nothing in .media: the shelf shows ES's."""
    card_dirs(card)
    for line in (ROOT/'config/systems.cfg').read_text(encoding='utf-8').splitlines():
        if line.startswith('sys|'): (card/'Roms'/line.split('|')[2].strip()).mkdir(parents=True,exist_ok=True)
    d=card/'Roms/psx';(d/'images').mkdir(parents=True);(d/'videos').mkdir()
    entries=[]
    for file,lr in LIBRARY['PlayStation']:
        stem=Path(file).stem;(d/file).touch()
        if art:
            got=art_file(art,LR_REPO['PlayStation'],'Named_Boxarts',lr)
            shutil.copyfile(got,d/'images'/(stem+'-thumb.png'))
            if manifest is not None: note_art(manifest,f'psx/images/{stem}-thumb.png','PlayStation','Named_Boxarts',lr,got)
            video=snap_video(art,'PlayStation',lr)
            if video: shutil.copyfile(video,d/'videos'/(stem+'-video.mp4'))
        name=stem.split(' (')[0].replace(' - ',': ')
        desc=SYNOPSIS if stem==FOCUS else ''
        entries.append('\t<game>\n\t\t<path>./%s</path>\n\t\t<name>%s</name>\n\t\t<desc>%s</desc>\n'
                       '\t\t<thumbnail>./images/%s-thumb.png</thumbnail>\n\t\t<video>./videos/%s-video.mp4</video>\n'
                       '\t\t<releasedate>19981021T000000</releasedate>\n\t\t<genre>Action</genre>\n\t</game>\n'
                       %(file.replace('&','&amp;'),name.replace('&','&amp;'),desc.replace('&','&amp;'),
                         stem.replace('&','&amp;'),stem.replace('&','&amp;')))
    (d/'gamelist.xml').write_text('<?xml version="1.0"?>\n<gameList>\n'+''.join(entries)+'</gameList>\n',encoding='utf-8')

def set_favs(card,favs,on):
    """A shot's favorites (SHOT_FAVS) into the card's library, or out of it"""
    if not favs: return   # a card with no library yet (ScummVM's) keeps none
    with sqlite3.connect(card/'shared/.zyperos/library.db') as db:
        db.execute('CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL)')
        for tag,file in favs:
            if on: db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)',(f'fav.{tag}\t{file}','1'))
            else: db.execute('DELETE FROM settings WHERE key=?',(f'fav.{tag}\t{file}',))

# The Install PortMaster page's shots: the status file as res/portmaster/
# pm-install.py writes it, read through ZYPEROS_PM_STATUS (--portmaster)
PM_SHOTS={'portmaster':'state downloading\nversion 2026.06.23-0015\ntotal 24121702\ndone 9800000\n'}

def ports_card(card):
    """A card whose ZyperOS has the PortMaster installer and whose Ports
    folder has no PortMaster yet: the Ports shelf offers to install it"""
    card_dirs(card)
    (card/'Roms/NES').mkdir(exist_ok=True);(card/'Roms/NES/Super Tilt Bro.nes').touch()
    (card/'Roms/Ports').mkdir(exist_ok=True)
    pm_art(card)
    shutil.copyfile(ROOT/'res/portmaster/pm-install.py',card/'ZyperOS/portmaster/pm-install.py')

def pm_art(card):
    """PortMaster's covers and icon in the card's ZyperOS, as the payload
    puts them there (tools/genportmaster.py)"""
    (card/'ZyperOS/portmaster').mkdir(parents=True,exist_ok=True)
    for name in ('cover-light.png','cover-dark.png','icon.png'):
        shutil.copyfile(ROOT/'res/portmaster'/name,card/'ZyperOS/portmaster'/name)

def pm_env(env,name,folder):
    """env with name's status file, when it is one of PM_SHOTS"""
    if name in PM_SHOTS:
        status=Path(folder)/('pm-'+name)/'status'
        status.parent.mkdir(parents=True,exist_ok=True)
        status.write_text(PM_SHOTS[name])
        env['ZYPEROS_PM_STATUS']=str(status)
    return env

def scummvm_card(card,art=None,manifest=None):
    card_dirs(card)
    for name in SCUMMVM:
        d=card/'Roms/ScummVM'/name;d.mkdir(parents=True);(d/'readme.txt').touch()
        if art:
            got=art_file(art,LR_REPO['ScummVM'],'Named_Boxarts',SCUMMVM_ART[name])
            (card/'Roms/ScummVM/.media').mkdir(exist_ok=True)
            shutil.copyfile(got,card/'Roms/ScummVM/.media'/(name+'.png'))
            if manifest is not None: note_art(manifest,'ScummVM/'+name,'ScummVM','Named_Boxarts',SCUMMVM_ART[name],got)

def portdocs_card(card):
    """A card whose Ports folder has the made-up ports of tools/fixtures/ports
    (port.json, keymaps, READMEs) and PortMaster's script: the in-game menu
    over a port and its Controls pages"""
    card_dirs(card)
    shutil.copytree(ROOT/'tools/fixtures/ports',card/'Roms/Ports')
    (card/'Roms/Ports/PortMaster.sh').touch()
    pm_art(card)

def openbor_card(card):
    card_dirs(card)
    d=card/'Roms/OpenBOR';d.mkdir(parents=True)
    for name in OPENBOR: (d/name).touch()

# The PSP's shelf, for its Controls page with R2 on it: a file stands in for a
# game, as OpenBOR's does
PSP=['Game.iso']
def psp_card(card):
    card_dirs(card)
    d=card/'Roms/PSP';d.mkdir(parents=True)
    for name in PSP: (d/name).touch()

# The File Transfer page: a computer's window on a console's folder with an
# upload going (throttled to 64 KB/s, shot 4.5 s in, once its time left shows), and a phone's on the PIN
# and on ROMs, a folder per console with its color
PAGES={
    'portal':dict(width=1280,height=800,scale=1,phone=False,folder='roms/NES',upload=('Super Tilt Bro.nes',524304)),
    'portal-pin':dict(width=390,height=844,scale=2,phone=True,gate=True),
    'portal-phone':dict(width=390,height=844,scale=2,phone=True,folder='roms'),
}
# Freeware and homebrew, by folder; sizes as the files have them, roughly
PORTAL_GAMES={
    'NES':[('Alter Ego.nes',40976),('Blade Buster.nes',393232),('Lawn Mower.nes',24592),('Nova the Squirrel.nes',262160)],  # and Super Tilt Bro., uploading
    'Game Boy':[('Libbet and the Magic Floor.gb',65536),('Tobu Tobu Girl.gb',131072)],
    'PICO-8':[('Celeste Classic.p8.png',30111)],
}
PORTAL_SCUMMVM=['Beneath a Steel Sky','Flight of the Amazon Queen']
CHROME='/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'

def portal_card(card):
    """A card as the first boot leaves it, a folder per console, with a few
    freeware games and the demonstration albums"""
    for line in (ROOT/'config/systems.cfg').read_text().splitlines():
        if line.startswith('sys|'): (card/'Roms'/line.split('|')[2]).mkdir(parents=True,exist_ok=True)
    for folder,files in PORTAL_GAMES.items():
        for name,size in files:
            with open(card/'Roms'/folder/name,'wb') as f: f.truncate(size)
    for name in PORTAL_SCUMMVM:
        d=card/'Roms/ScummVM'/name;d.mkdir(parents=True,exist_ok=True)
        with open(d/'readme.txt','wb') as f: f.truncate(2048)
    for album in ['First Light','Night Drive','Quiet Hours']:
        d=card/'Music/Demo Library'/album;d.mkdir(parents=True)
        for track in ['01 First Light.mp3','02 Open Road.mp3','03 Home Again.mp3']:
            with open(d/track,'wb') as f: f.truncate(4200000)
    for folder in ['Audiobooks','Videos','Bios','Saves','.userdata/shared/.zyperos','ZyperOS']:
        (card/folder).mkdir(parents=True,exist_ok=True)
    shutil.copyfile(ROOT/'config/systems.cfg',card/'ZyperOS/systems.cfg')
    # The pictures every folder wears on the page, as the card has them
    shutil.copytree(ROOT/'res/system-icons',card/'ZyperOS/system-icons')
    # The menu music's folder, which is no system and has its own picture
    (card/'Roms/BGM').mkdir(exist_ok=True)

def web_dir(web):
    """res/web as mk/payload.sh lays it out on the card"""
    shutil.copytree(ROOT/'res/web',web)
    for font in ['menu.ttf','title.ttf']: shutil.copyfile(ROOT/'res/fonts'/font,web/font)

class DevTools:
    """Just enough of a WebSocket client for Chrome's DevTools protocol:
    text frames, one call at a time, events read past."""
    def __init__(self,url):
        u=urllib.parse.urlparse(url)
        self.s=socket.create_connection((u.hostname,u.port),timeout=30)
        key=base64.b64encode(os.urandom(16)).decode()
        self.s.sendall((f'GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\nUpgrade: websocket\r\n'
                        f'Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        head=b''
        while b'\r\n\r\n' not in head: head+=self.s.recv(4096)
        head,self.buf=head.split(b'\r\n\r\n',1)
        if b' 101 ' not in head.split(b'\r\n')[0]: raise RuntimeError('DevTools refused the socket')
        self.buf=bytearray(self.buf);self.n=0
    def _take(self,n):
        while len(self.buf)<n:
            got=self.s.recv(1<<20)
            if not got: raise EOFError('DevTools closed')
            self.buf+=got
        out=bytes(self.buf[:n]);del self.buf[:n];return out
    def _message(self):
        whole=bytearray()
        while True:
            b0,b1=self._take(2);n=b1&0x7f
            if n==126: n=struct.unpack('>H',self._take(2))[0]
            elif n==127: n=struct.unpack('>Q',self._take(8))[0]
            if b1&0x80: self._take(4)
            whole+=self._take(n)
            if b0&0x0f==0x8: raise EOFError('DevTools closed')
            if b0&0x80: return whole.decode()
    def call(self,method,**params):
        self.n+=1
        data=json.dumps({'id':self.n,'method':method,'params':params}).encode()
        mask=os.urandom(4);head=bytearray([0x81])
        if len(data)<126: head.append(0x80|len(data))
        else: head.append(0x80|126);head+=struct.pack('>H',len(data))
        self.s.sendall(bytes(head)+mask+bytes(b^mask[i%4] for i,b in enumerate(data)))
        while True:
            m=json.loads(self._message())
            if m.get('id')==self.n:
                if 'error' in m: raise RuntimeError(f'{method}: {m["error"]}')
                return m.get('result',{})

def page_shots(chrome,dest,only,manifest):
    with tempfile.TemporaryDirectory(prefix='zyperos-portal-') as temporary:
        base=Path(temporary);card=base/'card';web=base/'web';profile=base/'chrome';dest.mkdir(parents=True,exist_ok=True)
        portal_card(card);web_dir(web)
        subprocess.run(['make','-s','build-native/portal-serve'],cwd=ROOT,check=True)
        # A 128 GB card's room, as the page's pictures show it
        server=subprocess.Popen([str(ROOT/'build-native/portal-serve'),str(card),str(web),'space=88.4/119.1'],
                                stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        browser=None
        try:
            pin,port=server.stdout.readline().split()
            origin=f'http://127.0.0.1:{port}'
            reply=urllib.request.urlopen(urllib.request.Request(origin+'/api/auth',data=pin.encode(),method='POST'))
            cookie=reply.headers['Set-Cookie'].split(';')[0].split('=',1)
            browser=subprocess.Popen([chrome,'--headless=new','--remote-debugging-port=0',f'--user-data-dir={profile}',
                                      '--no-first-run','--no-default-browser-check','--hide-scrollbars',
                                      '--disable-background-networking','--disable-component-update','--disable-sync',
                                      '--no-pings','about:blank'],
                                     stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            for _ in range(100):
                if (profile/'DevToolsActivePort').is_file() and (profile/'DevToolsActivePort').read_text().strip(): break
                time.sleep(0.1)
            debug=(profile/'DevToolsActivePort').read_text().split()[0]
            target=next(t for t in json.load(urllib.request.urlopen(f'http://127.0.0.1:{debug}/json/list')) if t['type']=='page')
            dt=DevTools(target['webSocketDebuggerUrl'])
            dt.call('Network.enable')
            dt.call('Emulation.setEmulatedMedia',features=[{'name':'prefers-color-scheme','value':'light'},
                                                           {'name':'prefers-reduced-motion','value':'reduce'}])
            for name,page in PAGES.items():
                if only and name not in only: continue
                gate=page.get('gate',False);folder=page.get('folder','')
                if gate: dt.call('Network.deleteCookies',name=cookie[0],url=origin+'/')
                else: dt.call('Network.setCookie',name=cookie[0],value=cookie[1],url=origin+'/',httpOnly=True)
                dt.call('Emulation.setDeviceMetricsOverride',width=page['width'],height=page['height'],
                        deviceScaleFactor=page['scale'],mobile=page['phone'])
                dt.call('Emulation.setTouchEmulationEnabled',enabled=page['phone'])
                dt.call('Page.navigate',url='about:blank')
                # Waited for: Chrome holds a navigation that comes before the
                # one ahead of it is in
                for _ in range(100):
                    if dt.call('Runtime.evaluate',returnByValue=True,expression="location.href").get('result',{}).get('value')=='about:blank': break
                    time.sleep(0.05)
                dt.call('Page.navigate',url=f'{origin}/#{urllib.parse.quote(folder)}')
                up=("!document.getElementById('gate').hidden && !!document.getElementById('app').hidden" if gate else
                    "!document.getElementById('app').hidden && document.querySelectorAll('#list li').length>0"
                    " && !document.getElementById('room').hidden")
                for _ in range(100):
                    ready=dt.call('Runtime.evaluate',returnByValue=True,awaitPromise=True,
                                  expression=f"document.fonts.ready.then(()=>document.readyState==='complete' && {up})")
                    if ready.get('result',{}).get('value'): break
                    time.sleep(0.1)
                else: raise RuntimeError(f'{name}: the page did not come up')
                if 'upload' in page:
                    file,size=page['upload']
                    dt.call('Network.emulateNetworkConditions',offline=False,latency=0,
                            downloadThroughput=-1,uploadThroughput=65536)
                    dt.call('Runtime.evaluate',expression=f"enqueue([new File([new Uint8Array({size})], {json.dumps(file)})])")
                    time.sleep(4.5)   # the time left needs three seconds of speed
                else: time.sleep(0.5)
                target=dest/(name+'.png')
                target.write_bytes(base64.b64decode(dt.call('Page.captureScreenshot',format='png')['data']))
                # The page asks before it is left mid-upload (beforeunload),
                # which would hold the next navigation: the upload is let go
                dt.call('Runtime.evaluate',expression='sending = false')
                dt.call('Network.emulateNetworkConditions',offline=False,latency=0,downloadThroughput=-1,uploadThroughput=-1)
                manifest['screenshots'].append({'file':f'light/{name}.png','card':'portal',
                    'args':dict(page,page='res/web/index.html'),
                    'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
                print('light/'+name,flush=True)
        finally:
            if browser: browser.terminate();browser.wait()
            server.stdin.close();server.wait(timeout=10)

# NES Player 2's play page on a phone held upright and on its side, as the
# README shows it, at a phone's 390x844 and 844x390 at 3x. Light: the page
# wears the handheld's theme, which the launcher hands p2d (src/webtheme.h);
# with none here, it is play.css's own, which is Beard's.
PLAY_PAGES={
    'play-phone':dict(width=390,height=844,scale=3,phone=True),
    'play-phone-side':dict(width=844,height=390,scale=3,phone=True),
}
# The frame the picture holds: p2d-feed's 60th, the box near the middle
PLAY_FRAMES=60

def play_shots(chrome,dest,only,manifest):
    with tempfile.TemporaryDirectory(prefix='zyperos-play-') as temporary:
        base=Path(temporary);web=base/'web';profile=base/'chrome';share=base/'share';log=base/'p2d.log'
        dest.mkdir(parents=True,exist_ok=True)
        # res/web as the card has it, the page's faces beside it too
        web_dir(web)
        for font in ['menu.ttf','title.ttf']: shutil.copyfile(ROOT/'res/fonts'/font,web/'play'/font)
        subprocess.run(['make','-s','build-native/p2d','build-native/p2d-feed'],cwd=ROOT,check=True)
        # The feed stops after PLAY_FRAMES and holds the share as it is until
        # it is told to go, so every shot shows the same still picture
        feed=subprocess.Popen([str(ROOT/'build-native/p2d-feed'),'--share',str(share),'--fps','60',
                               '--frames',str(PLAY_FRAMES)],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        server=browser=None
        try:
            def published():
                # p2s_head's frame, the frames written (src/p2share.h)
                try:
                    with open(share,'rb') as f: head=f.read(16)
                except OSError: return 0
                return struct.unpack('<I',head[12:16])[0] if len(head)==16 else 0
            for _ in range(300):
                if published()>=PLAY_FRAMES: break
                time.sleep(0.05)
            else: raise RuntimeError('p2d-feed made no share')
            with open(log,'wb') as out:
                server=subprocess.Popen([str(ROOT/'build-native/p2d'),'--root',str(web),'--share',str(share),
                                         '--addr','127.0.0.1','--port','0','--lang','en'],stdout=out,stderr=out)
            port=None
            for _ in range(100):
                for line in log.read_text(errors='replace').splitlines():
                    if line.startswith('p2d: http on port '): port=line.split()[-1]
                if port: break
                time.sleep(0.1)
            if not port: raise RuntimeError(f'p2d never said its port: {log.read_text(errors="replace")!r}')
            origin=f'http://127.0.0.1:{port}'
            browser=subprocess.Popen([chrome,'--headless=new','--remote-debugging-port=0',f'--user-data-dir={profile}',
                                      '--no-first-run','--no-default-browser-check','--hide-scrollbars',
                                      '--disable-background-networking','--disable-component-update','--disable-sync',
                                      '--no-pings','about:blank'],
                                     stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            for _ in range(100):
                if (profile/'DevToolsActivePort').is_file() and (profile/'DevToolsActivePort').read_text().strip(): break
                time.sleep(0.1)
            debug=(profile/'DevToolsActivePort').read_text().split()[0]
            for name,page in PLAY_PAGES.items():
                if only and name not in only: continue
                # A tab of its own for each shot, closed after it: a page only
                # navigated away from can stay alive in the browser's back
                # cache, holding player 2's place (one browser at a time is
                # player 2 on the NES), and with the picture still nothing
                # says it has gone
                tab=json.load(urllib.request.urlopen(urllib.request.Request(
                    f'http://127.0.0.1:{debug}/json/new?about:blank',method='PUT'),timeout=30))
                dt=DevTools(tab['webSocketDebuggerUrl'])
                dt.call('Emulation.setEmulatedMedia',features=[{'name':'prefers-color-scheme','value':'light'},
                                                               {'name':'prefers-reduced-motion','value':'reduce'}])
                dt.call('Emulation.setDeviceMetricsOverride',width=page['width'],height=page['height'],
                        deviceScaleFactor=page['scale'],mobile=page['phone'])
                dt.call('Emulation.setTouchEmulationEnabled',enabled=page['phone'],maxTouchPoints=5)
                dt.call('Page.navigate',url=origin+'/play/')
                # Player 2, the picture drawn, the faces in. A page told the
                # game is full while the last one's place frees tries again
                # every 2 s, so this waits past a few of those.
                for _ in range(300):
                    ready=dt.call('Runtime.evaluate',returnByValue=True,awaitPromise=True,
                                  expression="document.fonts.ready.then(()=>document.readyState==='complete'"
                                             " && typeof window.p2==='object' && window.p2.frame>0"
                                             " && document.getElementById('game').dataset.state==='playing')")
                    if ready.get('result',{}).get('value'): break
                    time.sleep(0.1)
                else:
                    seen=dt.call('Runtime.evaluate',returnByValue=True,
                                 expression="[location.href, (document.getElementById('game')||{dataset:{}}).dataset.state,"
                                            " window.p2 && window.p2.frame]").get('result',{}).get('value')
                    raise RuntimeError(f'{name}: the page did not come up as player 2: {seen}, {log.read_text(errors="replace")!r}')
                time.sleep(0.5)
                target=dest/(name+'.png')
                target.write_bytes(base64.b64decode(dt.call('Page.captureScreenshot',format='png')['data']))
                manifest['screenshots'].append({'file':f'light/{name}.png','card':'play',
                    'args':dict(page,page='res/web/play/index.html',frames=f'p2d-feed --frames {PLAY_FRAMES}'),
                    'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
                print('light/'+name,flush=True)
                dt.s.close()
                urllib.request.urlopen(f'http://127.0.0.1:{debug}/json/close/{tab["id"]}',timeout=30)
        finally:
            if browser: browser.terminate();browser.wait()
            for proc in (server,feed):
                if not proc: continue
                proc.terminate()
                try: proc.wait(timeout=10)
                except subprocess.TimeoutExpired: proc.kill();proc.wait()

def env_for(card):
    return dict(os.environ, ZYPEROS_HEADLESS='1', SDL_VIDEODRIVER='dummy', SDL_RENDER_DRIVER='software',
                ZYPEROS_ROOT=str(card/'ZyperOS'),ZYPEROS_CARD=str(card),ZYPEROS_ROMS=str(card/'Roms'),
                ZYPEROS_USERDATA=str(card/'userdata'),ZYPEROS_SHARED=str(card/'shared'),ZYPEROS_WEB=str(card/'web'),
                ZYPEROS_FONT=str(ROOT/'res/fonts/menu.ttf'))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--art',action='store_true',help="the games' real box art, from libretro-thumbnails (cached)")
    ap.add_argument('--art-cache',type=Path,default=ART_CACHE,help='where --art keeps what it fetched')
    ap.add_argument('--out',type=Path,default=ROOT/'build/readme-shots')
    ap.add_argument('--chrome',default=CHROME,help='the browser for the File Transfer page')
    ap.add_argument('--no-page',action='store_true',help='leave out the File Transfer page and the play page')
    ap.add_argument('only',nargs='*',help='shot names (default: all)')
    args=ap.parse_args();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    binary=ROOT/'build-native/zyperos'
    if not binary.is_file(): ap.error('run make native first')
    manifest={'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
              'renderer':'current native launcher, SDL software, headless',
              'data':'demonstration library (well-known games and freeware by name, empty files); built-in shot fixtures',
              'artwork':{},'screenshots':[]}
    with tempfile.TemporaryDirectory(prefix='zyperos-readme-') as temporary:
        cards={'demo':Path(temporary)/'demo','empty':Path(temporary)/'empty','scummvm':Path(temporary)/'scummvm',
               'openbor':Path(temporary)/'openbor','ports':Path(temporary)/'ports',
               'portdocs':Path(temporary)/'portdocs','media':Path(temporary)/'media',
               'psp':Path(temporary)/'psp','es':Path(temporary)/'es'}
        art=args.art_cache if args.art else None
        demo_card(cards['demo'],art,manifest)
        media_card(cards['media'],art)
        es_card(cards['es'],art,manifest)
        scummvm_card(cards['scummvm'],art,manifest)
        backdrop=art_file(art,LR_REPO['PlayStation'],'Named_Snaps',BACKDROP_GAME) if art else None
        openbor_card(cards['openbor'])
        psp_card(cards['psp'])
        ports_card(cards['ports'])
        portdocs_card(cards['portdocs'])
        card_dirs(cards['empty'])
        for theme in ['light','dark']:
            dest=out/theme;dest.mkdir(exist_ok=True)
            for name,(card_name,tag,flags) in SHOTS.items():
                if theme=='dark' and name not in DARK: continue
                if args.only and name not in args.only: continue
                card=cards[card_name]
                focus=SHOT_FOCUS.get(name) or {'PS1':FOCUS+'.chd','SCUMMVM':SCUMMVM_FOCUS,'OPENBOR':OPENBOR[0],'PRT':'PortMaster.sh'}.get(tag,'')
                (card/'ZyperOS/.last').write_text(tag+'\n'+focus+'\n')
                target=dest/(name+'.png')
                # The tutorial draws no status corner, so it has no --status
                status=[] if name.startswith('tour-') else ['--status',STATUS.get(name,'10:08 24h wifi=off batt=82')]
                # {card} is where this run's temporary card is; the manifest keeps the word
                command=[str(binary),'--shot',str(target),'--theme',theme,
                         '--screen','systems','--phase','3200',*status,'--fn-volume',*[f.replace('{card}',str(card)).replace('{art}',str(args.art_cache)) for f in flags]]
                if backdrop and name in BACKDROP: command+=['--backdrop',str(backdrop)]
                set_favs(card,SHOT_FAVS.get(name,[]),True)
                set_settings(card,SHOT_SETTINGS.get(name),True)
                result=subprocess.run(command,cwd=ROOT,env=pm_env(env_for(card),name,temporary),text=True,
                                      stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                set_favs(card,SHOT_FAVS.get(name,[]),False)
                set_settings(card,SHOT_SETTINGS.get(name),False)
                (dest/(name+'.log')).write_text(result.stdout)
                if result.returncode and not (name in CORNER_OWN and CORNER_OWN[name] in result.stdout):
                    raise RuntimeError(f'{theme}/{name}: {result.stdout}')
                if not target.is_file(): raise RuntimeError(f'missing {target}')
                manifest['screenshots'].append({'file':f'{theme}/{name}.png','card':card_name,'args':[a.replace(str(card),'{card}').replace(str(args.art_cache),'{art}') for a in command[3:]],
                    'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
                print(theme+'/'+name,flush=True)
    if not args.no_page and (not args.only or set(args.only)&set(PAGES)):
        if not Path(args.chrome).is_file(): ap.error(f'no browser at {args.chrome}: --chrome, or --no-page')
        page_shots(args.chrome,out/'light',args.only,manifest)
    if not args.no_page and (not args.only or set(args.only)&set(PLAY_PAGES)):
        if not Path(args.chrome).is_file(): ap.error(f'no browser at {args.chrome}: --chrome, or --no-page')
        play_shots(args.chrome,out/'light',args.only,manifest)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

if __name__=='__main__': main()
