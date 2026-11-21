#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Screen Colors in a game (DESIGN.md 11.42): the in-game menu's Display tab
has a Screen Colors row where the display has a table, and A on it opens
MENU > Display's own Screen Colors page over the paused game.

    python3 tools/ingame-colors-check.py        (make check-ingame-colors, after make native)
    python3 tools/ingame-colors-check.py --binary PATH    another launcher's walk

The real launcher in desk mode's dummy window, as check-mp4resume runs it, on
the README's demonstration card, with a Diatom of the check's own on the
resident's socket (src/resident.c): a game a power-off left running, so the
start goes straight back into it with the menu up. Then, as the Pixel 2 and
as the Brick:
  - R1 to the Display tab: Screen Colors is drawn, a row of it;
  - Up twice, round past the first row: the row right over On-Screen Stats,
    the tab's last; A on it: the page's rows (Temperature) are drawn;
  - Right on its Look: Standard to Vivid, kept in the device's database at
    once and on the panel (desk mode's stand-in for the display's table,
    `tools/desk state`'s colors), all while the game is paused, the
    stand-in Diatom told nothing;
  - B: back to the menu, the game still paused (no RESUME);
  - B again: the menu closes and the game goes on (RESUME).
And as the host's own profile, which has no table: no Screen Colors row.

Over your PICO-8 too (1.1.0-rc.8, its menu on the game's tabs): the real
launcher as portkeys starts it over the program (--port-menu PICO8 CART),
wearing the Pixel 2's and the Brick's profile (--device after the game):
  - R1 to the Display tab: Screen Colors right over On-Screen Stats, its
    last row, as over a game;
  - A on it: the same page over the paused cart's frame;
  - Right on its Look: Vivid kept in the device's database at once and on
    the panel (desk mode's `colors`);
  - B: back to the menu, on the row; B again: Continue (10);
as the host's own profile no row. And over OpenBOR, whose picture is its
own (1.1.0-rc.8, its menu on the game's tabs too), Screen Colors is its
Display tab's one row, A the page, a step kept, B back, B Continue.
"""
import argparse
import importlib.machinery
import importlib.util
import json
import os
import shutil
import socket
import sqlite3
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The game a power-off left running, its auto state beside it: the next
# start goes back into it with the menu up (src/place.c's playing_restore)
GAME_TAG, GAME_FOLDER, GAME = 'GBC', 'Game Boy Color', 'Colors Test.gbc'
# Vivid, the Look after Standard (src/sysmenu_screen.c's SC_LOOKS), in
# platform.h's order: temp, tint, contrast, saturation, gamma
VIVID = [0, 0, 3, 3, -2]
KEYS = ('screen_temp', 'screen_tint', 'screen_contrast', 'screen_saturation', 'screen_gamma')
fails = []


def check(ok, what):
    print(f'  {"ok  " if ok else "FAIL"} {what}')
    if not ok:
        fails.append(what)


def module(name, path):
    loader = importlib.machinery.SourceFileLoader(name, str(path))
    spec = importlib.util.spec_from_loader(name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


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


class Diatom:
    """The resident emulator's side of its socket, as check-mp4resume's:
    READY to a connection, RUNNING to RUN, PAUSED to PAUSE, RUNNING to
    RESUME, EXIT to STOP. Every line it was sent is kept, in `lines`."""

    def __init__(self, path):
        self.lines = []
        self.srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.srv.bind(str(path))
        self.srv.listen(4)
        threading.Thread(target=self.serve, daemon=True).start()

    def serve(self):
        while True:
            try:
                c, _ = self.srv.accept()
            except OSError:
                return
            threading.Thread(target=self.talk, args=(c,), daemon=True).start()

    def talk(self, c):
        answers = {'RUN': b'RUNNING\n', 'PAUSE': b'PAUSED\n', 'RESUME': b'RUNNING\n',
                   'STOP': b'EXIT\treason=user\n'}
        buf = b''
        try:
            c.sendall(b'READY\n')
            while True:
                got = c.recv(4096)
                if not got:
                    return
                buf += got
                while b'\n' in buf:
                    line, buf = buf.split(b'\n', 1)
                    text = line.decode(errors='replace')
                    self.lines.append(text)
                    if text.split('\t')[0] in answers:
                        c.sendall(answers[text.split('\t')[0]])
        except OSError:
            return
        finally:
            c.close()

    def words(self):
        return [line.split('\t')[0] for line in self.lines]

    def close(self):
        self.srv.close()


def game_left_running(card):
    """The game on the card, and what POWER in it leaves (power_off): its
    marker, asked-for off, and its auto state"""
    rom = card / 'Roms' / GAME_FOLDER / GAME
    rom.parent.mkdir(parents=True, exist_ok=True)
    rom.write_bytes(bytes(32768))
    state = card / 'shared/.zyperos' / GAME_FOLDER / f'{GAME}.auto.state'
    state.parent.mkdir(parents=True, exist_ok=True)
    state.write_bytes(b'state')
    (card / 'ZyperOS/.playing').write_text(f'{GAME_TAG}\n{GAME}\noff\n')


def kept(card):
    """The five Screen Colors settings in the device's database, 0 unset"""
    with sqlite3.connect(card / 'userdata/zyperos.db') as db:
        got = dict(db.execute('SELECT key,value FROM settings WHERE key LIKE "screen_%"').fetchall())
    return [int(got.get(k, 0)) for k in KEYS]


def drawn(logp, text):
    return any(line == f'fit: drawn: {text}'
               for line in logp.read_text(errors='replace').splitlines())


def walk(binary, desk, tmp, card, device):
    """One start on `card` as `device`, into the game, and the walk"""
    name = device
    sock = Path(tempfile.gettempdir()) / f'zyperos-gmcolors-{os.getpid()}-{name}.sock'
    dsock = Path(tempfile.gettempdir()) / f'zyperos-gmcolors-{os.getpid()}-{name}-d.sock'
    flags = tmp / f'{name}-flags'
    flags.mkdir(exist_ok=True)
    for f in (sock, dsock):
        if f.exists():
            f.unlink()
    env = desk.card_env(card, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(flags), ZYPEROS_CTL_DIR=str(tmp / f'{name}-ctl'),
               ZYPEROS_DIATOM_SOCKET=str(dsock), ZYPEROS_DIATOM_PIDFILE=str(tmp / f'{name}-diatom.pid'))
    env.pop('ZYPEROS_HEADLESS', None)
    diatom = Diatom(dsock)
    logp = tmp / f'{name}.log'
    table = device != 'desktop'
    with open(logp, 'w') as log:
        p = subprocess.Popen([str(binary), '--device', device], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            for _ in range(200):
                if (sock.exists() and 'PAUSE' in diatom.words()) or p.poll() is not None:
                    break
                time.sleep(0.05)
            if not sock.exists() or 'PAUSE' not in diatom.words():
                check(False, f'{name}: the start goes back into the game, the menu up')
                return logp

            def tap(key):
                ask(sock, f'tap {key} 40')
                ask(sock, 'wait-frames 8')

            ask(sock, 'wait-frames 8')
            tap('r1')
            if not table:
                check(not drawn(logp, 'Screen Colors'), f'{name}: no table, no Screen Colors row on the Display tab')
                return logp
            check(drawn(logp, 'Screen Colors'), f'{name}: R1, the Display tab: a Screen Colors row')
            tap('up')
            tap('up')
            tap('a')
            check(drawn(logp, 'Temperature') and drawn(logp, 'Gamma'),
                  f'{name}: A on it, right over On-Screen Stats: the Screen Colors page, its rows drawn')
            was = list(diatom.lines)
            tap('right')
            colors = ask(sock, 'state').get('colors')
            check(kept(card) == VIVID, f'{name}: Right on Look: Vivid kept at once ({kept(card)})')
            check(isinstance(colors, dict) and colors.get('kept') is True and
                  [colors.get(k) for k in ('temp', 'tint', 'contrast', 'saturation', 'gamma')] == VIVID,
                  f'{name}: and on the panel\'s table while the game is paused ({colors})')
            check(diatom.lines == was, f'{name}: the paused game told nothing ({diatom.lines[len(was):]})')
            tap('b')
            check('RESUME' not in diatom.words(), f'{name}: B: back to the menu, the game still paused')
            # No frames once the game goes on: the launcher waits on it, so
            # the press is not followed by a wait for frames
            ask(sock, 'tap b 40')
            for _ in range(100):
                if 'RESUME' in diatom.words():
                    break
                time.sleep(0.05)
            check('RESUME' in diatom.words(), f'{name}: B again: the menu closes and the game goes on')
        except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError) as e:
            check(False, f'{name}: the launcher answered its socket ({e})')
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
            diatom.close()
            for f in (sock, dsock):
                if f.exists():
                    f.unlink()
    return logp


def pico8_walk(binary, desk, tmp, base, device):
    """The menu over your PICO-8 as portkeys starts it, as `device`, and
    its Screen Colors"""
    dw = module('deskwalk', ROOT / 'tools/deskwalk.py')
    card = tmp / f'p8-{device}'
    shutil.copytree(base, card, symlinks=True)
    (card / 'Roms/PICO-8').mkdir(parents=True, exist_ok=True)
    (card / 'Roms/PICO-8/celeste.p8').write_text('pico-8 cartridge\n')
    (card / 'Bios/PICO-8').mkdir(parents=True, exist_ok=True)
    for f in ('pico8_64', 'pico8.dat'):
        (card / 'Bios/PICO-8' / f).write_bytes(b'stand-in')
    (card / 'ZyperOS/pico8').mkdir(parents=True, exist_ok=True)
    (card / 'ZyperOS/pico8/PICO-8.sh').write_text('#!/bin/sh\n')
    (card / 'Roms/OpenBOR').mkdir(parents=True, exist_ok=True)
    (card / 'Roms/OpenBOR/x.pak').write_bytes(b'')
    sock = Path(tempfile.gettempdir()) / f'zyperos-gmcolors-{os.getpid()}-p8{device}.sock'
    flags = tmp / f'p8-{device}-flags'
    flags.mkdir(exist_ok=True)
    env = desk.card_env(card, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(flags))
    env.pop('ZYPEROS_HEADLESS', None)
    table = device != 'desktop'
    procs = []

    def start(name, args):
        """The menu as portkeys starts it, opening on Game, on Continue, as
        a game's first does: no memory of the last menu (src/portmenu.h's
        PORT_MENU_MEM, which the launcher removes at each hand-over)"""
        if sock.exists():
            sock.unlink()
        (flags / 'zyperos-port-menu-mem').unlink(missing_ok=True)
        log = open(tmp / f'p8-{device}-{name}.log', 'w')
        p = subprocess.Popen([str(binary)] + args + ['--device', device], env=env, stdout=log,
                             stderr=subprocess.STDOUT)
        procs.append(p)
        return dw.Walk(sock, p, tmp / f'p8-{device}-{name}.log')

    name = f'{device}, over your PICO-8'
    try:
        m = start('menu', ['--port-menu', 'PICO8', f'{card}/Roms/PICO-8/celeste.p8'])
        ok = m.up('Game', 'Continue') and m.step('r1', 'Display', 'Display')
        rows = m.rows() if ok else []
        if not table:
            check(ok and 'Screen Colors' not in rows, f'{name}: no table, no Screen Colors row ({rows})')
            m.ends('b')
        else:
            check(rows[-2:] == ['Screen Colors', 'On-Screen Stats'],
                  f'{name}: R1, the Display tab: Screen Colors right over On-Screen Stats ({rows})')
            m.to('down', 'Screen Colors', 'Display')
            check(m.step('a', 'Screen Colors', 'Look') and
                  dw.until(lambda: 'Temperature' in m.drawn() and 'Gamma' in m.drawn()),
                  f'{name}: A on it: the Screen Colors page over the cart, its rows drawn')
            m.press('right')
            check(dw.until(lambda: kept(card) == VIVID), f'{name}: Right on Look: Vivid kept at once ({kept(card)})')
            colors = m.state().get('colors')
            check(isinstance(colors, dict) and colors.get('kept') is True and
                  [colors.get(k) for k in ('temp', 'tint', 'contrast', 'saturation', 'gamma')] == VIVID,
                  f'{name}: and on the panel\'s table ({colors})')
            check(m.step('b', 'Display', 'Screen Colors') and m.proc.poll() is None,
                  f'{name}: B: back to the menu, on the row, PICO-8 still stopped')
            rc = m.ends('b')
            check(rc == 10, f'{name}: B again: Continue ({rc})')
        # Over OpenBOR, a program whose picture is its own: the panel's
        # Screen Colors is its Display tab's one row, as over a port
        name = f'{device}, over OpenBOR'
        m = start('openbor', ['--port-menu', 'OPENBOR', f'{card}/Roms/OpenBOR/x.pak'])
        ok = m.up('Game', 'Continue') and m.step('r1', 'Display')
        if not table:
            check(ok and 'Screen Colors' not in m.drawn(), f'{name}: no table, no Screen Colors row')
            m.ends('b')
        else:
            rows = m.rows() if ok else []
            check(rows == ['Screen Colors'], f'{name}: R1, the Display tab: Screen Colors, its one row ({rows})')
            check(m.step('a', 'Screen Colors', 'Look') and
                  dw.until(lambda: 'Temperature' in m.drawn() and 'Gamma' in m.drawn()),
                  f'{name}: A on it: the Screen Colors page over the program')
            m.press('left')
            check(dw.until(lambda: kept(card) != VIVID), f'{name}: Left on Look: another look kept at once ({kept(card)})')
            check(m.step('b', 'Display', 'Screen Colors') and m.proc.poll() is None,
                  f'{name}: B: back to the menu, on the row')
            rc = m.ends('b')
            check(rc == 10, f'{name}: B again: Continue ({rc})')
    except (OSError, subprocess.TimeoutExpired, json.JSONDecodeError) as e:
        check(False, f'{name}: the launcher answered its socket ({e})')
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        if sock.exists():
            sock.unlink()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--binary', default=str(ROOT / 'build-native/zyperos'))
    args = ap.parse_args()
    binary = Path(args.binary)
    if not binary.is_file():
        print(f'check-ingame-colors: {binary} missing (make native)')
        return 1
    desk = module('desk', ROOT / 'tools/desk')
    rs = module('readme_shots', ROOT / 'tools/readme-shots.py')
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-gmcolors-'))
    try:
        base = tmp / 'base'
        rs.demo_card(base, None, {'artwork': {}})
        desk.card_sounds(base)
        subprocess.run([str(binary), '--set', 'tour.done=1'], env=desk.card_env(base, None), check=True,
                       stdout=subprocess.DEVNULL)
        for device in ('pixel2', 'brick', 'desktop'):
            print(f'as the {device}:')
            card = tmp / device
            shutil.copytree(base, card, symlinks=True)
            game_left_running(card)
            walk(binary, desk, tmp, card, device)
            pico8_walk(binary, desk, tmp, base, device)
    finally:
        if not fails:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f'check-ingame-colors: {len(fails)} failed (logs in {tmp})')
        return 1
    print('ok: Screen Colors from the in-game menu, its page over the paused game, over your PICO-8 and '
          'over OpenBOR, kept and on the panel at once')
    return 0


if __name__ == '__main__':
    sys.exit(main())
