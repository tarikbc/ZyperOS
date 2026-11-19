"""On-Screen Stats' FPS and Speed, end to end against the stub core.

Speed is how fast the game is emulated, against its own rate: the frames it
emulated a second over the frame rate the core announces. FPS is how many
pictures the core makes a second. They are the same number for a core that
draws every frame it runs, and not for one that draws less often than that:

  - a core that runs 60 frames a second and hands over a new picture on every
    second one (a repeat, a NULL frame, on the rest) is at full speed, at
    30 fps: Speed 1.00x, FPS 30
  - a core whose retro_run is two frames long when a game shows a new picture
    every second vblank (PPSSPP, Flycast, ParallelN64 "fullspeed") is at full
    speed, at 30 fps, though retro_run was called only 30 times a second. It
    read 0.5x, the Dreamcast at 30 fps on the Brick (1.0.6). The frames it
    emulated are the sound it made: two frames of sound a call.
  - a core too slow for its game reads below 1x, and fast forward at its step

The stub is told how it draws (STUBCORE_DUPE, STUBCORE_SPAN) and how long a
call takes (STUBCORE_FRAME_US, slept and spun); the desktop port writes down
the numbers the overlay would draw, each with the time its window ended
(DIATOM_HUD_LOG). And the now file px2-perf reads (src/now.h) is held to the
same speed, since it says the same thing.

How fast a runner runs the stub is not known here: a shared CI runner doing
other checks ran a 25 ms core at 9.7 calls a second, and the Mac does when it
is made to run in the background (`taskpolicy -b python3 test/hudspeed.py`).
So no speed is judged against the speed it should have. What is judged is
what a run says of itself, true however slow the machine is:

  - frames to a picture: the frames the overlay says were emulated, over the
    pictures it says were made, in the windows of one stretch of the run, are
    the core's: 1 for a core that draws every frame, 2 for one that repeats
    every second picture or whose call is two frames long, 3 for three. The
    count of calls, which Speed was, makes the first 1 for a call two frames
    long, and a FPS that counted calls makes the repeating core's 1
  - never over what the core allows: a core of 25 ms a frame cannot be over
    0.67x, however fast the machine
  - the overlay and the now file, over the same stretch (the windows that
    ended between two writings of it), say the same speed within the
    sampling's slack, since both are made of the same counters

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-fastforward does. SDL's dummy drivers: no window, no sound.

    python3 test/hudspeed.py
"""
import os, re, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-hudspeed-")
fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

CORE_FPS = 59.7275
PERIOD_US = 1e6 / CORE_FPS

def kv(path):
    try:
        with open(path) as f:
            return dict(l.rstrip("\n").split("=", 1) for l in f if "=" in l)
    except OSError:
        return {}

class Run:
    """What one game said of itself: the stretch between two writings of the
    now file, in which the overlay's windows are counted"""
    def __init__(self):
        self.pics = self.frames = self.secs = 0.0   # the overlay's, in the stretch
        self.windows = 0
        self.fps = self.speed = None                # their means
        self.now_speed = None                       # the now file's, the same stretch
        self.clock = self.short = None              # the step's own report

    def __repr__(self):
        return (f"fps {self.fps}, speed {self.speed}, now {self.now_speed}, "
                f"{self.windows} windows, {self.pics:.1f} pictures, {self.frames:.1f} frames, "
                f"step report {self.clock} with {self.short} turns short")

