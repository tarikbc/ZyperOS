#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The Link Cable on two Diatoms: Diatom's desktop build and Gambatte, with
mk/patches/gambatte-link.patch, built for this computer, each Diatom told
its side as the launcher tells it (src/link.c's link_opt_lines: the port,
the server's twelve digits and the mode, as SETOPT over the paused game),
and tools/gblink-rom.py's test ROM in both. What crossed is read off each
game's own screen (SAVE's preview=, the frame as Diatom has it).

    python3 tools/gblink-diatom.py check [--core C] [--diatom D]
        both on this computer, over 127.0.0.1; exit 0 when:
          A  START on the server's side: both screens say PASS (16 good
             bytes on each clock, none bad) within 30 s
          B  each side's count on one clock is the other's on the other
             (the same transfers), and what one sent last is what the
             other got last, give or take the transfer in flight
          C  the menu open on the client for 3 s (PAUSE, as the launcher's
             in-game menu): the server waits and no byte goes bad, and
             the counts go on after RESUME
        skips (exit 0, "skipped") with no core built for this computer:
        tools/gblink-core.sh builds it

    python3 tools/gblink-diatom.py join HOST [--core C] [--diatom D]
        this computer as the joiner of a Pixel 2 that hosts (Link Cable,
        Host): the launchers' hello on port 56399 (src/link.h), Diatom
        the client of HOST's Gambatte once welcomed, a beat every half
        second, bye at the end. In a window: Enter is START, arrows the
        d-pad, Z and X are B and A. Ctrl-C ends it.

The core is GBLINK_CORE, or build-native/gblink/gambatte_libretro.{dylib,so}
(tools/gblink-core.sh). Diatom is diatom/build/gblink/diatom (make
check-gblink builds it there).
"""
import argparse, os, select, shutil, signal, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import importlib.util
_spec = importlib.util.spec_from_file_location("gblink_rom", os.path.join(ROOT, "tools", "gblink-rom.py"))
gbrom = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(gbrom)

GAME_PORT, BEAT_PORT = "56400", 56399          # src/link.h
MODE = {"off": "Not Connected", "server": "Network Server", "client": "Network Client"}


def opt_lines(role, addr):
    """src/link.c's link_opt_lines: the port, the address's twelve digits
    and the mode last; off is the mode alone"""
    out = []
    if role != "off":
        octs = [int(x) for x in addr.split(".")]
        assert len(octs) == 4 and all(0 <= o <= 255 for o in octs), addr
        digits = "".join(f"{o:03d}" for o in octs)
        out.append(f"SETOPT\tkey=gambatte_gb_link_network_port\tvalue={GAME_PORT}")
        out += [f"SETOPT\tkey=gambatte_gb_link_network_server_ip_{k + 1}\tvalue={d}"
                for k, d in enumerate(digits)]
    out.append(f"SETOPT\tkey=gambatte_gb_link_mode\tvalue={MODE[role]}")
    return out


def default_core():
    if os.environ.get("GBLINK_CORE"): return os.environ["GBLINK_CORE"]
    for ext in ("dylib", "so"):
        p = os.path.join(ROOT, "build-native", "gblink", f"gambatte_libretro.{ext}")
        if os.path.exists(p): return p
    return None


def read_bmp(path):
    """A 24-bit BMP as Diatom writes it (bmp_encode): w, h, luminance(x, y)"""
    with open(path, "rb") as f: b = f.read()
    if b[:2] != b"BM": return None
    off = struct.unpack_from("<I", b, 10)[0]
    w, h = struct.unpack_from("<ii", b, 18)
    bpp = struct.unpack_from("<H", b, 28)[0]
    if bpp != 24: return None
    flip, h = h > 0, abs(h)
    row = (w * 3 + 3) & ~3

    def lum(x, y):
        yy = h - 1 - y if flip else y
        o = off + yy * row + x * 3
        return b[o] * 0.11 + b[o + 1] * 0.59 + b[o + 2] * 0.30
    return w, h, lum


def screen(path):
    """The test ROM's screen as {label: value}, and its rows"""
    got = read_bmp(path)
    if not got: return None, None
    rows = gbrom.screen_rows(got[2], got[0], got[1])
    if not rows: return None, None
    v = {}
    for r in (r.strip() for r in rows):
        for label in ("INT TX", "INT OK", "EXT TX", "EXT OK", "BAD", "LAST RX", "LAST TX"):
            if r.startswith(label):
                try: v[label] = int(r[len(label):].strip(), 16)
                except ValueError: v[label] = None
    v["ROLE"] = rows[3][7:].strip() if len(rows) > 3 else ""
    v["STATUS"] = rows[16].strip() if len(rows) > 16 else ""
    return v, rows


