#!/usr/bin/env python3
"""The tour video's card: a demonstration library with the games' real box art.

python3 video/footage/card.py CARD

Builds a card at CARD the host launcher (build-native/zyperos) can run on:
the README's demonstration library (tools/readme-shots.py), well-known games
as empty files, folders of them on the Game Boy Advance, and 1.0.5's new
systems, each with its cover from the
libretro-thumbnails sets (github.com/libretro-thumbnails), as the launcher's
own Scraper would put it in the folder's .media. The covers are fetched once
into ~/Library/Caches/zyperos-readme-art, the same cache tools/readme-shots.py
--art uses, and never go in the repository: the games and their art belong to
their owners.

The details of a few games (year, publisher, players, genre) are their public
facts; each synopsis is in my words.
"""
import importlib.util
from pathlib import Path
import re
import shutil
import sqlite3
import subprocess
import sys
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('readme_shots', ROOT / 'tools/readme-shots.py')
rs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rs)

ART_CACHE = Path.home() / 'Library/Caches/zyperos-readme-art'

# folder: [(the file on the card, libretro's name)]
LIBRARY = {
    'PlayStation': [
        ('Castlevania - Symphony of the Night (USA).chd', 'Castlevania - Symphony of the Night (USA)'),
        ('Crash Bandicoot (USA).chd', 'Crash Bandicoot (USA)'),
        ('Metal Gear Solid (USA).chd', 'Metal Gear Solid (USA)'),
        ('Spyro the Dragon (USA).chd', 'Spyro the Dragon (USA)'),
        ('Tekken 3 (USA).chd', 'Tekken 3 (USA)'),
        ("Tony Hawk's Pro Skater 2 (USA).chd", "Tony Hawk's Pro Skater 2 (USA)")],
    'NES': [
        ("Kirby's Adventure (USA).nes", "Kirby's Adventure (USA)"),
        ('Mega Man 2 (USA).nes', 'Mega Man 2 (USA)'),
        ('Metroid (USA).nes', 'Metroid (USA)'),
        ('Super Mario Bros. 3 (USA).nes', 'Super Mario Bros. 3 (USA)'),
        ('The Legend of Zelda (USA).nes', 'Legend of Zelda, The (USA)')],
    'SNES': [
        ('Chrono Trigger (USA).sfc', 'Chrono Trigger (USA)'),
        ('Donkey Kong Country (USA).sfc', 'Donkey Kong Country (USA)'),
        ('Super Mario World (USA).sfc', 'Super Mario World (USA)'),
        ('Super Metroid (USA).sfc', 'Super Metroid (Japan, USA) (En,Ja)'),
        ('The Legend of Zelda - A Link to the Past (USA).sfc', 'Legend of Zelda, The - A Link to the Past (USA)')],
    'Game Boy': [
        ('Pokemon - Red Version (USA, Europe).gb', 'Pokemon - Red Version (USA, Europe) (SGB Enhanced)'),
        ('Super Mario Land (World).gb', 'Super Mario Land (World)'),
        ('Tetris (World).gb', 'Tetris (World) (Rev 1)'),
        ("The Legend of Zelda - Link's Awakening (USA, Europe).gb", "Legend of Zelda, The - Link's Awakening (USA, Europe)")],
    'Game Boy Advance': [
        ('Advance Wars (USA).gba', 'Advance Wars (USA)'),
        ('Mario Kart - Super Circuit (USA).gba', 'Mario Kart - Super Circuit (USA)'),
        ('Metroid Fusion (USA).gba', 'Metroid Fusion (USA)'),
        ('Pokemon - Emerald Version (USA, Europe).gba', 'Pokemon - Emerald Version (USA, Europe)'),
        ('The Legend of Zelda - The Minish Cap (USA).gba', 'Legend of Zelda, The - The Minish Cap (USA)'),
        # Folders of games, as someone sorts a shelf (the README's, src/shelfdir.h):
        # a hack and a homebrew game with no art of their own show the generated card
        ('Commercial Owned/Castlevania - Aria of Sorrow (USA).gba', 'Castlevania - Aria of Sorrow (USA)'),
        ('Commercial Owned/Final Fantasy Tactics Advance (USA).gba', 'Final Fantasy Tactics Advance (USA)'),
        ('Commercial Owned/Golden Sun (USA).gba', 'Golden Sun (USA, Europe)'),
        ('Commercial Owned/Kirby & The Amazing Mirror (USA).gba', 'Kirby & The Amazing Mirror (USA)'),
        ('Friends Games/Fire Emblem (USA).gba', 'Fire Emblem (USA, Australia)'),
        ('Homebrew and Aftermarket/Goodboy Galaxy.gba', None),
        ('Rom Hacks/Metroid Zero Mission Redux.gba', None),
        ('Rom Hacks/Translations/Mother 3 (English).gba', 'Mother 3 (Japan)')],
    'Genesis': [
        ('Gunstar Heroes (USA).md', 'Gunstar Heroes (USA)'),
        ('Sonic The Hedgehog (USA).md', 'Sonic The Hedgehog (USA)'),
        ('Sonic The Hedgehog 2 (USA).md', 'Sonic The Hedgehog 2 (USA)'),
        ('Streets of Rage 2 (USA).md', 'Streets of Rage 2 (USA)')],
    'Nintendo 64': [
        ('GoldenEye 007 (USA).z64', 'GoldenEye 007 (USA)'),
        ('Mario Kart 64 (USA).z64', 'Mario Kart 64 (USA)'),
        ('Super Mario 64 (USA).z64', 'Super Mario 64 (USA)'),
        ('The Legend of Zelda - Ocarina of Time (USA).z64', 'Legend of Zelda, The - Ocarina of Time (USA)')],
    'Nintendo DS': [
        ('Mario Kart DS (USA).nds', 'Mario Kart DS (USA) (En,Fr,De,Es,It)')],
    'Dreamcast': [
        ('Crazy Taxi (USA).chd', 'Crazy Taxi (USA)'),
        ('Jet Grind Radio (USA).chd', 'Jet Grind Radio (USA)'),
        ('Sonic Adventure (USA).chd', 'Sonic Adventure (USA)'),
        ('Soulcalibur (USA).chd', 'Soulcalibur (USA)')],
    # 1.0.5's four new systems
    'PSP': [
        ('Daxter (USA).iso', 'Daxter (USA)'),
        ('God of War - Chains of Olympus (USA).iso', 'God of War - Chains of Olympus (USA)'),
        ('LocoRoco (USA).iso', 'LocoRoco (USA) (En,Ja,Fr,De,Es,It,Nl,Pt,Sv,No,Da,Fi,Zh,Ko,Ru)'),
        ('Lumines (USA).iso', 'Lumines (USA) (v1.01)'),
        ('Patapon (USA).iso', 'Patapon (USA)')],
    'Doom': [
        ('Doom.wad', 'Doom (v1.9)'),
        ('Doom II - Hell on Earth.wad', 'Doom II - Hell on Earth (v1.7)'),
        ('freedoom1.wad', None)],
    'ColecoVision': [
        ('Donkey Kong (USA).col', 'Donkey Kong (USA)'),
        ('Frogger (USA).col', 'Frogger (USA)'),
        ('Lady Bug (USA, Europe).col', 'Lady Bug (USA, Europe)'),
        ('Pitfall! (USA).col', 'Pitfall! (USA)')],
    'TIC-80': [
        ('8 Bit Panda.tic', '8 Bit Panda'),
        ('Balmung.tic', 'Balmung'),
        ('Cauliflower Power.tic', 'Cauliflower Power'),
        ('Dungeon Descent.tic', 'Dungeon Descent')],
}
# The 1.0.6 video's card (build(more=True)): fuller shelves, so a Grid has
# rows to page through, and the SG-1000's shelf, whose games have no art of
# their own, so each wears the card ZyperOS makes for it
MORE = {
    'SNES': [(f'{n} (USA).sfc', f'{n} (USA)') for n in (
        'Contra III - The Alien Wars', 'EarthBound', 'F-Zero', 'Kirby Super Star', 'Mega Man X', 'Pilotwings',
        'Secret of Mana', 'Star Fox', 'Super Castlevania IV', 'Super Mario Kart', 'Super Punch-Out!!')],
    'NES': [(f'{n}.nes', n) for n in (
        'Battletoads (USA)', 'Castlevania (USA)', 'Contra (USA)', 'Dr. Mario (Japan, USA)', 'Excitebike (Japan, USA)',
        'Final Fantasy (USA)', 'Mega Man 3 (USA)', 'Ninja Gaiden (USA)', 'Super Mario Bros. (World)', 'Tetris (USA)',
        'Zelda II - The Adventure of Link (USA)')],
    'PlayStation': [('Ape Escape (USA).chd', 'Ape Escape (USA)'), ('Final Fantasy Tactics (USA).chd', 'Final Fantasy Tactics (USA)'),
                    ('Final Fantasy VII (USA).chd', 'Final Fantasy VII (USA) (Disc 1)'),
                    ('Klonoa - Door to Phantomile (USA).chd', 'Klonoa - Door to Phantomile (USA)'),
                    ('Resident Evil 2 (USA).chd', 'Resident Evil 2 (USA) (Disc 1)'), ('Silent Hill (USA).chd', 'Silent Hill (USA)'),
                    ('Vagrant Story (USA).chd', 'Vagrant Story (USA)'), ('Wipeout XL (USA).chd', 'Wipeout XL (USA)')],
    'Nintendo 64': [(f'{n} (USA).z64', f'{n} (USA)') for n in (
        'Banjo-Kazooie', 'F-Zero X', 'Paper Mario', 'Pilotwings 64', 'Star Fox 64', 'Super Smash Bros.')],
    'Genesis': [('Altered Beast (USA, Europe).md', 'Altered Beast (USA, Europe)'), ('Comix Zone (USA).md', 'Comix Zone (USA)'),
                ('Earthworm Jim (USA).md', 'Earthworm Jim (USA)'), ('Ecco the Dolphin (USA, Europe).md', 'Ecco the Dolphin (USA, Europe)'),
                ('Golden Axe (World).md', 'Golden Axe (World)'), ('Phantasy Star IV (USA).md', 'Phantasy Star IV (USA)'),
                ('Sonic The Hedgehog 3 (USA).md', 'Sonic The Hedgehog 3 (USA)'), ('Vectorman (USA, Europe).md', 'Vectorman (USA, Europe)')],
    'SG-1000': [('Flicky (Japan).sg', None), ("Girl's Garden (Japan).sg", None), ('Lode Runner (Japan).sg', None)],
}
LR_REPO = {'PlayStation': 'Sony_-_PlayStation', 'NES': 'Nintendo_-_Nintendo_Entertainment_System',
           'SNES': 'Nintendo_-_Super_Nintendo_Entertainment_System', 'Game Boy': 'Nintendo_-_Game_Boy',
           'Game Boy Advance': 'Nintendo_-_Game_Boy_Advance', 'Genesis': 'Sega_-_Mega_Drive_-_Genesis',
           'Nintendo 64': 'Nintendo_-_Nintendo_64', 'Dreamcast': 'Sega_-_Dreamcast',
           'Nintendo DS': 'Nintendo_-_Nintendo_DS', 'PSP': 'Sony_-_PlayStation_Portable', 'Doom': 'DOOM',
           'ColecoVision': 'Coleco_-_ColecoVision', 'TIC-80': 'TIC-80'}

