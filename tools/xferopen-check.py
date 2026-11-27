#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""check-xferopen: USB File Transfer opens at once, whatever the USB
controller does (src/gadget.h). The host launcher in desk mode's dummy window,
as the Pixel 2, on tools/desk's sample card, with desk mode's stand-in cable
(src/device/deskcable.h): a folder of files for the controller's, its writes
made by the device's own code in hwput's child, each one slow
(ZYPEROS_DESK_CABLE_DELAY_MS). A computer on the cable is asked about; File
Transfer is chosen; then:

  - a link that is off: the page's first frame comes within a frame or two
    of the press, with the loading bar, while the bind is still on its way
    (3 s, past the 2 s the launcher says is too long); B leaves while it is;
    the bind lands, then the unbind leaving asked for, and no child is left
  - the same, and POWER: its power-off is asked for at once, and the
    launcher ends (its power-off's goodbye, about 2 s) with the bind, 8 s
    slow there, still on its way
  - a link a computer configured and answers on (Developer Mode's): no bind,
    no toggle, nothing written, the page at once
  - a configured link the computer does not answer on: made fresh
    (soft_connect "disconnect", then "connect"), the page at once all the same

Before gadget.h (the binds and the toggle waited for on the main thread) the
page waited out every write: XFEROPEN_BEFORE=1 runs it so (deskcable.h), and
it fails. build-native/zyperos must be built."""
import importlib.machinery
import importlib.util
import os
import re
import shutil
import socket
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build-native/zyperos'
DELAY_MS = 3000
fails = []
times = {}


def check(ok, what):
    print(f'  {"ok  " if ok else "FAIL"} {what}')
    if not ok:
        fails.append(what)


def desk_module():
    loader = importlib.machinery.SourceFileLoader('desk', str(ROOT / 'tools/desk'))
    spec = importlib.util.spec_from_loader('desk', loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


def ask(sock, line, timeout=15):
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


def put(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)


def cable(root, bound, state, carrier, answers):
    put(root / 'peer', 'computer\n')
    put(root / 'gadget/UDC', 'fake.udc\n' if bound else '\n')
    put(root / 'udc/fake.udc/state', state + '\n')
    put(root / 'udc/fake.udc/soft_connect', '')
    put(root / 'net/usb0/carrier', '1\n' if carrier else '0\n')
    put(root / 'address', '1\n' if carrier else '0\n')
    put(root / 'answers', '1\n' if answers else '0\n')


def children(pid):
    out = subprocess.run(['ps', '-A', '-o', 'pid=,ppid=,stat='], capture_output=True, text=True).stdout
    kids = []
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 3 and f[1] == str(pid):
            kids.append((int(f[0]), f[2]))
    return kids


def wait_for(logf, pattern, after=0, timeout=10.0):
    t0 = time.time()
    while time.time() - t0 < timeout:
        text = logf.read_text(errors='replace')[after:]
        m = re.search(pattern, text)
        if m:
            return m
        time.sleep(0.02)
    return None


def run(desk, tmp, name, bound, state, carrier, answers, expect, delay=DELAY_MS):
    print(f'{name}:')
    card = tmp / f'card-{name}'
    desk.make_card(card, 'pixel2', False)
    root = tmp / f'cable-{name}'
    cable(root, bound, state, carrier, answers)
    sock = Path(tempfile.gettempdir()) / f'zyperos-xferopen-{os.getpid()}-{name}.sock'
    flags = tmp / f'flags-{name}'
    flags.mkdir()
    env = desk.card_env(card, None)
    env.update(SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', ZYPEROS_DESK_FORCE='1',
               ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(flags), ZYPEROS_CTL_DIR=str(tmp / f'ctl-{name}'),
               ZYPEROS_DESK_CABLE=str(root), ZYPEROS_DESK_CABLE_DELAY_MS=str(delay))
    if os.environ.get('XFEROPEN_BEFORE'):
        env['ZYPEROS_DESK_CABLE_BEFORE'] = '1'      # 1.1.0's: every write waited for
    env.pop('ZYPEROS_HEADLESS', None)
    logf = tmp / f'{name}.log'
    log = open(logf, 'w')
    p = subprocess.Popen([str(BINARY), '--device', 'pixel2'], env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        for _ in range(300):
            if sock.exists() or p.poll() is not None:
                break
            time.sleep(0.05)
        if not sock.exists():
            check(False, f'{name}: the control socket is up')
            print(logf.read_text()[-2000:])
            return
        # The computer on the cable is asked about once the shelf is up
        m = wait_for(logf, r'usb: a computer is on the cable', timeout=15)
        check(m is not None, f'{name}: the computer on the cable is asked about')
        time.sleep(1.0)                          # the question's panel up
        ask(sock, 'tap right')
        time.sleep(0.3)
        mark = len(logf.read_text(errors='replace'))
        t_press = time.time()
        lat = ask(sock, 'latency a')
        m = wait_for(logf, r'xfer: opened in (\d+) ms', after=mark)
        opened = int(m.group(1)) if m else None
        f = wait_for(logf, r"xfer: the page's first frame (\d+) ms after it was asked for \(([^;]*); "
                           r"drawn by (\d+) ms, its present (\d+) ms\)", after=mark)
        first = int(f.group(1)) if f else None
        drawn = int(f.group(3)) if f else None
        times.setdefault(name, []).append((opened, drawn, first))
        t_first = time.time() - t_press
        # Its time is this computer's too (the card read, a fork under
        # load): what must not be in it is a controller write, DELAY_MS each
        check(opened is not None and opened < 1000, f'{name}: File Transfer opened in {opened} ms')
        # The first frame's drawing is this computer's (a loaded one draws
        # slowly, and desk mode's present draws the window's face): what must
        # not be in it is any of the link's writes, each DELAY_MS long
        check(drawn is not None and drawn < 1000,
              f'{name}: its first frame drawn {drawn} ms after it was asked for ({f.group(2) if f else "none"}), '
              f'on the window {first} ms after, {t_first * 1000:.0f} ms after the press here')
        text = logf.read_text(errors='replace')[mark:]
        if expect == 'bind':
            check(f is not None and f.group(2) == 'the loading bar', f'{name}: the loading bar while the link comes')
            check('usb: the link to the computer is on' not in text,
                  f'{name}: the page up before the bind landed ({DELAY_MS} ms a write)')
            # B leaves at once, the bind still on its way
            mark2 = len(logf.read_text(errors='replace'))
            lat = ask(sock, 'latency b')
            ms = lat.get('press_to_present_ms')
            c = wait_for(logf, r'xfer: closed', after=mark2, timeout=3)
            check(c is not None and ms is not None and ms < 500,
                  f'{name}: B left the page with the bind on its way ({ms} ms to the next frame)')
            check(wait_for(logf, r'usb: the controller did not answer \(the gadget.s bind', after=mark, timeout=4) is not None,
                  f'{name}: the slow bind said, past 2 s, and the launcher went on')
            check(wait_for(logf, r'usb: the link to the computer is on \(fake\.udc\)', after=mark, timeout=8) is not None,
                  f'{name}: the bind landed in the background')
            check(wait_for(logf, r'usb: the link to the computer is off: charging only', after=mark, timeout=10) is not None,
                  f'{name}: then the unbind that leaving asked for')
            # A plain file keeps what an unbind's "\n" did not write over
            check((root / 'gadget/UDC').read_text().split('\n')[0] == '', f'{name}: the gadget let go at the end')
            check(wait_for(logf, r'usb: the link was bound a moment ago: no fresh link', after=mark, timeout=1) is not None
                  or 'soft_connect' not in logf.read_text(errors='replace')[mark:],
                  f'{name}: no fresh link over a link just bound')
            check((root / 'udc/fake.udc/soft_connect').read_text() == '', f'{name}: soft_connect never written')
        elif expect == 'power':
            # POWER is had at once, the bind still on its way: the power-off
            # it asks for comes, and the launcher ends, not waiting on it
            t_pw = time.time()
            ask(sock, 'tap power')
            pw = wait_for(logf, r'power: a power-off asked', after=mark, timeout=2)
            ms = (time.time() - t_pw) * 1000
            check(pw is not None and 'usb: the link to the computer is on' not in
                  logf.read_text(errors='replace')[mark:],
                  f'{name}: POWER had with the bind on its way, its power-off asked ({ms:.0f} ms here)')
            try:
                p.wait(timeout=delay / 1000)
            except subprocess.TimeoutExpired:
                pass
            ms = (time.time() - t_pw) * 1000
            check(p.poll() is not None and ms < delay,
                  f'{name}: the launcher ended {ms:.0f} ms after POWER, the bind ({delay} ms) still on its way')
            return
        elif expect == 'still':
            time.sleep(1.5)
            text = logf.read_text(errors='replace')[mark:]
            check('usb: the computer answers over the cable: the link left as it is' in text,
                  f'{name}: the computer answered, the link left as it is')
            check("the gadget's UDC" not in text and 'soft_connect' not in text,
                  f'{name}: nothing bound, unbound or toggled')
            check((root / 'udc/fake.udc/soft_connect').read_text() == '', f'{name}: soft_connect never written')
            check((root / 'gadget/UDC').read_text() == 'fake.udc\n', f'{name}: the gadget still bound')
            ask(sock, 'tap b')
        elif expect == 'fresh':
            check(wait_for(logf, r'wire: the cable connection made fresh', after=mark, timeout=2 * DELAY_MS / 1000 + 4) is not None,
                  f'{name}: the link made fresh in the background')
            text = logf.read_text(errors='replace')[mark:]
            check('usb: the computer did not answer over the cable: a fresh link' in text and
                  text.find('usb: soft_connect disconnect') < text.find('usb: soft_connect connect'),
                  f'{name}: the computer did not answer: "disconnect", then "connect"')
            check((root / 'udc/fake.udc/soft_connect').read_text().startswith('connect'),
                  f'{name}: soft_connect written')
            ask(sock, 'tap b')
        time.sleep(0.5)
        # Every job's child reaped: none left, and none a zombie
        for _ in range(100):
            kids = [k for k in children(p.pid)]
            if not any(s.startswith('Z') for _, s in kids):
                break
            time.sleep(0.1)
        check(not any(s.startswith('Z') for _, s in children(p.pid)), f'{name}: no zombie left')
        ask(sock, 'quit')
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        log.close()
        if sock.exists():
            sock.unlink()
        if fails:
            print('    the log:', logf)


def main():
    if not BINARY.is_file():
        print('check-xferopen: build-native/zyperos missing (make native)')
        return 1
    desk = desk_module()
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-xferopen-'))
    try:
        run(desk, tmp, 'off', False, 'not attached', False, False, 'bind')
        run(desk, tmp, 'power', False, 'not attached', False, False, 'power', delay=8000)
        run(desk, tmp, 'configured', True, 'configured', True, True, 'still')
        run(desk, tmp, 'dropped', True, 'configured', True, False, 'fresh')
    finally:
        if not fails:
            shutil.rmtree(tmp, ignore_errors=True)
    for name, rows in times.items():
        for opened, drawn, first in rows:
            print(f'  {name}: opened in {opened} ms, its first frame drawn by {drawn} ms, on the window by {first} ms')
    print(f'xferopen: {"all passed" if not fails else f"{len(fails)} failed (kept in {tmp})"}')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
