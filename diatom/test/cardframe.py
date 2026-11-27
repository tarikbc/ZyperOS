"""The frame for the launcher's card of a game with no art, end to end
against the stub core (RUN's card=, main.c's card_frame):

  - written once the game has run a minute (here DIATOM_CARD_FRAME_MS, 300
    ms), as a BMP, whole (written aside and renamed);
  - never written over: a second game with the same path leaves it as it was,
    so a card keeps its look;
  - not written by a game that ends before the minute: the launcher shows the
    Auto picture until a game has run that long.

On Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK), so a game's
time is its frames' (STUBCORE_INPUT_LOG counts them): the first game runs
until its card is there, the second at least a second, past its 300 ms, on
any machine. The early one is on the wall clock, a second of it, as Diatom
on the test clock runs as fast as it can and could run its minute before
this script's STOP came, and on the wall clock a busy machine makes a game
slower, never faster. The card is written on a
thread of its own, and until 2026-11-24 the first game ran 1.2 s of this
script's time and the card was read at its EXIT, which a busy machine could
reach before that thread had written it.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/cardframe.py
"""
import os, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-cardframe-")
fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

class Run:
    """One Diatom. The waits are the wall clock's, and only waits: each ends
    as soon as what it waits for is there"""
    def __init__(self, name, clock=True, **env):
        self.sock, self.log, self.out = f"{TMP}/{name}.sock", f"{TMP}/{name}.input", f"{TMP}/{name}.out"
        with open(self.out, "w") as out:
            self.p = subprocess.Popen(
                [BIN, "--socket", self.sock, "--save", TMP, "--system", TMP],
                env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                         SDL_AUDIODRIVER="dummy", STUBCORE_INPUT_LOG=self.log,
                         **({"DIATOM_TEST_CLOCK": "0"} if clock else {}), **env),
                stdout=out, stderr=subprocess.DEVNULL)
        end = time.time() + 30
        while not os.path.exists(self.sock) and time.time() < end:
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(self.sock)
        self.s.settimeout(0.05)
        self.buf, self.got = b"", []

    def pump(self, seconds, until=None):
        end = time.time() + seconds
        while time.time() < end:
            try:
                d = self.s.recv(65536)
                if not d: return
                self.buf += d
            except socket.timeout:
                pass
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                self.got.append(l.decode().strip())
                if until and self.got[-1].startswith(until): return

    def send(self, line):
        self.s.sendall((line + "\n").encode())

    def frames(self):
        try:
            with open(self.log) as f: return sum(1 for _ in f)
        except OSError: return 0

    def game(self, card, frames=None):
        """A game until its card is there, or for `frames` frames"""
        n = self.frames()
        self.send(f"RUN\tcore={STUB}\tcard={card}")
        self.pump(30, "RUNNING")
        end = time.time() + 30
        while time.time() < end and (os.path.exists(card) if frames is None
                                     else self.frames() >= n + frames) is False:
            self.pump(0.02)
        self.send("STOP")
        self.pump(30, "EXIT")

    def close(self):
        """QUIT, and what it said of its cards once it has gone"""
        self.send("QUIT")
        try: self.p.wait(30)
        except subprocess.TimeoutExpired: self.p.kill()
        return [l for l in open(self.out, errors="replace") if l.startswith("diatom: card frame")]

def read(path):
    try:
        with open(path, "rb") as f: return f.read()
    except OSError: return None

card = f"{TMP}/game.card.bmp"
r = Run("once", DIATOM_CARD_FRAME_MS="300")
r.pump(30, "READY")
r.game(card)
first = read(card)
check("a game run past the minute leaves its card frame", first is not None, os.listdir(TMP))
check("as a whole BMP", first is not None and first[:2] == b"BM" and len(first) > 54 and
      int.from_bytes(first[2:6], "little") == len(first), first[:16] if first else None)
check("with nothing left aside", not os.path.exists(card + ".tmp"), os.listdir(TMP))
before = os.stat(card).st_mtime_ns if first else 0
r.game(card, 60)                # a second of its clock, past the 300 ms
said = r.close()
check("a second game never writes over it", first is not None and read(card) == first and
      os.stat(card).st_mtime_ns == before and len(said) == 1, said)

early = f"{TMP}/early.card.bmp"
r = Run("early", clock=False, DIATOM_CARD_FRAME_MS="60000")
r.pump(30, "READY")
r.game(early, 60)               # a second of its minute
r.close()
check("a game that ends before the minute writes none", not os.path.exists(early), os.listdir(TMP))

if fails:
    print(f"cardframe: {len(fails)} FAILED")
    sys.exit(1)
print("ok: the card frame is written once, past the minute, and never over itself")