def run(tag, stub_env, presses=0, seconds=5.0):
    """One game with On-Screen Stats on: `presses` times SELECT+R2 from poll 30,
    then a second of the step to settle, the now file read, `seconds` more at
    the step, the now file read again, STOP. The overlay's windows that ended
    between the two readings are what is counted. None if the game would not
    run."""
    steps = ["30:select"]
    poll = 33
    for _ in range(presses):
        steps += [f"{poll}:select+r2", f"{poll + 3}:select"]
        poll += 6
    steps.append(f"{poll}:")
    sock, out, now = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.out", f"{TMP}/{tag}.now"
    open(now + ".ask", "w").close()   # written again every quarter of a second
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", DIATOM_INPUT_SCRIPT=";".join(steps),
               DIATOM_HUD_LOG="1", DIATOM_NOW=now, STUBCORE_PRECISE="1", **stub_env)
    with open(out, "w") as f:
        p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                             env=env, stdout=f, stderr=subprocess.STDOUT)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.settimeout(0.1)
    buf, speeds, running = b"", [], False
    s.sendall(f"RUN\tcore={STUB}\n".encode())

    def pump(until, stop_when=None):
        nonlocal buf, running
        while time.time() < until:
            if stop_when and stop_when(): return
            try:
                d = s.recv(65536)
                if not d: return
                buf += d
            except socket.timeout:
                pass
            while b"\n" in buf:
                l, buf = buf.split(b"\n", 1)
                l = l.decode().strip()
                if l == "RUNNING" and not running:
                    running = True
                    s.sendall(b"SETHUD\ton=1\n")
                if l.startswith("SPEED\tx="): speeds.append(l.split("=", 1)[1])
                if l == "PAUSED": s.sendall(b"RESUME\n")

    pump(time.time() + 10, lambda: running)
    end = time.time() + 30
    while len(speeds) < presses and time.time() < end:
        pump(time.time() + 0.2)
    pump(time.time() + 1.0)
    a = kv(now)
    pump(time.time() + seconds)
    b = kv(now)
    s.sendall(b"STOP\n")
    pump(time.time() + 1.5)
    s.sendall(b"QUIT\n")
    try: p.wait(5)
    except subprocess.TimeoutExpired: p.kill()
    text = open(out, errors="replace").read()
    r = Run()
    huds = [(int(t), float(f), float(v)) for t, f, v in
            re.findall(r"diatom: hud t=(\d+) fps=([\d.]+) speed=(-?[\d.]+)", text)]
    try:
        ta, tb = int(a["t_us"]), int(b["t_us"])
        fps_core = float(b["fps"])
        dr = float(b["run_us"]) - float(a["run_us"])
        if dr > 0 and fps_core > 0 and tb > ta:
            r.now_speed = (float(b["frames"]) - float(a["frames"])) * 1e6 / fps_core / dr
    except (KeyError, ValueError):
        return None
    # The windows that ended between the two writings, each as long as it
    # was: the overlay's stretch is the now file's, to the width of a window
    for prev, cur in zip(huds, huds[1:]):
        if not (ta < cur[0] <= tb): continue
        dt = (cur[0] - prev[0]) / 1e6
        r.windows += 1
        r.secs += dt
        r.pics += cur[1] * dt
        r.frames += cur[2] * dt * CORE_FPS
    if r.windows < 2 or r.secs <= 0: return None
    r.fps = r.pics / r.secs
    r.speed = r.frames / r.secs / CORE_FPS
    rep = re.findall(r"diatom: speed ([\d.]+)x held [\d.]+s, reached [\d.]+x "
                     r"\(([\d.]+)x of the clock\), \d+ audio frame\(s\) dropped, "
                     r"slept [\d.]+s \([\d.]+s after (\d+) of \d+ turns short", text)
    if rep: r.clock, r.short = float(rep[-1][1]), int(rep[-1][2])
    return r

def say(name, r):
    if r: print(f"       {name}: FPS {r.fps:.1f}, Speed {r.speed:.2f}x"
                + (f", the now file {r.now_speed:.2f}x" if r.now_speed is not None else "")
                + f" ({r.windows} windows)")

def per_picture(r, k):
    """The frames emulated for each picture made, in the stretch, are k: a
    call two frames long is two frames to a picture whatever the machine's
    speed, and counting its calls makes it one; a FPS that counted calls
    makes a core that repeats every second picture one. One picture's frames
    of slack, for a window's edge, and a little for the sums."""
    return r is not None and r.pics >= 4 and abs(r.frames - k * r.pics) <= k + 0.03 * r.frames

