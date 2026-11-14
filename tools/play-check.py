#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The play page, a guest player's picture and pad, in a browser: make check-play.

    make build-native/p2d build-native/p2d-feed build-native/p2codec-check build-native/webtheme-check
    python3 tools/play-check.py [--chrome PATH]

build-native/p2codec-check writes its fixtures into this check's own temp
folder: 100 frames as the codec's messages and as the RGBA they decode to.
build-native/p2d-feed --rgba writes those frames into a share 60 times a
second, as Diatom does in a game, so the share's frame n is the fixture's
frame (n-1) % 100. build-native/p2d serves the real page (res/web/play and
res/web/i18n, copied into a temp root) on 127.0.0.1, on a port the kernel
picks, in French (--lang fr), and headless Chrome runs the page over the
DevTools protocol, as tools/portal-check.py does the File Transfer page.
Keys and touches are DevTools input, so the page gets them as a browser's
own: keydown, and Pointer Events of pointerType "touch". What it holds:

  - draws_the_frames: what the canvas shows is the fixture's frame of the
    number the page says it drew (window.p2.frame), a second in, and again
    150 frames on, past a periodic KEY and the fixture going round
  - keyboard_to_pad: every key's button reaches the share (the arrows,
    Z = B, X = A, Shift = SELECT, Enter = START); X and Right together are
    0x0180, still so past the 1 s p2d lets a silent pad go, and 0 let go
  - touch_to_pad: a finger on A sets bit 8; a second on Left makes 0x0140;
    the first rolled onto B, B and Left; a finger on the d-pad's corner,
    the diagonal; and 0 once they lift
  - haptics: a press ticks once (navigator.vibrate, 15 ms), a roll onto
    another button and a d-pad corner once each; a lift, a held finger and
    a key do not
  - status_and_buttons: the pad is drawn from the game's buttons, the
    NES's 0x01FD until the status names others; Y, X, L and R are drawn
    and their keys pressed only when it does, and the page never sends a
    bit the game has not got; "playing" with a player's number says that
    number, and the NES's buttons named again draw the NES's pad again
  - picture_size: upright, the picture right under the bar, as wide as the
    screen; on its side and on a laptop, the whole height; whole device
    pixels unless they would leave it under 85% of the room, and whole on
    the common phones on their side (667x375 at 2x: 2 a pixel of the NES's,
    731x411 at 2.625x: 3, 844x390 at 2x and 3x: 3 and 4) and on a laptop
    (1280x800 at 1x and 2x: 768x720); the pad's parts on the screen and
    clear of each other and of the picture
  - picture_off: switched on, the picture hides and the pad takes the
    screen, the canvas holds still for a second while the acks go on; off
    again, the page asks p2d for a KEY ('K'), and the picture is the game's
    again from the frame it asked at, not the codec's next KEY
  - left_and_back: a page left for another lets its place go at once, well
    before p2d's 5 s rule, and Back plays again
  - theme_from_handheld: the theme file p2d is given (--theme; the handheld's
    look, once by hand and once as build-native/webtheme-check's writer makes
    it) wins over the page's tokens and the browser's light or dark: its accent
    on a held button, its ground, its color-scheme, its square corners;
    without one, the page's own tokens by prefers-color-scheme
  - old_browser_message: with DecompressionStream deleted before the
    page's script runs, the page says the browser is too old and opens no
    socket; and so with ResizeObserver and replaceChildren gone too, so the
    check comes before anything else the page sets up
  - no_zoom: the viewport's limits, touch-action none on every part, and
    Safari's gesture events, dblclick, a double tap's second tap and two
    fingers moving off the pad cancelled; taps on Picture off never are,
    quick ones and one right after a press; a zoom that came anyway (a
    stand-in visualViewport: headless Chrome never zooms) puts the limits
    again, and at 1 nothing is touched
  - full_then_player: a second tab is told the game is full, the first
    plays on, and the second keeps trying: the first closed, it is player 2
  - stopped_then_back: p2d's SIGTERM with the share still there (the
    handheld turned multiplayer off, the game goes on): the page says so,
    still past its tries, and plays again once a p2d is there again
  - ended_message: the share removed, the page says the game ended, and
    still says so after p2d has gone and a retry would have come; a tab
    told the game is full says it is connecting once its retries no longer
    reach p2d

No device is touched and nothing is fetched from the internet. Chrome is
needed; without it the check says so and fails. make check runs three
checks at a time on a Mac whose load average reaches 20 to 30 and more, so
every wait is for a thing to happen, with seconds to spare. On GitHub's
macOS runner, three times slower, Chrome can be starved long enough that
p2d lets the page go (5 s with nothing from it) and the page comes back:
the steps that press or put a status to the page run on one link, and
again once the page plays again if the link changed under them
(on_one_link). A failing test prints the page's links and p2d's last lines,
which say when each player came and went and why.
"""
import argparse
import base64
import importlib.util
import json
import mmap
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('readme_shots', ROOT / 'tools/readme-shots.py')
rs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rs)
sys.path.insert(0, str(ROOT / 'tools'))
import i18n  # noqa: E402

P2D = ROOT / 'build-native/p2d'
FEED = ROOT / 'build-native/p2d-feed'
CODEC = ROOT / 'build-native/p2codec-check'
WEBTHEME = ROOT / 'build-native/webtheme-check'
LANG = 'fr'

W, H, FRAMES = 256, 240, 100          # the fixture's (tools/p2codec-check.c)
FRAME_BYTES = W * H * 4
SHARE_SIZE = 4096 + 3 * 512 * 480 * 4  # src/p2share.h's P2S_SIZE
PAD_AT, GUESTS_AT = 24, 40             # p2s_head's pad[0] and guests (tools/p2share-check.c)
FRAME_AT = 12                          # and frame, the newest frame's number

# Libretro's RetroPad bits, the mask's
B, SELECT, START, UP, DOWN, LEFT, RIGHT, A = 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7, 1 << 8

# DevTools' key events: key, code, virtual key code, and the button it is
KEYS = {
    'ArrowUp': ('ArrowUp', 'ArrowUp', 38, UP),
    'ArrowDown': ('ArrowDown', 'ArrowDown', 40, DOWN),
    'ArrowLeft': ('ArrowLeft', 'ArrowLeft', 37, LEFT),
    'ArrowRight': ('ArrowRight', 'ArrowRight', 39, RIGHT),
    'z': ('z', 'KeyZ', 90, B),
    'x': ('x', 'KeyX', 88, A),
    'Shift': ('Shift', 'ShiftLeft', 16, SELECT),
    'Enter': ('Enter', 'Enter', 13, START),
}
# And the keys of the buttons an NES has not got: Y, X, L and R
MORE_KEYS = {
    'a': ('a', 'KeyA', 65, 1 << 1),
    's': ('s', 'KeyS', 83, 1 << 9),
    'q': ('q', 'KeyQ', 81, 1 << 10),
    'w': ('w', 'KeyW', 87, 1 << 11),
    'Meta': ('Meta', 'MetaLeft', 91, 0),
}

failures = 0


def check(ok, what):
    global failures
    print(('  ok: ' if ok else '  FAIL: ') + what, flush=True)
    if not ok:
        failures += 1
    return ok


def wait_for(pred, seconds, step=0.05):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def words(code):
    """The play page's strings in `code`, as res/strings has them"""
    errors = []
    return {s.id: i18n.get(s, code) for s in i18n.parse(errors) if s.id.startswith('play.')}


