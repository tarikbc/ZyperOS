#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-saver-art: the screensaver's games and pictures are the shelf's
(src/esmedia.h's game_media_listed and game_media), in desk mode
(src/device/desk.h) in SDL's dummy window, the real loop left alone past
the screensaver's delay.

2026-11-23, moroboshi69 on the Discord (a Brick on 1.1.0): the screensaver
only ever showed his PlayStation, whose covers are in .media; his other
systems keep theirs where EmulationStation's gamelist.xml names them,
<image>./images/...</image>, which the shelf shows, and never came on. It
read .media alone.

Each card has one system and nothing pressed after its shelf opens:
  - es-cover: a SNES folder whose gamelist.xml names each game's picture in
    images/, nothing in .media: the screensaver starts on its covers;
  - es-names: the same pictures by EmulationStation's names
    (images/<game>-thumb.png), with no gamelist.xml;
  - es-video: a video by EmulationStation's names (videos/<game>-video.mp4):
    the screensaver starts on its videos;
  - own-cover: the game's own .media/<game>.png, as before;
  - none: nothing anywhere: it has nothing to show and says so.
build-native/zyperos must be built (make native)."""
import os
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
GAMES = ['Alpha', 'Bravo', 'Charlie']
fails = []


def check(ok, what):
    print(f'  {"ok  " if ok else "FAIL"} {what}')
    if not ok:
        fails.append(what)


def png(path, rgb=(40, 90, 200)):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(rgb) * 64 for _ in range(64))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 64, 64, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def ask(sock, line, timeout=10):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(timeout)
    s.connect(str(sock))
    s.sendall((line + '\n').encode())
    s.shutdown(socket.SHUT_WR)
    out = b''
    while True:
        b = s.recv(4096)
        if not b:
            break
        out += b
    return out


def card(tmp, name, setup):
    base = tmp / name
    for d in ('ZyperOS', 'Roms', 'userdata', 'shared', 'web'):
        (base / d).mkdir(parents=True)
    sfc = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
           if l.startswith('sys|') and l.split('|')[4] == 'SFC']
    (base / 'ZyperOS/systems.cfg').write_text(sfc[0] + '\n')
    (base / 'ZyperOS/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    folder = base / 'Roms' / sfc[0].split('|')[2]
    folder.mkdir(parents=True)
    for g in GAMES:
        (folder / f'{g}.sfc').write_bytes(b'\0' * 16)
    setup(folder)
    return base


def es_cover(folder):
    (folder / 'images').mkdir()
    rows = ''
    for g in GAMES:
        png(folder / 'images' / f'{g} box.png')
        rows += f'\t<game>\n\t\t<path>./{g}.sfc</path>\n\t\t<name>{g}</name>\n' \
                f'\t\t<image>./images/{g} box.png</image>\n\t</game>\n'
    (folder / 'gamelist.xml').write_text('<?xml version="1.0"?>\n<gameList>\n' + rows + '</gameList>\n')


def es_names(folder):
    (folder / 'images').mkdir()
    for g in GAMES:
        png(folder / 'images' / f'{g}-thumb.png')


def es_video(folder):
    (folder / 'videos').mkdir()
    for g in GAMES:
        (folder / 'videos' / f'{g}-video.mp4').write_bytes(b'\0' * 64)


def own_cover(folder):
    (folder / '.media').mkdir()
    for g in GAMES:
        png(folder / '.media' / f'{g}.png')


def nothing(folder):
    pass


def run(tmp, name, setup, wait_s=30.0):
    """The launcher on a card of `setup`'s, its shelf opened, then nothing
    pressed past the screensaver's delay (2 s); its log"""
    base = card(tmp, name, setup)
    env = dict(os.environ, ZYPEROS_ROOT=str(base / 'ZyperOS'), ZYPEROS_CARD=str(base),
               ZYPEROS_ROMS=str(base / 'Roms'), ZYPEROS_USERDATA=str(base / 'userdata'),
               ZYPEROS_SHARED=str(base / 'shared'), ZYPEROS_WEB=str(base / 'web'),
               ZYPEROS_FONT=str(ROOT / 'res/fonts/menu.ttf'))
    subprocess.run([str(BINARY), '--set', 'tour.done=1'], env=env, check=True, stdout=subprocess.DEVNULL)
    with sqlite3.connect(base / 'userdata/zyperos.db') as db:
        db.execute("insert or replace into settings(key,value) values('saver','2')")
    (base / 'ZyperOS/.last').write_text('SFC\nAlpha.sfc\n\n')
    sock = Path(tempfile.gettempdir()) / f'zyperos-saverart-{os.getpid()}-{name}.sock'
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(tmp / f'{name}-flags'),
               ZYPEROS_CTL_DIR=str(tmp / f'{name}-ctl'))
    (tmp / f'{name}-flags').mkdir()
    env.pop('ZYPEROS_HEADLESS', None)
    logfile = tmp / f'{name}.log'
    with open(logfile, 'w') as log:
        p = subprocess.Popen([str(BINARY), '--device', 'brick'], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            for _ in range(200):
                if sock.exists() or p.poll() is not None:
                    break
                time.sleep(0.05)
            if sock.exists():
                time.sleep(0.5)
                ask(sock, 'tap a 40')
                # The saver's start, waited for in the log, not guessed
                end = time.time() + wait_s
                while time.time() < end and 'saver: starts' not in logfile.read_text() \
                        and 'saver: nothing to show' not in logfile.read_text():
                    time.sleep(0.1)
                time.sleep(0.3)
                ask(sock, 'quit')
                p.wait(timeout=10)
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
            if sock.exists():
                sock.unlink()
    return logfile.read_text()


def main():
    if not BINARY.is_file():
        print('check-saver-art: build-native/zyperos is missing (make native)')
        return 1
    with tempfile.TemporaryDirectory(prefix='zyperos-saverart-') as t:
        tmp = Path(t)
        log = run(tmp, 'es-cover', es_cover)
        check('saver: starts, 3 games (no snaps on the card: covers)' in log,
              "gamelist.xml's ./images/ covers: the screensaver starts on them")
        check('saver: SNES/' in log and '(cover)' in log, "...and shows one of the system's games")
        log = run(tmp, 'es-names', es_names)
        check('saver: starts, 3 games (no snaps on the card: covers)' in log,
              "EmulationStation's images/<game>-thumb.png with no gamelist.xml: the same")
        log = run(tmp, 'es-video', es_video)
        check('saver: starts, 3 games with a video' in log,
              "EmulationStation's videos/<game>-video.mp4: the screensaver starts on the videos")
        log = run(tmp, 'own-cover', own_cover)
        check('saver: starts, 3 games (no snaps on the card: covers)' in log, "the game's own .media cover, as before")
        log = run(tmp, 'none', nothing)
        check('saver: nothing to show' in log and 'saver: starts' not in log,
              'no picture anywhere: nothing to show')
    if fails:
        print(f'check-saver-art: {len(fails)} failed')
        return 1
    print("ok: the screensaver shows every game the shelf has a picture or a video of, EmulationStation's too")
    return 0


if __name__ == '__main__':
    sys.exit(main())