# The Cover and Video page's pictures (src/main.c's media_shot names them):
# Metal Gear Solid's box from several regions, its title screen and screenshots
MEDIA_REAL = [
    ('now.png', 'Named_Boxarts', 'Metal Gear Solid (USA)'),
    ('c0.png', 'Named_Boxarts', 'Metal Gear Solid (USA) (Disc 1)'),
    ('c1.png', 'Named_Boxarts', 'Metal Gear Solid (Japan) (Disc 1) (Ichi)'),
    ('c2.png', 'Named_Boxarts', 'Metal Gear Solid (Germany) (Disc 1)'),
    ('c3.png', 'Named_Boxarts', 'Metal Gear Solid (Spain) (Disc 1)'),
    ('c4.png', 'Named_Boxarts', 'Metal Gear Solid (Japan, Asia) (Disc 1) (Ichi) (Premium Package)'),
    ('c5.png', 'Named_Titles', 'Metal Gear Solid (USA)'),
    ('c6.png', 'Named_Snaps', 'Metal Gear Solid (USA)'),
    ('c7.png', 'Named_Boxarts', 'Metal Gear Solid (USA)'),
    ('c8.png', 'Named_Titles', 'Metal Gear Solid (Japan) (Disc 1) (Ichi)'),
    # the plain (USA) snap: the Disc 1 and Disc 2 ones have Spanish subtitles
    ('c9.png', 'Named_Titles', 'Metal Gear Solid (USA)'),
    ('c10.png', 'Named_Boxarts', 'Metal Gear Solid (Spain) (Disc 1)'),
    ('snap.png', 'Named_Snaps', 'Metal Gear Solid (USA)'),
]

