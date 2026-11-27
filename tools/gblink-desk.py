#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Link Cable's real joiner (src/link.c, in desk mode) against a host.

    python3 tools/gblink-desk.py pixel [--window]
        a Pixel 2 that hosts on the USB-C cable (MENU > Link Cable > Host,
        tools/gblink-rom.py's test ROM on Gambatte there too): this computer,
        on the cable's 10.42.0.x, joins it as another Pixel 2 would
    python3 tools/gblink-desk.py standin
        a stand-in host on this computer instead (a Diatom of
        tools/gblink-diatom.py's as the server, and src/link.c's host word in
        Python on 127.0.0.1): the same steps end to end, no handheld; exit 0
        when both sides link and both screens say PASS

Either way: desk mode's launcher (build-native/zyperos) on a card of its own
in a temporary folder, with ZYPEROS_DESK_LINK_CABLE=1 (src/link_screen.c),
Diatom's desktop build and Gambatte built for this computer
(tools/gblink-core.sh); the test ROM opened on gambatte; MENU, the Link Cable
row, Join; then "link: linked" waited for in the launcher's log, START on this
side, and the counts read off its screen (and the stand-in's). In SDL's dummy
window unless --window. The shots and the log stay in --keep (default a
folder named in the output).
"""
import argparse, importlib.machinery, importlib.util, os, select, shutil, signal, socket, struct
import subprocess, sys, tempfile, time, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
_ld = importlib.machinery.SourceFileLoader("desk", str(ROOT / "tools/desk"))
desk = importlib.util.module_from_spec(importlib.util.spec_from_loader("desk", _ld))
_ld.exec_module(desk)
_sp = importlib.util.spec_from_file_location("gblink_diatom", ROOT / "tools/gblink-diatom.py")
gd = importlib.util.module_from_spec(_sp)
_sp.loader.exec_module(gd)
gbrom = gd.gbrom

STANDIN_HOST, STANDIN_PORT = "127.0.0.1", 56398
# MENU's Game tab: Continue, Save, Load, Quick Slot, then Link Cable
ROW_DOWNS = 4


def png_pixels(path):
    """An 8-bit RGB or RGBA PNG as (w, h, rows of bytes, bytes a pixel)"""
    b = Path(path).read_bytes()
    assert b[:8] == b"\x89PNG\r\n\x1a\n", path
    i, idat, w = 8, b"", 0
    while i < len(b):
        n, kind = struct.unpack(">I4s", b[i:i + 8])
        data = b[i + 8:i + 8 + n]
        if kind == b"IHDR":
            w, h, depth, color = struct.unpack(">IIBB", data[:10])
            assert depth == 8 and color in (2, 6), (depth, color)
            bpp = 3 if color == 2 else 4
        elif kind == b"IDAT":
            idat += data
        i += 12 + n
    raw, stride, rows, prev = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp)
    for y in range(h):
        f, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            c = prev[x - bpp] if x >= bpp else 0
            up = prev[x]
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + up) & 255
            elif f == 3: line[x] = (line[x] + (a + up) // 2) & 255
            elif f == 4:
                p = a + up - c
                pa, pb, pc = abs(p - a), abs(p - up), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else up if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return w, h, rows, bpp


def desk_screen(path):
    """The test ROM's counts off a desk shot of the panel: the game drawn over
    the whole panel (a fresh card's display mode), each Game Boy pixel read
    at its centre"""
    w, h, rows, bpp = png_pixels(path)

    def lum(x, y):
        X, Y = int((x + 0.5) * w / 160), int((y + 0.5) * h / 144)
        o = X * bpp
        r = rows[Y]
        return r[o] * 0.30 + r[o + 1] * 0.59 + r[o + 2] * 0.11
    got = gbrom.screen_rows(lum, 160, 144)
    if not got or "ZYPEROS LINK TEST" not in got[1]:
        return None, got
    v = {}
    for r in (r.strip() for r in got):
        for label in ("INT TX", "INT OK", "EXT TX", "EXT OK", "BAD", "LAST RX", "LAST TX"):
            if r.startswith(label):
                try: v[label] = int(r[len(label):].strip(), 16)
                except ValueError: v[label] = None
    v["ROLE"] = got[3][7:].strip()
    v["STATUS"] = got[16].strip()
    return v, got


class StandIn:
    """A host as src/link.c's: its Diatom the server once a hello comes,
    welcome and beats to the hello's address at STANDIN_PORT"""
    def __init__(self, tmp, core, rom):
        self.d = gd.Diatom("standin", str(ROOT / "diatom/build/gblink/diatom"), str(tmp))
        self.d.wait("READY", 5)
        assert self.d.run(core, rom), "the stand-in's Diatom did not run the game"
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.s.bind((STANDIN_HOST, 56399))
        self.peer, self.sent, self.heard, self.hello_from = None, 0.0, 0.0, None

    def tick(self):
        r, _, _ = select.select([self.s], [], [], 0)
        while r:
            b, frm = self.s.recvfrom(512)
            kind, who = gd.wire_read(b)
            if kind == "hello" and not self.peer:
                self.hello_from = (who, frm)
                if not self.d.link("server", STANDIN_HOST):
                    print("standin: its Diatom refused the server's options")
                self.peer = frm[0]
                self.s.sendto(gd.wire("welcome", "Pixel 2 5TND", "test"), (self.peer, STANDIN_PORT))
            elif self.peer and frm[0] == self.peer:
                self.heard = time.time()
            r, _, _ = select.select([self.s], [], [], 0)
        if self.peer and time.time() - self.sent > 0.5:
            self.s.sendto(gd.wire("beat", "Pixel 2 5TND", "test"), (self.peer, STANDIN_PORT))
            self.sent = time.time()

    def end(self):
        if self.peer:
            for _ in range(2): self.s.sendto(gd.wire("bye", "Pixel 2 5TND", ""), (self.peer, STANDIN_PORT))
        self.d.end()


def start_diatom(card, env, tmp):
    """The resident emulator as tools/desk's start_diatom starts it, from
    diatom/build/gblink and with its files in `tmp`, so a desk window of
    Tarik's and its card are never touched"""
    binary = Path(os.environ.get("GBLINK_DIATOM") or ROOT / "diatom/build/gblink/diatom")
    cores = tmp / "cores"
    for link, to in ((card / "ZyperOS/diatom", binary), (card / "ZyperOS/cores", cores)):
        if link.is_symlink() or link.exists(): link.unlink()
        link.symlink_to(to)
    for d in ("Saves", "Bios"): (card / d).mkdir(exist_ok=True)
    w, h = desk.PANEL["pixel2"]
    fb = tmp / "pixel2.fb"
    dsock = Path(tempfile.gettempdir()) / f"gblink-desk-{os.getpid()}-diatom.sock"
    pidfile = tmp / "diatom.pid"
    env.update(ZYPEROS_DIATOM_SOCKET=str(dsock), ZYPEROS_DIATOM_PIDFILE=str(pidfile),
               ZYPEROS_DESK_FB=str(fb), DIATOM_DESKFB=str(fb), DIATOM_SURFACE=f"{w}x{h}")
    log = open(tmp / "diatom.log", "w")
    p = subprocess.Popen([str(binary), "--socket", str(dsock), "--cores", str(cores),
                          "--save", str(card / "Saves"), "--system", str(card / "Bios")],
                         env=env, stdout=log, stderr=subprocess.STDOUT)
    pidfile.write_text(f"{p.pid}\n")
    return p


def ensure_built(build=True):
    if build and subprocess.run(["make", "-C", str(ROOT), "--no-print-directory", "native"],
                                stdout=subprocess.DEVNULL).returncode:
        sys.exit("make native failed")
    # Diatom's desktop build in a folder of its own, as check-gblink's
    if build and subprocess.run(["make", "-C", str(ROOT / "diatom"), "--no-print-directory",
                                 "BUILD=build/gblink", "all"], stdout=subprocess.DEVNULL).returncode:
        sys.exit("Diatom did not build")
    core = None
    for ext in ("dylib", "so"):
        p = ROOT / f"build-native/gblink/gambatte_libretro.{ext}"
        if p.exists(): core = p
    if not core:
        subprocess.run(["sh", str(ROOT / "tools/gblink-core.sh")], check=True)
        core = next((ROOT / "build-native/gblink").glob("gambatte_libretro.*"))
    return core


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("mode", choices=("pixel", "standin"))
    ap.add_argument("--window", action="store_true", help="a window to watch, not SDL's dummy one")
    ap.add_argument("--keep", help="a folder for the shots and the log")
    ap.add_argument("--wait", type=float, default=40, help="seconds for the link (40)")
    ap.add_argument("--no-build", action="store_true", help="the launcher and Diatom as built (check-gblink)")
    a = ap.parse_args()

    core = ensure_built(not a.no_build)
    tmp = Path(tempfile.mkdtemp(prefix="gblink-desk-"))
    (tmp / "cores").mkdir()
    shutil.copyfile(core, tmp / "cores/gambatte_libretro.so")
    keep = Path(a.keep) if a.keep else tmp / "keep"
    keep.mkdir(parents=True, exist_ok=True)
    card = tmp / "pixel2"
    desk.make_card(card, "pixel2", False)
    rom = card / "Roms/Game Boy/ZyperOS Link Test.gb"
    rom.parent.mkdir(parents=True, exist_ok=True)
    rom.write_bytes(gbrom.rom())

    env = desk.card_env(card, None)
    env.update(ZYPEROS_DESK_FORCE="1", ZYPEROS_DESK_LINK_CABLE="1")
    if not a.window:
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    if a.mode == "standin":
        env.update(ZYPEROS_DESK_LINK_HOST=STANDIN_HOST, ZYPEROS_DESK_LINK_PORT=str(STANDIN_PORT))
    standin = StandIn(tmp, str(core), str(rom)) if a.mode == "standin" else None

    dp = start_diatom(card, env, tmp)
    sock = Path(tempfile.gettempdir()) / f"gblink-desk-{os.getpid()}.sock"
    ctl = Path(tempfile.gettempdir()) / f"gblink-desk-{os.getpid()}-ctl"
    ctl.mkdir(mode=0o700)
    flags = tmp / "flags"
    flags.mkdir()
    env.update(ZYPEROS_DESK_SOCK=str(sock), ZYPEROS_FLAG_DIR=str(flags), ZYPEROS_CTL_DIR=str(ctl))
    env.pop("ZYPEROS_HEADLESS", None)
    logpath = keep / "launcher.log"
    log = open(logpath, "w")
    lp = subprocess.Popen([str(ROOT / "build-native/zyperos"), "--device", "pixel2"], env=env,
                          stdout=log, stderr=subprocess.STDOUT)

    def ask(line):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(20)
        s.connect(str(sock))
        s.sendall((line + "\n").encode())
        s.shutdown(socket.SHUT_WR)
        out = b""
        while True:
            b = s.recv(4096)
            if not b: break
            out += b
        return out.decode()

    def logged(text):
        log.flush()
        return text in logpath.read_text(errors="replace")

    def idle(secs):
        end = time.time() + secs
        while time.time() < end:
            if standin: standin.tick()
            time.sleep(0.05)

    def wait_for(text, secs):
        end = time.time() + secs
        while time.time() < end:
            if logged(text): return True
            idle(0.1)
        return False

    def tap(key):
        ask(f"tap {key} 100")
        idle(0.5)

    rc = 1
    try:
        for _ in range(200):
            if sock.exists(): break
            time.sleep(0.05)
        idle(2)
        c = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        c.settimeout(20)
        c.connect(str(ctl / "ctl.sock"))
        c.sendall(f"open\tpath={rom}\tcore=gambatte\n".encode())
        print("open:", c.makefile().readline().strip())
        idle(4)
        tap("menu")
        for _ in range(ROW_DOWNS): tap("down")
        tap("a")
        idle(1)
        ask(f"shot {keep}/1-tiles.png")
        on_cable = logged("On the USB-C cable")
        print("the Link Cable page:", "the cable's tiles" if on_cable else "NOT the cable's (see 1-tiles.png)")
        tap("right")
        tap("a")
        joined = wait_for("link: joining on the cable", 5)
        print("Join:", "joining on the cable" if joined else "NOTHING (see the log)")
        linked = wait_for("link: linked with", a.wait)
        lines = [l for l in logpath.read_text(errors="replace").splitlines() if l.startswith("link:")]
        for l in lines: print("  " + l)
        if standin and standin.hello_from:
            print(f"stand-in: hello from {standin.hello_from[0]} at {standin.hello_from[1][0]}")
        if not linked:
            print("not linked")
            return 1
        idle(1)
        tap("start")          # this side clocks first
        idle(10)
        ask(f"shot {keep}/2-linked.png")
        v, rows = desk_screen(keep / "2-linked.png")
        print("this computer's screen:", v if v else rows)
        ok = bool(v) and v.get("STATUS") == "PASS"
        if standin:
            sv, _ = standin.d.look()
            print("the stand-in's screen: ", sv)
            ok = ok and bool(sv) and sv.get("STATUS") == "PASS"
        else:
            print("the Pixel 2's screen: tools/px2 shot (its counts are the mirror of these)")
        print("gblink-desk:", "PASS: linked, bytes both ways on both clocks" if ok else "FAIL")
        rc = 0 if ok else 1
    finally:
        for p in (lp, dp):
            if p and p.poll() is None:
                p.send_signal(signal.SIGTERM)
                try: p.wait(5)
                except subprocess.TimeoutExpired: p.kill()
        if standin: standin.end()
        shutil.rmtree(ctl, ignore_errors=True)
        try: sock.unlink()
        except OSError: pass
        if a.keep or rc:
            print(f"gblink-desk: shots and the launcher's log in {keep}")
        if not a.keep and not rc:
            shutil.rmtree(tmp, ignore_errors=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