class Diatom:
    """One Diatom on its own socket, a game in it"""
    def __init__(self, name, binary, tmp, env_extra=None, visible=False):
        self.name, self.tmp = name, os.path.join(tmp, name)
        os.makedirs(self.tmp, exist_ok=True)
        # Short: a Unix socket's path is capped near 104 bytes
        self.sock_path = f"/tmp/gblink-{os.getpid()}-{name}.sock"
        if os.path.exists(self.sock_path): os.unlink(self.sock_path)
        env = dict(os.environ)
        if not visible:
            env.update(SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software", SDL_AUDIODRIVER="dummy")
        env.update(env_extra or {})
        self.log = open(os.path.join(self.tmp, "diatom.log"), "w")
        self.p = subprocess.Popen([binary, "--socket", self.sock_path, "--save", self.tmp,
                                   "--system", self.tmp], env=env,
                                  stdout=self.log, stderr=subprocess.STDOUT)
        for _ in range(200):
            if os.path.exists(self.sock_path): break
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(self.sock_path)
        self.buf = b""
        self.shots = 0

    def send(self, line):
        self.s.sendall((line + "\n").encode())

    def wait(self, prefix, timeout):
        end = time.time() + timeout
        self.s.settimeout(0.1)
        while time.time() < end:
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                l = l.decode(errors="replace")
                if l.startswith(prefix): return l
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                continue
            if not d: return None
            self.buf += d
        return None

    def run(self, core, rom):
        self.send(f"RUN\tcore={core}\trom={rom}\tpreview={self.tmp}/pause.bmp")
        return self.wait("RUNNING", 10) is not None

    def link(self, role, addr):
        """As the launcher does it: over the paused game"""
        self.send("PAUSE")
        if not self.wait("PAUSED", 5): return False
        ok = True
        for l in opt_lines(role, addr):
            self.send(l)
            ok = ok and (self.wait("OPTSET", 3) or "").endswith(l.split("\t")[1][4:])
        self.send("RESUME")
        return ok

    def look(self):
        """The game's screen now, read off a quick save's preview"""
        self.shots += 1
        st, pic = f"{self.tmp}/look{self.shots}.state", f"{self.tmp}/look{self.shots}.bmp"
        self.send(f"SAVE\tpath={st}\tpreview={pic}")
        if not self.wait("SAVED", 10) or not os.path.exists(pic): return None, None
        return screen(pic)

    def end(self):
        try:
            self.send("STOP"); self.wait("EXIT", 5)
            self.send("QUIT")
            self.p.wait(5)
        except Exception:
            pass
        if self.p.poll() is None:
            self.p.kill(); self.p.wait()
        try: os.unlink(self.sock_path)
        except OSError: pass


fails = []
def ck(ok, what, got=None):
    print(("  ok    " if ok else "  FAIL  ") + what + ("" if ok or got is None else f"  ({got})"))
    if not ok: fails.append(what)


def passed(v): return v and v.get("STATUS") == "PASS"


def check(args):
    core = args.core or default_core()
    if not core or not os.path.exists(core):
        print("check-gblink: skipped, no Gambatte built for this computer (tools/gblink-core.sh)")
        return 0
    binary = args.diatom
    if not os.path.exists(binary):
        print(f"check-gblink: skipped, no {binary} (make -C diatom BUILD=build/gblink)")
        return 0
    tmp = tempfile.mkdtemp(prefix="gblink-")
    rom = os.path.join(tmp, "linktest.gb")
    with open(rom, "wb") as f: f.write(gbrom.rom())
    print(f"gblink: two Diatoms, {os.path.basename(core)}, the test ROM, over 127.0.0.1")
    # START on the server's side once the link is surely up: the client
    # connects within a second of its options (the patch's retry)
    srv = Diatom("server", binary, tmp, {"DIATOM_INPUT_SCRIPT": "180:start;186:"})
    cli = Diatom("client", binary, tmp)
    sides = (srv, cli)
    try:
        for d in sides: d.wait("READY", 5)
        ck(all(d.run(core, rom) for d in sides), "both games run")
        ck(srv.link("server", "127.0.0.1"), "the server's 14 options taken (Network Server, 127.0.0.1)")
        ck(cli.link("client", "127.0.0.1"), "the client's 14 options taken (Network Client, 127.0.0.1)")

        # A
        end, vs, vc = time.time() + 30, None, None
        while time.time() < end:
            time.sleep(1)
            vs, rs = srv.look()
            vc, rc = cli.look()
            if passed(vs) and passed(vc): break
        for d, v, rows in ((srv, vs, rs), (cli, vc, rc)):
            print(f"  {d.name}'s screen:")
            for r in rows or []: print(f"      |{r}")
        ck(passed(vs) and passed(vc), "A  both screens say PASS: bytes cross both ways on both clocks",
           (vs, vc))

        # B
        if vs and vc and None not in vs.values() and None not in vc.values():
            ck(abs(vs["INT TX"] - vc["EXT TX"]) <= 3 and abs(vs["EXT TX"] - vc["INT TX"]) <= 3,
               "B  the server's INT TX is the client's EXT TX and back (the same transfers)",
               (vs["INT TX"], vc["EXT TX"], vs["EXT TX"], vc["INT TX"]))
            ck(vs["BAD"] == 0 and vc["BAD"] == 0, "B  no byte out of sequence on either side")
        else:
            ck(False, "B  the screens read", (vs, vc))

        # C
        before_s, _ = srv.look()
        cli.send("PAUSE"); cli.wait("PAUSED", 5)
        time.sleep(3)
        cli.send("RESUME")
        time.sleep(3)
        after_s, rs = srv.look()
        after_c, rc = cli.look()
        ck(after_s and after_c and after_s["BAD"] == 0 and after_c["BAD"] == 0,
           "C  the client's menu open 3 s: no byte went bad on either side",
           (after_s, after_c))
        ck(after_s and before_s and (after_s["INT TX"] + after_s["EXT TX"])
           > (before_s["INT TX"] + before_s["EXT TX"]) + 20,
           "C  and the transfers go on after RESUME", (before_s, after_s))
        if args.keep:
            os.makedirs(args.keep, exist_ok=True)
            for d in sides:
                shutil.copy(os.path.join(d.tmp, f"look{d.shots}.bmp"),
                            os.path.join(args.keep, f"{d.name}.bmp"))
                shutil.copy(os.path.join(d.tmp, "diatom.log"),
                            os.path.join(args.keep, f"{d.name}.log"))
    finally:
        for d in sides: d.end()
        shutil.rmtree(tmp, ignore_errors=True)
    if fails:
        print(f"gblink: {len(fails)} failed")
        return 1
    print("gblink: two Diatoms linked: bytes cross both ways on both clocks, a menu open is no lost link")
    return 0


def wire(kind, name, game):
    flat = lambda s: s.replace("\t", " ").replace("\n", " ").replace("\r", " ")
    return f"ZLINK1\t{kind}\t{flat(name)}\t{flat(game)}".encode()


def wire_read(b):
    f = b.decode(errors="replace").split("\t")
    return (f[1], f[2]) if len(f) >= 3 and f[0] == "ZLINK1" else (None, None)


def join(args):
    """This computer as a joiner: src/link.c's hello, welcome and beat"""
    core = args.core or default_core()
    if not core or not os.path.exists(core):
        sys.exit("no Gambatte built for this computer: tools/gblink-core.sh")
    host, name = args.host, args.name
    # The address this computer has toward the host, for the beat's socket:
    # the host answers to the hello's address on port 56399 (link.c's say)
    me = args.me
    if not me:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect((host, BEAT_PORT))
        me = probe.getsockname()[0]
        probe.close()
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind((me, BEAT_PORT))
    print(f"gblink: this computer is {me}, saying hello to {host}:{BEAT_PORT} as {name}")
    rom = args.rom
    tmp = tempfile.mkdtemp(prefix="gblink-join-")
    if not rom:
        rom = os.path.join(tmp, "linktest.gb")
        with open(rom, "wb") as f: f.write(gbrom.rom())
    d = Diatom("joiner", args.diatom, tmp, visible=not args.headless)
    stop = []
    signal.signal(signal.SIGINT, lambda *_: stop.append(1))
    try:
        d.wait("READY", 5)
        if not d.run(core, rom): sys.exit("Diatom did not run the game")
        game = os.path.basename(rom)
        peer, linked, sent, heard = None, False, 0.0, time.time()
        t0 = time.time()
        while not stop:
            now = time.time()
            if not linked and now - sent >= 0.5:
                s.sendto(wire("hello", name, game), (host, BEAT_PORT)); sent = now
                if now - t0 > 30: print("gblink: the host did not answer in 30 s"); break
            if linked and now - sent >= 0.5:
                s.sendto(wire("beat", name, game), (host, BEAT_PORT)); sent = now
            r, _, _ = select.select([s], [], [], 0.1)
            if r:
                b, frm = s.recvfrom(512)
                kind, who = wire_read(b)
                if frm[0] != host or not kind: continue
                if not linked and kind == "busy":
                    print(f"gblink: {who} is linked with another"); break
                if not linked and kind == "welcome":
                    peer, linked, heard = who, True, now
                    ok = d.link("client", host)
                    print(f"gblink: {who} welcomes us; Diatom the client of {host}:{GAME_PORT}: "
                          f"{'options taken' if ok else 'OPTIONS REFUSED'}")
                elif linked and kind == "bye":
                    print(f"gblink: {peer} left the link"); break
                elif linked:
                    heard = now
            if linked and now - heard > 6:
                print(f"gblink: link lost (no beat from {peer} for 6 s)"); break
            if d.p.poll() is not None: break
    finally:
        if linked:
            for _ in range(2): s.sendto(wire("bye", name, ""), (host, BEAT_PORT))
        d.end()
        print(f"gblink: Diatom's log in {d.tmp}/diatom.log")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for n in ("check", "join"):
        p = sub.add_parser(n)
        p.add_argument("--core")
        p.add_argument("--diatom", default=os.path.join(ROOT, "diatom", "build", "gblink", "diatom"))
        if n == "check":
            p.add_argument("--keep", help="a folder for each side's last screen and log")
        else:
            p.add_argument("host", help="the Pixel 2 that hosts: 10.42.0.1 on the cable, 10.42.1.1 on its Wi-Fi")
            p.add_argument("--name", default="Mac 0001", help="how the Pixel 2 names this computer")
            p.add_argument("--rom", help="the game (default: tools/gblink-rom.py's test ROM)")
            p.add_argument("--me", help="this computer's address toward the host (default: the route's)")
            p.add_argument("--headless", action="store_true", help="no window, no sound")
    a = ap.parse_args()
    return check(a) if a.cmd == "check" else join(a)


if __name__ == "__main__":
    sys.exit(main())
