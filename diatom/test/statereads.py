"""States a newer build of a core reads from an older one (src/save.c,
STATE_READS), end to end against the stub core named and versioned as
DSperate (STUBCORE_NAME, STUBCORE_VERSION):

  - DSperate v3.4.1 resumes a state DSperate v3.0.5 wrote, from its frame,
    and the log says which version wrote it
  - it refuses one another version wrote (v3.3.0), and the game starts from
    its beginning, as for any state that will not load
  - its own version's state resumes as always
  - one way only: DSperate v3.0.5 refuses a state v3.4.1 wrote
  - those two versions only: DSperate v3.3.0 refuses a v3.0.5 state
  - that core only: another core with the same two versions refuses it
  - Snes9x 2010 fe690dd and Genesis Plus GX b9c2316 resume the states
    7db129b and b7e79b3 wrote, one way only, as named and versioned there

The states are version 1 files written here (a 112-byte header, then the
core's bytes as they are), as test/states.py writes one.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-states does. SDL's dummy drivers: no window, no sound.

    python3 test/statereads.py
"""
import os, shutil, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-statereads-")
ROM = f"{TMP}/A Game.stub"
with open(ROM, "wb") as f:
    f.write(b"STUB" + bytes(range(60)))

fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def state(core, version, frame):
    """A version 1 state of the stub's 4 bytes (its frame) by `core` `version`"""
    path = f"{TMP}/{core}-{version}-{frame}.state"
    with open(path, "wb") as f:
        f.write(b"DIATOMST" + struct.pack("<II", 1, 4) + core.encode().ljust(64, b"\0")
                + version.encode().ljust(32, b"\0") + struct.pack("<I", frame))
    return path

class Diatom:
    """One Diatom with the stub core reporting `core` `version`"""
    def __init__(self, core, version):
        self.log_path, self.err_path = f"{TMP}/{core}-{version}.log", f"{TMP}/{core}-{version}.err"
        self.sock = f"{TMP}/d.sock"
        if os.path.exists(self.sock): os.unlink(self.sock)
        self.err = open(self.err_path, "w")
        self.p = subprocess.Popen([BIN, "--socket", self.sock, "--save", TMP, "--system", TMP],
                                  env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                                           SDL_AUDIODRIVER="dummy", STUBCORE_NAME=core,
                                           STUBCORE_VERSION=version, STUBCORE_INPUT_LOG=self.log_path),
                                  stdout=self.err, stderr=self.err)
        # The waits are the wall clock's, and only waits: each ends as soon
        # as what it waits for is there (2 to 4 s until 2026-11-24)
        end = time.time() + 30
        while not os.path.exists(self.sock) and time.time() < end:
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(self.sock)
        self.buf = b""
        self.drain(30.0, "READY")

    def drain(self, timeout, until):
        end = time.time() + timeout
        self.s.settimeout(0.2)
        while time.time() < end:
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                if l.decode().startswith(until): return l.decode()
            try:
                d = self.s.recv(65536)
            except socket.timeout:
                continue
            if not d: break
            self.buf += d
        return None

    def send(self, m):
        self.s.sendall((m + "\n").encode())

    def frames(self):
        try:
            with open(self.log_path) as f: return [int(l.split()[0]) for l in f if l.strip()]
        except OSError:
            return []

    def log(self):
        with open(self.err_path, errors="replace") as f: return f.read()

    def resume(self, path):
        """The game resumed from `path` for a moment, 30 of its frames: the
        first frame it ran. Half a second of this script's time until
        2026-11-24, in which a busy machine could run none."""
        before = len(self.frames())
        self.send(f"RUN\tcore={STUB}\trom={ROM}\tresume={path}")
        ok = self.drain(30.0, "RUNNING") is not None
        end = time.time() + 30
        while ok and len(self.frames()) - before < 30 and time.time() < end:
            time.sleep(0.02)
        f = self.frames()[before:]
        self.send("STOP")
        self.drain(30.0, "EXIT")
        return f[0] if ok and f else None

    def close(self):
        self.send("QUIT")
        try: self.p.wait(30)
        except subprocess.TimeoutExpired: self.p.kill()
        self.err.close()

