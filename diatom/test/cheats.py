"""Cheats end to end, against the stub core: STUBCORE_CHEAT_LOG writes down
each retro_cheat_reset and retro_cheat_set the core is handed, with the frame
it came on.

  - RUN cheats=<file> hands the core every code in the file once the game
    is loaded: a reset, then each code at index 0, 1, 2... enabled, as
    RetroArch hands them (cheat_manager_apply_cheats).
  - Frames that merely run hand the core nothing: no cost per frame.
  - SETCHEATS path=<file> replaces them whole, at once, and says how many.
  - A reset and a state load (the menu's and the quick slot's) hand them
    again: a core may drop its codes with its state.
  - SETCHEATS with an empty path turns every one off: a reset alone.
  - A file that cannot be read is refused and changes nothing.
  - While any cheat is on, achievements stop for the session: a set named
    on RUN never fires, and CHEEVOS says paused=cheats, still after every
    cheat is off again. The next game with none starts them again, and its
    core is reset first, so the last game's codes never reach it.

The launcher's side (the .cht files, the list, the switches) is the
launcher's check, check-cheats.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/cheats.py
"""
import os, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-cheats-")
fails = []

def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def write(name, text):
    path = f"{TMP}/{name}"
    with open(path, "w") as f: f.write(text)
    return path

class Diatom:
    """One Diatom, its socket and the stub core's cheat log"""
    def __init__(self):
        self.sock, self.clog = f"{TMP}/d.sock", f"{TMP}/cheats.log"
        self.p = subprocess.Popen(
            [BIN, "--socket", self.sock, "--save", TMP, "--system", TMP],
            env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                     SDL_AUDIODRIVER="dummy", STUBCORE_CHEAT_LOG=self.clog),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        # The waits are the wall clock's, and only waits: each ends as soon
        # as what it waits for is there
        end = time.time() + 30
        while not os.path.exists(self.sock) and time.time() < end:
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(self.sock)
        self.s.settimeout(0.05)
        self.buf, self.got, self.cursor, self.seen = b"", [], 0, 0

    def send(self, line):
        self.s.sendall((line + "\n").encode())

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            try:
                d = self.s.recv(65536)
                if not d: return
                self.buf += d
            except socket.timeout:
                pass
            except OSError:
                return
            while b"\n" in self.buf:
                l, self.buf = self.buf.split(b"\n", 1)
                self.got.append(l.decode().strip())

    def wait_for(self, what, limit=30.0):
        """The next line that starts with `what`, past the last one found"""
        end = time.time() + limit
        while time.time() < end:
            for i in range(self.cursor, len(self.got)):
                if self.got[i].startswith(what):
                    self.cursor = i + 1
                    return self.got[i]
            self.pump(0.05)
        return None

    def calls(self):
        """The core's cheat calls since the last look, without their frames"""
        try:
            with open(self.clog) as f:
                lines = [l.rstrip("\n").split(" ", 1)[1] for l in f if l.strip()]
        except OSError:
            lines = []
        new, self.seen = lines[self.seen:], len(lines)
        return new

    def logged(self, n, limit=30.0):
        """Until the core's cheat log holds `n` calls past the last look"""
        end = time.time() + limit
        while time.time() < end:
            try:
                with open(self.clog) as f:
                    if sum(1 for l in f if l.strip()) >= self.seen + n: return
            except OSError:
                pass
            self.pump(0.05)

    def ask(self, verb, what, limit=30.0):
        self.send(verb)
        return self.wait_for(what, limit)

    def end(self):
        self.send("QUIT")
        try: self.p.wait(30)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait()

# An achievement that fires on frame 10 (as test/stateplane.py's), so a frame
# loop that still evaluates the set while a cheat is on is seen at once.
setf = write("stub.set", "5001\t0xH0002<d0xH0002_0xH0001=10\n")
two = write("two.txt", "AAAA-BBBB\n\n7E0DBE:63+7E0019:02\n")
one = write("one.txt", "CODE2\n")
state = f"{TMP}/slot.state"

d = Diatom()

