"""SELECT's hotkeys end to end, against the stub core, with the pad driven by
script (DIATOM_INPUT_SCRIPT) and what the core read written down by the stub
(STUBCORE_INPUT_LOG): src/chord.h's rule as a game meets it, and the quick
slot's round trip as the launcher drives it.

  - A tap of SELECT reaches the game as it is let go, as long as it was held.
  - SELECT+R1 and SELECT+L1 say HOTKEY key=save and key=load, and the game
    never sees their SELECT or their shoulder.
  - SELECT then A: the game has SELECT on A's frame.
  - SELECT+R2 and SELECT+L2 step the speed, 1.5x, 2x, back, and answer at
    1x again when it is already there.
  - MENU+R1 steps nothing any more: R1 under MENU is the game's.
  - A SAVE sent while the game runs (the launcher's answer to the hotkey) is
    written on Diatom's thread, answered SAVED, with its picture beside it,
    and a LOAD of it is answered LOADED.
  - SETSTICK mode=plain|off|dpad|toggle|toggle-stick turns the d-pad into
    the left stick or back while a game runs, and an unknown mode is an ERROR
    that leaves the stick as it was. Plain has no SELECT rule and no R2 turn,
    and keeps the d-pad on the joypad too: the d-pad is both.
  - SETMAP select:start does not touch SELECT's chords: SELECT+R1 still says
    HOTKEY key=save, and the core never reads START.
  - RUN hotkeys= gives SELECT other keys: SELECT+Y writes a screenshot PNG
    where RUN shots= says and answers SHOT path=, SELECT+X says HOTKEY
    key=shader, SELECT+B pauses with PAUSED for=reset, and SELECT+R1 is the
    game's when no hotkey is on R1; SETHOTKEYS changes them while it runs.
  - RUN menukeys= gives MENU two chords in a game: MENU+L1 a screenshot,
    MENU+L2 the lock's pause (PAUSED for=lock), the game never reading
    their L1 or L2 and MENU opening no menu after them; without it MENU+L1
    is the game's L1, as ever.
  - RUN stick=toggle (the PSP's) with the map's r2:stick: R2 turns the
    d-pad into the left stick and back, says STICK each time, and never
    reaches the game; with no such pair R2 is the game's and turns nothing.
  - A map's stickhold (Stick While Held): while that button is held the
    d-pad is on its other side, and STICK is not said; stick=dpad-pad starts
    the N64's stick on the d-pad's side for such a button to turn.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/hotkeys.py
"""
import os, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-hotkeys-")
SOCK, LOG = f"{TMP}/d.sock", f"{TMP}/input.log"
STATE, PIC = f"{TMP}/Game.1.state", f"{TMP}/Game.1.bmp"
UNDO = f"{TMP}/Game.undo.state"

# The pad, a poll at a time (a poll is a frame at 1x). Each step holds its
# buttons from that poll on.
SCRIPT = ";".join([
    "60:select", "66:",                                   # a tap, 6 polls
    "100:select", "104:select+r1", "108:select", "112:",   # SELECT, then R1
    "140:select+l1", "144:",                              # both in one poll
    "170:select", "174:select+a", "180:",                 # SELECT, then A
    "200:menu", "203:menu+r1", "206:menu", "209:",         # MENU+R1: nothing
    "240:select", "243:select+r2", "246:select", "249:select+r2", "252:select",
    "255:select+l2", "258:select", "261:select+l2", "264:select", "267:select+l2",
    "270:",
])

# The stub reads RETRO_DEVICE_ID_JOYPAD_*: SELECT 2, A 8, L 10, R 11, L2 12, R2 13
SEL, A, L1, R1, L2, R2 = 1 << 2, 1 << 8, 1 << 10, 1 << 11, 1 << 12, 1 << 13

fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

p = subprocess.Popen([BIN, "--socket", SOCK, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy", DIATOM_INPUT_SCRIPT=SCRIPT,
                              STUBCORE_INPUT_LOG=LOG),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK): break
    time.sleep(0.05)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(SOCK)