# Public facts, and a synopsis in my words: folder, file, year, publisher,
# developer, players, genres, synopsis
DETAILS = [
    ('PlayStation', 'Metal Gear Solid (USA).chd', '1998', 'Konami', 'KCEJ', '1', 'Action',
     'Solid Snake slips into a nuclear weapons base on a snowbound Alaskan island, '
     'alone, to stop a rogue unit that threatens to launch.'),
    ('PlayStation', 'Castlevania - Symphony of the Night (USA).chd', '1997', 'Konami', 'KCET', '1', 'Action',
     "Alucard wakes to find Dracula's castle risen again, and explores it room by room, "
     'growing stronger with every weapon and spell he finds.'),
    ('PlayStation', 'Crash Bandicoot (USA).chd', '1996', 'Sony Computer Entertainment', 'Naughty Dog', '1', 'Platform',
     'Crash runs, spins and jumps across three islands to rescue Tawna from Doctor Neo Cortex.'),
    ('SNES', 'Super Metroid (USA).sfc', '1994', 'Nintendo', 'Nintendo R&D1', '1', 'Action',
     'Samus returns to planet Zebes to take back the last Metroid, '
     'and every new power opens another part of the map.'),
    ('Nintendo 64', 'Super Mario 64 (USA).z64', '1996', 'Nintendo', 'Nintendo EAD', '1', 'Platform',
     "Mario jumps into the paintings of Peach's castle to find the Power Stars Bowser took."),
]