# ---- at the start, and nothing while frames run ---------------------------
d.send(f"RUN\tcore={STUB}\tconsole=0\tcheevos={setf}\tcheats={two}")
check("the game starts", d.wait_for("RUNNING") is not None, d.got)
d.logged(3)     # handed just after RUNNING (main.c, diatom_cheats_begin)
check("RUN cheats= hands the core each code once it is loaded",
      d.calls() == ["reset", "set 0 1 AAAA-BBBB", "set 1 1 7E0DBE:63+7E0019:02"],
      open(d.clog).read() if os.path.exists(d.clog) else "no log")
d.pump(1.5)
check("frames that run hand the core nothing", d.calls() == [], d.seen)
check("a set named on RUN never fires while a cheat is on",
      not any(l.startswith("CHEEVO\tid=5001") for l in d.got), d.got)
line = d.ask("CHEEVOS", "CHEEVOS\t") or ""
check("CHEEVOS says achievements are paused for cheats", "\tpaused=cheats" in line, line)

# ---- a switch, a reset, the menu's load, the quick slot's load ------------
line = d.ask(f"SETCHEATS\tpath={one}", "CHEATS") or ""
check("SETCHEATS answers with the count", line == "CHEATS\tcount=1", line)
check("SETCHEATS replaces them whole, at once",
      d.calls() == ["reset", "set 0 1 CODE2"], d.seen)
check("a reset hands them again",
      d.ask("RESET", "RESETDONE") is not None and d.calls() == ["reset", "set 0 1 CODE2"],
      d.seen)
d.send("PAUSE"); d.wait_for("PAUSED")
check("a state is written", d.ask(f"SAVE\tpath={state}", "SAVED") is not None, d.got[-3:])
d.calls()
check("the menu's load hands them again",
      d.ask(f"LOAD\tpath={state}", "LOADED") is not None and
      d.calls() == ["reset", "set 0 1 CODE2"], d.seen)
d.send("RESUME"); d.pump(0.5)
d.calls()
check("the quick slot's load hands them again",
      d.ask(f"LOAD\tpath={state}", "LOADED") is not None and
      d.calls() == ["reset", "set 0 1 CODE2"], d.seen)

# ---- a file that is not there, then every cheat off -----------------------
line = d.ask(f"SETCHEATS\tpath={TMP}/missing.txt", "") or ""
d.pump(0.3)
check("a file that cannot be read is refused",
      any(l.startswith("ERROR\tcode=bad_cheats") for l in d.got[-3:]), d.got[-3:])
check("and the codes stay as they were", d.calls() == [], d.seen)
line = d.ask("SETCHEATS\tpath=", "CHEATS") or ""
check("an empty path turns every one off", line == "CHEATS\tcount=0", line)
check("with a reset alone", d.calls() == ["reset"], d.seen)
line = d.ask("CHEEVOS", "CHEEVOS\t") or ""
check("achievements stay paused for the session", "\tpaused=cheats" in line, line)

# ---- the next game, with none ---------------------------------------------
# The game ends with one on, so its core still holds a code.
d.ask(f"SETCHEATS\tpath={one}", "CHEATS"); d.calls()
d.send("STOP"); d.wait_for("EXIT")
d.send(f"RUN\tcore={STUB}\tconsole=0\tcheevos={setf}")
d.wait_for("RUNNING")
check("the next game with no cheats has its achievements back",
      d.wait_for("CHEEVO\tid=5001") is not None, d.got[-5:])
line = d.ask("CHEEVOS", "CHEEVOS\t") or ""
check("and CHEEVOS does not say paused", "paused=" not in line, line)
check("its core is reset once, so the last game's codes never reach it",
      d.calls() == ["reset"], d.seen)
d.send("STOP"); d.wait_for("EXIT")

# ---- and the game after it: nothing to reset ------------------------------
d.send(f"RUN\tcore={STUB}\tconsole=0")
d.wait_for("RUNNING"); d.pump(0.5)
check("a core with no codes is not touched", d.calls() == [], d.seen)
d.send("STOP"); d.wait_for("EXIT")
d.end()

print(f"cheats: {'FAIL' if fails else 'ok'} ({len(fails)} failed)")
sys.exit(1 if fails else 0)