def lines(d, path):
    return [l for l in d.log().splitlines() if path in l]

try:
    d = Diatom("DSperate", "v3.4.1")
    try:
        old = state("DSperate", "v3.0.5", 5000)
        first = d.resume(old)
        check("DSperate v3.4.1 resumes a state v3.0.5 wrote, from its frame 5000",
              first is not None and 5000 <= first <= 5002, first)
        check("and says which version wrote it",
              f"state: {old} was written by DSperate v3.0.5, which v3.4.1 reads" in d.log()
              and f"state: restored {old}" in d.log(), lines(d, old))

        other = state("DSperate", "v3.3.0", 6000)
        first = d.resume(other)
        check("it refuses one v3.3.0 wrote, and the game starts from its beginning",
              first is not None and first <= 2
              and f"state: {other} was written by DSperate v3.3.0, this is DSperate v3.4.1" in d.log(),
              (first, lines(d, other)))

        own = state("DSperate", "v3.4.1", 7000)
        first = d.resume(own)
        check("its own version's state resumes as always, with no word on its version",
              first is not None and 7000 <= first <= 7002
              and not any("which v3.4.1 reads" in l for l in lines(d, own)), (first, lines(d, own)))
    finally:
        d.close()

    d = Diatom("DSperate", "v3.0.5")
    try:
        newer = state("DSperate", "v3.4.1", 8000)
        first = d.resume(newer)
        check("one way only: DSperate v3.0.5 refuses a state v3.4.1 wrote",
              first is not None and first <= 2
              and f"state: {newer} was written by DSperate v3.4.1, this is DSperate v3.0.5" in d.log(),
              (first, lines(d, newer)))
    finally:
        d.close()

    d = Diatom("DSperate", "v3.3.0")
    try:
        old = state("DSperate", "v3.0.5", 8500)
        first = d.resume(old)
        check("those two versions only: DSperate v3.3.0 refuses a state v3.0.5 wrote",
              first is not None and first <= 2
              and f"state: {old} was written by DSperate v3.0.5, this is DSperate v3.3.0" in d.log(),
              (first, lines(d, old)))
    finally:
        d.close()

    # The two rows after DSperate's, each core under its own name: the newer
    # build resumes the older one's state, the older refuses the newer's
    frame = 10000
    for core, older, newer in (("Snes9x 2010", "1.52.4 7db129b", "1.52.4 fe690dd"),
                               ("Genesis Plus GX", "v1.7.4 b7e79b3", "v1.7.4 b9c2316")):
        d = Diatom(core, newer)
        try:
            old = state(core, older, frame)
            first = d.resume(old)
            check(f"{core} {newer} resumes a state {older} wrote, from its frame {frame}",
                  first is not None and frame <= first <= frame + 2
                  and f"state: {old} was written by {core} {older}, which {newer} reads" in d.log(),
                  (first, lines(d, old)))
        finally:
            d.close()
        d = Diatom(core, older)
        try:
            new = state(core, newer, frame + 500)
            first = d.resume(new)
            check(f"one way only: {core} {older} refuses a state {newer} wrote",
                  first is not None and first <= 2
                  and f"state: {new} was written by {core} {newer}, this is {core} {older}" in d.log(),
                  (first, lines(d, new)))
        finally:
            d.close()
        frame += 1000

    d = Diatom("Another", "v3.4.1")
    try:
        another = state("Another", "v3.0.5", 9000)
        first = d.resume(another)
        check("that core only: another core at v3.4.1 refuses its own v3.0.5 state",
              first is not None and first <= 2
              and f"state: {another} was written by Another v3.0.5, this is Another v3.4.1" in d.log(),
              (first, lines(d, another)))
    finally:
        d.close()
finally:
    shutil.rmtree(TMP, ignore_errors=True)

if fails:
    print(f"statereads: {len(fails)} failed")
    sys.exit(1)
print("ok: DSperate v3.4.1 reads v3.0.5 states, and Snes9x 2010 and Genesis Plus GX their older"
      " builds' states, one way, that core only")
