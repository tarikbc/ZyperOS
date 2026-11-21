#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-port-menu: the in-game menu over a program of its own, OpenBOR, a
PortMaster port and PortMaster itself (src/portmenu.h), is a game's in-game
menu, its tabs and places, with the rows the stopped program answers to
(1.1.0-rc.8: "openbor is also missing the menu as we have in other
systems", "yeah have the same menu in ports as well"; your PICO-8's own
rows are check-pico8-display's, check-pico8-keys' and check-p8fx's).

The real launcher (build-native/zyperos, make native) in desk mode's dummy
window as portkeys starts it (--port-menu TAG GAME), on a card with the
made-up ports of tools/fixtures/ports and an OpenBOR module, wearing the
host's own profile and the Pixel 2's (--device after the game):
  - it opens on the Game tab, on Continue: Continue and Quit;
  - R1, the Display tab: Screen Colors alone where the display has a table
    (the Pixel 2), nothing of the program's picture, which is its own;
    nothing as the host's own profile;
  - R1, the Controls tab: All Controls alone; A opens the program's
    read-only pages (its README's and keymap's for a port, PortMaster's own
    buttons, OpenBOR's), B is back on the row;
  - X from the Controls tab is Quit (11);
  - the next menu over it opens where the last was left (the Controls tab,
    All Controls; /tmp/zyperos-port-menu-mem), and B is Continue (10);
  - over a program still starting (--waiting), Keep Waiting and Quit.
Every wait is for an event, never a time or a count of frames
(tools/deskwalk.py): the panel a key lands on, as desk's `state` says it,
or the launcher's end.
"""
import importlib.machinery
import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
QUIT, CONTINUE = 11, 10
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


def main():
    if not BINARY.is_file():
        print('check-port-menu: build-native/zyperos missing (make native)')
        return 1
    dw = module('deskwalk', ROOT / 'tools/deskwalk.py')
    desk = module('desk', ROOT / 'tools/desk')
    rs = module('readme_shots', ROOT / 'tools/readme-shots.py')
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-port-menu-'))
    card, flags = tmp / 'card', tmp / 'flags'
    flags.mkdir()
    rs.portdocs_card(card)
    desk.card_sounds(card)
    (card / 'Roms/OpenBOR').mkdir(parents=True, exist_ok=True)
    (card / 'Roms/OpenBOR/Beats of Rage.pak').write_bytes(b'')
    env = desk.card_env(card, None)
    subprocess.run([str(BINARY), '--set', 'tour.done=1'], env=env, check=True, stdout=subprocess.DEVNULL)
    sock = Path(tempfile.gettempdir()) / f'zyportm-{os.getpid()}.sock'
    mem = flags / 'zyperos-port-menu-mem'
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(flags))
    env.pop('ZYPEROS_HEADLESS', None)
    procs = []

    def start(name, args, keep=False):
        if sock.exists():
            sock.unlink()
        if not keep:
            mem.unlink(missing_ok=True)
        log = open(tmp / f'{name}.log', 'w')
        p = subprocess.Popen([str(BINARY), '--port-menu'] + args, env=env, stdout=log, stderr=subprocess.STDOUT)
        procs.append(p)
        return dw.Walk(sock, p, tmp / f'{name}.log')

    programs = (('OpenBOR', ['OPENBOR', f'{card}/Roms/OpenBOR/Beats of Rage.pak']),
                ('a PortMaster port', ['PRT', f'{card}/Roms/Ports/Star Farm.sh']),
                ('PortMaster', ['PRT', f'{card}/Roms/Ports/PortMaster.sh']))
    try:
        for device in ('desktop', 'pixel2'):
            for what, args in programs:
                name = f'{what}, as the {device}'
                print(f'{name}:')
                more = ['--device', device]
                w = start('menu', args + more)
                ok = w.up('Game', 'Continue')
                rows = w.rows() if ok else []
                check(rows == ['Continue', 'Quit'], f'it opens on the Game tab: Continue and Quit ({rows})')
                if not w.step('r1', 'Display'):
                    check(False, 'R1: the Display tab')
                    continue
                if device == 'desktop':
                    seen = w.drawn()
                    check(not any(x in seen for x in ('Screen Colors', 'Shader', 'Border', 'On-Screen Stats')),
                          'R1, the Display tab, no table here: nothing')
                else:
                    rows = w.rows()
                    check(rows == ['Screen Colors'],
                          f'R1, the Display tab: Screen Colors alone, nothing of its own picture ({rows})')
                check(w.step('r1', 'Controls', 'All Controls') and w.rows() == ['All Controls'],
                      'R1, the Controls tab: All Controls alone')
                # The pages draw with menu_draw_caption, not the runner, so
                # desk's `state` keeps the tab: what they draw is the event
                # (their heading, the page count, the buttons' words: none of
                # them drawn before, as a string is said once a run)
                before = len(w.drawn())
                w.press('a')
                ok = dw.until(lambda: len(w.drawn()) > before + 2 or w.proc.poll() is not None)
                said = w.drawn()[before:before + 3]
                check(ok and w.proc.poll() is None, f'A opens its read-only pages ({said})')
                check(w.step('b', 'Controls', 'All Controls'), 'B is back on the row')
                rc = w.ends('x')
                check(rc == QUIT, f'X from the Controls tab is Quit ({rc})')
                w = start('again', args + more, keep=True)
                check(w.up('Controls', 'All Controls'), 'the next menu over it opens where the last was left')
                rc = w.ends('b')
                check(rc == CONTINUE, f'B is Continue ({rc})')
            print(f'OpenBOR still starting, as the {device}:')
            w = start('waiting', programs[0][1] + ['--waiting', '--device', device])
            ok = w.up('Game', 'Keep Waiting')
            rows = w.rows() if ok else []
            check(rows == ['Keep Waiting', 'Quit'], f'Keep Waiting and Quit ({rows})')
            rc = w.ends('b')
            check(rc == CONTINUE, f'B keeps waiting ({rc})')
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        if sock.exists():
            sock.unlink()
        if not fails:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f'check-port-menu: {len(fails)} failed (logs in {tmp})')
        return 1
    print('ok: the in-game menu over OpenBOR, a port and PortMaster has a game\'s tabs')
    return 0


if __name__ == '__main__':
    sys.exit(main())
