#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-pico8-keys: your PICO-8's hotkeys, Reset and buttons (DESIGN.md
11.55 and 11.61, src/portmenu.h, src/p8pad.h), what the launcher and
PICO-8.sh do of them. portkeys's half (SELECT's chords, the screenshot,
CTRL-R, Auto Off) is check-pkkeys's and check-portkeys-flow's.

1. sd/pixel2/PICO-8.sh, its card in a scratch folder and a stand-in
   pico8_64 that writes what it was started with: with no line in /tmp the
   pad's mapping is the one that ships (O on the B cap, X on the A cap); the
   launcher's line for this pad is taken as it is; a line of another pad, or
   with characters no mapping has, is not.
2. The real launcher (build-native/zyperos, make native) in desk mode's
   dummy window, a card with your PICO-8 on it, hotkeys.PICO8 and map.PICO8
   in its library:
   - a cart opened (px2-open's channel) leaves portkeys the hotkeys in force
     in Diatom's RUN form and the screenshot's folder and name
     (Screenshots/PICO-8/<the cart>), PICO-8.sh the pad's line from
     map.PICO8, "Screenshot saved" in both themes with an empty square
     and its place after the pixels (NOTICE_SLOT), and with Next Shader on
     a key, "Shader / <name>" for each of PICO-8's shaders;
   - the in-game menu over it (--port-menu PICO8 CART) has a game's tabs,
     and its Game tab is Continue, Reset and Quit, Reset before Quit as in a
     game's; A asks Reset Game's "Reset the game? Play since the last save is
     lost.", the cursor on Cancel, Reset at its right: A there is the menu again; Reset ends it
     with PORT_MENU_RESET (13), asking nothing of buttons;
   - --reset (PORT_MENU_RESET_ARG, the hotkey's) asks the same question with
     no menu: Cancel 10, Reset 13;
   - its Controls tab (L1 from Game) is a game's, the buttons themselves
     (Applies To, This Game, the A cap giving O, map.PICO8's): a button
     stepped is kept for the cart (map.PICO8.<cart>); the menu closed back
     to the cart (B) writes the pad's new line and asks Start Cart Again's
     question with the buttons' words in it, the cursor on Later; B there is
     the menu again where it was, and B from it asks again; Later lets the
     cart go on (10), the buttons kept for its next start, and as the menu ends the
     hotkeys are written for portkeys again with the screenshot's folder
     kept; Start Again is PORT_MENU_RESTART (14) with the Display on in the
     restart file; with no button changed B asks nothing; X with a button
     changed is Quit (11), asking nothing; Reset (13) the same; and a
     Display mode's start again writes the new line, asking once;
   - the menu opens where the last one over the cart was left (its tab and
     row, src/portmenu.h's PORT_MENU_MEM), and on Game, on Continue, with
     no memory;
   - over OpenBOR there is no Reset row.
Every wait in the walks is for an event, never a time or a count of frames
(tools/deskwalk.py): the panel a key lands on, as desk's `state` says it, a
setting, a file, the launcher's end. On a runner busy with other checks the
walk is slower, and says the same (GitHub's macOS runner, #406).
"""
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
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
GUID = '190043d2010000000221000000010000,gkd_pixel2_joypad,'
SHIPS = (GUID + 'a:b1,b:b0,x:b2,y:b3,back:b8,start:b9,leftshoulder:b4,rightshoulder:b5,dpup:b10,'
         'dpdown:b11,dpleft:b12,dpright:b13,lefttrigger:b6,righttrigger:b7,crc:d243,platform:Linux,')
SWAPPED = SHIPS.replace('a:b1,b:b0,', 'a:b0,b:b1,')
# The pad portkeys gives PICO-8 while it holds the joypad (tools/pkvpad.h):
# its own GUID and name, the same body, no checksum
VGUID = '06000000595a00000900000001000000,ZyperOS pad for PICO-8,'
VSHIPS = VGUID + SHIPS[len(GUID):].replace('crc:d243,', '')
VSWAPPED = VSHIPS.replace('a:b1,b:b0,', 'a:b0,b:b1,')
RESET, RESTART = 13, 14
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


# ---- 1. PICO-8.sh ---------------------------------------------------------

def script(tmp):
    d = tmp / 'p8sh'
    card, flags = d / 'card', d / 'flags'
    said = d / 'said'

    def run(line):
        shutil.rmtree(d, ignore_errors=True)
        for p in (card / 'Bios/PICO-8', card / 'Roms/PICO-8', card / 'Saves', flags, card / 'ZyperOS/pico8'):
            p.mkdir(parents=True, exist_ok=True)
        text = (ROOT / 'sd/pixel2/PICO-8.sh').read_text()
        check('/mnt/SDCARD' in text, 'PICO-8.sh has /mnt/SDCARD')
        sh = card / 'ZyperOS/pico8/PICO-8.sh'
        sh.write_text(text.replace('/mnt/SDCARD', str(card)))
        prog = card / 'Bios/PICO-8/pico8_64'
        prog.write_text(f'#!/bin/sh\nprintf "%s\\n" "$SDL_GAMECONTROLLERCONFIG" > "{said}"\n')
        prog.chmod(0o755)
        (card / 'Bios/PICO-8/pico8.dat').write_bytes(b'stand-in')
        (card / 'Roms/PICO-8/celeste.p8').write_text('pico-8 cartridge\n')
        if line is not None:
            (flags / 'zyperos-pico8-pad').write_text(line + '\n')
        # PICO-8's settings as it left them, a merge_joysticks of its own
        (card / 'Saves/PICO-8').mkdir(parents=True, exist_ok=True)
        (card / 'Saves/PICO-8/config.txt').write_text('// a comment\nwindow_size 0 0\nmerge_joysticks 0\n')
        env = dict(os.environ, ZYPEROS_FLAG_DIR=str(flags), ZYPEROS_DISPLAY='stretch')
        env.pop('SDL_GAMECONTROLLERCONFIG', None)
        out = subprocess.run(['sh', str(sh), str(card / 'Roms/PICO-8/celeste.p8')], env=env,
                             stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=20)
        cfg = (card / 'Saves/PICO-8/config.txt').read_text()
        return (said.read_text().rstrip('\n') if said.exists() else None), out.stdout + out.stderr + '\n' + cfg

    print('PICO-8.sh, the pads\' mapping lines:')
    got, log = run(None)
    check(got == SHIPS + '\n' + VSHIPS, f'no lines in /tmp: the joypad\'s and portkeys\'s pad\'s that ship ({got})')
    check(log.count('merge_joysticks') == 1 and 'merge_joysticks 2' in log and 'window_size 0 0' in log and
          '// a comment' in log, 'PICO-8\'s first two controllers one player 1 (merge_joysticks 2), its other '
          'settings as they were')
    got, log = run(SWAPPED + '\n' + VSWAPPED)
    check(got == SWAPPED + '\n' + VSWAPPED and 'the player\'s buttons' in log,
          'the launcher\'s two lines: both taken')
    got, log = run(SWAPPED)
    check(got == SWAPPED + '\n' + VSHIPS, 'the joypad\'s line alone: taken, portkeys\'s pad\'s as it ships')
    got, log = run('030000005e0400008e02000014010000,xbox,a:b0,')
    check(got == SHIPS + '\n' + VSHIPS and 'another pad' in log, 'another pad\'s line: the ones that ship')
    got, log = run(GUID + 'a:b1;reboot,b:b0,')
    check(got == SHIPS + '\n' + VSHIPS and 'no mapping' in log, 'a line with what no mapping has: the ones that ship')


# ---- 2. the launcher ------------------------------------------------------

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


def settings(path, put=None, key=None):
    c = sqlite3.connect(str(path))
    if put:
        for k, v in put.items():
            c.execute('INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)', (k, v))
        c.commit()
    r = c.execute('SELECT value FROM settings WHERE key=?', (key,)).fetchone() if key else None
    c.close()
    return r[0] if r else None


def dtov_slot(path):
    """The notice's size and the SLOT trailer's square, or None"""
    if not path.exists():
        return None
    b = path.read_bytes()
    if b[:4] != b'DTOV':
        return None
    w, h = b[4] | b[5] << 8, b[6] | b[7] << 8
    tr = b[8 + w * h * 4:]
    if len(tr) != 12 or tr[:4] != b'SLOT':
        return (w, h, None)
    return (w, h, tuple(tr[4 + i * 2] | tr[5 + i * 2] << 8 for i in range(4)))


def launcher(tmp):
    desk = module('desk', ROOT / 'tools/desk')
    rs = module('readme_shots', ROOT / 'tools/readme-shots.py')
    card, flags = tmp / 'card', tmp / 'flags'
    flags.mkdir(parents=True)
    rs.media_card(card)
    desk.card_sounds(card)
    env = desk.card_env(card, None)
    subprocess.run([str(BINARY), '--set', 'tour.done=1'], env=env, check=True, stdout=subprocess.DEVNULL)
    (card / 'Roms/PICO-8').mkdir(parents=True, exist_ok=True)
    (card / 'Roms/PICO-8/celeste.p8').write_text('pico-8 cartridge\n')
    (card / 'Bios/PICO-8').mkdir(parents=True, exist_ok=True)
    for f in ('pico8_64', 'pico8.dat'):
        (card / 'Bios/PICO-8' / f).write_bytes(b'stand-in')
    (card / 'ZyperOS/pico8').mkdir(parents=True, exist_ok=True)
    (card / 'ZyperOS/pico8/PICO-8.sh').write_text('#!/bin/sh\n')
    (card / 'Roms/OpenBOR').mkdir(parents=True, exist_ok=True)
    (card / 'Roms/OpenBOR/x.pak').write_bytes(b'')
    lib = next(iter(sorted((card / 'shared').rglob('library.db'))), None)
    check(lib is not None, 'the card\'s library')
    if lib is None:
        return
    # The player's hotkeys for PICO-8 and its buttons swapped, as the
    # Controls tab over a fake08 game keeps them (src/hkstore.h, remapstore.h)
    settings(lib, {'hotkeys.PICO8': '1;shot:y,shader:a,reset:b,quit:x', 'map.PICO8': '1;a:b,b:a'})
    keys_file, pad_file = flags / 'zyperos-port-keys', flags / 'zyperos-pico8-pad'
    restart = flags / 'zyperos_port_restart'
    short = Path(tempfile.gettempdir())
    dsock = short / f'zyp8k-{os.getpid()}.sock'
    cdir = short / f'zyp8kc-{os.getpid()}'
    cdir.mkdir(mode=0o700, exist_ok=True)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(dsock), ZYPEROS_FLAG_DIR=str(flags), ZYPEROS_CTL_DIR=str(cdir))
    env.pop('ZYPEROS_HEADLESS', None)
    procs = []
    dw = module('deskwalk', ROOT / 'tools/deskwalk.py')

    mem = flags / 'zyperos-port-menu-mem'

    def start(name, args, keep=False):
        """The launcher on the desk's socket: its walk (tools/deskwalk.py);
        its menu with no memory of the last one (src/portmenu.h's
        PORT_MENU_MEM), as a cart's first, unless `keep`"""
        if dsock.exists():
            dsock.unlink()
        if not keep:
            mem.unlink(missing_ok=True)
        log = open(tmp / f'{name}.log', 'w')
        p = subprocess.Popen([str(BINARY)] + args, env=env, stdout=log, stderr=subprocess.STDOUT)
        procs.append(p)
        return dw.Walk(dsock, p, tmp / f'{name}.log')

    def lines(path):
        return dict(l.split('\t', 1) for l in path.read_text().splitlines() if '\t' in l) if path.exists() else {}

    cart = f'{card}/Roms/PICO-8/celeste.p8'
    RESET_Q, RESTART_Q = 'Reset Game', 'Start Cart Again'
    try:
        print('a PICO-8 cart opened, the hand-over:')
        w = start('shelf', ['--device', 'pixel2'])
        check(dw.until(lambda: w.sock.exists() and w.state().get('ok')), 'the launcher started')
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(dw.CEILING)
        s.connect(str(cdir / 'ctl.sock'))
        s.sendall(f'open\tpath={cart}\n'.encode())
        said = s.makefile().readline().strip()
        s.close()
        check(said.startswith('ok') and 'core=run:ZyperOS/pico8/PICO-8.sh' in said,
              f'it opens on your PICO-8 ({said[:60]!r})')
        check(w.ends() == 0, 'the launcher ends for it')
        k = lines(keys_file)
        check(k.get('hotkeys') == 'save:r1,load:l1,faster:r2,slower:l2,shot:y,shader:a,reset:b,quit:x',
              f'portkeys gets the hotkeys in force, in Diatom\'s RUN form ({k.get("hotkeys")})')
        shots = k.get('shots', '')
        check(shots.startswith(f'{card}/Screenshots/PICO-8/') and shots.rsplit('/', 1)[-1].lower().startswith('celeste'),
              f'and where a screenshot goes, the cart by its name ({shots})')
        pad = pad_file.read_text().rstrip('\n') if pad_file.exists() else ''
        check(pad == SWAPPED + '\n' + VSWAPPED,
              f'PICO-8.sh gets the joypad\'s and portkeys\'s pad\'s lines from map.PICO8 ({pad})')
        for theme in ('light', 'dark'):
            sl = dtov_slot(flags / f'zyperos-port-shot-{theme}.dtov')
            ok = sl is not None and sl[2] is not None
            if ok:
                wd, ht, (x, y, side, corner) = sl
                ok = side > 0 and x + side <= wd and y + side <= ht and 0 < corner <= side // 2
                # the square is the slab's own, no picture laid in it
                if ok:
                    b_ = (flags / f'zyperos-port-shot-{theme}.dtov').read_bytes()
                    mid = 8 + ((y + side // 2) * wd + x + side // 2) * 4
                    edge = 8 + ((y + side // 2) * wd + x + side + 4) * 4
                    ok = b_[mid:mid + 4] == b_[edge:edge + 4]
            check(ok, f'"Screenshot saved" in {theme}, with an empty square and its place ({sl})')

        names = ('nearest', 'shimmerless', 'crt', 'crt-curved', 'scanlines', 'lcd')
        pops = [flags / f'zyperos-port-shader-{n}-{t}.dtov' for n in names for t in ('light', 'dark')]
        check(all(dtov_slot(f) is not None for f in pops),
              'Next Shader has a key: "Shader / <name>" for each of PICO-8\'s shaders, in both themes')

        print('the in-game menu over it, Reset:')
        w = start('menu', ['--port-menu', 'PICO8', cart])
        up = w.up('Game', 'Continue') and dw.until(lambda: 'Quit' in w.drawn())
        rows = w.rows() if up else []
        check(rows == ['Continue', 'Reset', 'Quit'], f'the Game tab: Continue, Reset and Quit, as a game\'s ({rows})')
        check(w.to('down', 'Reset', 'Game') and w.step('a', RESET_Q, 'Cancel') and
              dw.until(lambda: any('Play since the last save is lost.' in x for x in w.drawn())),
              'A asks first, as Reset Game\'s hotkey asks over a game, the cursor on Cancel')
        check(w.step('a', 'Game', 'Reset') and w.proc.poll() is None,
              'A on the question\'s first stop is Cancel: the menu again')
        w.step('a', RESET_Q, 'Cancel')
        w.step('right', RESET_Q, 'Reset')
        rc = w.ends('a')
        check(rc == RESET, f'Reset ends the menu with PORT_MENU_RESET ({rc})')

        print('--reset, the hotkey\'s question alone:')
        w = start('hot', ['--port-menu', 'PICO8', cart, '--reset'])
        check(w.up(RESET_Q, 'Cancel') and dw.until(lambda: 'Cancel' in w.drawn()) and 'Continue' not in w.drawn(),
              'the question, and no menu')
        rc = w.ends('a')
        check(rc == 10, f'Cancel: the game goes on ({rc})')
        w = start('hot-yes', ['--port-menu', 'PICO8', cart, '--reset'])
        w.up(RESET_Q, 'Cancel')
        w.step('right', RESET_Q, 'Reset')
        rc = w.ends('a')
        check(rc == RESET, f'Reset: PORT_MENU_RESET ({rc})')

        print('its Controls tab, a button changed:')
        keys_file.write_text(f'hotkeys\tnone\nshots\t{shots}\n')
        w = start('ctl', ['--port-menu', 'PICO8', cart])
        w.up('Game', 'Continue')
        # O on the B cap, as map.PICO8 swaps them (the A cap's row says it)
        check(w.step('l1', 'Controls', 'Applies To') and
              dw.until(lambda: 'This Game' in w.drawn() and 'Button O' in w.drawn()),
              'L1: the Controls tab, a game\'s, the buttons themselves: Applies To, This Game, the A cap giving O')
        w.to('down', 'A', 'Controls')
        w.press('right')
        check(dw.until(lambda: settings(lib, key='map.PICO8.celeste.p8') is not None),
              f'a button stepped is kept for the cart ({settings(lib, key="map.PICO8.celeste.p8")})')
        check(w.step('b', RESTART_Q, 'Later') and dw.until(lambda: any('new buttons' in x for x in w.drawn())),
              'B, the menu closing back to the cart, asks to start PICO-8 again for the new buttons, '
              'the cursor on Later')
        check(w.step('b', 'Controls', 'A') and w.proc.poll() is None and not restart.exists(),
              'B on the question goes back to the menu where it was, nothing done')
        check(w.step('b', RESTART_Q, 'Later'), 'and B there asks again')
        pad = pad_file.read_text().rstrip('\n') if pad_file.exists() else ''
        check(pad.startswith(GUID) and pad.split('\n')[0] != SWAPPED and len(pad.split('\n')) == 2 and
              pad.split('\n')[1].startswith(VGUID), f'and the pads\' new lines are written ({pad})')
        rc = w.ends('a')
        k = lines(keys_file)
        check(rc == 10 and not restart.exists(), f'Later: the cart goes on, no start again ({rc})')
        check(k.get('hotkeys') == 'save:r1,load:l1,faster:r2,slower:l2,shot:y,shader:a,reset:b,quit:x'
              and k.get('shots') == shots,
              f'as the menu ends the hotkeys are written again, the folder kept ({k})')

        print('the next menu over the cart, where the last was left:')
        w = start('ctl-mem', ['--port-menu', 'PICO8', cart], keep=True)
        check(w.up('Controls', 'A'), f'on the Controls tab, on the A cap\'s row ({w.state().get("screen")!r}, '
              f'{w.state().get("focus")!r})')
        check(w.ends('b') == 10 and not restart.exists(), 'B with no button changed asks nothing: Continue')

        was = settings(lib, key='map.PICO8.celeste.p8')
        w = start('ctl-yes', ['--port-menu', 'PICO8', cart])
        w.up('Game', 'Continue')
        w.step('l1', 'Controls', 'Applies To')
        w.to('down', 'A', 'Controls')
        w.press('right')
        dw.until(lambda: settings(lib, key='map.PICO8.celeste.p8') != was)
        w.step('b', RESTART_Q, 'Later')
        w.step('right', RESTART_Q, 'Start Again')
        rc = w.ends('a')
        mode = settings(card / 'userdata/zyperos.db', key='display.PICO8') or 'aspect'
        check(rc == RESTART and restart.exists() and restart.read_text() == mode + '\n',
              f'Start Again: PORT_MENU_RESTART, the Display on in the restart file ({rc})')

        print('a button changed, then X or Reset:')
        restart.unlink(missing_ok=True)
        was = settings(lib, key='map.PICO8.celeste.p8')
        w = start('ctl-x', ['--port-menu', 'PICO8', cart])
        w.up('Game', 'Continue')
        w.step('l1', 'Controls', 'Applies To')
        w.to('down', 'A', 'Controls')
        w.press('right')
        dw.until(lambda: settings(lib, key='map.PICO8.celeste.p8') != was)
        rc = w.ends('x')
        check(rc == 11 and not restart.exists() and RESTART_Q not in w.drawn(),
              f'X is Quit from the Controls tab, asking nothing; the buttons kept for the next start ({rc})')
        was = settings(lib, key='map.PICO8.celeste.p8')
        w = start('ctl-reset', ['--port-menu', 'PICO8', cart])
        w.up('Game', 'Continue')
        w.step('l1', 'Controls', 'Applies To')
        w.to('down', 'A', 'Controls')
        w.press('right')
        dw.until(lambda: settings(lib, key='map.PICO8.celeste.p8') != was)
        w.step('r1', 'Game', 'Continue')
        w.to('down', 'Reset', 'Game')
        w.step('a', RESET_Q, 'Cancel')
        w.step('right', RESET_Q, 'Reset')
        rc = w.ends('a')
        check(rc == RESET and RESTART_Q not in w.drawn(), f'Reset is CTRL-R, asking nothing of buttons ({rc})')

        print('a button changed, then a Display mode\'s start again:')
        restart.unlink(missing_ok=True)
        pad_file.unlink(missing_ok=True)
        was = settings(lib, key='map.PICO8.celeste.p8')
        w = start('ctl-display', ['--port-menu', 'PICO8', cart])
        w.up('Game', 'Continue')
        w.step('l1', 'Controls', 'Applies To')
        w.to('down', 'A', 'Controls')
        w.press('right')
        dw.until(lambda: settings(lib, key='map.PICO8.celeste.p8') != was)
        w.step('l1', 'Display', 'Display')
        # The Display list open on the mode on (its focus a mode's, no more
        # the row's), and the mode before it
        w.step('a', 'Display', then=lambda: w.state().get('focus') not in (None, 'Display'))
        w.press('up')
        w.step('a', RESTART_Q, 'Cancel')
        w.step('right', RESTART_Q, 'Start Again')
        rc = w.ends('a')
        pad = pad_file.read_text().split('\n')[0] if pad_file.exists() else ''
        check(rc == RESTART and restart.exists() and pad.startswith(GUID) and pad != SWAPPED,
              f'the new buttons written for it, asked about once ({rc}, {pad[:60]})')

        print('over OpenBOR:')
        w = start('openbor', ['--port-menu', 'OPENBOR', f'{card}/Roms/OpenBOR/x.pak'])
        up = w.up('Game', 'Continue') and dw.until(lambda: 'Quit' in w.drawn())
        rows = w.rows() if up else []
        check(rows == ['Continue', 'Quit'], f'its Game tab has no Reset row ({rows})')
        w.ends('b')
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        shutil.rmtree(cdir, ignore_errors=True)
        if dsock.exists():
            dsock.unlink()


def main():
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-pico8-keys-'))
    try:
        script(tmp)
        if BINARY.is_file():
            launcher(tmp)
        else:
            check(False, 'build-native/zyperos is there (make native)')
    finally:
        if not fails:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f'check-pico8-keys: {len(fails)} failed (logs in {tmp})')
        return 1
    print('ok: your PICO-8\'s hotkeys, Reset and buttons, handed over and asked')
    return 0


if __name__ == '__main__':
    sys.exit(main())