# A favorite on a few shelves, so the heart shows before its name
FAVORITES = [('PS1', 'Metal Gear Solid (USA).chd'), ('PS1', 'Crash Bandicoot (USA).chd'),
             ('SFC', 'Super Metroid (USA).sfc'), ('N64', 'Super Mario 64 (USA).z64')]


def art_file(cache, repo, kind, name):
    """One picture of a libretro-thumbnails set, from the cache, or fetched
    into it from GitHub the first time. libretro files a name's & * / : ` < >
    ? \\ | as _, and a set's link to another picture comes back as that
    picture's name, which is followed."""
    path = cache / repo / kind / (name + '.png')
    if path.is_file():
        return path
    safe = ''.join('_' if c in '&*/:`<>?\\|' else c for c in name)
    url = f'https://raw.githubusercontent.com/libretro-thumbnails/{repo}/master/{kind}/' + urllib.parse.quote(safe + '.png')
    for _ in range(4):
        with urllib.request.urlopen(url, timeout=60) as reply:
            data = reply.read()
        if data.startswith(b'\x89PNG'):
            break
        url = urllib.parse.urljoin(url, urllib.parse.quote(data.decode().strip()))
    else:
        raise RuntimeError(f'{repo}/{kind}/{name}: no picture')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return path


def build(card, cache=ART_CACHE, more=False):
    if card.exists():
        shutil.rmtree(card)
    rs.card_dirs(card)
    # The version System Update starts from (the one before this release)
    (card / 'ZyperOS/VERSION').write_text('1.0.4\n')
    library = {f: LIBRARY.get(f, []) + (MORE.get(f, []) if more else []) for f in {**LIBRARY, **(MORE if more else {})}}
    for folder, games in library.items():
        d = card / 'Roms' / folder
        (d / '.media').mkdir(parents=True, exist_ok=True)
        for file, lr in games:
            (d / file).parent.mkdir(parents=True, exist_ok=True)
            (d / file).touch()
            if lr:
                got = art_file(cache, LR_REPO[folder], 'Named_Boxarts', lr)
                shutil.copyfile(got, d / '.media' / (Path(file).stem + '.png'))
    # The DS game's screenshot, behind its in-game menu, and Super Mario Bros.
    # 3's, behind NES Player 2's (footage/takes.py)
    art_file(cache, LR_REPO['Nintendo DS'], 'Named_Snaps', LIBRARY['Nintendo DS'][0][1])
    art_file(cache, LR_REPO['NES'], 'Named_Snaps', 'Super Mario Bros. 3 (USA)')
    rs.pm_art(card)
    media = card / 'media-art'
    media.mkdir()
    for name, kind, lr in MEDIA_REAL:
        shutil.copyfile(art_file(cache, LR_REPO['PlayStation'], kind, lr), media / name)
    badges = card / 'Roms/PlayStation/.cheevos/badges'
    badges.mkdir(parents=True)
    for i, color in enumerate(rs.BADGE_COLORS):
        rs.stand_in_badge(badges / f'b{i + 1}.png', color)
    dbdir = card / 'shared/.zyperos'
    dbdir.mkdir(exist_ok=True)
    with sqlite3.connect(dbdir / 'library.db') as db:
        db.execute('CREATE TABLE games(folder TEXT NOT NULL,file TEXT NOT NULL,year TEXT,publisher TEXT,'
                   'developer TEXT,players TEXT,genres TEXT,esrb TEXT,note TEXT,synopsis TEXT,scraped INTEGER,'
                   'PRIMARY KEY(folder,file))')
        for folder, file, year, pub, dev, players, genres, syn in DETAILS:
            db.execute('INSERT INTO games VALUES(?,?,?,?,?,?,?,?,?,?,?)',
                       (folder, file, year, pub, dev, players, genres, '', '', syn, 0))
        db.execute('CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY NOT NULL,value TEXT NOT NULL)')
        for tag, file in FAVORITES:
            db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)', (f'fav.{tag}\t{file}', '1'))
    for album in ['First Light', 'Night Drive', 'Quiet Hours']:
        d = card / 'Music/Demo Library' / album
        d.mkdir(parents=True)
        for track in ['01 First Light.mp3', '02 Open Road.mp3', '03 Home Again.mp3']:
            (d / track).touch()
    (card / 'Videos').mkdir()
    (card / 'Videos/Sintel.mp4').touch()
    book = card / 'Audiobooks/Treasure Island'
    book.mkdir(parents=True)
    (book / '01 The Old Sea-dog at the Admiral Benbow.mp3').touch()
    # ZyperOS's own menu music, the one track the video plays too (src/music.ts
    # names it), so the song Zyper names on the home is the one heard
    track = re.search(r'file: "([^"]+)"', (ROOT / 'video/src/music.ts').read_text()).group(1)
    (card / 'ZyperOS/res/bgm').mkdir(parents=True)
    shutil.copyfile(ROOT / 'res/bgm' / track, card / 'ZyperOS/res/bgm' / track)
    shutil.copytree(ROOT / 'res/tour', card / 'ZyperOS/res/tour')
    # The Shader and Strength lists' pictures, each look as the handheld draws it
    shutil.copytree(ROOT / 'res/shaderdemo', card / 'ZyperOS/res/shaderdemo')
    up = card / 'userdata/update'
    up.mkdir(parents=True)
    # 1.0.4 offered 1.0.5 (83 MB, the two-slot download's size)
    (up / 'status').write_text('state available\ninstalled 1.0.4\nversion 1.0.5\ndownload 83000000\nkind system\n')
    (up / 'source').write_text('file:///nonexistent\n')
    # Ports: PortMaster's installer, for the Ports shelf's offer
    (card / 'Roms/Ports').mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'res/portmaster/pm-install.py', card / 'ZyperOS/portmaster/pm-install.py')
    return card


