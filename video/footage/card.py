#!/usr/bin/env python3
"""The tour video's card: a demonstration library with the games' real box art.

python3 video/footage/card.py CARD

Builds a card at CARD the host launcher (build-native/zyperos) can run on:
the README's demonstration library (tools/readme-shots.py), eight systems of
well-known games as empty files, each with its cover from the
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
        ('The Legend of Zelda - The Minish Cap (USA).gba', 'Legend of Zelda, The - The Minish Cap (USA)')],
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
}
LR_REPO = {'PlayStation': 'Sony_-_PlayStation', 'NES': 'Nintendo_-_Nintendo_Entertainment_System',
           'SNES': 'Nintendo_-_Super_Nintendo_Entertainment_System', 'Game Boy': 'Nintendo_-_Game_Boy',
           'Game Boy Advance': 'Nintendo_-_Game_Boy_Advance', 'Genesis': 'Sega_-_Mega_Drive_-_Genesis',
           'Nintendo 64': 'Nintendo_-_Nintendo_64', 'Dreamcast': 'Sega_-_Dreamcast',
           'Nintendo DS': 'Nintendo_-_Nintendo_DS'}

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


def build(card, cache=ART_CACHE):
    if card.exists():
        shutil.rmtree(card)
    rs.card_dirs(card)
    # The version System Update starts from (the one before this release)
    (card / 'ZyperOS/VERSION').write_text('1.0.1\n')
    for folder, games in LIBRARY.items():
        d = card / 'Roms' / folder
        (d / '.media').mkdir(parents=True)
        for file, lr in games:
            (d / file).touch()
            got = art_file(cache, LR_REPO[folder], 'Named_Boxarts', lr)
            shutil.copyfile(got, d / '.media' / (Path(file).stem + '.png'))
    # The DS game's screenshot, behind its in-game menu
    art_file(cache, LR_REPO['Nintendo DS'], 'Named_Snaps', LIBRARY['Nintendo DS'][0][1])
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
    # ZyperOS's own menu music, the one track the video plays too, so the
    # song Zyper names on the home is the one heard
    (card / 'ZyperOS/res/bgm').mkdir(parents=True)
    shutil.copyfile(ROOT / 'res/bgm/The Arplands.mp3', card / 'ZyperOS/res/bgm/The Arplands.mp3')
    shutil.copytree(ROOT / 'res/tour', card / 'ZyperOS/res/tour')
    up = card / 'userdata/update'
    up.mkdir(parents=True)
    # 1.0.1 offered 1.0.2, as it was (83 MB, the two-slot download's size)
    (up / 'status').write_text('state available\ninstalled 1.0.1\nversion 1.0.2\ndownload 83000000\nkind system\n')
    (up / 'source').write_text('file:///nonexistent\n')
    # Ports: PortMaster's installer, for the Ports shelf's offer
    (card / 'Roms/Ports').mkdir(exist_ok=True)
    shutil.copyfile(ROOT / 'res/portmaster/pm-install.py', card / 'ZyperOS/portmaster/pm-install.py')
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
