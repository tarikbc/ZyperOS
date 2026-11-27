# SPDX-License-Identifier: MPL-2.0
"""NES player 2 (src/player2.h) end to end, against the stub core: the frames
out through the shared file (src/p2share.h), and the guests' buttons in from
it, with the file mapped here as p2d maps it and what the core read written
down by the stub (STUBCORE_INPUT_LOG: the frame, then ports 0 to 3's bits).

  - Off for every game: between games SETPLAYER2 on=1 is answered on=0, and
    after RUN PLAYER2 says on=0 and there is no file.
  - SETPLAYER2 on=1: the file is there with its magic, its frame count grows,
    and the newest slot holds the stub's picture at its size, as XRGB8888;
    with no buttons= and players=, the header and PLAYER2 say the NES's
    0x01FD and 2.
  - Port 1 reads the pad p2d writes, within a few frames, and port 0 is
    untouched.
  - Only the NES pad's buttons: a pad of 0xFFFF reads 0x01FD.
  - A beat that stops (p2d died) lets the buttons go P2_BEAT_MS (500 ms)
    after the last beat Diatom saw: on Diatom's test clock (port/desktop.c,
    DIATOM_TEST_CLOCK), one beat, then none, holds the pad for exactly the
    30 frames of 59.7275 a second that are under 500 ms, on any machine.
    On the wall clock this waited 0.7 s of this script's time after the
    last beat and looked, which a busy machine's late frame or late look
    could take past (2026-11-24).
  - Player 2 holding a button keeps the game awake for the idle timer
    (SETIDLE), as player 1 does; with player 2's pad at 0, IDLE comes.
  - SETPLAYER2 on=0 removes the file, and a reader still holding it sees
    its magic gone.
  - SETPLAYER2 on=1 buttons=0x0FFF players=3: the header and PLAYER2 say
    them; a pad of 0xFFFF reads 0x0FFF on port 1 (X, bit 9, through, bit 12
    and up never), port 2 reads the second guest's pad, port 3 reads 0
    whatever its pad holds, and the second guest alone keeps the game awake.
    buttons= with no bit of the twelve and players= out of 2..4 are the
    NES's 0x01FD and 2.
  - Each guest's port is given a joypad (retro_set_controller_port_device)
    when player 2 first turns on in a game, once, as port 0 is at the load;
    not a port the core says it has not got (STUBCORE_PORTS).
  - STOP with it on removes the file too, and the next RUN starts with it
    off.
  - A core that calls exit() leaves no file behind, nor one that crashes
    (Linux only: on macOS each crash would write a crash report).

DIATOM_P2_SHARE puts the file in a folder of the test's own (the desktop
build only), so this never meets a real /tmp/zyperos-p2.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-hotkeys does. SDL's dummy drivers: no window, no sound.

    python3 test/player2.py
"""
import math, mmap, os, shutil, socket, struct, subprocess, sys, tempfile, threading, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

# p2share.h's layout (tools/p2share-check.c holds the offsets)
P2S_MAGIC, P2S_VERSION, P2S_SLOTS = 0x50325348, 1, 3
P2S_HEAD, P2S_SLOT_BYTES = 4096, 512 * 480 * 4
OFF_PLAYERS, OFF_BUTTONS, OFF_PAD, OFF_BEAT, OFF_SLOT = 16, 20, 24, 36, 44
P2S_PAD_MASK = 0x01FD
P2_BEAT_MS = 500                 # src/player2.c: five of p2d's beats missed
B, A, X = 1, 1 << 8, 1 << 9

TMP = tempfile.mkdtemp(prefix="diatom-p2-")
SHARE = f"{TMP}/p2"

fails, procs = [], []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)