s.settimeout(0.05)
buf, got = b"", []

def pump(seconds):
    """Read for `seconds`, answering the hotkeys as the launcher does"""
    global buf
    end = time.time() + seconds
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d: return
            buf += d
        except socket.timeout:
            pass
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            l = l.decode().strip()
            got.append(l)
            if l == "HOTKEY\tkey=save":
                s.sendall(f"SAVE\tpath={STATE}\tpreview={PIC}\n".encode())
            elif l == "HOTKEY\tkey=load":
                # As the launcher does (on_quick): the game as it is, for
                # Undo Load, then the slot over it, back to back
                s.sendall(f"SAVE\tpath={UNDO}\nLOAD\tpath={STATE}\n".encode())
            elif l == "PAUSED" or l.startswith("PAUSED\t"):
                # MENU let go opens the menu, or a hotkey's pause (Reset
                # Game's question, Quit): close it
                s.sendall(b"RESUME\n")

def frames_logged(path):
    try:
        with open(path) as f: return sum(1 for _ in f)
    except OSError: return 0

def pump_until(path, n, limit=90.0):
    """Answer the hotkeys until the core has read the pad `n` times (the
    script counts its polls, one a frame), however slowly this machine runs
    it: a fixed wait ended before the script did on a CI runner"""
    end = time.time() + limit
    while frames_logged(path) < n and time.time() < end:
        pump(0.2)
    pump(0.5)

pump(0.5)
check("READY says proto=14", any(l.startswith("READY\tproto=14") for l in got), got[:2])
s.sendall(f"RUN\tcore={STUB}\n".encode())
pump_until(LOG, 300)
s.sendall(b"STOP\n")
pump(1.5)
s.sendall(b"QUIT\n")
try: p.wait(5)
except subprocess.TimeoutExpired: p.kill()

check("SELECT+R1 says HOTKEY key=save once", got.count("HOTKEY\tkey=save") == 1, got)
check("SELECT+L1, pressed in one poll, says HOTKEY key=load once",
      got.count("HOTKEY\tkey=load") == 1, got)
check("a SAVE while the game runs is answered SAVED", f"SAVED\tpath={STATE}" in got, got)
check("and its picture is written beside the state",
      os.path.exists(STATE) and os.path.exists(PIC) and open(PIC, "rb").read(2) == b"BM",
      os.listdir(TMP))
check("a LOAD of it while the game runs is answered LOADED", f"LOADED\tpath={STATE}" in got, got)
check("Undo Load's SAVE sent just before it is written and answered first",
      os.path.exists(UNDO) and f"SAVED\tpath={UNDO}" in got and
      got.index(f"SAVED\tpath={UNDO}") < got.index(f"LOADED\tpath={STATE}"), got)
speeds = [l.split("=", 1)[1] for l in got if l.startswith("SPEED\tx=")]
check("SELECT+R2, R2, L2, L2, L2 step 1.5x, 2x, 1.5x, 1x and answer 1x at the end",
      speeds == ["1.5", "2", "1.5", "1", "1"], speeds)

frames = []
for line in open(LOG):
    f, b = line.split()[:2]       # the frame, port 0 (then ports 1 to 3)
    frames.append(int(b))
check("the core read the pad", len(frames) > 250, len(frames))
check("no chord's shoulder ever reached the game",
      not any(b & (L1 | L2 | R2) for b in frames), [i for i, b in enumerate(frames) if b & (L1 | L2 | R2)][:5])
# SELECT's presses as the game saw them: (first frame, length)
runs, start = [], None
for i, b in enumerate(frames + [0]):
    if b & SEL and start is None: start = i
    if not b & SEL and start is not None: runs.append((start, i - start)); start = None
check("the game saw two presses of SELECT: the tap and the one with A", len(runs) == 2, runs)
if len(runs) == 2:
    check("the tap as long as it was held, 6 frames (5 to 7)", 5 <= runs[0][1] <= 7, runs)
    first_a = next((i for i, b in enumerate(frames) if b & A), None)
    check("SELECT then A: SELECT on A's first frame", runs[1][0] == first_a, (runs, first_a))