# ---- the 1.1 videos' card (src/v110, src/reddit110) ---------------------------
# build(more=True), then what 1.1 brings to a card, as a player's would hold
# it: your own PICO-8's place (stand-in files, never Lexaloffle's), carts
# named on its shelf, three Collections, Screenshots, Recents with each game's
# last screen, and a game's cheats (tools/readme-shots.py's stand-ins). A
# screenshot or a last screen is the game's libretro snap from the art cache,
# at its own size, as the Screenshot hotkey would write it: the games and
# their art belong to their owners.
FF7 = 'Final Fantasy VII (USA) (Rev 1) (Disc 1)'
P8_CARTS = ['Celeste Classic', 'Dank Tomb', 'Just One Boss', 'Porklike', 'Poom', 'Taco Rider']
COLLECTIONS = {
    'Couch Co-op': ['SNES/Super Mario Kart (USA).sfc', 'Game Boy Advance/Mario Kart - Super Circuit (USA).gba',
                    'PlayStation/Tekken 3 (USA).chd', 'Nintendo 64/Super Smash Bros. (USA).z64',
                    'Genesis/Streets of Rage 2 (USA).md'],
    'RPGs to Finish': ['SNES/Chrono Trigger (USA).sfc', 'SNES/EarthBound (USA).sfc', 'PlayStation/' + FF7 + '.chd',
                       'Game Boy Advance/Commercial Owned/Golden Sun (USA).gba', 'SNES/Secret of Mana (USA).sfc',
                       'Nintendo 64/Paper Mario (USA).z64', 'Genesis/Phantasy Star IV (USA).md'],
    'Speedruns': ['SNES/Super Metroid (USA).sfc', 'NES/Mega Man 2 (USA).nes', 'Nintendo 64/Super Mario 64 (USA).z64',
                  'Genesis/Sonic The Hedgehog 2 (USA).md'],
}
# (Roms folder, file, libretro's name of its snap, when it was taken)
SCREENSHOTS = [
    ('SNES', 'Super Metroid (USA).sfc', 'Super Metroid (Japan, USA) (En,Ja)', '2026-11-20 21.12.40'),
    ('Game Boy Advance', 'Metroid Fusion (USA).gba', 'Metroid Fusion (USA)', '2026-11-20 19.05.11'),
    ('PlayStation', 'Castlevania - Symphony of the Night (USA).chd', 'Castlevania - Symphony of the Night (USA)',
     '2026-11-20 12.44.09'),
    ('SNES', 'Super Metroid (USA).sfc', 'Super Metroid (Japan, USA) (En,Ja)', '2026-11-19 22.40.05'),
    ('Nintendo 64', 'Super Mario 64 (USA).z64', 'Super Mario 64 (USA)', '2026-11-19 18.20.48'),
    ('Genesis', 'Sonic The Hedgehog 2 (USA).md', 'Sonic The Hedgehog 2 (World)', '2026-11-18 20.02.31'),
    ('SNES', 'Chrono Trigger (USA).sfc', 'Chrono Trigger (USA)', '2026-11-18 09.15.00'),
    ('Game Boy Advance', 'The Legend of Zelda - The Minish Cap (USA).gba', 'Legend of Zelda, The - The Minish Cap (USA)',
     '2026-11-17 23.31.12'),
]
# Recents, newest first: (tag, Roms folder, file, libretro's name, days before
# today, and the hour and minute it was played, in the launcher's zone:
# env_for's TZ, where the takes' 10:08 is too)
RECENTS = [('SFC', 'SNES', 'Super Metroid (USA).sfc', 'Super Metroid (Japan, USA) (En,Ja)', 0, 9, 12),
           ('GBA', 'Game Boy Advance', 'Metroid Fusion (USA).gba', 'Metroid Fusion (USA)', 1, 21, 40),
           ('PS1', 'PlayStation', 'Castlevania - Symphony of the Night (USA).chd',
            'Castlevania - Symphony of the Night (USA)', 1, 12, 44),
           ('N64', 'Nintendo 64', 'Super Mario 64 (USA).z64', 'Super Mario 64 (USA)', 3, 18, 5),
           ('MD', 'Genesis', 'Sonic The Hedgehog 2 (USA).md', 'Sonic The Hedgehog 2 (World)', 4, 20, 2)]


