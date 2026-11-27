"""How a core's life ends, end to end against the stub core:

  - A core that runs one game per load is opened afresh for each game. PrBoom
    keeps its menus and episodes in statics that its next game does not reset:
    in a resident Diatom, The Ultimate Doom after Doom II had two Quit Game
    rows, and a mod's episodes stayed in the next game's menu (1.0.5). The
    stub, named PrBoom (STUBCORE_NAME), counts the games it loaded since it
    was opened (STUBCORE_INIT_LOG): 1 at each of two games. Named otherwise
    it is kept open and counts 1, then 2.
  - A core ZyperOS did not ship is opened afresh for each game too (#304: a
    freeintv added by hand showed its last game in place of the next one's).
    Shipped is what the card's index (ZyperOS/.files) says: the core's path in
    it, at the size the file has. The stub is copied into a made-up ZyperOS
    folder under three names (one the index lacks, one it lists at another
    size, one it lists as it is), and each runs two games, with and without the
    premap, counting Diatom's dlopen and retro_init lines. The listed one must
    cost one of each, as before. With no index, nothing is closed.
  - A core that ends its own game (RETRO_ENVIRONMENT_SHUTDOWN: PrBoom's Quit
    Game, ScummVM's Quit) ends the session as the player quitting would: EXIT
    reason=user with no STOP sent, its state on exit written, so the shelf
    comes back. Ignored before 1.0.6, it left the last frame on the screen
    for good, which players took for a crash.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/coreend.py
"""
import os, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-coreend-")
fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

class Run:
    def __init__(self, name, cores=None, **env):
        self.sock = f"{TMP}/{name}.sock"
        self.err = f"{TMP}/{name}.err"
        self.p = subprocess.Popen(
            [BIN, "--socket", self.sock, "--save", TMP, "--system", TMP] +
            (["--cores", cores] if cores else []),
            env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                     SDL_AUDIODRIVER="dummy", **env),
            stdout=subprocess.DEVNULL, stderr=open(self.err, "w"))
        for _ in range(100):
            if os.path.exists(self.sock): break
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

    def close(self):
        self.send("QUIT")
        try: self.p.wait(5)
        except subprocess.TimeoutExpired: self.p.kill()

def inits(path):
    try:
        with open(path) as f: return [l.strip() for l in f]
    except OSError: return []

# One game per load: PrBoom, two games in one Diatom
for name, want in (("PrBoom", ["loads=1", "loads=1"]), ("diatom-stub", ["loads=1", "loads=2"])):
    log = f"{TMP}/{name}.loads"
    r = Run(name, STUBCORE_NAME=name, STUBCORE_INIT_LOG=log)
    r.pump(0.5, "READY")
    for _ in range(2):
        r.send(f"RUN\tcore={STUB}")
        r.pump(10, "RUNNING")
        r.pump(0.3)
        r.send("STOP")
        r.pump(10, "EXIT")
    r.close()
    if name == "PrBoom":
        check("a core named PrBoom is opened afresh for each game", inits(log) == want, inits(log))
    else:
        check("any other core stays open between games", inits(log) == want, inits(log))

# A core ZyperOS does not ship is opened afresh for each game (#304). freeintv,
# added by hand to the card's cores folder, keeps the last game in statics: its
# second game, with no resume point, showed the first one frozen under "LOAD
# CART OKAY / INTELLIVISION HALTED". ZyperOS's own cores stay open: that is
# the warm launch. "Shipped" is what the card's index says (ZyperOS/.files,
# which mk/payload.sh and System Update write): the core's path in it, at the
# size it has on the card. The stub is copied under each name below, in a
# ZyperOS folder of its own, so a name here is all that tells them apart.
import hashlib, re

def make_card(name, cores, listed):
    """TMP/<name>/ZyperOS with the stub copied to cores/<core>_libretro.so for
    each of `cores`, and an index naming `listed` (core -> the size the index
    says it has), or no index at all when `listed` is None."""
    z = f"{TMP}/{name}/ZyperOS"
    os.makedirs(f"{z}/cores")
    sha = hashlib.sha256(open(STUB, "rb").read()).hexdigest()
    for c in cores:
        shutil.copy(STUB, f"{z}/cores/{c}_libretro.so")
    if listed is not None:
        with open(f"{z}/.files", "w") as f:
            f.write(f"{'0' * 64} 123 launch.sh\n")
            for c, size in listed.items():
                f.write(f"{sha} {size} cores/{c}_libretro.so\n")
            f.write(f"{'1' * 64} 456 res/web/index.html\n")
    return z

