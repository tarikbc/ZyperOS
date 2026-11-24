#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-menukeys: MENU's chords on every screen (src/menukeys.h, DESIGN.md
11.61) as a player meets them, in desk mode (src/device/desk.h) in SDL's
dummy window, over its control socket, on a shelf.

MENU held and L1 pressed saves the screen to Screenshots/ZyperOS on the card,
a PNG at the drawing's size, and MENU let go after it opens nothing; the shot
is on the home's Screenshots group as ZyperOS, opens whole and is deleted
with X as a game's; MENU
tapped alone still opens the menu, as it is let go. MENU held and L2 pressed
locks: SELECT+START held for less than Hold to Unlock keeps it locked, held
past it unlocks.
build-native/zyperos must be built (make native)."""
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
GAMES = ['Alpha', 'Bravo', 'Charlie']
fails = []


def check(ok, what):
    print(f'  {"ok  " if ok else "FAIL"} {what}')
    if not ok:
        fails.append(what)


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
    return json.loads(out.decode() or '{}')


def until(get, ok, secs=5.0):
    end = time.time() + secs
    got = get()
    while not ok(got) and time.time() < end:
        time.sleep(0.1)
        got = get()
    return got


def png_size(path):
    with open(path, 'rb') as f:
        head = f.read(24)
    return struct.unpack('>II', head[16:24]) if head[:8] == b'\x89PNG\r\n\x1a\n' else None


def frames(sock, tmp, name):
    """The window as it is now, for a before and after"""
    f = tmp / f'{name}.png'
    ask(sock, f'shot {f}')
    return f


def apart(a, b):
    """How much of the screen differs between two shots, 0 to 1: the clock
    in the corner and the focused card's motion move a little on their own,
    a menu opening moves a great deal"""
    from PIL import Image, ImageChops

    x, y = Image.open(a).convert('RGB'), Image.open(b).convert('RGB')
    if x.size != y.size:
        return 1.0
    d = ImageChops.difference(x, y).convert('L').point(lambda v: 255 if v > 24 else 0)
    return d.histogram()[255] / (x.size[0] * x.size[1])


def main():
    if not BINARY.is_file():
        print('check-menukeys: build-native/zyperos missing (make native)')
        return 1
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-mkcheck-'))
    base = tmp / 'card'
    for d in ('ZyperOS', 'Roms', 'userdata', 'shared', 'web'):
        (base / d).mkdir(parents=True)
    sfc = [l for l in (ROOT / 'config/systems.cfg').read_text().splitlines()
           if l.startswith('sys|') and l.split('|')[4] == 'SFC']
    (base / 'ZyperOS/systems.cfg').write_text(sfc[0] + '\n')
    (base / 'ZyperOS/system-icons').symlink_to(ROOT / 'res/system-icons', target_is_directory=True)
    folder = base / 'Roms' / sfc[0].split('|')[2]
    folder.mkdir(parents=True)
    for g in GAMES:
        (folder / f'{g}.sfc').touch()
    env = dict(os.environ, ZYPEROS_ROOT=str(base / 'ZyperOS'), ZYPEROS_CARD=str(base),
               ZYPEROS_ROMS=str(base / 'Roms'), ZYPEROS_USERDATA=str(base / 'userdata'),
               ZYPEROS_SHARED=str(base / 'shared'), ZYPEROS_WEB=str(base / 'web'),
               ZYPEROS_FONT=str(ROOT / 'res/fonts/menu.ttf'))
    subprocess.run([str(BINARY), '--set', 'tour.done=1'], env=env, check=True, stdout=subprocess.DEVNULL)
    (base / 'ZyperOS/.last').write_text('SFC\nAlpha.sfc\n\n')
    sock = Path(tempfile.gettempdir()) / f'zyperos-mkcheck-{os.getpid()}.sock'
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(tmp / 'flags'),
               ZYPEROS_CTL_DIR=str(tmp / 'ctl'))
    (tmp / 'flags').mkdir()
    env.pop('ZYPEROS_HEADLESS', None)
    logfile = tmp / 'launcher.log'
    log = open(logfile, 'w')
    p = subprocess.Popen([str(BINARY), '--device', 'brick'], env=env, stdout=log, stderr=subprocess.STDOUT)
    shots = base / 'Screenshots/ZyperOS'
    try:
        for _ in range(200):
            if sock.exists() or p.poll() is not None:
                break
            time.sleep(0.05)
        check(sock.exists(), 'the control socket is up')
        if not sock.exists():
            print(logfile.read_text()[-2000:])
            return 1
        time.sleep(1.0)
        ask(sock, 'tap a 40')
        until(lambda: ask(sock, 'state').get('screen'), lambda s: s == 'games', 10.0)
        time.sleep(1.0)
        before = frames(sock, tmp, 'before')

        # MENU+L1: the screen on the card, and no menu as MENU is let go
        ask(sock, 'press menu')
        time.sleep(0.2)
        ask(sock, 'tap l1 40')
        time.sleep(0.2)
        ask(sock, 'release menu')
        got = until(lambda: sorted(shots.glob('ZyperOS *.png')) if shots.is_dir() else [], lambda l: l, 6.0)
        keep = os.environ.get('MENUKEYS_KEEP')      # a folder for the pictures, to look at
        if keep:
            time.sleep(0.4)
            Path(keep).mkdir(parents=True, exist_ok=True)
            shutil.copy(frames(sock, tmp, 'notice'), Path(keep) / 'notice.png')
            if got:
                shutil.copy(got[0], Path(keep) / 'screenshot.png')
        check(len(got) == 1, f'MENU+L1 saves one screenshot in Screenshots/ZyperOS ({[g.name for g in got]})')
        if got:
            check(png_size(got[0]) == (1024, 768), f'  ...a PNG of the drawing, 1024 x 768 ({png_size(got[0])})')
        time.sleep(2.5)                      # the notice up and gone
        after = frames(sock, tmp, 'after')
        k = apart(before, after)
        check(k < 0.05, f'MENU let go after the chord opens no menu: the shelf as it was ({k:.1%} apart)')

        # MENU alone: the menu, as it is let go
        ask(sock, 'press menu')
        time.sleep(0.3)
        held = frames(sock, tmp, 'held')
        ask(sock, 'release menu')
        time.sleep(1.0)
        opened = frames(sock, tmp, 'opened')
        k = apart(before, held)
        check(k < 0.05, f'MENU held alone opens nothing yet ({k:.1%} apart)')
        k = apart(before, opened)
        check(k > 0.15, f'MENU let go alone opens the menu ({k:.1%} apart)')
        ask(sock, 'tap b 40')
        time.sleep(1.0)

        # MENU+L2: locked; SELECT+START held 1 s keeps it, 2.5 s opens it
        ask(sock, 'press menu')
        time.sleep(0.2)
        ask(sock, 'tap l2 40')
        time.sleep(0.2)
        ask(sock, 'release menu')
        locked = until(lambda: 'lock: locked' in logfile.read_text(), lambda x: x, 4.0)
        check(locked, 'MENU+L2 locks')
        if keep:
            shutil.copy(frames(sock, tmp, 'locked'), Path(keep) / 'locked.png')
        time.sleep(0.5)
        ask(sock, 'press select+start')
        time.sleep(1.0)
        ask(sock, 'release select+start')
        time.sleep(0.5)
        check('lock: unlocked' not in logfile.read_text(), 'SELECT+START held 1 s keeps it locked (Hold to Unlock 2 s)')
        ask(sock, 'press select+start')
        time.sleep(2.6)
        ask(sock, 'release select+start')
        unlocked = until(lambda: 'lock: unlocked' in logfile.read_text(), lambda x: x, 3.0)
        check(unlocked, 'SELECT+START held past 2 s unlocks')
        ask(sock, 'tap right 40')
        moved = until(lambda: ask(sock, 'state').get('cursor'), lambda c: c == 1, 4.0)
        check(moved == 1, f'and the shelf takes presses again (cursor {moved})')

        # The shot on the home's Screenshots group, as a game's: newest
        # first, named ZyperOS, opened whole, deleted with X
        text = logfile.read_text()
        check('scan: Screenshots      1 screenshot' in text, 'the Screenshots group is read again with the shot on it')
        ask(sock, 'tap b 40')
        until(lambda: ask(sock, 'state').get('screen'), lambda x: x == 'systems', 5.0)
        for _ in range(6):
            if ask(sock, 'state').get('focus') == 'SHOT':
                break
            ask(sock, 'tap left 40')
            time.sleep(0.4)
        st = ask(sock, 'state')
        check(st.get('focus') == 'SHOT', f'the home has the Screenshots group ({st.get("screen")}, {st.get("focus")})')
        ask(sock, 'tap a 40')
        st = until(lambda: ask(sock, 'state'), lambda x: x.get('screen') == 'games', 5.0)
        check(st.get('focus') == 'ZyperOS', f'on it the launcher\'s shot, as ZyperOS ({st.get("focus")})')
        ask(sock, 'tap a 40')                      # the viewer, the shot whole
        time.sleep(1.0)
        if keep:
            shutil.copy(frames(sock, tmp, 'viewer'), Path(keep) / 'viewer.png')
        ask(sock, 'tap x 40')                      # Delete Screenshot's question
        time.sleep(0.8)
        ask(sock, 'tap up 40')                     # from Cancel to Delete
        time.sleep(0.4)
        ask(sock, 'tap a 40')
        gone = until(lambda: sorted(shots.glob('ZyperOS *.png')), lambda l: not l, 5.0)
        check(not gone, f'X deletes it from the card ({[g.name for g in gone]})')
    finally:
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
        if fails:
            print(logfile.read_text()[-3000:])
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'menukeys: {"all passed" if not fails else f"{len(fails)} failed"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
