# SPDX-License-Identifier: GPL-3.0-only
"""A check's walk through the real launcher in desk mode's dummy window,
every wait a wait for an event, never for a time or a count of frames: on a
runner busy with other checks the walk is slower, and says the same
(tools/pico8-keys-check.py, pico8-display-check.py, p8fx-check.py).

- A key is a press and a let-go (a press and a let-go in one pass are a tap
  to every screen), then a wait for what it does: the panel it lands on (the
  desk's `state`: a menu panel's heading as `screen` and the row under the
  cursor as `focus`, src/menu_runner.c), a file or a setting, or the
  launcher's end.
- A string drawn is found wherever its line begins (drawn_lines): ALSA's
  complaints from the interface sounds' thread can come out in front of
  "fit: drawn: " on its line, on a runner with no sound card (as #415 found).
- The ceiling (CEILING) is far beyond the slowest runner's answer, and is
  reached only when the event never comes."""
import json
import socket
import subprocess
import time
from pathlib import Path

CEILING = 30


def until(ok, ceiling=None):
    """True as soon as ok() says so, looked at every 20 ms; False at the
    ceiling"""
    end = time.monotonic() + (ceiling or CEILING)
    while True:
        try:
            if ok():
                return True
        except (OSError, ValueError):
            pass
        if time.monotonic() > end:
            return False
        time.sleep(0.02)


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


def drawn_lines(log):
    """Every string the launcher says it drew (src/ui.c's "fit: drawn: "),
    in order, wherever its line begins: on Linux with no sound card, ALSA's
    complaints from the interface sounds' thread come out on the same
    stderr in pieces, and one can land before it on its line ("ALSA lib
    confmisc.c:1342:(snd_func_refer) fit: drawn: Paused", check-mp4resume's
    CI run of #406). check-mp4resume and check-pico8-mp4 read with it too."""
    return [part.split('\n', 1)[0] for part in log.split('fit: drawn: ')[1:]]


class Walk:
    """One launcher on the desk socket `sock`, its process `proc` and its
    log at `log`"""

    def __init__(self, sock, proc, log):
        self.sock, self.proc, self.log = Path(sock), proc, Path(log)

    def text(self):
        return self.log.read_text(errors='replace') if self.log.exists() else ''

    def drawn(self):
        return drawn_lines(self.text())

    def state(self):
        try:
            return ask(self.sock, 'state')
        except (OSError, ValueError):
            return {}

    def at(self, screen=None, focus=None):
        """The panel drawn last: its heading begins with `screen` (None for
        any) and the row under the cursor says `focus` (None for any)"""
        s = self.state()
        if not s.get('ok'):
            return False
        if screen is not None and not (s.get('screen', '') == screen if screen == '' else
                                       s.get('screen', '').startswith(screen)):
            return False
        return focus is None or s.get('focus') == focus

    def up(self, screen=None, focus=None):
        """The launcher up on its socket, and its first panel drawn as said"""
        return until(lambda: self.sock.exists() or self.proc.poll() is not None) and self.sock.exists() and \
            until(lambda: self.at(screen, focus) or self.proc.poll() is not None) and self.proc.poll() is None

    def press(self, key):
        """A press and a let-go; False when the launcher has gone"""
        try:
            ask(self.sock, f'press {key}')
            ask(self.sock, f'release {key}')
            return True
        except (OSError, ValueError):
            return False

    def step(self, key, screen=None, focus=None, then=None):
        """`key`, then a wait until the panel is as said and then() (when
        given) says so, or the launcher has ended: True when it is as said"""
        self.press(key)
        return until(lambda: self.proc.poll() is not None or
                     (self.at(screen, focus) and (then is None or then())))

    def to(self, key, focus, screen=None, most=12):
        """`key` until the row under the cursor says `focus`, each press
        waited for (the cursor on another row); True when it does"""
        for _ in range(most):
            if self.at(screen, focus):
                return True
            was = self.state().get('cursor')
            self.press(key)
            if not until(lambda: self.proc.poll() is not None or self.state().get('cursor') != was):
                return False
        return self.at(screen, focus)

    def rows(self, most=16):
        """The stops of the panel up, in order from the row under the
        cursor: Down, each press waited for, until the cursor is back where
        it began (a list goes round on a fresh press, DESIGN.md 11.3). A
        string drawn is said once a run (drawn_lines), so a tab's rows, whose
        names the tab row may have drawn already, are read off the cursor.
        A press is waited for by the launcher's loop going round twice after
        its let-go: `state` is answered as the loop reads its events, and
        the pass that read the let-go acts on the press, so the second
        answer after it says where the cursor went, or that it stayed (a
        tab of one row)"""
        first = self.state().get('focus')
        out = [first]
        for _ in range(most):
            was = self.state().get('cursor')
            if not self.press('down'):
                break
            self.state()
            s = self.state()
            if self.proc.poll() is not None or s.get('cursor') == was:
                break
            focus = s.get('focus')
            if focus == first:
                break
            out.append(focus)
        return out

    def ends(self, key=None):
        """`key` (when given), then the launcher's end: its exit status, or
        None if it never ends"""
        if key:
            self.press(key)
        try:
            return self.proc.wait(timeout=CEILING)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
            return None