r1_frames = [i for i, b in enumerate(frames) if b & R1]
check("R1 reached the game only under MENU, 3 frames in a row: MENU+R1 is the game's R1 now",
      len(r1_frames) == 3 and r1_frames[-1] - r1_frames[0] == 2, r1_frames)
check("and MENU+R1 stepped no speed", len(speeds) == 5, speeds)

# The N64, with ZyperOS's map|N64: SELECT holds the core's R2, which makes
# the face buttons Mupen64Plus-Next's C buttons, and the R2 key gives nothing.
# SELECT then X: the core reads R2 and X from X's first frame, C up, and
# never R2 before it (SELECT waits). R2 alone: nothing. X alone: X.
N64_SCRIPT = ";".join(["60:select", "64:select+x", "70:select", "72:",
                       "100:r2", "106:", "130:x", "134:",
                       "150:a", "154:", "170:select", "172:select+a", "178:select", "180:"])
X = 1 << 9
RB, RY = 1 << 0, 1 << 1
SOCK2, LOG2 = f"{TMP}/n64.sock", f"{TMP}/n64.log"
p = subprocess.Popen([BIN, "--socket", SOCK2, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy", DIATOM_INPUT_SCRIPT=N64_SCRIPT,
                              STUBCORE_INPUT_LOG=LOG2),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK2): break
    time.sleep(0.05)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(SOCK2)
s.settimeout(0.05)
buf, got = b"", []
pump(0.3)
s.sendall(f"RUN\tcore={STUB}\nSETMAP\tmap=select:r2,r2:none,a:b,b:y,y:none,select+a:a,select+b:b,select+y:y\n".encode())
pump_until(LOG2, 200)
s.sendall(b"STOP\n")
pump(1.0)
s.sendall(b"QUIT\n")
try: p.wait(5)
except subprocess.TimeoutExpired: p.kill()
check("the N64's map is taken, no ERROR for it", not any(l.startswith("ERROR") for l in got), got)
frames = [int(line.split()[1]) for line in open(LOG2)]
x_on = [i for i, b in enumerate(frames) if b & X]
r2_on = [i for i, b in enumerate(frames) if b & R2]
first_group = [i for i in x_on if i < (x_on[0] + 20)] if x_on else []
check("N64: SELECT then X is R2 and X from X's first frame, C up",
      bool(first_group) and all(frames[i] & R2 for i in first_group) and
      r2_on and r2_on[0] == first_group[0], (first_group[:3], r2_on[:3]))
check("N64: R2 only while SELECT is the game's, never for the R2 key",
      bool(r2_on) and max(i for i in r2_on if i < 145) < first_group[-1] + 8, r2_on)
check("N64: X alone is X, no C button", any(not frames[i] & R2 for i in x_on[len(first_group):]) and
      len(x_on) > len(first_group), x_on)
# The A cap: alone, the RetroPad's B (the N64's A), never its A; under SELECT,
# the RetroPad's A with R2 (C right), never its B
a_alone = [i for i in range(145, 160) if i < len(frames) and frames[i] & RB]
check("N64: the A cap alone is the N64's A (the RetroPad's B)",
      bool(a_alone) and not any(frames[i] & A for i in range(145, 160) if i < len(frames)), a_alone)
c_right = [i for i in range(168, 182) if i < len(frames) and frames[i] & A]
check("N64: SELECT+A is C right (the RetroPad's A under R2), not the N64's A",
      bool(c_right) and all(frames[i] & R2 for i in c_right) and
      not any(frames[i] & RB for i in range(168, 182) if i < len(frames)), c_right)

# The PSP's d-pad (RUN stick=toggle): it starts as the d-pad, R2 turns it
# into the left stick and says STICK on=1, R2 again turns it back and says
# on=0, and the game never reads R2. UP is the game's d-pad before the first
# R2 and after the second, and never between them.
PSP_SCRIPT = ";".join(["40:up", "46:", "60:r2", "64:", "80:up", "86:",
                       "100:r2", "104:", "120:up", "126:"])
