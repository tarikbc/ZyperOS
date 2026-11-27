#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-pico8-display: the in-game menu's Display row over your PICO-8
(DESIGN.md 11.55, src/portmenu.h's port_menu_display), and the start again
it asks for, since PICO-8 draws its picture where it was told as it started.

1. launch.sh's run_port, on a copy with the card and /tmp in a scratch folder
   and stand-ins for the launcher, the program, portkeys and killall (as
   check-pico8-mp4 does): portkeys's stand-in writes the restart file and
   sends USR2 as the menu's start again does, and the program is ended and
   started again with that Display, portkeys and MP4's follower going on
   through it, once; a word in the file that is no mode's is a plain Quit; a
   file left from before the program started is no start again; POWER while
   a start again is under way starts nothing.
2. Its default: a PICO-8 shelf whose Display was never set plays your PICO-8
   at Original Shape (480 x 480, as before its Display modes), and only a
   pick of Full Screen stretches it; fake08 on it keeps Full Screen. The
   launcher's rows (--rows-report) and the request it writes as a cart opens
   (px2-open's channel): never set, Original Shape and "aspect"; core.PICO8
   on fake08, Full Screen; a "stretch" from before 1.1.0 (every row was
   written at any change then) let go of once, and a marker kept; a change of
   another shelf's Display writes that shelf's row alone; Full Screen picked
   on the shelf's own row, "stretch" in the request.
3. The real launcher in desk mode's dummy window (build-native/zyperos, make
   native), as portkeys starts it over the program (--port-menu PICO8 CART):
   a game's tabs, on Game's Continue, and R1 the Display tab on its Display
   row (the mode on, from display.PICO8), as over a game since 1.1.0-rc.8;
   Left and Right on Display step nothing; A opens the Display list,
   and a mode other than the one on asks "Start the cart again?" with the
   cursor on Cancel: A there is Cancel (the menu, nothing kept, no file), B
   Continue; the yes ends the menu with PORT_MENU_RESTART (14), display.PICO8
   kept and the restart file saying the mode; over OpenBOR no Display row;
   and --display (PORT_MENU_DISPLAY_ARG, the Next Display hotkey's) asks for
   the next mode with no menu, Cancel 10 and nothing kept, Start Again 14;
   and On-Screen Stats the Display tab's last row, Off, Right turning it On
   in the device's database (portkeys reads it as the menu closes); over
   OpenBOR no Display row and no On-Screen Stats.
Every wait in the walks is for an event, never a time or a count of frames
(tools/deskwalk.py): the panel a key lands on, as desk's `state` says it, a
setting, a file, the launcher's end. On a runner busy with other checks the
walk is slower, and says the same (GitHub's macOS runner, #406).
"""
import importlib.machinery
import importlib.util
import os
import shutil
import signal
import socket
import json
import sqlite3
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
RESTART = 14
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


# Every wait in a walk a wait for an event (tools/deskwalk.py)
dw = module('deskwalk', ROOT / 'tools/deskwalk.py')


# ---- 1. launch.sh ---------------------------------------------------------

def launch_sh(tmp):
    d = tmp / 'sh'
    card, bin_ = d / 'card', d / 'bin'
    calls = d / 'calls'
    zy = card / 'ZyperOS'
    tab = '\t'

    def edit(src):
        text = src.read_text()
        for pat in ('/mnt/SDCARD', '/tmp/', 'exec setsid /bin/bash', 'PORT_RESTART_FILE=/tmp/zyperos_port_restart'):
            if src.name == 'launch.sh' and pat not in text:
                check(False, f'launch.sh has {pat!r}')
        # /tmp/ first: the scratch folder is itself under /tmp on Linux, and
        # the card's path put in first would be moved again
        return (text.replace('/tmp/', f'{d}/tmp/').replace('/mnt/SDCARD', str(card))
                .replace('/usr/share/zyperos/card', str(d / 'seed')).replace('/dev/kmsg', str(d / 'kmsg')))

    def put(path, text, run=True):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        if run:
            path.chmod(0o755)

    def run(request, keys, stale=None):
        """launch.sh with `request`; `keys` is portkeys's stand-in's doing
        once the program has started (a shell snippet)"""
        shutil.rmtree(d, ignore_errors=True)
        for p in (zy, bin_, d / 'tmp', card / '.userdata/pixel2', card / 'Roms/PICO-8'):
            p.mkdir(parents=True, exist_ok=True)
        put(zy / 'launch.sh', edit(ROOT / 'sd/zyperos/launch.sh'), False)
        put(zy / 'device.sh', edit(ROOT / 'sd/pixel2/device.sh') + '\ndevice_radios() { :; }\n', False)
        for c in ('poweroff', 'reboot'):
            put(bin_ / c, f'#!/bin/sh\necho {c} >> "{calls}"\n')
        put(bin_ / 'pidof', '#!/bin/sh\nexit 1\n')
        put(bin_ / 'dmesg', '#!/bin/sh\n:\n')
        put(bin_ / 'killall', f'#!/bin/sh\necho "killall $*" >> "{calls}"\n')
        # setsid, as the device's: the program leads a group of its own,
        # which TERM to the group reaches
        put(bin_ / 'setsid', '#!/usr/bin/env python3\nimport os, sys\nos.setsid()\n'
                             'os.execvp(sys.argv[1], sys.argv[1:])\n')
        put(zy / 'diatom', '#!/bin/sh\nexit 0\n')
        put(zy / 'zyperos.elf', f'''#!/bin/sh
case $1 in
--mp4-follow)
	echo "follow start $$" >> "{calls}"
	trap 'echo "follow TERM" >> "{calls}"; exit 0' TERM
	while :; do sleep 0.05; done ;;
--port-menu) exit 10 ;;
--port-played) exit 0 ;;
esac
n=$(cat "{d}/runs" 2> /dev/null || echo 0); n=$((n + 1)); echo $n > "{d}/runs"
echo "launcher $n" >> "{calls}"
if [ $n = 1 ]; then printf '%s\\n' '{request}' > "{d}/tmp/zyperos_port"; else : > "{d}/tmp/zyperos_poweroff"; fi
exit 0
''')
        # portkeys: started once; after the program's first start, `keys`
        put(zy / 'portkeys', f'''#!/bin/sh
echo "portkeys $*" >> "{calls}"
trap 'echo "portkeys TERM" >> "{calls}"; exit 0' TERM
i=0
while [ $i -lt 100 ] && ! grep -q '^program 1 ' "{calls}"; do sleep 0.05; i=$((i + 1)); done
{keys}
while :; do sleep 0.05; done
''')
        # The program: its start's number and its Display; the first one
        # runs until it is ended (TERM), the second ends by itself
        put(zy / 'pico8/PICO-8.sh', f'''#!/bin/bash
n=$(cat "{d}/pruns" 2> /dev/null || echo 0); n=$((n + 1)); echo $n > "{d}/pruns"
echo "program $n display=$ZYPEROS_DISPLAY" >> "{calls}"
trap 'echo "program $n TERM" >> "{calls}"; sleep 0.4; exit 0' TERM
if [ $n = 1 ]; then i=0; while [ $i -lt 100 ]; do sleep 0.05; i=$((i + 1)); done; fi
sleep 0.2
exit 0
''')
        if stale:
            (d / 'tmp/zyperos_port_restart').write_text(stale + '\n')
        env = dict(os.environ, PATH=f'{bin_}:{os.environ["PATH"]}')
        try:
            subprocess.run(['sh', str(zy / 'launch.sh')], env=env, stdin=subprocess.DEVNULL,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=40)
        except subprocess.TimeoutExpired:
            check(False, 'launch.sh ended (it ran 40 s)')
        lines = calls.read_text().splitlines() if calls.exists() else []
        log = ''
        for f in (card / '.userdata/pixel2').glob('*.txt'):
            log += f.read_text(errors='replace')
        return lines, log, (d / 'tmp/zyperos_port_restart').exists()

    request = tab.join((f'{zy}/pico8/PICO-8.sh', f'{card}/Roms/PICO-8/celeste.p8', 'PICO8', 'stretch', 'mp4'))
    restart = f'echo native > "{d}/tmp/zyperos_port_restart"; kill -USR2 $PPID'

    print('launch.sh, a start again from the in-game menu (the restart file, then USR2):')
    lines, log, left = run(request, restart)
    progs = [l for l in lines if l.startswith('program ') and 'display=' in l]
    check(progs == ['program 1 display=stretch', 'program 2 display=native'],
          f'the program ends and starts again with the Display the file says ({progs})')
    check('program 1 TERM' in lines and lines.index('program 1 TERM') < lines.index('program 2 display=native'),
          'TERM ends the first before the second starts')
    check(sum(l.startswith('portkeys --') for l in lines) == 1 and
          sum(l.startswith('follow start ') for l in lines) == 1 and 'follow TERM' in lines and
          lines.index('follow TERM') > lines.index('program 2 display=native'),
          'portkeys and the follower are started once and go on through it')
    check('killall -TERM mp4d' not in lines, 'MP4 is not stopped')
    check('launcher 2' in lines and lines.index('launcher 2') > lines.index('program 2 display=native'),
          'the launcher comes back after the second ends')
    check(not left, 'the file is gone')

    print('launch.sh, a word in the file that is no mode\'s:')
    lines, log, left = run(request, f'echo "x;y" > "{d}/tmp/zyperos_port_restart"; kill -USR2 $PPID')
    progs = [l for l in lines if l.startswith('program ') and 'display=' in l]
    check(progs == ['program 1 display=stretch'] and 'launcher 2' in lines, f'a plain Quit ({progs})')

    print('launch.sh, a file left from before, then Quit:')
    lines, log, left = run(request, 'kill -USR2 $PPID', stale='native')
    progs = [l for l in lines if l.startswith('program ') and 'display=' in l]
    check(progs == ['program 1 display=stretch'], f'no start again ({progs})')

    print('launch.sh, POWER while a start again is under way:')
    lines, log, left = run(request, restart + '; sleep 0.05; kill -USR1 $PPID')
    progs = [l for l in lines if l.startswith('program ') and 'display=' in l]
    check(progs == ['program 1 display=stretch'] and 'poweroff' in lines,
          f'nothing starts again, and the handheld powers off ({progs})')


# ---- 2. the default -------------------------------------------------------

def defaults(tmp):
    desk = module('desk', ROOT / 'tools/desk')
    rs = module('readme_shots', ROOT / 'tools/readme-shots.py')
    card, flags = tmp / 'dcard', tmp / 'dflags'
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
    (card / 'Roms/NES').mkdir(parents=True, exist_ok=True)
    (card / 'Roms/NES/x.nes').write_bytes(b'')
    db = card / 'userdata/zyperos.db'
    short = Path(tempfile.gettempdir())
    dsock = short / f'zyp8df-{os.getpid()}.sock'
    cdir = short / f'zyp8dc-{os.getpid()}'
    cdir.mkdir(mode=0o700, exist_ok=True)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(dsock), ZYPEROS_FLAG_DIR=str(flags), ZYPEROS_CTL_DIR=str(cdir))
    env.pop('ZYPEROS_HEADLESS', None)
    request = flags / 'zyperos_port'

    def setting(key, value=None, delete=False):
        c = sqlite3.connect(str(db))
        if delete:
            c.execute('DELETE FROM settings WHERE key=?', (key,))
        elif value is not None:
            c.execute('INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)', (key, value))
        r = c.execute('SELECT value FROM settings WHERE key=?', (key,)).fetchone()
        c.commit()
        c.close()
        return r[0] if r else None

    def shelf(tag, focus):
        (card / 'ZyperOS/.last').write_text(f'{tag}\n{focus}\n')

    def row(name):
        """the PICO-8 shelf's system menu row `name`'s value (--rows-report)"""
        shelf('PICO8', 'celeste.p8')
        out = subprocess.run([str(BINARY), '--shot', str(tmp / 'rows.png'), '--screen', 'games',
                              '--rows-report', '--device', 'pixel2'], env=env, text=True,
                             stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=60).stdout
        for line in out.splitlines():
            f = line.split('\t')
            if len(f) > 2 and f[0] == 'rows: system' and f[1] == name:
                return f[2]
        return None

    def press(tag, focus, key):
        """--menu Display --press KEY on a shelf: its row stepped, as the
        player steps it (display_save)"""
        shelf(tag, focus)
        subprocess.run([str(BINARY), '--shot', str(tmp / 'press.png'), '--screen', 'games', '--menu', 'Display',
                        '--press', key, '--device', 'pixel2'], env=env, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, timeout=60)

    def opened():
        """a cart opened as px2-open opens it: the request's Display"""
        if dsock.exists():
            dsock.unlink()
        request.unlink(missing_ok=True)
        shelf('PICO8', 'celeste.p8')
        log = open(tmp / 'open.log', 'w')
        p = subprocess.Popen([str(BINARY), '--device', 'pixel2'], env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            # up on its socket and its control channel: events, not times
            # (tools/deskwalk.py)
            if not (dw.until(lambda: dsock.exists() or p.poll() is not None) and dsock.exists() and
                    dw.until(lambda: (cdir / 'ctl.sock').exists() or p.poll() is not None)):
                return None
            sk = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sk.settimeout(dw.CEILING)
            sk.connect(str(cdir / 'ctl.sock'))
            sk.sendall(f'open\tpath={card}/Roms/PICO-8/celeste.p8\n'.encode())
            sk.makefile().readline()
            sk.close()
            p.wait(timeout=dw.CEILING)
        finally:
            if p.poll() is None:
                p.kill()
                p.wait()
        f = request.read_text().rstrip('\n').split('\t') if request.exists() else []
        return f[3] if len(f) > 3 else None

    try:
        print('a PICO-8 shelf whose Display was never set:')
        check(row('Display') == 'Original Shape', f'its row says Original Shape ({row("Display")!r})')
        check(opened() == 'aspect', 'a cart opens at Original Shape, 480 x 480, as before')
        check(setting('display.PICO8') is None, 'and no row is written for it')
        setting('core.PICO8', 'fake08')
        check(row('Display') == 'Full Screen', 'on fake08 the same shelf says Full Screen, as before')
        setting('core.PICO8', delete=True)

        print('a "stretch" from before 1.1.0, when any change wrote every row:')
        setting('pico8.display_settled', delete=True)
        setting('display.PICO8', 'stretch')
        check(opened() == 'aspect', 'let go of once: Original Shape')
        check(setting('display.PICO8') is None and setting('pico8.display_settled') == '1',
              'the row gone, and the marker kept')

        print('another shelf\'s Display changed:')
        press('NES', 'x.nes', 'right')
        check(setting('display.NES') is not None and setting('display.PICO8') is None,
              f'writes that shelf\'s row alone (NES {setting("display.NES")!r}, PICO-8 {setting("display.PICO8")!r})')

        print('Full Screen picked on the PICO-8 shelf\'s own row:')
        press('PICO8', 'celeste.p8', 'left')
        check(setting('display.PICO8') == 'stretch', f'kept as a pick ({setting("display.PICO8")!r})')
        check(row('Display') == 'Full Screen' and opened() == 'stretch',
              'its row Full Screen, and a cart opens stretched, 640 x 480')
    finally:
        shutil.rmtree(cdir, ignore_errors=True)
        if dsock.exists():
            dsock.unlink()


# ---- 3. the in-game menu over it ------------------------------------------

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
    # The device's database, which --set tour.done made: the shelf's own
    # Display row for PICO-8 on Sharp Pixels
    db = card / 'userdata/zyperos.db'
    c = sqlite3.connect(str(db))
    c.execute("INSERT OR REPLACE INTO settings(key, value) VALUES('display.PICO8', 'integer')")
    c.commit()
    c.close()
    restart = flags / 'zyperos_port_restart'
    short = Path(tempfile.gettempdir())
    dsock = short / f'zyp8dd-{os.getpid()}.sock'
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(dsock), ZYPEROS_FLAG_DIR=str(flags))
    env.pop('ZYPEROS_HEADLESS', None)
    procs = []

    def display():
        try:
            c = sqlite3.connect(str(db))
            r = c.execute("SELECT value FROM settings WHERE key='display.PICO8'").fetchone()
            c.close()
            return r[0] if r else None
        except sqlite3.Error as e:
            return f'<{e}>'

    def start(name, args):
        """The launcher on the desk's socket: its walk (tools/deskwalk.py),
        its menu opening on Game, on Continue, as a cart's first does (no
        memory of the last menu, src/portmenu.h's PORT_MENU_MEM)"""
        if dsock.exists():
            dsock.unlink()
        (flags / 'zyperos-port-menu-mem').unlink(missing_ok=True)
        log = open(tmp / f'{name}.log', 'w')
        p = subprocess.Popen([str(BINARY)] + args, env=env, stdout=log, stderr=subprocess.STDOUT)
        procs.append(p)
        return dw.Walk(dsock, p, tmp / f'{name}.log')

    cart = f'{card}/Roms/PICO-8/celeste.p8'
    ASK = 'Start Cart Again'
    try:
        print('the in-game menu over your PICO-8:')
        m = start('menu', ['--port-menu', 'PICO8', cart])
        check(m.up('Game', 'Continue') and dw.until(lambda: 'Quit' in m.drawn()), 'it is up, on the Game tab')
        check(m.step('r1', 'Display', 'Display') and dw.until(lambda: 'Sharp Pixels' in m.drawn()),
              'R1: the Display tab, on its Display row, its value the mode on (Sharp Pixels)')
        # Right and Left on Display, then Down and Up: the cursor's move says
        # the two before it were read
        m.press('right')
        m.press('left')
        m.to('down', 'Shader', 'Display')
        m.to('up', 'Display', 'Display')
        check(display() == 'integer' and not restart.exists(), 'Left and Right on Display step nothing')
        check(m.step('a', 'Display', 'Sharp Pixels') and
              dw.until(lambda: 'Full Screen' in m.drawn() and 'Original Shape' in m.drawn()),
              'A opens the Display list, on the mode on')
        m.to('down', 'Actual Size', 'Display')
        check(m.step('a', ASK, 'Cancel') and dw.until(lambda: any('starts again in Actual Size' in l for l in m.drawn())),
              'a mode other than the one on asks, naming it, the cursor on Cancel')
        check(m.step('a', 'Display', 'Display') and m.proc.poll() is None and display() == 'integer' and
              not restart.exists(), 'A on the question\'s first stop is Cancel: the menu, nothing kept, no file')
        m.step('a', 'Display', 'Sharp Pixels')
        check(m.step('a', 'Display', 'Display') and m.proc.poll() is None and not restart.exists(),
              'picking the mode on asks nothing: the menu again')
        m.step('a', 'Display', 'Sharp Pixels')
        m.to('down', 'Actual Size', 'Display')
        m.step('a', ASK, 'Cancel')
        m.step('right', ASK, 'Start Again')
        rc = m.ends('a')
        check(rc == RESTART, f'Start Again ends the menu with PORT_MENU_RESTART ({rc})')
        check(display() == 'native', f'display.PICO8 kept ({display()})')
        check(restart.exists() and restart.read_text() == 'native\n', 'the restart file says native')

        print('a menu that ends otherwise, a file left from before:')
        restart.write_text('fill\n')
        m = start('menu-b', ['--port-menu', 'PICO8', cart])
        m.up('Game', 'Continue')
        rc = m.ends('b')
        check(rc == 10 and not restart.exists(), f'B is Continue, and no restart file is left ({rc})')

        print('On-Screen Stats over your PICO-8:')
        hud = lambda: (lambda c: (c.execute("SELECT value FROM settings WHERE key='hud'").fetchone() or [None])[0])(
            sqlite3.connect(str(db)))
        m = start('menu-hud', ['--port-menu', 'PICO8', cart])
        ok = m.up('Game', 'Continue') and m.step('r1', 'Display', 'Display')
        tab = m.rows() if ok else []
        check(ok and tab[:1] == ['Display'] and tab[-1:] == ['On-Screen Stats'],
              f'the Display tab\'s last row, as over a game ({tab})')
        check(hud() in (None, '0'), 'Off')
        m.to('down', 'On-Screen Stats', 'Display')
        m.press('right')
        check(dw.until(lambda: hud() == '1'), f'Right turns it On, kept for portkeys ({hud()!r})')
        m.press('left')
        check(dw.until(lambda: hud() == '0'), 'Left turns it Off again')
        rc = m.ends('b')
        check(rc == 10, f'B is Continue ({rc})')

        print('X from the Display tab:')
        m = start('menu-x', ['--port-menu', 'PICO8', cart])
        m.up('Game', 'Continue')
        m.step('r1', 'Display', 'Display')
        rc = m.ends('x')
        check(rc == 11 and not restart.exists(), f'Quit, from any tab, as over a game ({rc})')

        print('over OpenBOR:')
        m = start('openbor', ['--port-menu', 'OPENBOR', f'{card}/Roms/OpenBOR/x.pak'])
        ok = m.up('Game', 'Continue') and dw.until(lambda: 'Quit' in m.drawn())
        m.step('r1', 'Display')
        seen = m.drawn()
        check(ok and 'Continue' in seen and 'Sharp Pixels' not in seen and 'On-Screen Stats' not in seen,
              'its Display tab has no Display row and no On-Screen Stats (its picture is its own)')
        m.ends('b')

        print('--display, the Next Display hotkey\'s question alone:')
        m = start('next', ['--port-menu', 'PICO8', cart, '--display'])
        ok = m.up(ASK, 'Cancel') and dw.until(lambda: any('starts again in Full Screen' in l for l in m.drawn()))
        check(ok and 'Continue' not in m.drawn(), 'the next mode after Actual Size, round: Full Screen, and no menu')
        rc = m.ends('a')
        check(rc == 10 and display() == 'native' and not restart.exists(),
              f'Cancel: 10, nothing kept, no file ({rc})')
        m = start('next-yes', ['--port-menu', 'PICO8', cart, '--display'])
        m.up(ASK, 'Cancel')
        m.step('right', ASK, 'Start Again')
        rc = m.ends('a')
        check(rc == RESTART and display() == 'stretch' and restart.exists() and restart.read_text() == 'stretch\n',
              f'Start Again: 14, Full Screen kept and in the file ({rc}, {display()})')
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        if dsock.exists():
            dsock.unlink()


def main():
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-pico8-display-'))
    try:
        launch_sh(tmp)
        if BINARY.is_file():
            defaults(tmp)
            launcher(tmp)
        else:
            check(False, 'build-native/zyperos is there (make native)')
    finally:
        if not fails:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f'check-pico8-display: {len(fails)} failed (logs in {tmp})')
        return 1
    print('ok: the Display row over your PICO-8, its question and its start again')
    return 0


if __name__ == '__main__':
    sys.exit(main())
