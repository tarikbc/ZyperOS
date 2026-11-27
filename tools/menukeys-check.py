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
past it unlocks. MENU > System > Hotkeys' footer says what the row under
the cursor is; MENU's capture keeps MENU's glyph as its screen leaves;
Hold to Unlock at 0.5 s opens the lock in less than a second. And the
tutorial, pressed through to its lock lessons: MENU+L2 locks only the stage,
SELECT+START held opens it, and Music takes SELECT alone. And in a game:
RUN carries menukeys= to Diatom, and its PAUSED for=lock locks the
handheld over the paused game until SELECT+START opens it (RESUME).
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


def tour(tmp, env):
    """The tutorial's lock lessons in desk mode, pressed through from the
    language step (the Pixel 2 test of 1.1.1-test.3): MENU+L2 in Lock the
    Screen locks nothing but the stage, and is its press; SELECT+START held
    past Hold to Unlock in Unlock the Screen is its press; and Music takes
    SELECT alone, a shoulder there a wrong press."""
    env = dict(env)
    sock = Path(tempfile.gettempdir()) / f'zyperos-mkcheck-tour-{os.getpid()}.sock'
    env['ZYPEROS_DESK_SOCK'] = str(sock)
    # the tutorial from its start, with Hold to Unlock back at 2 s
    subprocess.run([str(BINARY), '--set', 'tour.done=0'], env=env, check=True, stdout=subprocess.DEVNULL)
    subprocess.run([str(BINARY), '--set', 'menukeys='], env=env, check=True, stdout=subprocess.DEVNULL)
    logfile = tmp / 'tour.log'
    log = open(logfile, 'w')
    p = subprocess.Popen([str(BINARY), '--device', 'pixel2'], env=env, stdout=log, stderr=subprocess.STDOUT)

    def step():
        said = [l for l in logfile.read_text().splitlines() if l.startswith('tour: the ') and l.endswith(' step')]
        return said[-1][len('tour: the '):-len(' step')] if said else None

    def to(name, secs=4.0):
        return until(step, lambda s: s == name, secs)

    try:
        for _ in range(200):
            if sock.exists() or p.poll() is not None:
                break
            time.sleep(0.05)
        check(sock.exists(), 'the tutorial\'s control socket is up')
        if not sock.exists():
            return
        time.sleep(1.0)
        for key, then in (('a', 'welcome'), ('a', 'move'), ('right', None), ('left', 'a'), ('a', 'b'),
                          ('b', 'x'), ('x', 'y'), ('y', 'menu'), ('menu', 'quit')):
            ask(sock, f'tap {key} 40')
            if then:
                to(then)
            time.sleep(0.35)
        ask(sock, 'tap menu 40')
        time.sleep(0.4)
        ask(sock, 'tap x 40')
        check(to('lock') == 'lock', f'pressed through to Lock the Screen ({step()})')
        time.sleep(0.4)
        ask(sock, 'press menu')
        time.sleep(0.2)
        ask(sock, 'tap l2 40')
        time.sleep(0.2)
        ask(sock, 'release menu')
        check(to('unlock') == 'unlock', f'MENU+L2 is the lock lesson\'s press ({step()})')
        check('lock: locked' not in logfile.read_text(),
              'and locks nothing but the stage: the launcher\'s lock never comes')
        time.sleep(0.5)
        ask(sock, 'press select+start')
        time.sleep(1.0)
        ask(sock, 'release select+start')
        time.sleep(0.6)
        check(step() == 'unlock' and 'tour: tour.done' in logfile.read_text(),
              'SELECT+START let go after 1 s: still locked')
        ask(sock, 'press select+start')
        time.sleep(2.4)
        ask(sock, 'release select+start')
        check(to('select') == 'select', f'held past Hold to Unlock: the unlock lesson\'s press, and Music comes ({step()})')
        # a line of the lock's own, not the clock's ("clock: auto zone ...")
        check(not any(l.startswith('lock: ') for l in logfile.read_text().splitlines()),
              'and the launcher was never locked or unlocked')
        time.sleep(0.5)
        for key in ('l1', 'l2', 'r2'):
            ask(sock, f'tap {key} 40')
            time.sleep(1.3)
            check(step() == 'select', f'{key.upper()} in Music is no right answer: Music still waits for SELECT')
        ask(sock, 'tap select 40')
        check(to('vol') == 'vol', f'SELECT lands it ({step()})')
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
            print(logfile.read_text()[-2000:])