def v110(card, cache=ART_CACHE):
    # The BIOS files the demonstration's systems need, as stand-ins, so no
    # game's line says one is missing (tools/readme-shots.py's)
    rs.stand_in_bios(card)
    # Your PICO-8: the payload's script and Splore's art, and the two files
    # the player adds as empty stand-ins, so the shelf has it as on the card
    p8 = card / 'ZyperOS/pico8'
    p8.mkdir(parents=True, exist_ok=True)
    shutil.copy(ROOT / 'sd/pixel2/PICO-8.sh', p8 / 'PICO-8.sh')
    for art in ('cover-light.png', 'cover-dark.png', 'icon.png'):
        shutil.copy(ROOT / 'res/pico8' / art, p8 / art)
    bios = card / 'Bios/PICO-8'
    bios.mkdir(parents=True, exist_ok=True)
    for f in ('pico8_64', 'pico8.dat'):
        (bios / f).write_bytes(b'\0')
    (bios / 'version.txt').write_text('0.2.7\n')
    carts = card / 'Roms/PICO-8'
    carts.mkdir(parents=True, exist_ok=True)
    for name in P8_CARTS:
        (carts / f'{name}.p8').touch()
    # Final Fantasy VII by its whole file name, its tags and all, for its
    # details' File row (1.1)
    ps1 = card / 'Roms/PlayStation'
    (ps1 / 'Final Fantasy VII (USA).chd').rename(ps1 / (FF7 + '.chd'))
    (ps1 / '.media/Final Fantasy VII (USA).png').rename(ps1 / '.media' / (FF7 + '.png'))
    for name, games in COLLECTIONS.items():
        (card / 'Collections').mkdir(exist_ok=True)
        (card / 'Collections' / f'{name}.txt').write_text(''.join(f'/Roms/{g}\n' for g in games))
    for folder, file, lr, when in SCREENSHOTS:
        d = card / 'Screenshots' / folder
        d.mkdir(parents=True, exist_ok=True)
        repo = LR_REPO[folder]
        shutil.copyfile(art_file(cache, repo, 'Named_Snaps', lr), d / f'{Path(file).stem} {when}.png')
    # Each recent game's resume point and the picture Diatom writes beside
    # it (a 24-bit BMP), and its play session
    from datetime import datetime, timedelta
    from zoneinfo import ZoneInfo
    now = datetime.now(ZoneInfo(env_for(card)['TZ']))
    with sqlite3.connect(card / 'shared/.zyperos/library.db') as db:
        for n, (tag, folder, file, lr, days, hour, minute) in enumerate(RECENTS):
            day = now - timedelta(days=days)
            at = datetime(day.year, day.month, day.day, hour, minute, tzinfo=now.tzinfo).timestamp()
            d = card / 'shared/.zyperos' / folder
            d.mkdir(parents=True, exist_ok=True)
            (d / f'{file}.auto.state').write_text('state')
            subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', str(art_file(cache, LR_REPO[folder], 'Named_Snaps', lr)),
                            '-pix_fmt', 'bgr24', str(d / f'{file}.auto.bmp')], check=True)
            db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)',
                       (f'sess.{int(at)}.{900 + n}.{tag}\t{file}', '1800\tquit'))
        # Two of Tony Hawk's Pro Skater 2's stand-in cheats on (tools/readme-shots.py's)
        db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)',
                   ('cheats.PS1.' + rs.CHEATS_GAME + '.chd', ','.join(rs.cheat_id(f, d, c) for f, d, c in rs.CHEATS_ON)))
    rs.cheats_card(card)
    return card


