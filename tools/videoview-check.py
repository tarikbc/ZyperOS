#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-videoview: the Video view (src/videoview.h, DESIGN.md 11.57) as a
player meets it, in desk mode (src/device/desk.h) in SDL's dummy window,
over its control socket: a shelf of eight games, MENU > View > Video, its
snap playing (`--snap 1`, the test picture).

The snap playing holds the screensaver (DESIGN.md 11.37): past its delay
with nothing pressed, Right still steps to the next game. The screensaver
took the screen over the view with videos of its own, and ate the press
that ended it (2026-11-19, on the 1.1.0 test build: on full-screen video
every key "just expands the collapsed bar"). The covers' glide is
check-games' (vv_draw's frames), the snap held under a page check-snapshelf's.

The Screenshots viewer (DESIGN.md 11.68): at rest nothing covers the shot,
the band off the foot of the screen with no strip and the name's card off
the top (2026-11-20: "the top card that contains the name and etc should
animate and disappear as well ... the collapsed footer can be nothing"); A
there puts them away at once, held too, and the next press brings them back
("when clicking A while in play mode, it goes straight to full without
needing to wait for it to collapse").
build-native/zyperos must be built (make native)."""
import json
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
GAMES = ['Alpha', 'Bravo', 'Charlie', 'Delta', 'Echo', 'Foxtrot', 'Golf', 'Hotel']
fails = []


def check(ok, what):
    print(f'  {"ok  " if ok else "FAIL"} {what}')
    if not ok:
        fails.append(what)


def ask(sock, line, timeout=30):
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
    return json.loads(out.decode() or '{}')


def solid_png(path, w, h, rgb):
    """A plain RGB PNG, no Pillow, as the Screenshot hotkey writes one"""
    import struct
    import zlib

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    raw = b''.join(b'\0' + bytes(rgb) * w for _ in range(h))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def press_once(sock, key):
    """One press and no repeat, however slowly the launcher draws: desk
    mode's own tap (src/device/desk.c), down at once and let go by a timer's
    event 40 ms on. The let-go is read at a poll after the one that read the
    press, so the pass that read it sees the key down; and it comes long
    before the repeat's 300 ms (src/keyrepeat.h), so no pass sees it held
    that long. A press held instead for two drawn frames repeated on a slow
    runner (2026-11-21, CI: a tap of Down two groups on, Right three games)"""
    ask(sock, f'tap {key} 40')


def until(get, ok, secs=30.0):
    """get() again until ok(it) holds or `secs` pass (a busy computer draws
    late); the last one got. It ends as soon as ok holds, so the ceiling is
    only how long a loaded machine may take (2026-11-24: 3 s to 10 s were
    guesses at one, under make check's parallel run)"""
    end = time.time() + secs
    got = get()
    while not ok(got) and time.time() < end:
        time.sleep(0.1)
        got = get()
    return got


def run(tmp, name, snaps, saver, extra, setup=None, last='SFC\nAlpha.sfc\n\n'):
    base = tmp / name
    for d in ('ZyperOS', 'Roms', 'userdata', 'shared', 'web'):
        (base / d).mkdir(parents=True)
    sfc = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
           if l.startswith('sys|') and l.split('|')[4] == 'SFC']
    (base / 'ZyperOS/systems.cfg').write_text(sfc[0] + '\n')
    (base / 'ZyperOS/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    folder = base / 'Roms' / sfc[0].split('|')[2]
    (folder / '.media').mkdir(parents=True)
    for g in GAMES:
        (folder / f'{g}.sfc').touch()
        if snaps:
            (folder / '.media' / f'{g}.mp4').write_bytes(b'\0' * 64)
    env = dict(os.environ, ZYPEROS_ROOT=str(base / 'ZyperOS'), ZYPEROS_CARD=str(base),
               ZYPEROS_ROMS=str(base / 'Roms'), ZYPEROS_USERDATA=str(base / 'userdata'),
               ZYPEROS_SHARED=str(base / 'shared'), ZYPEROS_WEB=str(base / 'web'),
               ZYPEROS_FONT=str(ROOT / 'res/fonts/menu.ttf'))
    subprocess.run([str(BINARY), '--set', 'tour.done=1'], env=env, check=True, stdout=subprocess.DEVNULL)
    with sqlite3.connect(base / 'userdata/zyperos.db') as db:
        db.execute("insert or replace into settings(key,value) values('view.SFC','video')")
        db.execute("insert or replace into settings(key,value) values('saver',?)", (str(saver),))
    if setup:
        setup(base)
    (base / 'ZyperOS/.last').write_text(last)
    sock = Path(tempfile.gettempdir()) / f'zyperos-vvcheck-{os.getpid()}-{name}.sock'
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(tmp / f'{name}-flags'),
               ZYPEROS_CTL_DIR=str(tmp / f'{name}-ctl'))
    (tmp / f'{name}-flags').mkdir()
    env.pop('ZYPEROS_HEADLESS', None)
    log = open(tmp / f'{name}.log', 'w')
    p = subprocess.Popen([str(BINARY), '--device', 'brick', *extra], env=env, stdout=log,
                         stderr=subprocess.STDOUT)
    try:
        end = time.time() + 30
        while time.time() < end and not sock.exists() and p.poll() is None:
            time.sleep(0.05)
        check(sock.exists(), f'{name}: the control socket is up')
        if not sock.exists():
            print((tmp / f'{name}.log').read_text()[-2000:])
            return None
        time.sleep(0.5)
        # A on the first system: its shelf, looked for as late as a slow
        # runner draws it (once: a second A would be on the shelf's game)
        press_once(sock, 'a')
        until(lambda: ask(sock, 'state').get('screen'), lambda s: s == 'games')
        return sock, p, log, tmp / f'{name}.log'
    except Exception:
        p.kill()
        raise


def stop(sock, p, log):
    try:
        ask(sock, 'quit')
        p.wait(timeout=10)
    except Exception:
        pass
    if p.poll() is None:
        p.kill()
        p.wait()
    log.close()
    if sock.exists():
        sock.unlink()


def saver(tmp):
    got = run(tmp, 'saver', True, 3, ['--snap', '1'])
    if not got:
        return
    sock, p, log, logfile = got
    try:
        time.sleep(6.0)                   # twice the screensaver's delay, nothing pressed
        c0 = ask(sock, 'state')['cursor']
        press_once(sock, 'right')
        c1 = until(lambda: ask(sock, 'state')['cursor'], lambda c: c != c0)
        started = 'saver: starts' in logfile.read_text()
        check(not started and c1 == (c0 + 1) % len(GAMES),
              f'the snap playing holds the screensaver: Right steps on past its delay '
              f'(cursor {c0} -> {c1}; the screensaver {"started" if started else "did not start"})')
    finally:
        stop(sock, p, log)


SHOT_RGB = (200, 40, 160)


def shots(tmp):
    """The Screenshots viewer's band and card, read off the panel"""
    try:
        from PIL import Image
    except ImportError:
        check(False, 'the Screenshots viewer: Pillow is missing (pip install pillow)')
        return

    def setup(base):
        d = base / 'Screenshots/SNES'
        d.mkdir(parents=True)
        solid_png(d / 'Alpha (USA) 2026-11-20 09.12.40.png', 256, 224, SHOT_RGB)
        solid_png(d / 'Bravo (USA) 2026-11-19 22.05.11.png', 256, 224, SHOT_RGB)

    got = run(tmp, 'shots', False, 0, [], setup, 'SHOT\n\n\n')
    if not got:
        return
    sock, p, log, logfile = got
    n = [0]

    def look():
        """(the band's foot clear, the name card gone, the top middle the
        shot's): read off the panel"""
        n[0] += 1
        f = tmp / f'shots-{n[0]}.png'
        ask(sock, f'shot {f}')
        im = Image.open(f).convert('RGB')
        w, h = im.size
        near = lambda xy: all(abs(c - t) <= 24 for c, t in zip(im.getpixel(xy), SHOT_RGB))
        return near((w // 2, h - 30)), near((int(w * 0.17), int(h * 0.2))), near((w // 2, int(h * 0.17)))

    def until(ok, secs=30.0):
        """Looks until `ok` holds or `secs` pass (a busy runner draws late);
        the last look. It ends as soon as ok holds: the ceiling is only how
        long a loaded machine may take"""
        end = time.time() + secs
        got = look()
        while not ok(got) and time.time() < end:
            time.sleep(0.15)
            got = look()
        return got

    # A press held until the launcher has drawn two frames with it down, so
    # a busy runner's slow pass cannot miss it, then let go: the shot's
    # picture is fetched only while no key is held (shelf_snap's `!held`)
    def tap(key):
        ask(sock, f'press {key}')
        ask(sock, 'wait-frames 2')
        ask(sock, f'release {key}')

    whole = lambda r: r[2]
    try:
        # the shelf's first shot: the viewer (A again if the shelf was still
        # coming in from the home and let the press go by). Each A waited
        # for long, so a viewer slow to open on a loaded machine is not
        # taken for a press let go by, and A again pressed in the viewer,
        # which puts the band away (2026-11-24: 4 s was too short)
        for _ in range(4):
            tap('a')
            foot, card, top = until(whole, 15)
            if top:
                break
        check(top and not foot and not card, f'the viewer opens with its band and card up '
              f'(the shot {"whole" if top else "not open"}, the foot {"clear" if foot else "covered"}, '
              f'the card {"gone" if card else "there"})')
        tap('right')                                         # a press: the rest starts again
        until(lambda r: r[2] and not r[0], 10)               # the next shot in, the band up
        t0 = time.time()
        tap('a')                                             # A: away at once, not 2 s on
        foot, card, top = until(lambda r: r[0] and r[1], 1.5)
        check(foot and card, f'A puts the band and the card away at once, nothing left at the foot '
              f'(the foot {"clear" if foot else "covered"}, the card {"gone" if card else "there"}, '
              f'after {time.time() - t0:.1f} s; the rest would take 2 s)')
        ask(sock, 'press a')                                 # A held: they stay away
        time.sleep(1.2)
        foot, card, top = look()
        ask(sock, 'release a')
        check(foot and card, 'A held keeps them away')
        tap('right')                                         # another press: back up
        foot, card, top = until(lambda r: not r[0] and not r[1])
        check(not foot and not card, 'the next press brings the band and the card back')
        tap('b')                                             # B still goes back from the viewer
        foot, card, top = until(lambda r: not r[2])
        check(not top, 'B goes back to the shelf from the viewer')
    finally:
        stop(sock, p, log)


def main():
    if not BINARY.is_file():
        print('check-videoview: build-native/zyperos missing (make native)')
        return 1
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-vvcheck-'))
    try:
        saver(tmp)
        shots(tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'videoview: {"all passed" if not fails else f"{len(fails)} failed"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