class Diatom:
    """One Diatom on a socket, read a line at a time"""
    def __init__(self, tag, crash=None, ports=None, clock=False):
        self.log = f"{TMP}/{tag}.log"
        self.devlog = f"{TMP}/{tag}.devices"
        sock = f"{TMP}/{tag}.sock"
        env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                   SDL_AUDIODRIVER="dummy", STUBCORE_INPUT_LOG=self.log,
                   STUBCORE_DEVICE_LOG=self.devlog, DIATOM_P2_SHARE=SHARE)
        if crash: env["STUBCORE_CRASH"] = crash
        if ports is not None: env["STUBCORE_PORTS"] = str(ports)
        env.pop("DIATOM_TEST_CLOCK", None)
        if clock: env["DIATOM_TEST_CLOCK"] = "0"
        self.p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                                  env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        procs.append(self.p)
        # The waits here are the wall clock's, and only waits: each ends as
        # soon as what it waits for is there, so a busy machine makes them
        # longer, never a check different
        end = time.time() + 30
        while not os.path.exists(sock) and time.time() < end:
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(sock)
        self.buf, self.got = b"", []

    def send(self, line):
        self.s.sendall((line + "\n").encode())

    def until(self, prefix, timeout=30.0):
        """The first line from now on that starts with prefix, or None"""
        end = time.time() + timeout
        self.s.settimeout(0.05)
        while time.time() < end:
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                l = l.decode().strip()
                self.got.append(l)
                if l.startswith(prefix): return l
            try:
                d = self.s.recv(65536)
                if not d: return None
                self.buf += d
            except socket.timeout:
                pass
        return None

    def ask(self, line, prefix):
        self.send(line)
        return self.until(prefix)

    def lines(self):
        """What the core read, a frame a line: ports 0 to 3's bits"""
        try:
            with open(self.log) as f: text = f.read()
        except OSError:
            return []
        return [tuple(int(v) for v in l.split()[1:]) for l in text.split("\n")[:-1]]

    def devices(self):
        """Each device the core was given, in order: (port, device)"""
        try:
            with open(self.devlog) as f: text = f.read()
        except OSError:
            return []
        return [tuple(int(v) for v in l.split()) for l in text.split("\n") if l.strip()]

    def frames_after(self, n, count, limit=30.0):
        """Wait until the core has read the pad count times past line n"""
        end = time.time() + limit
        while len(self.lines()) < n + count and time.time() < end:
            time.sleep(0.02)
        return self.lines()[n:]

    def quit(self):
        try: self.send("QUIT")
        except OSError: pass
        try: self.p.wait(30)
        except subprocess.TimeoutExpired: self.p.kill()


def u32(m, off):
    return struct.unpack_from("<I", m, off)[0]


def pad(m, k, bits):
    """p2d's pad[k], port k + 1's buttons"""
    struct.pack_into("<I", m, OFF_PAD + 4 * k, bits)


def share():
    """The file once Diatom has made it whole, mapped as p2d maps it; None
    after 30 s without"""
    end = time.time() + 30
    while time.time() < end:
        try:
            with open(SHARE, "r+b") as f:
                if os.fstat(f.fileno()).st_size >= P2S_HEAD + P2S_SLOTS * P2S_SLOT_BYTES:
                    m = mmap.mmap(f.fileno(), 0)
                    if u32(m, 0) == P2S_MAGIC: return m
                    m.close()
        except OSError:
            pass
        time.sleep(0.02)
    return None


def bump(m):
    """One of p2d's beats"""
    struct.pack_into("<I", m, OFF_BEAT, (u32(m, OFF_BEAT) + 1) & 0xFFFFFFFF)


class Beat(threading.Thread):
    """p2d's heartbeat: pad_beat bumped every 50 ms until stopped; `first`
    set once the first is in"""
    def __init__(self, m):
        super().__init__(daemon=True)
        self.m, self.go, self.first = m, True, threading.Event()
    def run(self):
        while self.go:
            bump(self.m)
            self.first.set()
            time.sleep(0.05)
    def stop(self):
        self.go = False
        self.join()