UP = 1 << 4
SOCK3, LOG3 = f"{TMP}/psp.sock", f"{TMP}/psp.log"
p = subprocess.Popen([BIN, "--socket", SOCK3, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy", DIATOM_INPUT_SCRIPT=PSP_SCRIPT,
                              STUBCORE_INPUT_LOG=LOG3),
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for _ in range(100):
    if os.path.exists(SOCK3): break
    time.sleep(0.05)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(SOCK3)
s.settimeout(0.05)
buf, got = b"", []
pump(0.3)
s.sendall(f"RUN\tcore={STUB}\tstick=toggle\nSETMAP\tmap=r2:stick\n".encode())
pump_until(LOG3, 150)
s.sendall(b"STOP\n")
pump(1.0)
s.sendall(b"QUIT\n")
try: p.wait(5)
except subprocess.TimeoutExpired: p.kill()
frames = [int(line.split()[1]) for line in open(LOG3)]
up = [i for i, b in enumerate(frames) if b & UP]
sticks = [l for l in got if l.startswith("STICK\t")]
check("PSP: R2 says STICK on=1, then on=0", sticks == ["STICK\ton=1", "STICK\ton=0"], sticks)
check("PSP: the game never reads R2", not any(b & R2 for b in frames), [i for i, b in enumerate(frames) if b & R2][:5])
check("PSP: UP is the d-pad's before the first R2", any(38 <= i <= 50 for i in up), up)
check("PSP: and not while the d-pad is the stick", not any(66 <= i <= 98 for i in up), up)
check("PSP: and the d-pad's again after the second R2", any(118 <= i <= 130 for i in up), up)

# SETSTICK, live. Each case is its own run: RUN with no stick, SETSTICK in
# the same send (before the script's first poll at 40), then a script. The
# core reads the left stick's x as the 6th column (STUBCORE_INPUT_ANALOG).
RIGHT, START = 1 << 7, 1 << 3
FULL = 0x7fff

def live_run(tag, script, lines_out, nframes, run_extra=""):
    sock, log = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.log"
    global s, buf, got, p
    p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                         env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                                  SDL_AUDIODRIVER="dummy", DIATOM_INPUT_SCRIPT=script,
                                  STUBCORE_INPUT_LOG=log, STUBCORE_INPUT_ANALOG="1"),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.settimeout(0.05)
    buf, got = b"", []
    pump(0.3)
    s.sendall(f"RUN\tcore={STUB}{run_extra}\n{lines_out}".encode())
    pump_until(log, nframes)
    s.sendall(b"STOP\n")
    pump(1.0)
    s.sendall(b"QUIT\n")
    try: p.wait(5)
    except subprocess.TimeoutExpired: p.kill()
    rows = [list(map(int, l.split())) for l in open(log)]
    return [r[1] for r in rows], [r[5] for r in rows]

HOLD_RIGHT = "40:right;46:"
bits, ax = live_run("plain", HOLD_RIGHT + ";60:select+right;66:;80:r2;86:", "SETSTICK\tmode=plain\n", 100)
check("SETSTICK plain answers STICK on=1", "STICK\ton=1" in got, got)
check("plain: a held RIGHT is the stick at full, x = 0x7fff", any(ax[i] == FULL for i in range(41, 46)), ax[38:48])
check("plain: and the joypad's RIGHT still, so a game that keeps its pad digital moves",
      any(bits[i] & RIGHT for i in range(41, 46)), bits[38:48])
check("plain: SELECT then RIGHT is still the stick (no SELECT rule), and the joypad's RIGHT",
      any(ax[i] == FULL for i in range(61, 66)) and any(bits[i] & RIGHT for i in range(61, 66)),
      (ax[58:68], bits[58:68]))
check("plain: R2 reaches the game, and turns nothing",
      any(bits[i] & R2 for i in range(80, 86)) and not any(l.startswith("STICK") and l != "STICK\ton=1" for l in got), got)