def two_games(name, core, premap=False):
    """Two games in a row on `core` in one Diatom: what the stub says it has
    loaded since it was opened, and how often Diatom opened and initialized it.
    With `premap`, as the launcher starts it: the cores folder mapped at boot."""
    log = f"{TMP}/{name}.loads"
    r = Run(name, cores=os.path.dirname(core) if premap else None, STUBCORE_INIT_LOG=log)
    r.pump(0.5, "READY")
    if premap:
        r.send("PREMAP")
        r.pump(1.5)         # the thread maps the folder while this waits
    for _ in range(2):
        r.send(f"RUN\tcore={core}")
        r.pump(10, "RUNNING")
        r.pump(0.3)
        r.send("STOP")
        r.pump(10, "EXIT")
    r.close()
    base = re.escape(os.path.basename(core))
    err = open(r.err, errors="replace").read().splitlines()
    n_open = sum(bool(re.match(rf"diatom: dlopen +{base} +[0-9.]+ ms$", l)) for l in err)
    n_init = sum(bool(re.match(rf"diatom: retro_init +{base} +[0-9.]+ ms$", l)) for l in err)
    # and each game's load timed, every time: how long a core that unpacks
    # its own zip took to (DSperate's .dsperate, src/dscache.h)
    n_load = sum(bool(re.match(rf"diatom: retro_load_game +{base} +[0-9.]+ ms$", l)) for l in err)
    check(f"{name}: both games' loads are timed in the log", n_load == 2, n_load)
    return inits(log), n_open, n_init

SIZE = os.path.getsize(STUB)
z = make_card("card", ["freeintv", "fceumm", "swapped"],
              {"fceumm": SIZE, "swapped": SIZE + 1})
for what, core, want in (
        ("a core the card's index does not list (freeintv, added by hand)", "freeintv", (["loads=1", "loads=1"], 2, 2)),
        ("a shipped core's file replaced by another build (a different size)", "swapped", (["loads=1", "loads=1"], 2, 2)),
        ("a core the card's index lists, at its size (fceumm)", "fceumm", (["loads=1", "loads=2"], 1, 1))):
    got = two_games(core, f"{z}/cores/{core}_libretro.so")
    verb = "stays open between games, one dlopen and one retro_init" if want[1] == 1 else \
           "is closed and opened afresh for each game, two dlopens and two retro_inits"
    check(f"{what} {verb}", got == want, got)

# The same at boot, the launcher's way: the folder premapped before the first
# game. A shipped core is opened once, by the premap, and its first game is warm;
# an added one is opened by the premap too, and then again for its second game
for core, want in (("fceumm", (["loads=1", "loads=2"], 1, 1)),
                   ("freeintv", (["loads=1", "loads=1"], 2, 2))):
    got = two_games(f"premap-{core}", f"{z}/cores/{core}_libretro.so", premap=True)
    check(f"premapped, {core} is opened {want[1]} time(s) over two games, "
          f"initialized {want[2]}", got == want, got)

# No index at all (a card from before System Update, a desktop build): nothing
# says what is shipped, so nothing changes, and every core stays open
z = make_card("bare", ["freeintv"], None)
got = two_games("bare", f"{z}/cores/freeintv_libretro.so")
check("with no index on the card, a core stays open between games, as before",
      got == (["loads=1", "loads=2"], 1, 1), got)

# The core's own quit
state = f"{TMP}/quit.state"
r = Run("quit", STUBCORE_SHUTDOWN_AT="90")
r.pump(0.5, "READY")
r.send(f"RUN\tcore={STUB}\texit_state={state}")
r.pump(10, "RUNNING")
r.pump(15, "EXIT")
ended = [l for l in r.got if l.startswith("EXIT")]
check("a core that shuts itself down ends its game, with no STOP sent",
      ended == ["EXIT\treason=user"], r.got[-6:])
check("and its state on exit is written, as a quit's is", os.path.exists(state), os.listdir(TMP))
r.send(f"RUN\tcore={STUB}")
r.pump(10, "RUNNING")
check("and the next game runs in the same Diatom", "RUNNING" in r.got[r.got.index(ended[0]) if ended else 0:], r.got[-4:])
r.pump(0.5)
r.send("STOP")
r.pump(10, "EXIT")
r.close()

if fails:
    print(f"coreend: {len(fails)} FAILED")
    sys.exit(1)
print("ok: PrBoom opened afresh for each game; a core's own quit ends its game")
