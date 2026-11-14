"""The rumble motor end to end, against the stub core: STUBCORE_RUMBLE asks
for it as an N64 Rumble Pak game under ParallelN64 does (weak, then strong,
two calls each time), and DIATOM_RUMBLE_LOG gives the desktop port a pretend
motor that writes down each level the host tells it.

  - The game's own stop reaches the port: on, off, and on again.
  - MENU (PAUSED) holds the motor at 0, and RESUME gives the game's back.
  - A reset and a state load stop it: the game after them never stops what
    the game before them started.
  - STOP, QUIT and SIGTERM (the power key) while it runs: the last word to
    the port is 0.

What the port does with a level - the GKD Pixel 2's force feedback, whose
stop never reached the motor on ZyperOS 1.0.4 - is test/rumble_test.c's.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/rumble.py
"""
import os, signal, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-rumble-")
ON, OFF = "65535 65535", "0 0"

fails = []
def has_run(lv, run):
    """`run`, line after line, somewhere in the log"""
    return any(lv[i:i + len(run)] == run for i in range(len(lv)))

def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

class Run:
    """One Diatom, its socket, its motor's log and its frame count"""
    def __init__(self, name, rumble, script=""):
        self.sock, self.rlog = f"{TMP}/{name}.sock", f"{TMP}/{name}.rumble"
        self.ilog = f"{TMP}/{name}.input"
        self.p = subprocess.Popen(
            [BIN, "--socket", self.sock, "--save", TMP, "--system", TMP],
            env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                     SDL_AUDIODRIVER="dummy", STUBCORE_RUMBLE=rumble,
                     DIATOM_RUMBLE_LOG=self.rlog, STUBCORE_INPUT_LOG=self.ilog,
                     DIATOM_INPUT_SCRIPT=script),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        for _ in range(100):
            if os.path.exists(self.sock): break
            time.sleep(0.05)
        self.s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.s.connect(self.sock)
        self.s.settimeout(0.05)
        self.buf, self.got, self.cursor = b"", [], 0

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
        """Read until the next line that starts with `what` (past the last one
        found); the motor's log as it is then (the host tells the port before
        it says the line, and nothing moves the motor in between here)"""
        end = time.time() + limit
        while time.time() < end:
            for i in range(self.cursor, len(self.got)):
                if self.got[i].startswith(what):
                    self.cursor = i + 1
                    return self.levels()
            self.pump(0.05)
        return None

    def wait_level(self, level, after=0, limit=30.0):
        """Until the port's last level is `level`, past line `after`"""
        end = time.time() + limit
        while time.time() < end:
            lv = self.levels()
            if len(lv) > after and lv[-1] == level: return lv
            self.pump(0.05)
        return self.levels()

    def levels(self):
        try:
            with open(self.rlog) as f: return [l.strip() for l in f if l.strip()]
        except OSError: return []

    def end(self, how="QUIT"):
        if how == "SIGTERM": self.p.send_signal(signal.SIGTERM)
        else: self.send(how)
        try: self.p.wait(10)
        except subprocess.TimeoutExpired: self.p.kill(); self.p.wait()

# ---- the game's own stop, MENU, a reset, a load, STOP ---------------------
r = Run("main", "30:on,60:off,90:on", "150:menu;154:")
r.pump(0.3)
check("READY", any(l.startswith("READY") for l in r.got), r.got[:2])
r.send(f"RUN\tcore={STUB}")
lv = r.wait_level(ON)
check("the Rumble Pak's on reaches the port: weak, then both full",
      has_run(lv, ["0 65535", ON]), lv)
lv = r.wait_level(OFF, after=len(lv))
check("and its off, weak first, ends on 0 0: the game's stop is the port's",
      has_run(lv, ["0 65535", ON, "65535 0", OFF]), lv)
lv = r.wait_level(ON, after=len(lv))
check("on again at frame 90", lv[-1:] == [ON], lv)
lv = r.wait_for("PAUSED")
check("MENU: by PAUSED the motor is held at 0", lv is not None and lv[-1] == OFF, lv)
n = len(lv or [])
r.send("RESUME")
lv = r.wait_for("RUNNING")
check("RESUME: the game's rumble is back by RUNNING",
      lv is not None and len(lv) > n and lv[-1] == ON, lv)
r.send("RESET")
lv = r.wait_for("RESETDONE")
check("a RESET while it runs: stopped by RESETDONE", lv is not None and lv[-1] == OFF, lv)
# Saved early in the reset game, before its frame 30 turns the motor on
state = f"{TMP}/Game.1.state"
r.send(f"SAVE\tpath={state}")
r.wait_for("SAVED")
lv = r.wait_level(ON, after=len(lv or []))
check("on again after the reset, at its frame 30", lv[-1:] == [ON], lv)
r.send(f"LOAD\tpath={state}")
lv = r.wait_for("LOADED")
check("a state loaded while it runs: stopped by LOADED", lv is not None and lv[-1] == OFF, lv)
lv = r.wait_level(ON, after=len(lv or []))
check("and on again when the loaded game says so", lv[-1:] == [ON], lv)
r.send("STOP")
lv = r.wait_for("EXIT")
check("STOP while it runs: the last word to the port is 0 0, by EXIT",
      lv is not None and lv[-1] == OFF, lv)
r.end("QUIT")

# ---- QUIT and the power key while it runs ---------------------------------
for how in ("QUIT", "SIGTERM"):
    r = Run(how.lower(), "20:on")
    r.pump(0.3)
    r.send(f"RUN\tcore={STUB}")
    lv = r.wait_level(ON)
    r.end(how)
    lv = r.levels()
    check(f"{how} while it runs: the motor was on, and the last word is 0 0",
          ON in lv and lv[-1] == OFF, lv)

print("rumble end to end: %s" % ("all passed" if not fails else f"{len(fails)} FAILED"))
sys.exit(1 if fails else 0)