bits, ax = live_run("runplain", HOLD_RIGHT, "", 60, "\tstick=plain")
check("RUN stick=plain: a held RIGHT is the stick, and the joypad's RIGHT",
      any(ax[i] == FULL for i in range(41, 46)) and any(bits[i] & RIGHT for i in range(41, 46)),
      (ax[38:48], bits[38:48]))

bits, ax = live_run("off", HOLD_RIGHT, "SETSTICK\tmode=plain\nSETSTICK\tmode=off\n", 60)
check("SETSTICK off answers STICK on=0 last", [l for l in got if l.startswith("STICK")][-1:] == ["STICK\ton=0"], got)
check("off: RIGHT is the joypad's again", any(bits[i] & RIGHT for i in range(41, 46)), bits[38:48])
check("off: and the stick is centered", not any(ax[i] for i in range(38, 48)), ax[38:48])

# ...and the modes after it have the d-pad the stick alone, as before
bits, ax = live_run("plainthendpad", HOLD_RIGHT, "SETSTICK\tmode=plain\nSETSTICK\tmode=dpad\n", 60)
check("plain then dpad: RIGHT is the stick alone again, no joypad RIGHT",
      any(ax[i] == FULL for i in range(41, 46)) and not any(bits[i] & RIGHT for i in range(38, 48)),
      (ax[38:48], bits[38:48]))
bits, ax = live_run("plainthentoggle", HOLD_RIGHT, "SETSTICK\tmode=plain\nSETSTICK\tmode=toggle-stick\n", 60)
check("plain then toggle-stick: RIGHT is the stick alone, no joypad RIGHT",
      any(ax[i] == FULL for i in range(41, 46)) and not any(bits[i] & RIGHT for i in range(38, 48)),
      (ax[38:48], bits[38:48]))

bits, ax = live_run("dpad", HOLD_RIGHT + ";60:select;64:select+right;70:select;72:", "SETSTICK\tmode=dpad\n", 90)
check("SETSTICK dpad answers STICK on=1", "STICK\ton=1" in got, got)
check("dpad: RIGHT is the stick", any(ax[i] == FULL for i in range(41, 46)) and
      not any(bits[i] & RIGHT for i in range(38, 48)), (ax[38:48], bits[38:48]))
check("dpad: SELECT then RIGHT is the joypad's RIGHT (the SELECT rule)",
      any(bits[i] & RIGHT for i in range(64, 70)), bits[60:72])

bits, ax = live_run("toggle", "40:up;46:", "SETSTICK\tmode=toggle\n", 60)
check("SETSTICK toggle answers STICK on=0", "STICK\ton=0" in got, got)
check("toggle: the d-pad is the joypad's until R2", any(bits[i] & UP for i in range(41, 46)), bits[38:48])
bits, ax = live_run("togglestick", "40:right;46:", "SETSTICK\tmode=toggle-stick\n", 60)
check("SETSTICK toggle-stick answers STICK on=1", "STICK\ton=1" in got, got)
check("toggle-stick: RIGHT is the stick", any(ax[i] == FULL for i in range(41, 46)), ax[38:48])

# Stick While Held (a map's stickhold): under stick=toggle, L2 held is the
# stick and let go the d-pad again, with no STICK said for it; the game never
# reads L2. And toggle with no stick pair: R2 is the game's, and turns nothing.
bits, ax = live_run("hold", "40:right;46:;60:l2;62:l2+right;68:l2;70:;80:right;86:",
                    "SETSTICK\tmode=toggle\nSETMAP\tmap=l2:stickhold\n", 100)
check("stickhold: RIGHT before L2 is the joypad's", any(bits[i] & RIGHT for i in range(41, 46)), bits[38:48])
check("stickhold: RIGHT under L2 is the stick, not the joypad's",
      any(ax[i] == FULL for i in range(63, 68)) and not any(bits[i] & RIGHT for i in range(63, 68)),
      (ax[60:70], bits[60:70]))
