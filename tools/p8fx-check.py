#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-p8fx: your PICO-8's Shader and Border, all but the drawing
(src/p8fx.h; the drawing is check-p8fx-gl's, tools/p8fx-gl-check.py).

1. build-native/p8fx-check: the look file, the shaders and their steps, Next
   Shader on a scratch database, the picture out of PICO-8's preblit, the
   rect that covers the screen; and its list of shaders is the launcher's
   SHADERS (src/app.h) for the PICO-8 shelf on a GPU device, in order.
2. sd/pixel2/PICO-8.sh, on a card in a scratch folder with a stand-in
   pico8_64 that writes down its LD_PRELOAD: p8fx.so beside the script is
   loaded first, before one already set; with none, nothing is, and the log
   says so.
3. The real launcher in desk mode's dummy window (build-native/zyperos, make
   native), as portkeys starts it over the program (--port-menu PICO8 CART):
   its Display tab (R1 from Game, a game's tabs since 1.1.0-rc.8) Display,
   Border, Shader and On-Screen Stats in a game's order, Strength under an
   old screen; Shader stepped is shader.PICO8 kept and the look
   file written for the shim, Strength and Border the same; Your Own's
   picture made into its DTOV file; no Border where the mode covers the
   screen; over OpenBOR none of them. And a cart opened on your PICO-8 from
   the shelf writes the look file from the shelf's rows as the launcher
   hands over.
Every wait in the walks is for an event, never a time or a count of frames
(tools/deskwalk.py): the panel a key lands on, as desk's `state` says it, a
setting, a file, the launcher's end. On a runner busy with other checks the
walk is slower, and says the same (GitHub's macOS runner, #406).
"""
import json
import os
import re
import shutil
import socket
import sqlite3
import struct
import subprocess
import sys
import tempfile
import time
import importlib.machinery
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
CHECKER = ROOT / 'build-native/p8fx-check'
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


# ---- 1. the plain part ----------------------------------------------------

def plain():
    print('the look file, the shaders, Next Shader, the picture:')
    r = subprocess.run([str(CHECKER)], capture_output=True, text=True)
    for line in r.stdout.splitlines():
        if 'FAIL' in line:
            print(line)
    last = r.stdout.strip().splitlines()[-1:] or ['(nothing)']
    check(r.returncode == 0, f'build-native/p8fx-check: {last[0]}')
    listed = subprocess.run([str(CHECKER), '--list'], capture_output=True, text=True).stdout.split()
    text = (ROOT / 'src/app.h').read_text()
    rows = re.findall(r'\{\s*"([a-z-]+)",\s*S_[A-Z_]+,\s*(true|false),\s*(NULL|"[^"]*")\s*\}', text)
    want = [n for n, gpu, only in rows if only == 'NULL' or ' PICO8 ' in only]
    check(rows and listed == want, f'p8fx_shaders is SHADERS for PICO8 on a GPU device, in order ({listed})')


# ---- 2. PICO-8.sh ---------------------------------------------------------

def script(tmp):
    print('PICO-8.sh loads the library:')
    root = tmp / 'sh'
    card = root / 'card'
    zy = card / 'ZyperOS/pico8'
    for p in (card / 'Bios/PICO-8', card / 'Roms/PICO-8', root / 'bin', root / 'drm/card0-DSI-1', zy):
        p.mkdir(parents=True, exist_ok=True)
    (root / 'drm/card0-DSI-1/modes').write_text('480x640\n')
    stand = card / 'Bios/PICO-8/pico8_64'
    stand.write_text('#!/bin/sh\nprintf "%s" "${LD_PRELOAD-<unset>}" > "$P8_PRELOAD"\n')
    stand.chmod(0o755)
    (card / 'Bios/PICO-8/pico8.dat').write_text('stand-in')
    (card / 'Roms/PICO-8/x.p8').write_text('pico-8 cartridge\n')
    (root / 'bin/mount').write_text('#!/bin/sh\nexit 0\n')
    (root / 'bin/mount').chmod(0o755)
    text = (ROOT / 'sd/pixel2/PICO-8.sh').read_text()
    check('CARD=/mnt/SDCARD\n' in text and '/sys/class/drm/' in text, 'PICO-8.sh still has its CARD line and DRM folder')
    text = text.replace('CARD=/mnt/SDCARD\n', f"CARD='{card}'\n").replace('/sys/class/drm/', f'{root}/drm/')
    sh = zy / 'PICO-8.sh'
    sh.write_text(text)
    sh.chmod(0o755)
    got = root / 'preload'

    def run(extra=None):
        env = dict(os.environ, PATH=f'{root}/bin:' + os.environ['PATH'], ZYPEROS_DISPLAY='integer',
                   P8_PRELOAD=str(got))
        env.pop('LD_PRELOAD', None)
        if extra is not None:
            env['LD_PRELOAD'] = extra
        got.unlink(missing_ok=True)
        r = subprocess.run(['sh', str(sh), str(card / 'Roms/PICO-8/x.p8')], env=env, capture_output=True,
                           text=True, cwd=str(root))
        return (got.read_text() if got.exists() else None), r.stdout + r.stderr

    lib = zy / 'p8fx.so'
    lib.write_bytes(b'stand-in')
    pre, _ = run()
    check(pre == str(lib), f'p8fx.so beside the script is LD_PRELOAD ({pre})')
    pre, _ = run('/usr/lib/other.so')
    check(pre == f'{lib} /usr/lib/other.so', f'...first, before one already set ({pre})')
    lib.unlink()
    pre, out = run()
    check(pre == '<unset>', f'none beside it: no LD_PRELOAD ({pre})')
    check('no p8fx.so beside this script' in out, '...and the log says so')


# ---- 3. the launcher ------------------------------------------------------

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


def png(path, w, h):
    """A small PNG for Your Own, made with the standard library"""
    import zlib
    raw = b''.join(b'\x00' + b''.join(bytes((x * 255 // w, y * 255 // h, 90)) for x in range(w))
                   for y in range(h))

    def chunk(kind, data):
        c = struct.pack('>I', len(data)) + kind + data
        return c + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


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
    db = card / 'userdata/zyperos.db'

    def setting(key, value=None):
        c = sqlite3.connect(str(db))
        if value is None:
            c.execute('DELETE FROM settings WHERE key=?', (key,))
        else:
            c.execute('INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)', (key, value))
        c.commit()
        c.close()

    def value(key):
        try:
            c = sqlite3.connect(str(db))
            r = c.execute('SELECT value FROM settings WHERE key=?', (key,)).fetchone()
            c.close()
            return r[0] if r else None
        except sqlite3.Error as e:
            return f'<{e}>'

    look = flags / 'zyperos-pico8fx'
    picture = flags / 'zyperos-pico8-border.dtov'

    def look_lines():
        return dict(l.split(' ', 1) for l in look.read_text().splitlines()) if look.exists() else {}

    setting('display.PICO8', 'integer')
    short = Path(tempfile.gettempdir())
    dsock = short / f'zyp8fx-{os.getpid()}.sock'
    cdir = short / f'zyp8fxc-{os.getpid()}'
    cdir.mkdir(mode=0o700, exist_ok=True)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(dsock), ZYPEROS_FLAG_DIR=str(flags), ZYPEROS_CTL_DIR=str(cdir))
    env.pop('ZYPEROS_HEADLESS', None)
    procs = []

    def start(name, args):
        """The launcher on the desk's socket: its walk (tools/deskwalk.py),
        the menu on Game, on Continue, as a cart's first (no memory of the
        last, src/portmenu.h's PORT_MENU_MEM)"""
        if dsock.exists():
            dsock.unlink()
        (flags / 'zyperos-port-menu-mem').unlink(missing_ok=True)
        log = open(tmp / f'{name}.log', 'w')
        p = subprocess.Popen([str(BINARY)] + args, env=env, stdout=log, stderr=subprocess.STDOUT)
        procs.append(p)
        return dw.Walk(dsock, p, tmp / f'{name}.log')

    def kept(key, word, line, lineword):
        """`key` kept as `word` and the look file's `line` saying `lineword`"""
        return dw.until(lambda: value(key) == word and look_lines().get(line) == lineword)

    cart = f'{card}/Roms/PICO-8/celeste.p8'
    try:
        print('the in-game menu over your PICO-8, at Sharp Pixels:')
        m = start('menu', ['--port-menu', 'PICO8', cart])
        check(m.up('Game', 'Continue') and m.step('r1', 'Display', 'Display'), 'it is up, R1 its Display tab')
        rows = m.rows()
        check(rows == ['Display', 'Border', 'Shader', 'On-Screen Stats'],
              f'Display, Border, Shader, On-Screen Stats: a game\'s Display tab\'s order ({rows})')
        check('Strength' not in rows, 'no Strength while the shader is Off')
        m.to('down', 'Shader', 'Display')
        m.press('right')                                      # Shader: Off -> Sharp-Shimmerless
        check(kept('shader.PICO8', 'shimmerless', 'shader', 'shimmerless'),
              f'Right on Shader: shader.PICO8 kept, and the look file says it ({value("shader.PICO8")}, {look_lines()})')
        m.press('right')                                      # Sharp-Shimmerless -> CRT
        check(kept('shader.PICO8', 'crt', 'shader', 'crt'), 'Right again: CRT')
        check(dw.until(lambda: 'Strength' in m.drawn()), '...and Strength under it')
        m.step('down', 'Display', 'Strength')
        m.press('left')                                       # Strength 100 -> 90
        check(kept('shader_strength.PICO8', '90', 'strength', '90'),
              f'Left on Strength: 90%, kept and in the file ({value("shader_strength.PICO8")}, {look_lines()})')
        m.to('up', 'Border', 'Display')
        m.press('right')                                      # Border: Off -> Blur
        check(kept('border.PICO8', 'blur', 'border', 'blur'),
              f'Right on Border: Blur, kept and in the file ({value("border.PICO8")}, {look_lines()})')
        check(m.ends('b') == 10, 'B is Continue')

        print('Your Own, a picture in Borders/PICO-8:')
        (card / 'Borders/PICO-8').mkdir(parents=True, exist_ok=True)
        png(card / 'Borders/PICO-8/frame.png', 64, 48)
        m = start('own', ['--port-menu', 'PICO8', cart])
        m.up('Game', 'Continue')
        m.step('r1', 'Display', 'Display')
        m.to('down', 'Border', 'Display')
        m.press('right')                                      # Border: Blur -> Your Own
        kept('border.PICO8', 'own', 'border', 'picture')
        lines = look_lines()
        check(value('border.PICO8') == 'own' and lines.get('border') == 'picture' and
              lines.get('border_file') == str(picture), f'Your Own: the look file names its picture ({lines})')

        def dtov():
            head = picture.read_bytes()[:8] if picture.exists() else b''
            size = struct.unpack('<HH', head[4:8]) if len(head) == 8 else None
            # The panel's: the Pixel 2's 640x480 on the device, the desk's
            # own 1024x768 here, where --port-menu takes no --device
            return size if (head[:4] == b'DTOV' and size in ((640, 480), (1024, 768)) and
                            picture.stat().st_size == 8 + size[0] * size[1] * 4) else None
        dw.until(lambda: dtov() is not None)
        check(dtov() is not None, f'...a DTOV file the panel\'s size ({dtov()})')
        m.ends('b')

        print('at Full Screen, which covers the screen:')
        setting('display.PICO8', 'stretch')
        m = start('stretch', ['--port-menu', 'PICO8', cart])
        ok = m.up('Game', 'Continue') and m.step('r1', 'Display', 'Display')
        rows = m.rows() if ok else []
        check('Shader' in rows and 'Border' not in rows, f'Shader, and no Border ({rows})')
        m.ends('b')

        print('over OpenBOR:')
        m = start('openbor', ['--port-menu', 'OPENBOR', f'{card}/Roms/OpenBOR/x.pak'])
        ok = m.up('Game', 'Continue') and dw.until(lambda: 'Quit' in m.drawn())
        seen = m.drawn()
        check(ok and 'Continue' in seen and 'Shader' not in seen and 'Border' not in seen,
              'no Shader and no Border')
        m.ends('b')

        print('a cart opened on your PICO-8 from the shelf:')
        setting('display.PICO8', 'integer')
        setting('shader.PICO8', 'lcd')
        setting('shader_strength.PICO8', '40')
        setting('border.PICO8', 'blur')
        look.unlink(missing_ok=True)
        w = start('shelf', ['--device', 'pixel2'])
        check(dw.until(lambda: w.sock.exists() and w.state().get('ok') and (cdir / 'ctl.sock').exists()),
              'the launcher started')
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(dw.CEILING)
        s.connect(str(cdir / 'ctl.sock'))
        s.sendall(f'open\tpath={cart}\n'.encode())
        said = s.makefile().readline().strip()
        s.close()
        check(said.startswith('ok') and 'core=run:ZyperOS/pico8/PICO-8.sh' in said,
              f'it opens on your PICO-8 ({said[:60]!r})')
        check(w.ends() == 0, 'the launcher ends for it')
        lines = look_lines()
        check(lines.get('shader') == 'lcd' and lines.get('strength') == '40' and lines.get('border') == 'blur',
              f'the look file is the shelf\'s rows ({lines})')
        check('p8fx: PICO-8: shader lcd at 40%, border blur' in w.text(), 'and the log says it')
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
                p.wait()
        shutil.rmtree(cdir, ignore_errors=True)
        if dsock.exists():
            dsock.unlink()


def main():
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-p8fx-'))
    try:
        if CHECKER.is_file():
            plain()
        else:
            check(False, 'build-native/p8fx-check is there')
        script(tmp)
        if BINARY.is_file():
            launcher(tmp)
        else:
            check(False, 'build-native/zyperos is there (make native)')
    finally:
        if not fails:
            shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f'check-p8fx: {len(fails)} failed (logs in {tmp})')
        return 1
    print('ok: your PICO-8\'s Shader and Border, kept, written for the shim and loaded with it')
    return 0


if __name__ == '__main__':
    sys.exit(main())