def same_now(r):
    """The overlay's speed in the stretch and the now file's, the same to the
    slack of a window's edge at each end of it: a window that ended just
    after a writing is left out, and one that began before the first is in"""
    return (r is not None and r.now_speed is not None
            and abs(r.speed - r.now_speed) <= 0.05 + 0.30 * r.now_speed)

def upto(r, speed, fps):
    """Never over what the core allows, however fast the machine, and a
    speed at all"""
    return r is not None and 0.0 < r.speed <= speed and r.fps <= fps

# The control: a core that draws every frame it runs
r = run("plain", {})
say("plain", r)
check("a core that draws every frame is one frame to a picture, and not over 60 fps and 1x",
      per_picture(r, 1) and upto(r, 1.06, 62), r)

# 60 frames a second, a new picture every second one: 30 fps
r = run("dupe", {"STUBCORE_DUPE": "2"})
say("dupe", r)
check("a core that repeats every second picture is two frames to a picture, not over 30 fps and 1x",
      per_picture(r, 2) and upto(r, 1.06, 31.5), r)
check("and the now file says the same speed", same_now(r), r)

# A call two frames long, a picture and two frames of sound, taking two
# frames' time: 30 fps at full speed, called 30 times a second
r = run("span", {"STUBCORE_SPAN": "2", "STUBCORE_FRAME_US": str(int(2 * PERIOD_US) + 100)})
say("span", r)
check("a core whose call is two frames long is two frames to a picture, not 0.5x of them",
      per_picture(r, 2) and upto(r, 1.06, 31.5), r)
check("and the now file says the same speed", same_now(r), r)

# The same, three frames long: a 20 fps game
r = run("span3", {"STUBCORE_SPAN": "3", "STUBCORE_FRAME_US": str(int(3 * PERIOD_US) + 100)})
say("span3", r)
check("a core whose call is three frames long is three frames to a picture, not 0.33x of them",
      per_picture(r, 3) and upto(r, 1.06, 21), r)

# A core too slow for its game reads under 1x, and so does a 30 fps game that
# is slow: a call of 3 frames' time that is 2 frames long. 25 ms a frame is
# 40 a second, 0.67x, at the most
r = run("slow", {"STUBCORE_FRAME_US": "25000"})
say("slow", r)
check("a core at 25 ms a frame is one frame to a picture, and not over 0.67x",
      per_picture(r, 1) and upto(r, 0.72, 41), r)
check("and the now file says the same speed", same_now(r), r)
r = run("slowspan", {"STUBCORE_SPAN": "2", "STUBCORE_FRAME_US": str(int(3 * PERIOD_US))})
say("slowspan", r)
check("a 30 fps game at two thirds of its speed is two frames to a picture, not over 0.67x and 20 fps",
      per_picture(r, 2) and upto(r, 0.72, 21), r)
check("and the now file says the same speed", same_now(r), r)

# Fast forward: the step, dupes or not. A 1 ms core reaches it on a machine
# that keeps up, and then the overlay says the step and what the step's own
# report says it reached; on one that does not (turns short of the step), it
# is under the step and still the now file's
for presses, want in ((2, 2.0), (3, 3.0)):
    for tag, stub_env, k in (("every frame drawn", {}, 1), ("dupe", {"STUBCORE_DUPE": "2"}, 2)):
        r = run(f"ff{presses}{k}", dict(stub_env, STUBCORE_FRAME_US="1000"), presses)
        say(f"{want}x, {tag}", r)
        check(f"fast forward at {want}x, {tag}: {k} frame(s) to a picture, not over the step",
              per_picture(r, k) and upto(r, want * 1.06, 1e9), r)
        check(f"fast forward at {want}x, {tag}: the now file says the same speed", same_now(r), r)
        if r and r.short == 0 and r.clock is not None:
            check(f"and with no turn short of the step, near {want}x and what its report says",
                  r.speed >= want * 0.88 and abs(r.speed - r.clock) <= 0.15, r)
        elif r:
            print(f"  --   not judged against {want}x: this machine could not keep the step")

shutil.rmtree(TMP, ignore_errors=True)
if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: Speed is the game's speed against its own rate, FPS the pictures a second, dupes or long calls or not")