check("stickhold: RIGHT after L2 is let go is the joypad's again",
      any(bits[i] & RIGHT for i in range(81, 86)) and not any(ax[i] for i in range(80, 88)), (ax[78:88], bits[78:88]))
check("stickhold: the game never reads L2", not any(b & L2 for b in bits), [i for i, b in enumerate(bits) if b & L2][:5])
check("stickhold: and no STICK is said for a hold", [l for l in got if l.startswith("STICK")] == ["STICK\ton=0"], got)

bits, ax = live_run("nopair", "40:r2;44:;60:up;66:", "SETSTICK\tmode=toggle\n", 80)
check("toggle with no stick pair: R2 is the game's", any(bits[i] & R2 for i in range(41, 44)), bits[38:46])
check("and it turns nothing: UP is the d-pad's after it", any(bits[i] & UP for i in range(61, 66)), bits[58:68])

# The N64's stick off with a button that turns it (stick=dpad-pad): the
# d-pad is the d-pad until R1 (r1:stick) turns it into the stick, said.
bits, ax = live_run("dpadpad", "40:right;46:;60:r1;62:;70:right;76:", "SETSTICK\tmode=dpad-pad\nSETMAP\tmap=r1:stick\n", 90)
check("dpad-pad answers STICK on=0", "STICK\ton=0" in got, got)
check("dpad-pad: RIGHT is the joypad's first", any(bits[i] & RIGHT for i in range(41, 46)) and
      not any(ax[i] for i in range(38, 48)), (ax[38:48], bits[38:48]))
check("dpad-pad: R1 turns it into the stick and says STICK on=1",
      "STICK\ton=1" in got and any(ax[i] == FULL for i in range(71, 76)) and
      not any(bits[i] & RIGHT for i in range(71, 76)), (got, ax[68:78], bits[68:78]))

bits, ax = live_run("badmode", HOLD_RIGHT, "SETSTICK\tmode=plain\nSETSTICK\tmode=sideways\n", 60)
check("an unknown mode is an ERROR", any(l.startswith("ERROR\tcode=bad_stick") for l in got), got)
check("and leaves the stick as it was (plain: the stick and the joypad's RIGHT)",
      any(ax[i] == FULL for i in range(41, 46)) and any(bits[i] & RIGHT for i in range(41, 46)),
      (ax[38:48], bits[38:48]))

# The player's hotkeys (RUN hotkeys=, chord.h): SELECT+Y a screenshot into
# RUN shots=, SELECT+X the next shader (the launcher steps it), SELECT+B
# Reset Game's pause, and R1 no hotkey: SELECT then R1 the game's, both
SHOTS = f"{TMP}/Screenshots/Stub/Game"
Y, X, B = 1 << 9, 1 << 1, 1 << 0
bits, ax = live_run("hk", "40:select;43:select+y;47:select;50:;70:select;73:select+x;77:;"
                    "100:select;103:select+r1;108:;130:select;133:select+b;137:;",
                    "", 170, f"\thotkeys=shot:y,shader:x,reset:b\tshots={SHOTS}")
shot = [l for l in got if l.startswith("SHOT\tpath=")]
check("hotkeys=: SELECT+Y answers SHOT path= under shots=", len(shot) == 1 and shot[0][10:].startswith(SHOTS + " "), got)
if shot:
    pth = shot[0][10:]
    ok = os.path.exists(pth) and open(pth, "rb").read(8) == b"\x89PNG\r\n\x1a\n" and pth.endswith(".png")
    check("and the file is a PNG in its own folder, made for it", ok, pth)
check("the game never sees that SELECT or Y", not any(bits[i] & (Y | SEL) for i in range(40, 50)), bits[38:52])
check("SELECT+X says HOTKEY key=shader", "HOTKEY\tkey=shader" in got, got)
check("SELECT+B pauses for Reset Game's question: PAUSED for=reset", "PAUSED\tfor=reset" in got, got)
check("R1 no hotkey: no save asked, and SELECT then R1 is the game's",
      "HOTKEY\tkey=save" not in got and any(bits[i] & R1 and bits[i] & SEL for i in range(103, 108)), bits[100:110])

