"""Diatom's own keys pressed quickly in a slow game, end to end, against the
stub core at 10 fps (STUBCORE_FRAME_US=100000: a Saturn game's rate on the
GKD Pixel 2, where an 80 ms MENU never opened the menu, 2026-11-20). The pad
is driven by script (DIATOM_INPUT_SCRIPT, `~` a button that went down and
up again between two polls) and what the core read is written down by the
stub (STUBCORE_INPUT_LOG):

  - MENU tapped between two polls opens the menu once (PAUSED), and MENU
    held for six polls once, as before.
  - SELECT held and R1 tapped between two polls says HOTKEY key=save once;
    SELECT and R1 held together for five polls once, as before.
  - R1 or SELECT tapped alone: no hotkey and no menu, and the game reads
    neither, as libretro polls the pad (it was up at the poll).
  - RUN stick=toggle with the map's r2:stick: R2 tapped turns the stick, on
    and then off, once a tap.
  - At 2x (SELECT+R2 twice), where a turn runs four frames and so four
    polls, the stick's L1 (l1:stick) tapped at each of the four turns the
    stick each time, and MENU tapped opens the menu once.

On Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK): the stub's
100 ms frames are 100 ms on it, so SELECT held two polls before R1 is 200 ms
of the 400 ms CHORD_HOLD_MS allows on any machine, where on the wall clock a
busy one could stretch the two frames past it and the chord be the game's
SELECT; and the 9 s of a game at 10 fps take a fraction of that.

Needs a desktop build (diatom and stubcore.so, build/desktop unless the
argument names another folder) and SKIPS without it, as check-proto does.
SDL's dummy drivers: no window, no sound.

    python3 test/menutap.py [BUILD]
"""
import os, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else f"{ROOT}/build/desktop"
BIN, STUB = f"{BUILD}/diatom", f"{BUILD}/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print(f"skipped: no {BUILD}/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-menutap-")

# The stub reads RETRO_DEVICE_ID_JOYPAD_*: SELECT 2, R 11, R2 13
SEL, R1, R2 = 1 << 2, 1 << 11, 1 << 13

fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def frames_logged(path):
    try:
        with open(path) as f: return sum(1 for _ in f)
    except OSError: return 0

def session(name, script, polls, run_extra="", lines_out="", stub_env=None):
    """One Diatom and one game, the pad by `script`, until the core has read
    it `polls` times; the launcher's side as it is: each PAUSED (MENU let go)
    answered RESUME at once. The lines Diatom said, and port 0 of each frame
    the core read."""
    sock, log = f"{TMP}/{name}.sock", f"{TMP}/{name}.log"
    p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                         env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                                  SDL_AUDIODRIVER="dummy", DIATOM_TEST_CLOCK="0",
                                  DIATOM_INPUT_SCRIPT=script,
                                  STUBCORE_INPUT_LOG=log, **(stub_env or {})),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(600):
        if os.path.exists(sock): break
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.settimeout(0.05)
    buf, got = b"", []

    def pump(seconds, until=None):
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end and not (until and any(l.startswith(until) for l in got)):
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
                if l == "PAUSED" or l.startswith("PAUSED\t"):
                    s.sendall(b"RESUME\n")

    pump(30, "READY")
    s.sendall(f"RUN\tcore={STUB}{run_extra}\n{lines_out}".encode())
    # However slowly this machine runs it: the script counts polls, one a
    # frame, and a fixed wait ended before the script did on a CI runner
    end = time.time() + 120
    while frames_logged(log) < polls and time.time() < end:
        pump(0.2)
    # Every line said before the STOP's EXIT, however late this script
    # reads them
    s.sendall(b"STOP\n")
    pump(30, "EXIT")
    s.sendall(b"QUIT\n")
    try: p.wait(30)
    except subprocess.TimeoutExpired: p.kill()
    pads = []
    try:
        for line in open(log):
            pads.append(int(line.split()[1]))
    except OSError:
        pass
    return got, pads

def paused(got):
    return sum(1 for l in got if l == "PAUSED" or l.startswith("PAUSED\t"))

# A Saturn game's 10 fps: a poll a tenth of a second, so SELECT held two
# polls is 0.2 s, inside CHORD_HOLD_MS, and a hotkey's chord.
SLOW = ";".join([
    "10:~menu", "11:",                                    # MENU tapped: the menu
    "20:menu", "26:",                                     # MENU held 0.6 s: the menu
    "34:select", "36:select+~r1", "37:select", "38:",     # SELECT held, R1 tapped: save
    "46:~r1", "47:",                                      # R1 tapped alone: nothing
    "52:~select", "53:",                                  # SELECT tapped alone: nothing
    "58:select", "60:select+r1", "65:select", "66:",      # SELECT+R1 held: save
    "72:~r2", "73:", "78:~r2", "79:",                     # R2, Stick On/Off: on, off
])
got, pads = session("slow", SLOW, 90, "\tstick=toggle", "SETMAP\tmap=r2:stick\n",
                    {"STUBCORE_FRAME_US": "100000"})
check("10 fps: the core read the pad", len(pads) >= 85, len(pads))
check("10 fps: MENU tapped between two polls, then held, opens the menu twice, once each",
      paused(got) == 2, [l for l in got if l.startswith("PAUSED")])
check("10 fps: SELECT+R1, R1 tapped and then held, says HOTKEY key=save twice, once each",
      got.count("HOTKEY\tkey=save") == 2, [l for l in got if l.startswith("HOTKEY")])
check("10 fps: no other hotkey", all(l == "HOTKEY\tkey=save" for l in got if l.startswith("HOTKEY")),
      [l for l in got if l.startswith("HOTKEY")])
sticks = [l for l in got if l.startswith("STICK\t")]
check("10 fps: R2 tapped twice turns the stick on and then off", sticks == ["STICK\ton=1", "STICK\ton=0"],
      sticks)
check("10 fps: the game never reads SELECT, R1 or R2 (chords hidden, taps never made up)",
      not any(b & (SEL | R1 | R2) for b in pads),
      [(i, b) for i, b in enumerate(pads) if b & (SEL | R1 | R2)][:6])

# 2x: a turn runs four frames, so a key tapped at the first, second or third
# poll of a turn is gone from the last one, which the turn ends on. Taps nine
# polls apart land at each of the four places in turn, and each two turns or
# more after the last (the turn after a tap is its release). Stick On/Off says
# STICK at each and never pauses, so the turns keep their places.
FAST = ";".join([
    "20:select", "22:select+r2", "25:select", "28:select+r2", "31:select", "33:",   # 1.5x, 2x
    "80:~l1", "81:", "89:~l1", "90:", "98:~l1", "99:", "107:~l1", "108:",
    "140:~menu", "141:",
])
got, pads = session("fast", FAST, 180, "\tstick=toggle", "SETMAP\tmap=l1:stick\n")
speeds = [l.split("=", 1)[1] for l in got if l.startswith("SPEED\tx=")]
check("2x: SELECT+R2 twice steps 1.5x and 2x", speeds[:2] == ["1.5", "2"], speeds)
sticks = [l for l in got if l.startswith("STICK\t")]
check("2x: L1 tapped at four places in a turn turns the stick four times",
      sticks == ["STICK\ton=1", "STICK\ton=0"] * 2, sticks)
check("2x: MENU tapped opens the menu once", paused(got) == 1,
      [l for l in got if l.startswith("PAUSED")])

if fails:
    print(f"menutap: {len(fails)} FAILED")
    sys.exit(1)
print("menutap: all passed")
