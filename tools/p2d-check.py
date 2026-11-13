#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""p2d, NES player 2's server, on the host: make check-p2d.

    make build-native/p2d build-native/p2d-feed
    python3 tools/p2d-check.py

build-native/p2d-feed writes frames into a share file 60 times a second, as
Diatom does in a game, and build-native/p2d serves them on 127.0.0.1, on
ports the kernel picks (--port 0, and p2d says which in its log). The root
is the check's own, a stub play page and two i18n files, so nothing here
waits on the real page, and the WebSocket client is the check's own too, a
few lines over a socket that mask what they send as a browser does. Each
test starts its own feed and p2d and kills them when it ends. What it holds:

  - the page: /play/ is the page, / and a phone's internet check are sent
    there, /i18n.js is the handheld's language or English
  - the handshake: anything on /p2ws but a WebSocket request is refused,
    and so is one from any Origin but the page's own, or with none
  - frames: the first a KEY with its palette, 30 a second and more while
    each is acked, and never more than 2 waiting for an ack
  - the pad: what the guest holds reaches the share, the NES's buttons only,
    and is let go after a second of silence and when the guest leaves; the
    beat Diatom watches moves with a guest and without
  - a second browser is told player 2 is taken, and the first plays on
  - a guest that says nothing for 5 s with a frame unacked is gone (a phone
    out of range sends no FIN), and one that acks every frame stays
  - a ping is answered, and a message the page never sends closes the guest
  - the end: the share removed, its magic cleared, or a SIGTERM, and the
    guest hears the game ended and p2d exits 0 within a second; no share at
    all, and p2d gives up after 5 s with 1
  - DNS: with --dns, every name is p2d's address
  - p2d-feed --rgba: a fixture's frames, in order and round again, are the
    share's, and SIGTERM clears the magic and removes the file