try:
    d = Diatom("main")
    ready = d.until("READY")
    check("READY says proto=14", ready is not None and ready.startswith("READY\tproto=14"), ready)

    # off_by_default
    check("between games SETPLAYER2 on=1 is refused: PLAYER2 on=0",
          d.ask("SETPLAYER2\ton=1", "PLAYER2") == "PLAYER2\ton=0", d.got[-3:])
    check("and makes no file", not os.path.exists(SHARE), os.listdir(TMP))
    d.send(f"RUN\tcore={STUB}")
    check("the game runs", d.until("RUNNING") is not None, d.got)
    JOYPAD = 1
    check("at the load port 0 alone is given a device, the joypad", d.devices() == [(0, JOYPAD)],
          d.devices())
    check("off by default: after RUN, PLAYER2 says on=0",
          d.ask("PLAYER2", "PLAYER2") == "PLAYER2\ton=0", d.got[-3:])
    check("and there is no file", not os.path.exists(SHARE), os.listdir(TMP))

    # frames_arrive
    check("SETPLAYER2 on=1 is answered PLAYER2 on=1, the NES's buttons and 2 players",
          d.ask("SETPLAYER2\ton=1", "PLAYER2") == "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2",
          d.got[-3:])
    check("player 2 on gives the guest's port 1 a joypad", d.devices() == [(0, JOYPAD), (1, JOYPAD)],
          d.devices())
    m = share()
    check("within 1 s the file is there, at the share's size", m is not None, os.listdir(TMP))
    if m is None:
        raise RuntimeError("no share to read")
    check("with P2S_MAGIC and P2S_VERSION",
          (u32(m, 0), u32(m, 4)) == (P2S_MAGIC, P2S_VERSION), (hex(u32(m, 0)), u32(m, 4)))
    check("with no buttons= or players=, the header says the NES's 0x01FD and 2",
          (u32(m, OFF_BUTTONS), u32(m, OFF_PLAYERS)) == (P2S_PAD_MASK, 2),
          (hex(u32(m, OFF_BUTTONS)), u32(m, OFF_PLAYERS)))
    check("and PLAYER2 says them",
          d.ask("PLAYER2", "PLAYER2") == "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2", d.got[-3:])
    f0 = u32(m, 12)
    end = time.time() + 30
    while u32(m, 12) == f0 and time.time() < end:
        time.sleep(0.02)
    f1 = u32(m, 12)
    check("and its frame count grows", f1 > f0, (f0, f1))
    s = u32(m, 8) % P2S_SLOTS
    seq, w, h, sf = struct.unpack_from("<4I", m, OFF_SLOT + 16 * s)
    check("the newest slot holds the stub's picture at its size (256 or 512 x 224)",
          (w, h) in ((256, 224), (512, 224)) and seq % 2 == 0 and 0 < sf, (seq, w, h, sf))
    data = P2S_HEAD + s * P2S_SLOT_BYTES
    corner, inside = u32(m, data), u32(m, data + (w + 1) * 4)
    # The stub draws RGB565: a white border, and inside it one of two
    # checkerboard colors, which as XRGB8888 are 0x181821 and 0x313442
    check("as XRGB8888, converted from the core's RGB565",
          corner & 0xFFFFFF == 0xFFFFFF and inside & 0xFFFFFF in (0x181821, 0x313442),
          (hex(corner), hex(inside)))

    # port1_reads_pad. Counted from the first frame the core read after the
    # first beat was in: that frame polled after it, so reads the pad. Until
    # 2026-11-24 from a count taken before the beat began, which a busy
    # machine could let 10 frames pass between
    pad(m, 0, A)
    pad(m, 1, A)
    beat = Beat(m)
    beat.start()
    beat.first.wait(30)
    n = len(d.lines())
    seen = d.frames_after(n, 30)
    first = next((i for i, l in enumerate(seen) if l[1] == A), None)
    check("port 1 reads p2d's A (256) within 10 frames", first is not None and first <= 10,
          (first, seen[:12]))
    check("and port 0 is untouched", all(l[0] == 0 for l in seen), seen[:12])
    check("and with players=2, port 2 reads nothing, its pad set or not",
          all(l[2] == 0 and l[3] == 0 for l in seen), seen[:12])
    pad(m, 1, 0)

    # hotkeys_never_through
    n = len(d.lines())
    pad(m, 0, 0xFFFF)
    seen = d.frames_after(n, 20)
    check("a pad of 0xFFFF reads 0x01FD: the NES pad's buttons, never a hotkey",
          seen and seen[-1][1] == P2S_PAD_MASK and all(l[1] & ~P2S_PAD_MASK == 0 for l in seen),
          [l[1] for l in seen])

    # idle_counts_player2: player 1 touches nothing while player 2 plays a
    # turn, and the launcher powers off at IDLE at once
    d.send("SETIDLE\tms=300")
    check("player 2 holding a button keeps the game awake: no IDLE in 1 s",
          d.until("IDLE", 1.0) is None, d.got[-3:])
    pad(m, 0, 0)
    check("and with player 2's pad at 0, IDLE comes", d.until("IDLE") == "IDLE",
          d.got[-3:])
    d.send("SETIDLE\tms=0")
    pad(m, 0, 0xFFFF)

    # off_cleans_up
    check("SETPLAYER2 on=0 is answered PLAYER2 on=0",
          d.ask("SETPLAYER2\ton=0", "PLAYER2") == "PLAYER2\ton=0", d.got[-3:])
    check("and removes the file", not os.path.exists(SHARE), os.listdir(TMP))
    check("a reader still holding it sees its magic gone", u32(m, 0) == 0, hex(u32(m, 0)))
    n = len(d.lines())
    seen = d.frames_after(n, 5)
    check("and port 1 reads nothing, its beat still going", seen and all(l[1] == 0 for l in seen),
          seen)
    beat.stop()
    m.close()

    # buttons_and_players: a system with more buttons and more players
    on = d.ask("SETPLAYER2\ton=1\tbuttons=0x0FFF\tplayers=3", "PLAYER2")
    check("SETPLAYER2 on=1 buttons=0x0FFF players=3 is answered with them",
          on == "PLAYER2\ton=1\tbuttons=0x0FFF\tplayers=3", d.got[-3:])
    check("on again, with 3 players: port 2 given a joypad, port 1 not again",
          d.devices() == [(0, JOYPAD), (1, JOYPAD), (2, JOYPAD)], d.devices())
    m = share()
    check("a new file", m is not None, os.listdir(TMP))
    if m is None:
        raise RuntimeError("no share to read")
    check("and its header says them",
          (u32(m, OFF_BUTTONS), u32(m, OFF_PLAYERS)) == (0x0FFF, 3),
          (hex(u32(m, OFF_BUTTONS)), u32(m, OFF_PLAYERS)))
    pad(m, 0, 0xFFFF)
    pad(m, 1, B | A)
    pad(m, 2, 0xFFFF)
    beat = Beat(m)
    beat.start()
    n = len(d.lines())
    seen = d.frames_after(n, 20)
    check("port 1 reads 0x0FFF for a pad of 0xFFFF: X (bit 9) through, bit 12 and up never",
          seen and seen[-1][1] == 0x0FFF and seen[-1][1] & X and all(l[1] & ~0x0FFF == 0 for l in seen),
          [l[1] for l in seen])
    check("port 2 reads the second guest's pad, B and A",
          seen and seen[-1][2] == B | A and all(l[2] in (0, B | A) for l in seen), [l[2] for l in seen])
    check("port 3 reads 0 with players=3, whatever its pad holds", all(l[3] == 0 for l in seen),
          [l[3] for l in seen])
    check("and port 0 is untouched", all(l[0] == 0 for l in seen), seen[:5])
    # Only the second guest holds a button: the game stays awake for it
    pad(m, 0, 0)
    pad(m, 2, 0)
    d.send("SETIDLE\tms=300")
    check("the second guest holding a button keeps the game awake: no IDLE in 1 s",
          d.until("IDLE", 1.0) is None, d.got[-3:])
    pad(m, 1, 0)
    check("and with every guest's pad at 0, IDLE comes", d.until("IDLE") == "IDLE", d.got[-3:])
    d.send("SETIDLE\tms=0")
    beat.stop()
    m.close()
    d.ask("SETPLAYER2\ton=0", "PLAYER2")
    check("buttons= with none of the twelve and players=7 are the NES's",
          d.ask("SETPLAYER2\ton=1\tbuttons=0xF000\tplayers=7", "PLAYER2")
          == "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2", d.got[-3:])
    d.ask("SETPLAYER2\ton=0", "PLAYER2")
    check("buttons= in decimal: 4095 is 0x0FFF",
          d.ask("SETPLAYER2\ton=1\tbuttons=4095\tplayers=4", "PLAYER2")
          == "PLAYER2\ton=1\tbuttons=0x0FFF\tplayers=4", d.got[-3:])
    d.send("STOP")
    check("STOP ends the game", d.until("EXIT") is not None, d.got[-3:])
    check("and STOP with it on removed the file before EXIT", not os.path.exists(SHARE),
          os.listdir(TMP))
    d.send(f"RUN\tcore={STUB}")
    d.until("RUNNING")
    check("the next RUN starts with it off", d.ask("PLAYER2", "PLAYER2") == "PLAYER2\ton=0",
          d.got[-3:])
    d.send("STOP")
    d.until("EXIT")
    d.quit()

    # pad_dropped_when_beat_stops, on Diatom's test clock: its time moves
    # only by the stub's frames, each 1/59.7275 s, so one beat and then none
    # holds the pad for the frames under P2_BEAT_MS of it, 30, from the frame
    # that saw the beat, however late any of them ran here; then 0 for good
    BEAT_FRAMES = math.ceil(P2_BEAT_MS / (1000 / 59.7275))
    c = Diatom("beat", clock=True)
    c.until("READY")
    c.send(f"RUN\tcore={STUB}")
    c.until("RUNNING")
    c.ask("SETPLAYER2\ton=1", "PLAYER2")
    m = share()
    if m is None:
        raise RuntimeError("no share to read")
    pad(m, 0, A)

    def held_after(beats, after=1):
        """The beat bumped once more, then the frames port 1 read A in a row
        from it, once `after` frames past them are read, and those frames"""
        def stretches():
            vals = [l[1] for l in c.lines()]
            out, i = [], 0
            while i < len(vals):
                if vals[i] == A:
                    j = i
                    while j < len(vals) and vals[j] == A: j += 1
                    out.append((j - i, vals[j:j + after]))
                    i = j
                else:
                    i += 1
            return out
        bump(m)
        end = time.time() + 30
        while time.time() < end:
            st = stretches()
            if len(st) >= beats and len(st[beats - 1][1]) >= after: return st[beats - 1]
            time.sleep(0.02)
        return None, []

    frames, then = held_after(1)
    check(f"one beat, then none: port 1 reads the pad for {BEAT_FRAMES} frames of Diatom's clock "
          f"({P2_BEAT_MS} ms), then 0", frames == BEAT_FRAMES and then == [0], (frames, then))
    frames, then = held_after(2, 100)
    check(f"and the pad again once the beat moves, for {BEAT_FRAMES} frames", frames == BEAT_FRAMES,
          (frames, then[:5]))
    check("and 0 for good with no beat: 100 frames after", len(then) == 100 and set(then) == {0},
          then[:12])
    m.close()
    c.send("STOP")
    c.until("EXIT")
    c.quit()

    # A core that says it has one port: its port 1 is never given a device
    c = Diatom("oneport", ports=1)
    c.until("READY")
    c.send(f"RUN\tcore={STUB}")
    c.until("RUNNING")
    on = c.ask("SETPLAYER2\ton=1", "PLAYER2")
    check("a core with one port (STUBCORE_PORTS=1): player 2 on, port 1 given no device",
          on == "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2" and c.devices() == [(0, JOYPAD)],
          (on, c.devices()))
    c.send("STOP")
    c.until("EXIT")
    c.quit()

    # A game that ends by its core calling exit(), or by crashing: the file
    # goes all the same, so nothing reads a dead game's frames
    for mode in ["exit@90"] + (["segv@90"] if sys.platform.startswith("linux") else []):
        c = Diatom(mode.split("@")[0], crash=mode)
        c.until("READY")
        c.send(f"RUN\tcore={STUB}")
        c.until("RUNNING")
        on = c.ask("SETPLAYER2\ton=1", "PLAYER2")
        there = os.path.exists(SHARE)
        ended = c.until("EXIT\treason=crash")
        try: c.p.wait(30)
        except subprocess.TimeoutExpired: pass
        check(f"STUBCORE_CRASH={mode}: on, then the game's end, and no file left",
              on == "PLAYER2\ton=1\tbuttons=0x01FD\tplayers=2" and there and ended is not None and not os.path.exists(SHARE),
              (on, there, ended, os.listdir(TMP)))
except RuntimeError:
    pass
finally:
    for p in procs:
        if p.poll() is None: p.kill()
    shutil.rmtree(TMP, ignore_errors=True)

if fails:
    print(f"FAIL: {len(fails)} player 2 check(s)")
    sys.exit(1)
print("ok: player 2's frames out and buttons in, off for every game and gone at every end")