def in_game(tmp):
    """MENU's lock chord in a game, the launcher's half (Diatom's own is its
    test/hotkeys.py): RUN carries menukeys= with every game, and PAUSED
    for=lock from Diatom locks the handheld over the paused game, SELECT+
    START held opens it and the game goes on (RESUME). The real launcher in
    desk mode with a Diatom of the check's own on the resident's socket, as
    check-ingame-colors runs it: a game a power-off left running, so the
    start goes straight back into it with the menu up."""
    import importlib.machinery
    import threading

    def module(name, path):
        loader = importlib.machinery.SourceFileLoader(name, str(path))
        spec = importlib.util.spec_from_loader(name, loader)
        mod = importlib.util.module_from_spec(spec)
        loader.exec_module(mod)
        return mod

    import importlib.util
    gm = module('ingame_colors', ROOT / 'tools/ingame-colors-check.py')
    desk = module('desk', ROOT / 'tools/desk')
    rs = module('readme_shots', ROOT / 'tools/readme-shots.py')

    class Diatom(gm.Diatom):
        """And a line of its own to the launcher, as a hotkey's pause is"""
        def __init__(self, path):
            self.conns = []
            super().__init__(path)

        def talk(self, c):
            self.conns.append(c)
            super().talk(c)

        def push(self, line):
            for c in list(self.conns):
                try:
                    c.sendall((line + '\n').encode())
                except OSError:
                    pass

    card = tmp / 'game-card'
    rs.demo_card(card, None, {'artwork': {}})
    desk.card_sounds(card)
    subprocess.run([str(BINARY), '--set', 'tour.done=1'], env=desk.card_env(card, None), check=True,
                   stdout=subprocess.DEVNULL)
    gm.game_left_running(card)
    sock = Path(tempfile.gettempdir()) / f'zyperos-mkcheck-game-{os.getpid()}.sock'
    dsock = Path(tempfile.gettempdir()) / f'zyperos-mkcheck-game-{os.getpid()}-d.sock'
    for f in (sock, dsock):
        if f.exists():
            f.unlink()
    (tmp / 'game-flags').mkdir(exist_ok=True)
    env = desk.card_env(card, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(tmp / 'game-flags'),
               ZYPEROS_CTL_DIR=str(tmp / 'game-ctl'), ZYPEROS_DIATOM_SOCKET=str(dsock),
               ZYPEROS_DIATOM_PIDFILE=str(tmp / 'game-diatom.pid'))
    env.pop('ZYPEROS_HEADLESS', None)
    diatom = Diatom(dsock)
    logfile = tmp / 'game.log'
    log = open(logfile, 'w')
    p = subprocess.Popen([str(BINARY), '--device', 'pixel2'], env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(200):
            if (sock.exists() and 'PAUSE' in diatom.words()) or p.poll() is not None:
                break
            time.sleep(0.05)
        check(sock.exists() and 'PAUSE' in diatom.words(), 'a game: the start goes back into it, the menu up')
        if not sock.exists():
            return
        run = [l for l in diatom.lines if l.startswith('RUN\t')]
        check(run and '\tmenukeys=shot:l1,lock:l2' in run[-1],
              f'RUN carries MENU\'s chords to Diatom: menukeys=shot:l1,lock:l2 ({run[-1][:60] if run else None}...)')
        ask(sock, 'wait-frames 8', timeout=30)
        ask(sock, 'tap b 40')
        resumed = until(lambda: diatom.words().count('RESUME'), lambda n: n >= 1, 5.0)
        check(resumed >= 1, 'B closes the menu, the game goes on')
        time.sleep(0.3)
        diatom.push('PAUSED\tfor=lock')
        locked = until(lambda: "lock: locked (MENU's chord, over a game)" in logfile.read_text(), lambda x: x, 5.0)
        check(locked, 'PAUSED for=lock: the handheld locks over the paused game')
        time.sleep(0.5)
        ask(sock, 'press select+start')
        time.sleep(1.0)
        ask(sock, 'release select+start')
        time.sleep(0.4)
        check(diatom.words().count('RESUME') == 1 and 'lock: unlocked' not in logfile.read_text(),
              'SELECT+START held 1 s: still locked, the game still paused')
        ask(sock, 'press select+start')
        time.sleep(2.6)
        # once the game goes on the launcher waits on it, and desk mode's
        # socket with it: the let-go is not answered
        try:
            ask(sock, 'release select+start', timeout=2)
        except OSError:
            pass
        back = until(lambda: diatom.words().count('RESUME'), lambda n: n >= 2, 5.0)
        check('lock: unlocked' in logfile.read_text() and back >= 2,
              'held past Hold to Unlock: open, and the game goes on (RESUME)')
        check(diatom.words().count('STOP') == 0, 'and the game was never stopped')
    finally:
        try:
            ask(sock, 'quit', timeout=2)
            p.wait(timeout=10)
        except Exception:
            pass
        if p.poll() is None:
            p.kill()
            p.wait()
        log.close()
        diatom.close()
        for f in (sock, dsock):
            if f.exists():
                f.unlink()
        if fails:
            print(logfile.read_text()[-2000:])


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
        ask(sock, 'tap right 40')                  # from Cancel to Delete, beside it (#479)
        time.sleep(0.4)
        ask(sock, 'tap a 40')
        gone = until(lambda: sorted(shots.glob('ZyperOS *.png')), lambda l: not l, 5.0)
        check(not gone, f'X deletes it from the card ({[g.name for g in gone]})')

        # MENU > System > Hotkeys: the footer says what the row under the
        # cursor is (the Pixel 2 test of 1.1.1-test.3: "Home Screen is
        # fixed" on every row); MENU's capture keeps MENU's glyph as it
        # leaves (it turned into SELECT's); Hold to Unlock has 0.5 s
        ask(sock, 'tap b 40')
        time.sleep(0.6)
        ask(sock, 'tap menu 40')
        until(lambda: ask(sock, 'state').get('screen'), lambda x: x not in ('systems', 'games'), 4.0)
        for _ in range(8):
            if ask(sock, 'state').get('screen') == 'System':
                break
            ask(sock, 'tap r1 40')
            time.sleep(0.4)
        for _ in range(16):
            if ask(sock, 'state').get('focus') == 'Hotkeys':
                break
            ask(sock, 'tap down 40')
            time.sleep(0.3)
        ask(sock, 'tap a 40')
        time.sleep(1.0)

        def footer():
            said = [l for l in logfile.read_text().splitlines() if l.startswith("hotkeys: the footer says '")]
            return said[-1][len("hotkeys: the footer says '"):-1] if said else None

        rows = []
        for _ in range(14):
            st = ask(sock, 'state')
            if rows and st.get('cursor') == rows[0][0]:
                break
            rows.append((st.get('cursor'), st.get('focus'), footer()))
            ask(sock, 'tap down 40')
            time.sleep(0.35)
        game = 'In a game, with SELECT held.'
        names = [r[1] for r in rows]
        check(names[:2] == ['Quick Save', 'Quick Load'] and names[-3:] == ['Screenshot', 'Lock Screen', 'Hold to Unlock'],
              f'the Hotkeys list from Quick Save to Hold to Unlock ({names})')
        # Fast Forward Speed (#474) is Fast Forward's speed, no hotkey: no line
        keys = [r for r in rows[:-3] if r[1] != 'Fast Forward Speed']
        check(all(r[2] == game for r in keys),
              f'each of the game\'s hotkeys: "{game}" ({[(r[1], r[2]) for r in keys]})')
        check([r[2] for r in rows[-3:]] == ['On every screen, in a game too.'] * 2 + ['How long to hold SELECT+START.'],
              f'MENU\'s chords and Hold to Unlock say theirs ({[(r[1], r[2]) for r in rows[-3:]]})')
        check(not any(r[2] and 'Home' in r[2] for r in rows), 'and no row says the Home row\'s line')

        # MENU's Screenshot: MENU held, R1, MENU let go; the glyph stays MENU's
        for _ in range(16):
            if ask(sock, 'state').get('focus') == 'Lock Screen':
                break
            ask(sock, 'tap down 40')
            time.sleep(0.3)
        ask(sock, 'tap up 40')
        time.sleep(0.4)
        mark = len(logfile.read_text().splitlines())
        ask(sock, 'tap a 40')
        time.sleep(0.8)
        ask(sock, 'press menu')
        time.sleep(0.3)
        ask(sock, 'tap r1 40')
        time.sleep(0.3)
        ask(sock, 'release menu')
        time.sleep(1.2)
        shown = [l[len('hotkeys: the capture shows '):] for l in logfile.read_text().splitlines()[mark:]
                 if l.startswith('hotkeys: the capture shows ')]
        check(shown and shown[-1] == 'MENU+R1' and all(k.startswith('MENU') for k in shown),
              f'MENU+R1 captured, and its glyph stays MENU\'s as the screen leaves ({shown})')

        # Hold to Unlock: 2 s, left to 1 s and to 0.5 s; the lock opens in 0.8 s
        for _ in range(4):
            if ask(sock, 'state').get('focus') == 'Hold to Unlock':
                break
            ask(sock, 'tap down 40')
            time.sleep(0.3)
        ask(sock, 'tap left 40')
        time.sleep(0.3)
        ask(sock, 'tap left 40')
        time.sleep(0.3)
        ask(sock, 'tap menu 40')            # MENU closes every screen
        until(lambda: ask(sock, 'state').get('screen'), lambda x: x in ('systems', 'games'), 4.0)
        time.sleep(0.8)
        n0 = logfile.read_text().count('lock: unlocked')
        ask(sock, 'press menu')
        time.sleep(0.2)
        ask(sock, 'tap l2 40')
        time.sleep(0.2)
        ask(sock, 'release menu')
        time.sleep(0.6)
        ask(sock, 'press select+start')
        time.sleep(0.8)
        ask(sock, 'release select+start')
        opened = until(lambda: logfile.read_text().count('lock: unlocked'), lambda x: x > n0, 2.0)
        check(opened > n0, 'Hold to Unlock at 0.5 s: SELECT+START held 0.8 s opens the lock')
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
        else:
            tour(tmp, env)
        if not fails:
            in_game(tmp)
        shutil.rmtree(tmp, ignore_errors=True)
    print(f'menukeys: {"all passed" if not fails else f"{len(fails)} failed"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