# ---- the share, the feed and p2d -----------------------------------------

class Rig:
    """p2d-feed writing the fixture's frames into a share, and p2d serving it
    and the page from `root`"""

    def __init__(self, tmp, root, fixture):
        self.share = tmp / 'share'
        self.log = tmp / 'p2d.log'
        self.root = root
        self.feed = self.p2d = self.m = None
        try:
            self.start(fixture)
        except BaseException:
            self.close()
            raise

    def start(self, fixture):
        self.feed = subprocess.Popen([str(FEED), '--share', str(self.share), '--fps', '60',
                                      '--rgba', str(fixture)],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not wait_for(lambda: self.share.exists() and self.share.stat().st_size >= SHARE_SIZE, 10):
            raise RuntimeError('p2d-feed made no share')
        with open(self.share, 'rb') as f:
            self.m = mmap.mmap(f.fileno(), SHARE_SIZE, access=mmap.ACCESS_READ)
        self.serve('0')

    def serve(self, port):
        """p2d on `port` ('0': the kernel's pick), and its origin"""
        with open(self.log, 'wb') as log:
            self.p2d = subprocess.Popen([str(P2D), '--root', str(self.root), '--share', str(self.share),
                                         '--addr', '127.0.0.1', '--port', port, '--lang', LANG,
                                         '--theme', str(Path(self.root).parent / 'theme.css')],
                                        stdout=log, stderr=log)
        got = wait_for(lambda: re.search(r'p2d: http on port (\d+)', self.log.read_text(errors='replace')), 10)
        if not got:
            raise RuntimeError(f'p2d never said its port: {self.log.read_text(errors="replace")!r}')
        self.port = got.group(1)
        self.origin = f'http://127.0.0.1:{self.port}'

    def word(self, at):
        return struct.unpack_from('<I', self.m, at)[0]

    pad = property(lambda self: self.word(PAD_AT))
    guests = property(lambda self: self.word(GUESTS_AT))
    frame = property(lambda self: self.word(FRAME_AT))

    def close(self):
        for p in (self.p2d, self.feed):
            if p and p.poll() is None:
                p.kill()
            if p:
                p.wait()
        if self.m:
            self.m.close()


# ---- the browser ------------------------------------------------------------

class Browser:
    """Headless Chrome, its tabs opened over its DevTools HTTP endpoint"""

    def __init__(self, chrome, profile):
        self.p = subprocess.Popen(
            [chrome, '--headless=new', '--remote-debugging-port=0', f'--user-data-dir={profile}',
             '--no-first-run', '--no-default-browser-check', '--hide-scrollbars',
             '--disable-background-networking', '--disable-component-update', '--disable-sync',
             '--no-pings', 'about:blank'],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        port = profile / 'DevToolsActivePort'
        if not wait_for(lambda: port.is_file() and port.read_text().strip(), 30, 0.1):
            self.close()
            raise RuntimeError('Chrome never opened its DevTools port')
        self.port = port.read_text().split()[0]

    def tab(self, width=390, height=844, scale=2, phone=True):
        """A new tab, a phone held upright unless said otherwise, with touch"""
        req = urllib.request.Request(f'http://127.0.0.1:{self.port}/json/new?about:blank', method='PUT')
        return Tab(self, json.load(urllib.request.urlopen(req, timeout=30)), width, height, scale, phone)

    def close(self):
        if self.p.poll() is None:
            self.p.terminate()
            try:
                self.p.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.p.kill()
                self.p.wait()


class Tab:
    def __init__(self, browser, target, width, height, scale, phone):
        self.browser, self.id = browser, target['id']
        self.dt = rs.DevTools(target['webSocketDebuggerUrl'])
        self.dt.call('Page.enable')
        self.dt.call('Emulation.setDeviceMetricsOverride', width=width, height=height,
                     deviceScaleFactor=scale, mobile=phone)
        self.dt.call('Emulation.setTouchEmulationEnabled', enabled=True, maxTouchPoints=5)
        # Keys go to a page that has the focus, which a tab in a headless
        # browser of several is not sure to have
        self.dt.call('Emulation.setFocusEmulationEnabled', enabled=True)

    def scheme(self, which):
        """The browser's prefers-color-scheme, with no motion, so a color read
        is never one in the middle of a transition"""
        self.dt.call('Emulation.setEmulatedMedia', features=[{'name': 'prefers-color-scheme', 'value': which},
                                                            {'name': 'prefers-reduced-motion', 'value': 'reduce'}])

    def js(self, expr):
        r = self.dt.call('Runtime.evaluate', expression=expr, returnByValue=True, awaitPromise=True)
        if 'exceptionDetails' in r:
            raise RuntimeError(f'{expr[:60]}...: {r["exceptionDetails"]}')
        return r.get('result', {}).get('value')

    def until(self, expr, seconds=15):
        return wait_for(lambda: self.js(expr), seconds, 0.1)

    def open(self, url, before=None):
        """The page at url, `before` run ahead of every script of it"""
        if before:
            self.dt.call('Page.addScriptToEvaluateOnNewDocument', source=before)
        self.dt.call('Page.navigate', url=url)
        return self.until("document.readyState === 'complete' && typeof window.p2 === 'object'", 20)

    def state(self):
        return self.js("(document.getElementById('state') || {}).textContent || ''")

    def center(self, selector):
        return self.js(f"""(() => {{ const e = document.querySelector({json.dumps(selector)});
            if (!e) return null; const r = e.getBoundingClientRect();
            return [r.left + r.width / 2, r.top + r.height / 2, r.width, r.height]; }})()""")

    def key(self, name, down):
        k, code, vk, _ = KEYS.get(name) or MORE_KEYS[name]
        self.dt.call('Input.dispatchKeyEvent', type='keyDown' if down else 'keyUp', key=k, code=code,
                     windowsVirtualKeyCode=vk, nativeVirtualKeyCode=vk)

    def touch(self, kind, points):
        """touchStart, touchMove or touchEnd with every finger down after it:
        {id: (x, y)}"""
        self.dt.call('Input.dispatchTouchEvent', type=kind,
                     touchPoints=[{'x': x, 'y': y, 'id': i} for i, (x, y) in points.items()])

    def drawn(self):
        """(the frame number the page drew, the canvas's size, its RGBA), read
        in one go, so no frame lands between the number and the pixels"""
        got = self.js("""(() => { const c = document.getElementById('screen');
            const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
            let s = '';
            for (let i = 0; i < d.length; i += 0x8000) s += String.fromCharCode.apply(null, d.subarray(i, i + 0x8000));
            return { frame: window.p2.frame, w: c.width, h: c.height, px: btoa(s) }; })()""")
        return got['frame'], (got['w'], got['h']), base64.b64decode(got['px'])

    def close(self):
        try:
            urllib.request.urlopen(f'http://127.0.0.1:{self.browser.port}/json/close/{self.id}', timeout=10)
        except OSError:
            pass


# ---- the tests --------------------------------------------------------------

def matches(fixture, frame, size, px):
    """Whether px is the fixture's frame for share frame `frame`; and why not"""
    if frame < 1:
        return False, 'no frame drawn'
    if size != (W, H):
        return False, f'the canvas is {size[0]}x{size[1]}'
    at = ((frame - 1) % FRAMES) * FRAME_BYTES
    want = fixture[at:at + FRAME_BYTES]
    if px == want:
        return True, ''
    bad = sum(1 for i in range(0, FRAME_BYTES, 4) if px[i:i + 4] != want[i:i + 4])
    return False, f'{bad} of {W * H} pixels differ from fixture frame {(frame - 1) % FRAMES}'


PARTS = ('#dpad', '#b', '#a', '#select', '#start', '.state', '#keys', '.switch')


def apart(tab):
    """The pad's parts on the screen and clear of each other: the d-pad, B,
    A, SELECT and START, the status, the keys and Picture off, and the
    picture where it shows; [] or what is wrong"""
    return tab.js("""(() => {
        const parts = [...%s, '#screen'].map((s) => [s, document.querySelector(s).getBoundingClientRect()])
            .filter(([, r]) => r.width > 0 && r.height > 0);
        const bad = [];
        for (const [s, r] of parts)
            if (r.left < 0 || r.top < 0 || r.right > innerWidth || r.bottom > innerHeight) bad.push(s + ' is off the screen');
        for (let i = 0; i < parts.length; i++)
            for (let j = i + 1; j < parts.length; j++) {
                const [s, r] = parts[i], [z, q] = parts[j];
                if (r.left < q.right && q.left < r.right && r.top < q.bottom && q.top < r.bottom) bad.push(s + ' meets ' + z);
            }
        return bad; })()""" % json.dumps(list(PARTS)))


def player(say, n):
    return say['play.state.player'].replace('%d', str(n))


def draws_the_frames(tab, rig, fixture, say):
    check(tab.until('window.p2.frame > 0', 20), 'the first frame was drawn')
    first = tab.js('window.p2.frame')
    check(tab.state() == player(say, 2), f"the page says it is player 2: {tab.state()!r}")
    check(rig.guests == 1, 'p2d has the page as player 2')
    time.sleep(1)
    ok, why = matches(fixture, *tab.drawn())
    check(ok, f'a second in, the canvas is the frame it says it drew{": " + why if why else ""}')
    # 150 frames on: past the codec's KEY every 120 and the fixture's 100,
    # so the deltas chained right across both
    check(tab.until(f'window.p2.frame > {first + 150}', 30), f'frames kept coming, 150 past {first}')
    ok, why = matches(fixture, *tab.drawn())
    check(ok, f'past a periodic KEY and the fixture going round, still the right frame{": " + why if why else ""}')


def keyboard_to_pad(tab, rig, fixture, say):
    tab.dt.call('Page.bringToFront')

    def let_go():
        for name in list(KEYS) + ['Meta']:
            tab.key(name, False)
        wait_for(lambda: rig.pad == 0, PAD_WAIT)

    def each():
        out = []
        for name, (_, _, _, bit) in KEYS.items():
            tab.key(name, True)
            got = wait_for(lambda: rig.pad == bit, PAD_WAIT)
            tab.key(name, False)
            gone = wait_for(lambda: rig.pad == 0, PAD_WAIT)
            out.append((got and gone, f'{name} is {bit:#06x} held and 0 let go (the share has {rig.pad:#06x})'))
        return out

    def two():
        tab.key('x', True)
        tab.key('ArrowRight', True)
        out = [(wait_for(lambda: rig.pad == A | RIGHT, PAD_WAIT), f'X and Right are 0x0180: {rig.pad:#06x}')]
        # p2d lets a pad go after 1 s with no 'P'; the page repeats a held one
        time.sleep(1.6)
        out.append((rig.pad == A | RIGHT, f'still 0x0180 after 1.6 s held: {rig.pad:#06x}'))
        tab.key('x', False)
        tab.key('ArrowRight', False)
        out.append((wait_for(lambda: rig.pad == 0, PAD_WAIT), f'let go, 0: {rig.pad:#06x}'))
        return out

    def cmd():
        # A Mac sends no keyup for a key let go while Cmd is down
        tab.key('x', True)
        out = [(wait_for(lambda: rig.pad == A, PAD_WAIT), f'X held: {rig.pad:#06x}')]
        tab.key('Meta', True)
        out.append((wait_for(lambda: rig.pad == 0, PAD_WAIT), f'Cmd down lets it go: {rig.pad:#06x}'))
        tab.key('Meta', False)
        tab.key('x', False)
        return out

    for block in (each, two, cmd):
        on_one_link(tab, block, rig, let_go)
    bad = tab.js('window.__pads.filter((m) => (m & ~0x01fd) !== 0)')
    check(not bad, f"every mask the page sent is in the NES's 0x01FD: {bad}")


def touch_to_pad(tab, rig, fixture, say):
    a, b = tab.center('#a'), tab.center('#b')
    left, dpad = tab.center('[data-dir="left"]'), tab.center('#dpad')
    if not check(a and b and left and dpad, 'the pad has A, B, the d-pad and its Left'):
        return
    check(min(a[2], a[3], b[2], b[3]) >= 48, f'A and B are 48 px or more: {a[2:]}, {b[2:]}')
    bad = apart(tab)
    check(not bad, f'the picture and the pad are on the screen and clear of each other{": " + str(bad) if bad else ""}')

    def lift():
        lifted(tab)
        wait_for(lambda: rig.pad == 0, PAD_WAIT)

    def two_fingers():
        tab.touch('touchStart', {0: a[:2]})
        out = [(wait_for(lambda: rig.pad == A, PAD_WAIT), f'a finger on A is bit 8: {rig.pad:#06x}')]
        tab.touch('touchStart', {0: a[:2], 1: left[:2]})
        out.append((wait_for(lambda: rig.pad == A | LEFT, PAD_WAIT), f'and one on Left, 0x0140: {rig.pad:#06x}'))
        tab.touch('touchMove', {0: b[:2], 1: left[:2]})
        out.append((wait_for(lambda: rig.pad == B | LEFT, PAD_WAIT), f'the first rolled onto B, B and Left: {rig.pad:#06x}'))
        tab.touch('touchEnd', {})
        out.append((wait_for(lambda: rig.pad == 0, PAD_WAIT), f'both lifted, 0: {rig.pad:#06x}'))
        return out

    def onto_start():
        # A thumb rolls within its group: from A onto START is A let go
        start = tab.center('#start')
        tab.touch('touchStart', {0: a[:2]})
        out = [(wait_for(lambda: rig.pad == A, PAD_WAIT), f'A again: {rig.pad:#06x}')]
        tab.touch('touchMove', {0: start[:2]})
        time.sleep(0.5)
        out.append((wait_for(lambda: rig.pad == 0, PAD_WAIT), f'slid from A onto START, nothing held: {rig.pad:#06x}'))
        tab.touch('touchEnd', {})
        return out

    def corner():
        # The d-pad's corner, up and to the left: a diagonal
        at = (dpad[0] - dpad[2] * 0.36, dpad[1] - dpad[3] * 0.36)
        tab.touch('touchStart', {0: at})
        out = [(wait_for(lambda: rig.pad == UP | LEFT, PAD_WAIT), f"the d-pad's corner is up and left: {rig.pad:#06x}")]
        tab.touch('touchEnd', {})
        out.append((wait_for(lambda: rig.pad == 0, PAD_WAIT), f'lifted, 0: {rig.pad:#06x}'))
        return out

    for block in (two_fingers, onto_start, corner):
        on_one_link(tab, block, rig, lift)


def haptics(tab, rig, fixture, say):
    """A tick under the finger as a button goes down: one a press, none for
    a lift, a key or a held finger"""
    buzzed = lambda n: tab.js('window.__buzz')[n:]
    a, b = tab.center('#a'), tab.center('#b')
    dpad = tab.center('#dpad')

    def lift():
        lifted(tab)
        tab.key('x', False)
        wait_for(lambda: rig.pad == 0, PAD_WAIT)

    def block():
        n = tab.js('window.__buzz.length')
        out = []
        tab.touch('touchStart', {0: a[:2]})
        wait_for(lambda: rig.pad == A, PAD_WAIT)
        out.append((buzzed(n) == [15], f'a finger on A ticks once, 15 ms: {buzzed(n)}'))
        time.sleep(0.4)
        out.append((buzzed(n) == [15], f'held, no more: {buzzed(n)}'))
        tab.touch('touchMove', {0: b[:2]})
        wait_for(lambda: rig.pad == B, PAD_WAIT)
        out.append((buzzed(n) == [15, 15], f'rolled onto B, B ticks: {buzzed(n)}'))
        tab.touch('touchEnd', {})
        wait_for(lambda: rig.pad == 0, PAD_WAIT)
        out.append((buzzed(n) == [15, 15], f'a lift does not tick: {buzzed(n)}'))
        corner = (dpad[0] - dpad[2] * 0.36, dpad[1] - dpad[3] * 0.36)
        tab.touch('touchStart', {0: corner})
        wait_for(lambda: rig.pad == UP | LEFT, PAD_WAIT)
        tab.touch('touchEnd', {})
        wait_for(lambda: rig.pad == 0, PAD_WAIT)
        out.append((len(buzzed(n)) == 3, f"the d-pad's corner, two arms, one tick: {buzzed(n)}"))
        tab.key('x', True)
        wait_for(lambda: rig.pad == A, PAD_WAIT)
        tab.key('x', False)
        wait_for(lambda: rig.pad == 0, PAD_WAIT)
        out.append((len(buzzed(n)) == 3, f'a key does not tick: {buzzed(n)}'))
        return out

    on_one_link(tab, block, rig, lift)


DRAWN = ("[...document.querySelectorAll('.key')].filter((e) => e.getClientRects().length > 0)"
         ".map((e) => e.id).join(' ')")


def status(tab, text):
    """A status message from p2d, as the page's socket would have it"""
    tab.js(f"window.__ws.dispatchEvent(new MessageEvent('message', {{ data: {json.dumps(text)} }}))")


LINK = '[window.__opens, window.__closes]'
YXLR = 0x0E02                   # Y, X, L and R: no NES has them


# A press's way to the share, on a runner three times slower than a Mac
# with two other checks beside it: a long wait costs nothing when it comes
PAD_WAIT = 10


def lifted(tab):
    """Every finger up, whether or not one was down (DevTools refuses an end
    with no start)"""
    try:
        tab.touch('touchEnd', {})
    except RuntimeError:
        pass


PLAYING = ("window.__ws && window.__ws.readyState === WebSocket.OPEN"
           " && document.getElementById('game').dataset.state === 'playing'")


def on_one_link(tab, block, rig=None, reset=None, tries=3):
    """block()'s checks, [(ok, what)], from a run in which the page kept one
    link to p2d. On a busy machine p2d can let the page go (5 s with nothing
    from it, Chrome starved), and the page comes back: a press read in the
    gap reads nothing, and a status the check put to the page is overwritten
    by p2d's own for the new link. GitHub's macOS runner did both,
    2026-11-13. Such a run is not judged: once the page plays again (and p2d
    has it, with `rig`), reset() lets go of what the block held and the
    block runs again on the new link."""
    for i in range(tries):
        tab.until(PLAYING, 20)
        if rig:
            wait_for(lambda: rig.guests & 1, 20)
        link = tab.js(LINK)
        got = block()
        if tab.js(LINK) == link:
            break
        print(f'  (the page\'s link to p2d came back meanwhile: again, {i + 2} of {tries})', flush=True)
        if reset:
            reset()
    for ok, what in got:
        check(ok, what)


def status_and_buttons(tab, rig, fixture, say):
    def nes():
        out = [(tab.js(DRAWN) == 'b a select start', f"the NES's buttons are drawn: {tab.js(DRAWN)!r}")]
        sent = tab.js('window.__pads.length')
        for k in ('a', 's', 'q', 'w'):
            tab.key(k, True)
            tab.key(k, False)
        time.sleep(0.5)
        held = [m for m in tab.js('window.__pads')[sent:] if m]
        out.append((not held, f'the keys of Y, X, L and R hold nothing: {held}'))
        return out

    def more():
        # A status that names the player and the buttons: these, and only these
        status(tab, 'S{"state":"playing","player":3,"buttons":4095}')
        out = [(tab.state() == player(say, 3) and tab.js('document.title') == player(say, 3),
                f'"playing" as player 3 says so, and in the tab: {tab.state()!r}, {tab.js("document.title")!r}'),
               (tab.js(DRAWN) == 'l y b x a r select start', f'a game with more buttons draws them: {tab.js(DRAWN)!r}')]
        tab.key('a', True)
        out.append((tab.until('window.__pads[window.__pads.length - 1] === 2', 5), 'its Y is the A key, bit 1'))
        tab.key('a', False)
        out.append((tab.until('window.__pads[window.__pads.length - 1] === 0', 5), 'let go, 0'))
        return out

    def nes_again():
        status(tab, 'S{"state":"playing","player":2,"buttons":509}')
        out = [(tab.state() == player(say, 2), f'player 2 again: {tab.state()!r}'),
               (tab.js(DRAWN) == 'b a select start', f'the NES\'s buttons named again draw its pad: {tab.js(DRAWN)!r}')]
        sent = tab.js('window.__pads.length')
        tab.key('a', True)
        time.sleep(0.5)
        tab.key('a', False)
        held = [m for m in tab.js('window.__pads')[sent:] if m]
        out.append((not held, f'and Y\'s key holds nothing again: {held}'))
        return out

    on_one_link(tab, nes, rig)
    on_one_link(tab, more, rig)
    on_one_link(tab, nes_again, rig)
    bad = tab.js('window.__pads.filter((m) => (m & ~4095) !== 0)')
    check(not bad, f'no mask sent with a bit outside the buttons named: {bad}')


LAYOUT = """(() => {
    const r = (s) => { const e = document.querySelector(s); const b = e.getBoundingClientRect();
        return { top: b.top, bottom: b.bottom, w: b.width, h: b.height, shown: e.getClientRects().length > 0 }; };
    const g = getComputedStyle(document.getElementById('game'));
    return { pic: r('#screen'), view: r('#view'), bar: r('.bar'), keys: r('#keys'), dpr: devicePixelRatio,
             inner: innerHeight - parseFloat(g.paddingTop) - parseFloat(g.paddingBottom) }; })()"""


def scale_rule(room_w, room_h, dpr):
    """The picture's scale: whole device pixels, unless those leave it under
    85% of the exact fit"""
    exact = min(room_w / W, room_h / H)
    whole = int(exact * dpr) / dpr
    return whole if whole >= 0.85 * exact else exact


def settled(tab, metrics, touch):
    """The tab at another size, waited for until the picture has its new size"""
    before = tab.js(LAYOUT)['pic']
    tab.dt.call('Emulation.setDeviceMetricsOverride', **metrics)
    tab.dt.call('Emulation.setTouchEmulationEnabled', enabled=touch, maxTouchPoints=5)
    tab.until(f"document.getElementById('screen').getBoundingClientRect().width !== {before['w']}", 5)
    time.sleep(0.3)
    return tab.js(LAYOUT)


def picture_size(tab, rig, fixture, say):
    upright = tab.js(LAYOUT)
    pic, view = upright['pic'], upright['view']
    check(pic['top'] - upright['bar']['bottom'] <= 24,
          f"upright, the picture is right under the bar: {pic['top'] - upright['bar']['bottom']:.0f} px between")
    check(pic['w'] >= 0.85 * view['w'] - 1, f"and as wide as the screen takes it: {pic['w']:.0f} of {view['w']:.0f}")
    bad = apart(tab)
    check(not bad, f'the picture and the pad clear of each other{": " + str(bad) if bad else ""}')
    # On its side and on a laptop, each with the device pixels an NES pixel
    # is: whole on the common ones. In an order where each picture's width
    # differs from the one before, which settled() waits for.
    for name, (w, h, dpr), touch, keys, px in (
            ('a phone on its side', (844, 390, 2), True, False, 3),
            ('a laptop', (1280, 800, 1), False, True, 3),
            ('a 667x375 phone on its side', (667, 375, 2), True, False, 2),
            ('a 731x411 phone on its side', (731, 411, 2.625), True, False, 3),
            ('an 844x390 phone at 3x on its side', (844, 390, 3), True, False, 4),
            ('a laptop at 2x', (1280, 800, 2), False, True, 6)):
        got = settled(tab, dict(width=w, height=h, deviceScaleFactor=dpr, mobile=touch), touch)
        pic, view = got['pic'], got['view']
        want = scale_rule(view['w'], view['h'], got['dpr'])
        check(view['h'] >= got['inner'] - 1, f"{name}: the picture's room is the whole height: {view['h']:.0f} of {got['inner']:.0f}")
        check(abs(pic['w'] - W * want) <= 1 and abs(pic['h'] - H * want) <= 1,
              f"{name}: the picture {pic['w']:.1f}x{pic['h']:.1f}, the rule's {W * want:.1f}x{H * want:.1f} "
              f"in {view['w']:.0f}x{view['h']:.0f} at {got['dpr']}x")
        each = pic['w'] * got['dpr'] / W
        check(abs(each - px) < 0.01 and abs(pic['h'] * got['dpr'] / H - px) < 0.01,
              f"{name}: an NES pixel is {px} device pixels each way, whole: {each:.2f} "
              f"({pic['w']:.1f}x{pic['h']:.1f} at {got['dpr']}x)")
        check(got['keys']['shown'] == keys, f"{name}: the keys {'shown' if keys else 'not shown'}")
        bad = apart(tab)
        check(not bad, f'{name}: the picture and the pad clear of each other{": " + str(bad) if bad else ""}')
    settled(tab, dict(width=390, height=844, deviceScaleFactor=2, mobile=True), True)


def picture_off(tab, rig, fixture, say):
    shown = "(() => { const r = document.getElementById('screen').getBoundingClientRect(); " \
            "return r.width > 0 && r.height > 0; })()"
    check(tab.js(shown), 'the picture shows')
    a = tab.center('#a')
    tab.js("document.getElementById('picoff').click()")
    check(tab.until('!' + shown, 5), 'Picture off hides the picture')
    big = tab.center('#a')
    check(a and big and big[2] > a[2], f'and the pad takes the screen: A {a and a[2]} px wide, now {big and big[2]}')
    bad = apart(tab)
    check(not bad, f'its parts on the screen and clear of each other{": " + str(bad) if bad else ""}')
    time.sleep(0.3)                       # a frame decoding as it was switched lands
    frame, _, px = tab.drawn()
    acks = tab.js('window.p2.acks')
    time.sleep(1)
    frame2, _, px2 = tab.drawn()
    check(frame2 == frame and px2 == px, f'the canvas held still for a second: frame {frame}, then {frame2}')
    check(tab.until(f'window.p2.acks >= {acks + 3}', 10) and tab.js('window.p2.frame') == frame,
          f'while the acks went on: {acks}, then {tab.js("window.p2.acks")}')
    check(rig.guests == 1, 'and p2d still has player 2')
    asks, at = tab.js('window.__asks'), rig.frame
    tab.js("document.getElementById('picoff').click()")
    check(tab.until(shown, 5), 'Picture off switched off shows it again')
    check(tab.until(f'window.p2.frame > {frame}', 20), 'and frames are drawn again')
    # Its deltas skipped, it had no frame to add the next to: it asked p2d
    # for a KEY once, and drew from about the frame it asked at, where the
    # codec's own KEY could have been up to 120 frames on
    first = tab.js('window.p2.frame')
    check(tab.js('window.__asks') == asks + 1, f"it asked for a KEY ('K') once: {asks}, then {tab.js('window.__asks')}")
    check(first - at <= 45, f'and drew again from frame {first}, {first - at} past the one it asked at, wanted 45 or fewer')
    ok, why = matches(fixture, *tab.drawn())
    check(ok, f'the right frames{": " + why if why else ""}')


def left_and_back(tab, rig, fixture, say):
    """Left for another page with Back still there: its socket goes as the
    page does, so its place is free at once (p2d's 5 s rule would hold it),
    and Back plays again"""
    check(rig.guests == 1, 'p2d has the page as player 2')
    tab.dt.call('Page.navigate', url='about:blank')
    t0 = time.monotonic()
    check(wait_for(lambda: rig.guests == 0, 3.5), f'left, its place is free well before 5 s: guests {rig.guests}')
    took = time.monotonic() - t0
    check(took < 3.5, f'in {took:.1f} s')
    tab.js('history.back()')
    check(tab.until(f"document.getElementById('state') && "
                    f"document.getElementById('state').textContent === {json.dumps(player(say, 2))}", 20),
          f'back, it is player 2 again: {tab.state()!r}')
    frame = tab.js('window.p2.frame')
    check(tab.until(f'window.p2.frame > {frame}', 20), 'and draws the game')
    check(wait_for(lambda: rig.guests == 1, 5), 'p2d has it as player 2')


def full_then_player(tab, rig, fixture, say, browser, origin):
    """The second tab, which is player 2 at the end"""
    other = browser.tab()
    other.open(origin + '/play/')
    check(other.until(f"document.getElementById('state').textContent === {json.dumps(say['play.state.full'])}", 15),
          f'a second tab is told the game is full: {other.state()!r}')
    frame = tab.js('window.p2.frame')
    check(tab.until(f'window.p2.frame > {frame}', 10) and tab.state() == player(say, 2), 'and the first plays on')
    check(rig.guests == 1, 'p2d has one player 2')
    # A full game is tried again, as a lost link is: the slot it waits for
    # may be its own, held by p2d until it finds the old link gone
    tab.close()
    check(other.until(f"document.getElementById('state').textContent === {json.dumps(player(say, 2))}", 15),
          f'the first tab closed, the second is player 2: {other.state()!r}')
    check(other.until('window.p2.frame > 0', 20), 'and draws the game')
    check(wait_for(lambda: rig.guests == 1, 5), 'p2d has it as player 2')
    return other


# What the page sends, recorded on the way out (the pad's masks), its
# socket, for a status the check puts to it as p2d's, how many links it
# opened and saw closed, and its ticks under the finger (navigator.vibrate)
RECORD = """window.__pads = [];
window.__asks = 0;
window.__buzz = [];
navigator.vibrate = (ms) => { window.__buzz.push(ms); return true; };
window.__opens = 0;
window.__closes = 0;
const RealWebSocket = WebSocket;
window.WebSocket = function (...args) {
    const s = new RealWebSocket(...args);
    window.__opens++;
    s.addEventListener('close', () => { window.__closes++; });
    return s;
};
window.WebSocket.prototype = RealWebSocket.prototype;
for (const k of ['CONNECTING', 'OPEN', 'CLOSING', 'CLOSED']) window.WebSocket[k] = RealWebSocket[k];
const RealSend = WebSocket.prototype.send;
WebSocket.prototype.send = function (data) {
    window.__ws = this;
    if (data instanceof Uint8Array && data[0] === 0x50) window.__pads.push(data[1] | data[2] << 8);
    if (data instanceof Uint8Array && data[0] === 0x4b && data.length === 1) window.__asks++;
    return RealSend.call(this, data);
};
"""

# The handheld's look as the launcher will write it: a dark theme with an
# accent no theme has, and square corners
THEME = """:root {
    color-scheme: dark;
    --ground: #000000;
    --ground-rgb: 0, 0, 0;
    --accent: #ff00ff;
    --accent-rgb: 255, 0, 255;
    --corner: 0;
}
"""
# The writer's own file, made by build-native/webtheme-check --emit
WRITER = object()
LOOK = """(() => { const html = getComputedStyle(document.documentElement);
    return { scheme: html.colorScheme, ground: html.backgroundColor,
             a: getComputedStyle(document.getElementById('a')).backgroundColor,
             corner: getComputedStyle(document.getElementById('screen')).borderTopLeftRadius }; })()"""


def theme_from_handheld(browser, origin, root, say):
    css = root.parent / 'theme.css'            # p2d's --theme
    for case, theme, scheme, want in (
            ('the handheld\'s theme, the browser light', THEME, 'light',
             {'scheme': 'dark', 'ground': 'rgb(0, 0, 0)', 'a': 'rgb(255, 0, 255)', 'corner': '0px'}),
            ('the writer\'s file, the browser light', WRITER, 'light',
             {'scheme': 'dark', 'ground': 'rgb(14, 21, 38)', 'a': 'rgb(255, 0, 255)', 'corner': '0px'}),
            ('no theme, the browser light', None, 'light',
             {'scheme': 'light dark', 'ground': 'rgb(230, 228, 218)', 'a': 'rgb(190, 242, 60)', 'corner': '5px'}),
            ('no theme, the browser dark', None, 'dark',
             {'scheme': 'light dark', 'ground': 'rgb(17, 19, 16)', 'a': 'rgb(190, 242, 60)', 'corner': '5px'})):
        if theme == WRITER:
            # The launcher's own writer (src/webtheme.c): dark, square, a
            # magenta accent, the deep blue tone
            subprocess.run([str(WEBTHEME), '--emit', str(css)], check=True)
        elif theme:
            css.write_text(theme)
        elif css.exists():
            css.unlink()
        t = browser.tab()
        try:
            t.scheme(scheme)
            t.open(origin + '/play/')
            t.key('x', True)              # A held: the accent fill
            t.until("document.getElementById('a').classList.contains('on')", 5)
            got = t.js(LOOK)
            t.key('x', False)
            check(got == want, f'{case}: {got}')
        finally:
            t.close()


# A visualViewport the check moves: headless Chrome never zooms a page, so
# the way back from a zoom (an older Safari's, a rotation's) is tried on one
FAKE_VV = """(() => {
    const vv = new EventTarget();
    vv.scale = 1;
    Object.defineProperty(window, 'visualViewport', { value: vv, configurable: true });
    window.__vv = vv;
    window.__metas = [];
    const set = Element.prototype.setAttribute;
    Element.prototype.setAttribute = function (k, v) {
        if (this.name === 'viewport' && k === 'content') window.__metas.push(v);
        return set.call(this, k, v);
    };
})();
"""
VIEWPORT = 'width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover'
CANCELLED = """((kind, target, init) => { const e = new (kind === 'dblclick' ? MouseEvent : Event)(kind,
    Object.assign({ bubbles: true, cancelable: true }, init || {}));
    document.querySelector(target).dispatchEvent(e); return e.defaultPrevented; })"""


def no_zoom(browser, origin, say):
    """Nothing zooms the pad: a double tap or a pinch in iOS's sign-in sheet
    zoomed it and it stayed so (2026-11-13). What a phone does with them is
    the phone's (the device test); here, every guard the page has"""
    t = browser.tab()
    try:
        t.open(origin + '/play/', before=FAKE_VV)
        meta = t.js("document.querySelector('meta[name=\"viewport\"]').getAttribute('content')")
        check(meta == VIEWPORT, f'the viewport keeps its scale at 1 and its zoom off: {meta!r}')
        actions = t.js("['html', 'body', '.game', '.bar', '#view', '#dpad'].map((s) => "
                       "getComputedStyle(document.querySelector(s)).touchAction)")
        check(set(actions) == {'none'}, f'touch-action none on the page, the bar, the picture and the pad: {actions}')
        for kind in ('gesturestart', 'gesturechange', 'gestureend', 'dblclick'):
            check(t.js(f"{CANCELLED}('{kind}', '.bar')"), f"Safari's {kind} is cancelled")
        # A double tap on the bar: the second tap's end is cancelled, the
        # first not. Judged by the gap the page saw: a pair the machine was
        # too busy to give inside a double tap's 350 ms is tried again
        t.js("window.__ends = []; window.addEventListener('touchend', "
             "(e) => window.__ends.push([e.timeStamp, e.defaultPrevented])); true")
        bar = t.center('.bar')
        pair = None
        for _ in range(3):
            time.sleep(0.5)
            for _ in range(2):
                t.touch('touchStart', {0: bar[:2]})
                t.touch('touchEnd', {})
            (t1, p1), (t2, p2) = t.js('window.__ends')[-2:]
            pair = (round(t2 - t1), p1, p2)
            if t2 - t1 < 300:
                break
        check(pair[0] < 300 and not pair[1] and pair[2],
              f'a double tap: the second is cancelled, not the first (ms apart, first, second): {pair}')
        time.sleep(0.5)
        t.touch('touchStart', {0: bar[:2]})
        t.touch('touchEnd', {})
        check(t.js('window.__ends')[-1][1] is False, 'a tap well after is a tap')
        # Picture off: a tap always switches it, a quick second one and one
        # right after a press of A too
        sw, a = t.center('.switch'), t.center('#a')
        on = lambda: t.js("document.getElementById('picoff').checked")
        was = on()
        t.touch('touchStart', {0: sw[:2]})
        t.touch('touchEnd', {})
        first = t.until(f"document.getElementById('picoff').checked === {json.dumps(not was)}", 3)
        t.touch('touchStart', {0: sw[:2]})
        t.touch('touchEnd', {})
        second = t.until(f"document.getElementById('picoff').checked === {json.dumps(was)}", 3)
        check(first and second, f'two quick taps on Picture off switch it twice: {first}, {second}')
        t.touch('touchStart', {0: a[:2]})
        t.touch('touchEnd', {})
        t.touch('touchStart', {0: sw[:2]})
        t.touch('touchEnd', {})
        check(t.until(f"document.getElementById('picoff').checked === {json.dumps(not was)}", 3),
              f'a tap on Picture off right after a press of A switches it: {on()}')
        t.touch('touchStart', {0: sw[:2]})
        t.touch('touchEnd', {})
        t.until(f"document.getElementById('picoff').checked === {json.dumps(was)}", 3)
        # Two fingers that move off the pad: cancelled; on the pad, the pad's
        t.js("window.__moves = []; window.addEventListener('touchmove', (e) => window.__moves.push(e.defaultPrevented)); true")
        t.touch('touchStart', {0: bar[:2], 1: (bar[0] + 60, bar[1])})
        t.touch('touchMove', {0: (bar[0] - 20, bar[1]), 1: (bar[0] + 90, bar[1])})
        t.touch('touchEnd', {})
        check(t.js('window.__moves')[-1:] == [True], f'two fingers moving off the pad are cancelled: {t.js("window.__moves")}')
        # Zoomed anyway: the viewport's limits put again, and so back to 1
        t.js("window.__vv.scale = 2.5; window.__vv.dispatchEvent(new Event('resize')); true")
        check(t.until(f"window.__metas.length >= 2 && document.querySelector('meta[name=\"viewport\"]')"
                      f".getAttribute('content') === {json.dumps(VIEWPORT)}", 3),
              f'zoomed to 2.5, the limits are put again: {t.js("window.__metas")}')
        n = t.js('window.__metas.length')
        t.js("window.__vv.scale = 1; window.__vv.dispatchEvent(new Event('resize')); true")
        time.sleep(0.3)
        check(t.js('window.__metas.length') == n, 'at 1, nothing is touched')
    finally:
        t.close()


OLD = """delete window.DecompressionStream;
window.__sockets = 0;
const RealWebSocket = window.WebSocket;
window.WebSocket = function (...args) { window.__sockets++; return new RealWebSocket(...args); };
"""
# And older still: what Safari before 13.1 and Chrome before 86 also lack
OLDER = OLD + """delete window.ResizeObserver;
delete Element.prototype.replaceChildren;
"""


def old_browser_message(browser, origin, say):
    for case, script in (('without DecompressionStream', OLD),
                         ('without ResizeObserver and replaceChildren too', OLDER)):
        old = browser.tab()
        try:
            old.open(origin + '/play/', before=script)
            text = json.dumps(say['play.old_browser'])
            check(old.until(f"[...document.querySelectorAll('[data-t=\"play.old_browser\"]')]"
                            f".some((e) => e.textContent === {text} && e.getClientRects().length > 0)", 10),
                  f'a browser {case} says it is too old')
            time.sleep(2.5)               # past a retry, had it tried and failed
            check(old.js('window.__sockets') == 0, f'and opened no socket: {old.js("window.__sockets")}')
        finally:
            old.close()


def stopped_then_back(tab, rig, fixture, say, browser, origin):
    """The handheld's A turns multiplayer off with the game going on: p2d
    goes on SIGTERM with the share still there. The page says so, and still
    does past its tries, which reach nothing; turned on again (a new p2d on
    the same address), the page plays again by itself."""
    stopped = json.dumps(say['play.state.stopped'])
    rig.p2d.send_signal(signal.SIGTERM)
    check(tab.until(f"document.getElementById('state').textContent === {stopped}", 10),
          f'p2d stopped, the game going on: the page says multiplayer is off: {tab.state()!r}')
    try:
        code = rig.p2d.wait(timeout=10)
    except subprocess.TimeoutExpired:
        code = None
    check(code == 0, f'p2d exited 0: {code}')
    time.sleep(3)                         # past a try that reached nothing
    check(tab.state() == say['play.state.stopped'], f'and still says so past a try: {tab.state()!r}')
    rig.serve(rig.port)
    check(rig.origin == origin, f'the new p2d is on the page\'s address: {rig.origin}')
    check(tab.until(f"document.getElementById('state').textContent === {json.dumps(player(say, 2))}", 15),
          f'turned on again, the page is player 2 again by itself: {tab.state()!r}')
    frame = tab.js('window.p2.frame')
    check(tab.until(f'window.p2.frame > {frame}', 20), 'and draws the game')


def ended_message(tab, rig, fixture, say, browser, origin):
    # A tab turned away, which keeps trying while the game goes on
    full = browser.tab()
    try:
        full.open(origin + '/play/')
        check(full.until(f"document.getElementById('state').textContent === {json.dumps(say['play.state.full'])}", 15),
              f'another tab is told the game is full: {full.state()!r}')
        os.unlink(rig.share)
        ended = json.dumps(say['play.state.ended'])
        check(tab.until(f"document.getElementById('state').textContent === {ended}", 10),
              f'the share removed, the page says the game ended: {tab.state()!r}')
        try:
            code = rig.p2d.wait(timeout=10)
        except subprocess.TimeoutExpired:
            code = None
        check(code == 0, f'p2d exited 0: {code}')
        time.sleep(3)                     # past the 2 s a lost link waits to try again
        check(tab.state() == say['play.state.ended'], f'and it still says so: {tab.state()!r}')
        # Its retries reach nothing now: not "full" for ever
        connecting = json.dumps(say['play.state.connecting'])
        check(full.until(f"document.getElementById('state').textContent === {connecting}", 10),
              f'the tab that was told the game is full says it is connecting once p2d has gone: {full.state()!r}')
    finally:
        full.close()


# ---- the run -----------------------------------------------------------------

def page_root(root):
    """The page and its words as p2d serves them: DIR/play and DIR/i18n"""
    play = ROOT / 'res/web/play'
    if play.is_dir():
        shutil.copytree(play, root / 'play')
    else:
        (root / 'play').mkdir(parents=True)
    shutil.copytree(ROOT / 'res/web/i18n', root / 'i18n')


def run(name, fn, *args):
    print(name + ':', flush=True)
    before = failures
    try:
        return fn(*args)
    except Exception as e:                # noqa: BLE001 - any crash is a failure to report
        check(False, f'{name} crashed: {type(e).__name__}: {e}')
        return None
    finally:
        if failures > before:
            explain(args)


def explain(args):
    """What a failing test leaves for whoever reads CI's log: the page's links
    to p2d, and p2d's own lines (who joined and left)"""
    for a in args:
        if isinstance(a, Tab):
            try:
                print(f'  (the page opened {a.js("window.__opens")} links to p2d and saw '
                      f'{a.js("window.__closes")} closed)', flush=True)
            except Exception:              # noqa: BLE001 - a closed tab says nothing
                pass
        if isinstance(a, Rig):
            try:
                tail = Path(a.log).read_text(errors='replace').splitlines()[-12:]
                print('  (p2d said, last: ' + ' | '.join(tail) + ')', flush=True)
            except OSError:
                pass


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--chrome', default=rs.CHROME)
    args = ap.parse_args()
    if not Path(args.chrome).is_file():
        print(f'play: FAIL: no browser at {args.chrome} (--chrome PATH)')
        return 1
    for p in (P2D, FEED, CODEC, WEBTHEME):
        if not p.is_file():
            print(f'play: FAIL: {p.relative_to(ROOT)} is missing (make {p.relative_to(ROOT)})')
            return 1
    say = words(LANG)
    tmp = Path(tempfile.mkdtemp(prefix='zyperos-play-check-'))
    rig = browser = None
    try:
        # The fixtures into a folder of this check's own: check-p2codec may
        # be writing build-native's at this moment
        (tmp / 'fixture').mkdir()
        subprocess.run([str(CODEC), str(tmp / 'fixture')], check=True, stdout=subprocess.DEVNULL)
        fixture_file = tmp / 'fixture/p2codec-fixture.rgba'
        fixture = fixture_file.read_bytes()
        if len(fixture) != FRAMES * FRAME_BYTES:
            print(f'play: FAIL: the fixture is {len(fixture)} bytes')
            return 1
        page_root(tmp / 'web')
        rig = Rig(tmp, tmp / 'web', fixture_file)
        browser = Browser(args.chrome, tmp / 'chrome')
        tab = browser.tab()
        print('the page:', flush=True)
        if check(tab.open(rig.origin + '/play/', before=RECORD), 'the page came up') and \
           check(tab.js('document.documentElement.lang') == LANG, "in the handheld's language"):
            run('draws_the_frames', draws_the_frames, tab, rig, fixture, say)
            run('keyboard_to_pad', keyboard_to_pad, tab, rig, fixture, say)
            run('touch_to_pad', touch_to_pad, tab, rig, fixture, say)
            run('haptics', haptics, tab, rig, fixture, say)
            run('status_and_buttons', status_and_buttons, tab, rig, fixture, say)
            run('picture_size', picture_size, tab, rig, fixture, say)
            run('picture_off', picture_off, tab, rig, fixture, say)
            run('left_and_back', left_and_back, tab, rig, fixture, say)
            run('theme_from_handheld', theme_from_handheld, browser, rig.origin, tmp / 'web', say)
            run('old_browser_message', old_browser_message, browser, rig.origin, say)
            run('no_zoom', no_zoom, browser, rig.origin, say)
            # From here the second tab is player 2: the first is closed
            tab = run('full_then_player', full_then_player, tab, rig, fixture, say, browser, rig.origin) or tab
            run('stopped_then_back', stopped_then_back, tab, rig, fixture, say, browser, rig.origin)
            run('ended_message', ended_message, tab, rig, fixture, say, browser, rig.origin)
    finally:
        if browser:
            browser.close()
        if rig:
            rig.close()
        # Chrome's helpers can write into its profile a moment after it
        # exits, so the removal is tried until the folder is gone
        for _ in range(50):
            shutil.rmtree(tmp, ignore_errors=True)
            if not tmp.exists():
                break
            time.sleep(0.1)
    if failures:
        print(f'play: {failures} check(s) failed')
        return 1
    print('play: ok')
    return 0


if __name__ == '__main__':
    signal.signal(signal.SIGTERM, lambda *_: sys.exit(1))
    sys.exit(main())