"""
import base64
import contextlib
import hashlib
import http.client
import itertools
import mmap
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
P2D = ROOT / 'build-native' / 'p2d'
FEED = ROOT / 'build-native' / 'p2d-feed'
GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
EOF = 'eof'

# src/p2share.h
MAGIC = 0x50325348
HEAD = 4096
SLOT_BYTES = 512 * 480 * 4
SIZE = HEAD + 3 * SLOT_BYTES
PAD_MASK = 0x01FD


class Fail(Exception):
    pass


def check(cond, what):
    if not cond:
        raise Fail(what)


def wait_for(pred, timeout, step=0.01):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


# ---- the share, as p2d and Diatom see it ---------------------------------

class Share:
    def __init__(self, path, write=False):
        with open(path, 'r+b' if write else 'rb') as f:
            self.m = mmap.mmap(f.fileno(), SIZE,
                               access=mmap.ACCESS_WRITE if write else mmap.ACCESS_READ)

    def word(self, off):
        return struct.unpack_from('<I', self.m, off)[0]

    magic = property(lambda s: s.word(0))
    frame = property(lambda s: s.word(12))
    pad = property(lambda s: s.word(16))
    beat = property(lambda s: s.word(20))
    guests = property(lambda s: s.word(24))

    def slot(self, s):
        seq, w, h, frame = struct.unpack_from('<4I', self.m, 28 + 16 * s)
        return seq, w, h, frame

    def pixel(self, s, x, y, w):
        at = HEAD + s * SLOT_BYTES + (y * w + x) * 4
        return self.m[at:at + 4]

    def close(self):
        self.m.close()


# ---- the processes -------------------------------------------------------

class Rig:
    """a feed writing the share, and p2d serving it"""
    count = itertools.count(1)        # next() is one step for the slow tests' threads

    def __init__(self, tmp, args=(), feed=True, share=None):
        n = next(Rig.count)
        self.tmp = tmp
        self.share = share or tmp / f'share-{n}'
        self.log = tmp / f'p2d-{n}.log'
        self.feed = None
        self.p2d = None
        self.args = list(args)
        self.want_feed = feed
        self.port = self.dns_port = None

    def __enter__(self):
        try:
            if self.want_feed:
                self.feed = subprocess.Popen([str(FEED), '--share', str(self.share), '--fps', '60'],
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            with open(self.log, 'wb') as log:
                self.started = time.monotonic()
                self.p2d = subprocess.Popen([str(P2D), '--root', str(self.tmp / 'web'),
                                             '--share', str(self.share), '--addr', '127.0.0.1',
                                             '--port', '0'] + self.args,
                                            stdout=log, stderr=log)
            if self.want_feed:
                self.port = int(self.logged(r'p2d: http on port (\d+)', 5))
                if '--dns' in self.args:
                    self.dns_port = int(self.logged(r'p2d: dns on port (\d+)', 3))
        except BaseException:
            self.__exit__()
            raise
        return self

    def __exit__(self, *exc):
        for p in (self.p2d, self.feed):
            if p and p.poll() is None:
                p.kill()
            if p:
                p.wait()
        return False

    def logged(self, pattern, timeout):
        def find():
            m = re.search(pattern, self.log.read_text(errors='replace'))
            return m.group(1) if m else None
        got = wait_for(find, timeout)
        check(got, f'p2d never logged /{pattern}/; its log: {self.log.read_text(errors="replace")!r}')
        return got

    def get(self, path, host=None, headers=None):
        c = http.client.HTTPConnection('127.0.0.1', self.port, timeout=3)
        h = dict(headers or {})
        if host:
            h['Host'] = host
        c.request('GET', path, headers=h)
        r = c.getresponse()
        body = r.read()
        c.close()
        return r.status, {k.lower(): v for k, v in r.getheaders()}, body

    def mapped(self, write=False):
        return Share(self.share, write)


# ---- a WebSocket client, as small as one can be --------------------------

class WS:
    def __init__(self, port, upgrade='websocket', origin=''):
        """origin '' is the page's own, http://127.0.0.1:PORT; None sends none"""
        self.s = socket.create_connection(('127.0.0.1', port), timeout=3)
        key = base64.b64encode(os.urandom(16)).decode()
        if origin == '':
            origin = f'http://127.0.0.1:{port}'
        self.s.sendall((f'GET /p2ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\n'
                        + (f'Origin: {origin}\r\n' if origin is not None else '') +
                        f'Upgrade: {upgrade}\r\nConnection: Upgrade\r\n'
                        f'Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        self.buf = b''
        while b'\r\n\r\n' not in self.buf:
            got = self.s.recv(4096)
            check(got, 'the handshake was cut off')
            self.buf += got
        head, self.buf = self.buf.split(b'\r\n\r\n', 1)
        check(head.startswith(b'HTTP/1.1 101 '), f'the handshake was answered {head[:60]!r}')
        want = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest())
        check(b'Sec-WebSocket-Accept: ' + want in head, f'a wrong Sec-WebSocket-Accept: {head!r}')

    def send(self, op, data):
        mask = os.urandom(4)
        n = len(data)
        head = bytes([0x80 | op]) + (bytes([0x80 | n]) if n < 126 else
                                     bytes([0x80 | 126]) + struct.pack('>H', n))
        self.s.sendall(head + mask + bytes(b ^ mask[i & 3] for i, b in enumerate(data)))

    def ack(self, frame):
        self.send(2, b'A' + struct.pack('<I', frame))

    def pad(self, mask):
        self.send(2, b'P' + struct.pack('<H', mask))

    def recv(self, timeout):
        """(opcode, payload); None when nothing whole came in time; (EOF, b'') once closed"""
        end = time.monotonic() + timeout
        while True:
            m = self._take()
            if m:
                return m
            left = end - time.monotonic()
            if left <= 0:
                return None
            self.s.settimeout(left)
            try:
                got = self.s.recv(65536)
            except socket.timeout:
                return None
            except (ConnectionResetError, BrokenPipeError):
                return (EOF, b'')
            if not got:
                return (EOF, b'')
            self.buf += got

    def _take(self):
        b = self.buf
        if len(b) < 2:
            return None
        check(not b[1] & 0x80, 'the server masked a frame')
        op, n, at = b[0] & 15, b[1] & 127, 2
        if n == 126:
            if len(b) < 4:
                return None
            n, at = struct.unpack_from('>H', b, 2)[0], 4
        elif n == 127:
            if len(b) < 10:
                return None
            n, at = struct.unpack_from('>Q', b, 2)[0], 10
        if len(b) < at + n:
            return None
        self.buf = b[at + n:]
        return op, b[at:at + n]

    def until(self, want, timeout):
        """the next message of opcode `want`, others skipped; None on timeout or EOF"""
        end = time.monotonic() + timeout
        while True:
            m = self.recv(max(0, end - time.monotonic()))
            if m is None or m[0] == EOF:
                return None
            if m[0] == want:
                return m[1]

    def status(self, timeout=2.0):
        t = self.until(1, timeout)
        return t.decode() if t is not None else None

    def frame(self, timeout=2.0):
        """the next frame: (flags, w, h, number)"""
        f = self.until(2, timeout)
        if f is None:
            return None
        check(len(f) >= 10 and f[0] == ord('F'), f'not a frame: {f[:10]!r}')
        return f[1], struct.unpack_from('<H', f, 2)[0], struct.unpack_from('<H', f, 4)[0], \
            struct.unpack_from('<I', f, 6)[0]

    def hung_up(self, timeout=2.0):
        """the server closed the socket, whatever came before"""
        end = time.monotonic() + timeout
        while True:
            m = self.recv(max(0, end - time.monotonic()))
            if m is None:
                return False
            if m[0] == EOF:
                return True

    def closed_by_server(self, timeout=2.0):
        """the server's close came, was answered as a browser answers it, and
        the server then hung up"""
        if self.until(8, timeout) is None:
            return False
        self.send(8, struct.pack('>H', 1000))
        return self.hung_up(timeout)

    def close(self):
        self.s.close()


# ---- the tests -----------------------------------------------------------

TESTS = []


def test(fn):
    TESTS.append(fn)
    return fn


@test
def serves_the_page(tmp):
    with Rig(tmp) as r:
        s, h, body = r.get('/play/')
        check(s == 200, f'GET /play/ was {s}')
        check(h.get('content-type', '').startswith('text/html'), f'/play/ is {h.get("content-type")}')
        check(b'stub play page' in body, f'/play/ was not the page: {body[:80]!r}')
        s, h, _ = r.get('/play/play.js')
        check(s == 200 and h.get('content-type', '').startswith('text/javascript'),
              f'/play/play.js was {s} {h.get("content-type")}')
        s, h, _ = r.get('/')
        check(s == 302 and h.get('location') == '/play/', f'GET / was {s} to {h.get("location")}')
        for path in ('/play/../secret.txt', '/play/.hidden', '/play/nothing.js', '/other'):
            s, _, _ = r.get(path)
            check(s == 404, f'GET {path} was {s}, wanted 404')


@test
def i18n_in_the_language(tmp):
    for args, want in (([], b"'en'"), (['--lang', 'fr'], b"'fr'"), (['--lang', 'xx'], b"'en'"),
                       (['--lang', '../../etc'], b"'en'")):
        with Rig(tmp, args) as r:
            s, h, body = r.get('/i18n.js')
            check(s == 200 and h.get('content-type', '').startswith('text/javascript'),
                  f'/i18n.js with {args} was {s} {h.get("content-type")}')
            check(want in body, f'/i18n.js with {args} was {body!r}')


@test
def captive_redirect(tmp):
    with Rig(tmp) as r:
        for path in ('/', '/hotspot-detect.html', '/generate_204'):
            s, h, _ = r.get(path, host='captive.apple.com')
            check(s == 302 and h.get('location') == f'http://127.0.0.1:{r.port}/play/',
                  f'GET {path} for captive.apple.com was {s} to {h.get("location")}')
        for host in ('127.0.0.1', f'127.0.0.1:{r.port}', 'zyperos.local', f'zyperos.local:{r.port}'):
            s, _, _ = r.get('/play/', host=host)
            check(s == 200, f'GET /play/ for {host} was {s}, wanted the page')


@test
def handshake_refused(tmp):
    key = base64.b64encode(os.urandom(16)).decode()
    with Rig(tmp) as r:
        ws = {'Upgrade': 'websocket', 'Connection': 'Upgrade', 'Sec-WebSocket-Key': key,
              'Sec-WebSocket-Version': '13', 'Origin': f'http://127.0.0.1:{r.port}'}
        for drop, swap in (('Upgrade', None), ('Sec-WebSocket-Key', None),
                           (None, ('Sec-WebSocket-Version', '8')), (None, ('Upgrade', 'h2c'))):
            h = {k: v for k, v in ws.items() if k != drop}
            if swap:
                h[swap[0]] = swap[1]
            s, _, _ = r.get('/p2ws', headers=h)
            check(s == 400, f'/p2ws without {drop or swap} was {s}, wanted 400')
        a = WS(r.port, upgrade='WebSocket')
        check(a.status() == 'S{"state":"player2"}', 'Upgrade: WebSocket (capitals) was not taken')
        a.close()


@test
def origin_checked(tmp):
    """on the cable the laptop has the internet: a site in another of its tabs
    must not take player 2 or watch the game"""
    key = base64.b64encode(os.urandom(16)).decode()
    with Rig(tmp) as r:
        sh = r.mapped()
        p = r.port
        ws = {'Upgrade': 'websocket', 'Connection': 'Upgrade', 'Sec-WebSocket-Key': key,
              'Sec-WebSocket-Version': '13'}
        for origin in (None, 'null', 'http://evil.example', f'http://evil.example:{p}',
                       'http://127.0.0.1', f'http://127.0.0.1:{p + 1}', f'https://127.0.0.1:{p}',
                       f'http://127.0.0.1:{p}.evil.example', f'http://127.0.0.10:{p}',
                       f'http://127.0.0.1:{p}/', 'http://zyperos.local',
                       f'http://zyperos.local.evil.example:{p}'):
            h = dict(ws)
            if origin is not None:
                h['Origin'] = origin
            s, _, _ = r.get('/p2ws', headers=h)
            check(s == 403, f'/p2ws with Origin {origin} was {s}, wanted 403')
        check(sh.guests == 0, 'a refused origin became player 2')
        for origin in (f'http://127.0.0.1:{p}', f'http://zyperos.local:{p}', f'HTTP://ZyperOS.Local:{p}'):
            a = WS(p, origin=origin)
            check(a.status() == 'S{"state":"player2"}', f'Origin {origin} was not player 2')
            a.close()
            check(wait_for(lambda: sh.guests == 0, 2), f'the guest from {origin} did not leave')
        sh.close()


@test
def frames_flow_with_acks(tmp):
    with Rig(tmp) as r:
        ws = WS(r.port)
        st = ws.status()
        check(st == 'S{"state":"player2"}', f'the first guest was told {st!r}')
        f = ws.frame()
        check(f is not None, 'no frame came')
        check(f[0] & 3 == 3, f'the first frame has flags {f[0]}, wanted KEY and PAL')
        check(f[1:3] == (256, 240), f'the first frame is {f[1]}x{f[2]}')
        last = f[3]
        ws.ack(last)
        got = 1
        end = time.monotonic() + 1.0
        while time.monotonic() < end:
            f = ws.frame(end - time.monotonic())
            if f is None:
                break
            check(f[3] > last, f'frame {f[3]} came after {last}')
            last = f[3]
            ws.ack(last)
            got += 1
        check(got >= 30, f'{got} frames in a second with every one acked, wanted 30 or more')
        # Everything that came was acked: from here no more than 2 may come
        unacked = 0
        end = time.monotonic() + 0.6
        while ws.frame(max(0, end - time.monotonic())) is not None:
            unacked += 1
        check(unacked <= 2, f'{unacked} frames came without an ack, wanted 2 at most')
        ws.ack(last + 1000)
        check(ws.frame() is not None, 'frames did not start again after an ack')


@test
def pad_reaches_share(tmp):
    with Rig(tmp) as r:
        sh = r.mapped()
        b0 = sh.beat
        time.sleep(0.6)
        check(sh.beat - b0 >= 4, f'pad_beat moved {sh.beat - b0} in 600 ms with no guest')
        check(sh.guests == 0, f'guests is {sh.guests} with nobody there')
        ws = WS(r.port)
        check(ws.status() == 'S{"state":"player2"}', 'not player 2')
        ws.pad(0x0101)
        check(wait_for(lambda: sh.pad == 0x0101, 2), f'pad is {sh.pad:#06x}, wanted 0x0101')
        check(sh.guests == 1, f'guests is {sh.guests} with player 2 there')
        ws.pad(0xFFFF)
        check(wait_for(lambda: sh.pad == PAD_MASK, 2), f'pad is {sh.pad:#06x} for 0xffff, wanted 0x01fd')
        b0 = sh.beat
        time.sleep(0.6)
        check(sh.beat - b0 >= 4, f'pad_beat moved {sh.beat - b0} in 600 ms with a guest')
        ws.close()
        sh.close()


@test
def pad_clears_on_silence(tmp):
    with Rig(tmp) as r:
        sh = r.mapped()
        ws = WS(r.port)
        check(ws.status() == 'S{"state":"player2"}', 'not player 2')
        pressed = time.monotonic()
        ws.pad(0x0100)
        check(wait_for(lambda: sh.pad == 0x0100, 2), f'pad is {sh.pad:#06x}, wanted 0x0100')
        time.sleep(max(0, pressed + 0.6 - time.monotonic()))
        held, at = sh.pad, time.monotonic() - pressed
        # p2d lets go a second after it reads the press, which is after
        # `pressed`: a look a second or more after it (a loaded machine)
        # cannot tell held from let go, so it does not fail
        check(held == 0x0100 or at >= 1.0, f'the pad was let go {at:.2f} s after the press')
        check(wait_for(lambda: sh.pad == 0, 1.6), f'pad is {sh.pad:#06x} after 1.6 s more of silence')
        took = time.monotonic() - pressed
        check(took >= 0.9, f'the pad was let go {took:.2f} s after the press, wanted a second')
        check(sh.guests == 1, 'the guest was dropped for being quiet')

        # Its close: the pad goes with it, and player 2 is free again
        ws.pad(0x0100)
        check(wait_for(lambda: sh.pad == 0x0100, 2), 'the pad did not come back')
        ws.send(8, struct.pack('>H', 1000))
        check(ws.until(8, 2) is not None, 'the close was not answered with a close')
        check(ws.hung_up(), 'the server did not hang up after the close')
        check(wait_for(lambda: sh.pad == 0 and sh.guests == 0, 2),
              f'after a close pad {sh.pad:#06x} guests {sh.guests}, wanted 0 and 0')

        # A hang-up with no close: the same
        b = WS(r.port)
        check(b.status() == 'S{"state":"player2"}', 'the next guest was not player 2')
        f = b.frame()
        check(f and f[0] & 3 == 3, 'the next guest\'s first frame was not KEY and PAL')
        b.pad(0x0040)
        check(wait_for(lambda: sh.pad == 0x0040 and sh.guests == 1, 2), 'the next guest\'s pad did not come')
        b.close()
        check(wait_for(lambda: sh.pad == 0 and sh.guests == 0, 2),
              f'after a hang-up pad {sh.pad:#06x} guests {sh.guests}, wanted 0 and 0')
        sh.close()


@test
def second_guest_is_taken(tmp):
    with Rig(tmp) as r:
        sh = r.mapped()
        a = WS(r.port)
        check(a.status() == 'S{"state":"player2"}', 'the first guest was not player 2')
        b = WS(r.port)
        st = b.status()
        check(st == 'S{"state":"taken"}', f'the second guest was told {st!r}')
        check(b.closed_by_server(), 'the second guest was not closed')
        got, end = 0, time.monotonic() + 1.0
        while got < 10 and time.monotonic() < end:
            f = a.frame(end - time.monotonic())
            if f is None:
                break
            a.ack(f[3])
            got += 1
        check(got >= 10, f'the first guest had {got} frames in a second after the second came')
        check(sh.guests == 1, f'guests is {sh.guests}')
        a.close()
        sh.close()


@test
def ping_and_bad_messages(tmp):
    with Rig(tmp) as r:
        sh = r.mapped()
        ws = WS(r.port)
        check(ws.status() == 'S{"state":"player2"}', 'not player 2')
        ws.send(9, b'hi')
        check(ws.until(10, 2) == b'hi', 'the ping had no pong')
        for bad in (b'Q12', b'A\x01\x02', b'P\x01', b'P\x01\x00\x00'):
            ws.pad(0x0001)
            check(wait_for(lambda: sh.pad == 1, 2), 'the pad did not come')
            ws.send(2, bad)
            check(ws.closed_by_server(), f'{bad!r} did not close the guest')
            check(wait_for(lambda: sh.pad == 0 and sh.guests == 0, 2),
                  f'after {bad!r} pad {sh.pad:#06x} guests {sh.guests}')
            ws = WS(r.port)
            check(ws.status() == 'S{"state":"player2"}', f'no player 2 after {bad!r}')
        ws.send(1, b'P\x01\x00')
        check(ws.closed_by_server(), 'a text message did not close the guest')
        sh.close()


@test
def ended_when_share_goes(tmp):
    for how in ('removed', 'magic cleared', 'SIGTERM'):
        with Rig(tmp) as r:
            sh = r.mapped(write=True)
            ws = WS(r.port)
            check(ws.status() == 'S{"state":"player2"}', 'not player 2')
            check(ws.frame() is not None, 'no frame came')
            ws.pad(0x0100)
            check(wait_for(lambda: sh.pad == 0x0100, 2), 'the pad did not come')
            t0 = time.monotonic()
            if how == 'removed':
                os.unlink(r.share)
            elif how == 'magic cleared':
                struct.pack_into('<I', sh.m, 0, 0)
            else:
                r.p2d.send_signal(signal.SIGTERM)
            st = ws.status(2.0)
            check(st == 'S{"state":"ended"}', f'share {how}: the guest was told {st!r}')
            check(ws.closed_by_server(), f'share {how}: the guest was not closed')
            try:
                rc = r.p2d.wait(timeout=1.5)
            except subprocess.TimeoutExpired:
                rc = None
            took = time.monotonic() - t0
            check(rc == 0, f'share {how}: p2d exited {rc}, wanted 0')
            check(took < 1.0, f'share {how}: p2d took {took:.2f} s to go, wanted under 1')
            check(sh.pad == 0 and sh.guests == 0,
                  f'share {how}: p2d left pad {sh.pad:#06x} guests {sh.guests}')
            sh.close()


@test
def dns_answers(tmp):
    with Rig(tmp, ['--dns', '--dns-port', '0']) as r:
        q = struct.pack('>6H', 0x1234, 0x0100, 1, 0, 0, 0) + b'\x07example\x03com\x00' + \
            struct.pack('>2H', 1, 1)
        u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        u.settimeout(2)
        try:
            u.sendto(q, ('127.0.0.1', r.dns_port))
            a, _ = u.recvfrom(512)
        except socket.timeout:
            a = b''
        finally:
            u.close()
        check(len(a) >= 16 and a[:2] == b'\x12\x34', f'no answer to the query: {a!r}')
        check(struct.unpack_from('>H', a, 6)[0] == 1, 'the answer has no A record')
        check(a[-4:] == bytes([127, 0, 0, 1]), f'example.com is {list(a[-4:])}, wanted 127.0.0.1')


@test
def feed_replays_rgba(tmp):
    w, h, colors = 256, 240, [(200, 10, 30), (20, 180, 40), (5, 60, 250)]
    fixture = tmp / 'fixture.rgba'
    with open(fixture, 'wb') as f:
        for c in colors:
            f.write(bytes(c + (255,)) * (w * h))
    share = tmp / 'share-rgba'
    feed = subprocess.Popen([str(FEED), '--share', str(share), '--fps', '60', '--rgba', str(fixture),
                             '--frames', '5'], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        check(wait_for(lambda: share.exists() and share.stat().st_size >= SIZE, 2), 'no share file')
        sh = Share(share)
        check(wait_for(lambda: sh.magic == MAGIC and sh.frame == 5, 2), f'frame is {sh.frame}, wanted 5')
        time.sleep(0.1)
        check(sh.frame == 5, f'the feed went on past --frames 5 to {sh.frame}')
        for s in range(3):
            seq, sw, shh, n = sh.slot(s)
            check(seq % 2 == 0 and (sw, shh) == (w, h), f'slot {s}: seq {seq}, {sw}x{shh}')
            r, g, b = colors[(n - 1) % 3]
            for x, y in ((0, 0), (255, 239), (100, 50)):
                px = sh.pixel(s, x, y, w)
                check(px[:3] == bytes([b, g, r]), f'share frame {n} at {x},{y} is {list(px)}, '
                      f'wanted fixture frame {(n - 1) % 3} as XRGB8888')
        feed.send_signal(signal.SIGTERM)
        check(feed.wait(timeout=2) == 0, 'the feed did not exit 0 on SIGTERM')
        check(sh.magic == 0, 'the feed left the magic set')
        check(not share.exists(), 'the feed left the share file')
        sh.close()
    finally:
        if feed.poll() is None:
            feed.kill()
        feed.wait()
        if feed.stderr:
            feed.stderr.close()


# The slow ones, 5 s or more each: started first, each in a thread of its own

SLOW = []


def slow(fn):
    SLOW.append(fn)
    return fn


@slow
def quiet_guest_is_dropped(tmp):
    """a phone out of range sends no FIN: 5 s of nothing with a frame unacked"""
    with Rig(tmp) as r:
        sh = r.mapped()
        t0 = time.monotonic()           # before p2d's clock for the guest starts
        ws = WS(r.port)
        check(ws.status() == 'S{"state":"player2"}', 'not player 2')
        check(ws.frame() is not None, 'no frame came')
        gone = wait_for(lambda: sh.guests == 0, 8, step=0.05)
        took = time.monotonic() - t0
        check(gone, 'a guest that said nothing for 8 s is still player 2')
        check(4.9 <= took <= 7.0, f'the quiet guest was dropped after {took:.1f} s, wanted 5')
        check(ws.hung_up(), 'the quiet guest\'s socket was not closed')
        b = WS(r.port)
        check(b.status() == 'S{"state":"player2"}', 'the next guest was not player 2')
        b.close()
        sh.close()


@slow
def acking_guest_stays(tmp):
    with Rig(tmp) as r:
        sh = r.mapped()
        ws = WS(r.port)
        check(ws.status() == 'S{"state":"player2"}', 'not player 2')
        end = time.monotonic() + 6.0
        while time.monotonic() < end:
            f = ws.frame()
            check(f is not None, 'frames stopped for a guest that acks every one')
            ws.ack(f[3])
        check(sh.guests == 1, 'a guest that acks every frame was dropped')
        ws.close()
        sh.close()


@slow
def no_share_gives_up(tmp):
    with Rig(tmp, feed=False, share=tmp / 'never') as r:
        t0 = r.started                  # before p2d's own 5 s began
        try:
            rc = r.p2d.wait(timeout=9)
        except subprocess.TimeoutExpired:
            rc = None
        took = time.monotonic() - t0
        check(rc == 1, f'p2d with no share exited {rc}, wanted 1')
        check(4.9 <= took <= 7.0, f'p2d gave up on the share after {took:.1f} s, wanted 5')
        check('share' in r.log.read_text(errors='replace'), 'p2d said nothing about the share')


# ---- the run -------------------------------------------------------------

def run(fn, tmp, failures):
    try:
        fn(tmp)
    except Fail as e:
        failures.append(f'p2d: FAIL: {fn.__name__}: {e}')
    except Exception as e:                  # noqa: BLE001 - any crash is a failure to report
        failures.append(f'p2d: FAIL: {fn.__name__}: {type(e).__name__}: {e}')


def main():
    for p in (P2D, FEED):
        if not p.exists():
            print(f'p2d: FAIL: {p} is missing (make {p.relative_to(ROOT)})')
            return 1
    tmp = Path(tempfile.mkdtemp(prefix='p2d-check-'))
    failures = []
    try:
        (tmp / 'web' / 'play').mkdir(parents=True)
        (tmp / 'web' / 'i18n').mkdir()
        (tmp / 'web' / 'play' / 'index.html').write_text('<!doctype html><p>stub play page</p>\n')
        (tmp / 'web' / 'play' / 'play.js').write_text('// stub\n')
        (tmp / 'web' / 'play' / '.hidden').write_text('no\n')
        (tmp / 'web' / 'secret.txt').write_text('no\n')
        (tmp / 'web' / 'i18n' / 'en.js').write_text("window.I18N = 'en';\n")
        (tmp / 'web' / 'i18n' / 'fr.js').write_text("window.I18N = 'fr';\n")
        threads = [threading.Thread(target=run, args=(t, tmp, failures)) for t in SLOW]
        for t in threads:
            t.start()
        for t in TESTS:
            run(t, tmp, failures)
        for t in threads:
            t.join()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for f in failures:
        print(f)
    if failures:
        print(f'p2d: {len(failures)} test(s) failed')
        return 1
    print('p2d: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