def brick_intro(card):
    """The Brick introduction's card (src/brick-intro): 1.1's, with System
    Update offering what a Brick on 1.0.6 was offered, 1.1.0, at the size of
    the release's ZyperOS-v1.1.0-brick-update.zpk (288396245 bytes)"""
    (card / 'ZyperOS/VERSION').write_text('1.0.6\n')
    (card / 'userdata/update/status').write_text(
        'state available\ninstalled 1.0.6\nversion 1.1.0\ndownload 288396245\nkind system\n')
    return card

def env_for(card):
    env = rs.env_for(card)
    # No sound from the computer, and the launcher's clock zone for REC_CLOCK
    env.update(SDL_AUDIODRIVER='dummy', SDL_AUDIO_DRIVER='dummy', TZ='America/New_York')
    return env


def device_settings(card, binary, **kv):
    """The device's settings (userdata/zyperos.db), after the launcher made it"""
    subprocess.run([str(binary), '--dump'], cwd=ROOT, env=env_for(card), check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with sqlite3.connect(card / 'userdata/zyperos.db') as db:
        for k, v in kv.items():
            db.execute('INSERT OR REPLACE INTO settings(key,value) VALUES(?,?)', (k, str(v)))


if __name__ == '__main__':
    target = Path(sys.argv[1]).resolve()
    build(target)
    device_settings(target, ROOT / 'build-native/zyperos', **{'tour.done': 1, 'clock': '24h'})
    print(target)