# SETHOTKEYS while it runs: quit on START; SELECT+START pauses for=quit
bits, ax = live_run("hkset", "60:select;63:select+start;67:;", "SETHOTKEYS\tkeys=quit:start\n", 100)
check("SETHOTKEYS answers HOTKEYS ok=1", "HOTKEYS\tok=1" in got, got)
check("SELECT+START, quit's: PAUSED for=quit", "PAUSED\tfor=quit" in got, got)
bits, ax = live_run("hkbad", "60:select;63:select+r1;67:;", "SETHOTKEYS\tkeys=shot:select\n", 100)
check("a bad SETHOTKEYS: ok=0, and the keys as they were (SELECT+R1 still saves)",
      "HOTKEYS\tok=0" in got and "HOTKEY\tkey=save" in got, got)

# A remapped SELECT (select:start): the chords go by the SELECT key, not by
# what it is mapped to, so SELECT+R1 still saves and the core never reads START.
bits, ax = live_run("selmap", "40:select;44:select+r1;50:select;54:", "SETMAP\tmap=select:start\n", 70)
check("select:start: SELECT+R1 still says HOTKEY key=save", got.count("HOTKEY\tkey=save") == 1, got)
check("select:start: the core never reads START", not any(b & START for b in bits), [i for i, b in enumerate(bits) if b & START][:5])

# MENU's chords in a game (RUN menukeys=, chord.h's mchord; ZyperOS's
# 1.1.1-test.3 notes: the screenshot and the lock work everywhere, in games
# too): MENU held, then L1, a screenshot as SELECT's (HOTKEY key=shot, SHOT
# path=); MENU held, then L2, the lock's pause (PAUSED for=lock); MENU let go
# after either opens no menu, and the game never reads that L1 or L2; L1
# alone is the game's. With no menukeys= (an old launcher), MENU+L1 is the
# game's L1 and MENU let go opens the menu, as ever.
MK_SCRIPT = "40:menu;43:menu+l1;47:menu;50:;70:menu;73:menu+l2;77:menu;80:;100:l1;104:;"
bits, ax = live_run("mk", MK_SCRIPT, "", 140, f"\tmenukeys=shot:l1,lock:l2\tshots={SHOTS}")
shot = [l for l in got if l.startswith("SHOT\tpath=")]
check("menukeys=: MENU+L1 says HOTKEY key=shot and answers SHOT path= under shots=",
      "HOTKEY\tkey=shot" in got and len(shot) == 1 and shot[0][10:].startswith(SHOTS + " "), got)
check("menukeys=: MENU+L2 pauses for the lock: PAUSED for=lock", got.count("PAUSED\tfor=lock") == 1, got)
check("menukeys=: MENU let go after either opens no menu (no PAUSED of MENU's)", "PAUSED" not in got, got)
check("menukeys=: the game never reads the chords' L1 or L2",
      not any(bits[i] & (L1 | L2) for i in range(40, 90)), [i for i in range(40, 90) if bits[i] & (L1 | L2)])
check("menukeys=: L1 alone is the game's", any(bits[i] & L1 for i in range(100, 104)), bits[98:106])
bits, ax = live_run("nomk", MK_SCRIPT, "", 140, f"\tshots={SHOTS}")
check("no menukeys=: MENU+L1 is the game's L1, as ever", any(bits[i] & L1 for i in range(43, 47)), bits[40:50])
check("no menukeys=: no screenshot, no lock", not any(l.startswith("SHOT\t") for l in got) and
      "PAUSED\tfor=lock" not in got, got)
check("no menukeys=: MENU let go opens the menu", "PAUSED" in got, got)

if fails:
    print(f"FAIL: {len(fails)} hotkey check(s)")
    sys.exit(1)
print("ok: SELECT's chords, the quick slot and the PSP's d-pad, end to end")
