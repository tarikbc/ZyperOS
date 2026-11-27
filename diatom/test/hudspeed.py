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
call takes (STUBCORE_FRAME_US); the desktop port writes down
the numbers the overlay would draw, each with the time its window ended
(DIATOM_HUD_LOG). And the now file px2-perf reads (src/now.h) is held to the
same speed, since it says the same thing.

Every run is on Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK):
time moves only by the stub's calls (STUBCORE_FRAME_US, spent on it) and
the loop's sleeps, and the overlay's windows, the now file's t_us and the
step's report are all of it. So each run's speed and FPS are what the core
and the pacing give, the same on an idle Mac, a loaded one or CI, and
judged exactly, to 1%, every run. On the wall clock a shared CI runner ran
a 25 ms core at 9.7 calls a second, so no speed could be judged against
what it should be, a fast forward step only in a run that kept up, and the
rest had slack for whatever the machine did (2026-11-24). Here:

  - Speed and FPS each what the core allows: 1x and 60 for a core that
    draws every frame, 1x and 30 for one that repeats every second picture
    or whose call is two frames long, 20 for three; a core of 25 ms a frame
    0.67x and 40. The count of calls, which Speed was, makes a call two
    frames long 0.5x, and a FPS that counted calls makes the repeating
    core's 60
  - frames to a picture: the frames the overlay says were emulated, over the
    pictures it says were made, in the windows of one stretch of the run, are
    the core's: 1, 2 or 3
  - the overlay and the now file, over the same stretch (the windows that
    ended between two writings of it), say the same speed, since both are
    made of the same counters
  - at a fast forward step, 2x and 3x, the step and what the step's own
    report says it reached, no turn short and no time lost (kept_up)
  - a game that loses time: each of the loop's sleeps running up to 90 ms
    over (the test clock's SLEEP), past the one turn of debt the schedule
    keeps, as a runner whose sleep wakes late does. It reads under the step
    with no turn short of it (GitHub's macOS runner, 2.0x read 1.5x with
    none short, 2026-11-19), and the step's report says what it lost and
    accounts for what it reached: the step less the time the schedule gave
    up, to within a turn. It was a game stopped by SIGSTOP on the wall
    clock until 2026-11-24, whose loss a busy machine made more of

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
        self.clock = self.short = self.turns = None   # the step's own report: of the clock,
                                                      # turns short, and over the turns' time
        self.held = self.lost = None                  # ... seconds held, and seconds the
                                                      # schedule gave up (step_report)

    def __repr__(self):
        return (f"fps {self.fps}, speed {self.speed}, now {self.now_speed}, "
                f"{self.windows} windows, {self.pics:.1f} pictures, {self.frames:.1f} frames, "
                f"step report {self.clock}x of the clock, {self.turns}x of the turns, {self.short} turns short, "
                f"{self.lost}s lost of {self.held}s held")

def run(tag, stub_env, presses=0, seconds=5.0, clock="0"):
    """One game with On-Screen Stats on: `presses` times SELECT+R2 from poll 30,
    then a second of the step to settle, the now file read, `seconds` more at
    the step, the now file read again, STOP. The seconds are Diatom's own,
    the now file's t_us: on the test clock, DIATOM_TEST_CLOCK=`clock`, unless
    it is None. The overlay's windows that ended between the two readings
    are what is counted. None if the game would not run."""
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
    env.pop("DIATOM_TEST_CLOCK", None)
    if clock is not None: env["DIATOM_TEST_CLOCK"] = clock
    with open(out, "w") as f:
        p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                             env=env, stdout=f, stderr=subprocess.STDOUT)
    # The waits are the wall clock's, and only waits: each ends as soon as
    # what it waits for is there, so a busy machine makes them longer, never
    # the run different
    end = time.time() + 30
    while not os.path.exists(sock) and time.time() < end:
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.settimeout(0.02)
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

    def t_now():
        try: return int(kv(now).get("t_us", 0))
        except ValueError: return 0

    def after(t_us):
        """The now file once Diatom's clock is past t_us"""
        pump(time.time() + 120, lambda: t_now() > t_us)
        return kv(now)

    pump(time.time() + 30, lambda: running)
    pump(time.time() + 30, lambda: len(speeds) >= presses)
    a = after(t_now() + 1000000)
    try: b = after(int(a["t_us"]) + int(seconds * 1e6))
    except (KeyError, ValueError): b = {}
    s.sendall(b"STOP\n")
    pump(time.time() + 30, lambda: "frames in" in open(out, errors="replace").read())
    s.sendall(b"QUIT\n")
    try: p.wait(30)
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
    rep = re.findall(r"diatom: speed ([\d.]+)x held ([\d.]+)s, reached ([\d.]+)x "
                     r"\(([\d.]+)x of the clock\), \d+ audio frame\(s\) dropped, "
                     r"slept [\d.]+s \([\d.]+s after (\d+) of \d+ turns short of the step\), "
                     r"lost ([\d.]+)s", text)
    if rep:
        r.held, r.turns, r.clock = float(rep[-1][1]), float(rep[-1][2]), float(rep[-1][3])
        r.short, r.lost = int(rep[-1][4]), float(rep[-1][5])
    return r

def say(name, r):
    if r: print(f"       {name}: FPS {r.fps:.1f}, Speed {r.speed:.2f}x"
                + (f", the now file {r.now_speed:.2f}x" if r.now_speed is not None else "")
                + f" ({r.windows} windows)")

def per_picture(r, k):
    """The frames emulated for each picture made, in the stretch, are k: a
    call two frames long is two frames to a picture, and counting its calls
    makes it one; a FPS that counted calls makes a core that repeats every
    second picture one. To 1%: the test clock's rates are exact, and a
    stretch holds a hundred pictures and more."""
    return r is not None and r.pics >= 50 and abs(r.frames - k * r.pics) <= 0.01 * r.frames

def same_now(r, slack=0.01):
    """The overlay's speed in the stretch and the now file's, the same: to
    1% where the speed is steady, as the test clock's is, so a window's
    edge at either end of the stretch moves neither"""
    return (r is not None and r.now_speed is not None
            and abs(r.speed - r.now_speed) <= slack * r.now_speed)

def at(r, speed, fps):
    """Speed and FPS what the core allows, to 1%"""
    return (r is not None and abs(r.speed - speed) <= 0.01 * speed
            and abs(r.fps - fps) <= 0.01 * fps)

def allows(span, cost_us=0):
    """The Speed a core of `span` frames a call, each call `cost_us`, runs
    at: its own rate, or what its cost allows, whichever is less"""
    calls = CORE_FPS / span if cost_us <= 0 else min(CORE_FPS / span, 1e6 / cost_us)
    return calls * span / CORE_FPS

def kept_up(r):
    """A run that held its step as a machine that keeps up does: no turn short
    of the step's frames, and little time lost, which the step's report says
    (diatom: speed ..., "lost"): the time the schedule gave up, being more
    than one turn behind. A runner whose sleep woke late, or that took the CPU
    for a moment, loses it there and reads under the step with no turn short
    of it, and the same from the clock as from the turns: GitHub's macOS
    runner at 2.0x, 1.5x of both with none short (2026-11-19, twice, the
    second with the loss inside the turns, where no figure but this one sees
    it). That is the machine's, and the check says it could not tell."""
    return (r is not None and r.short == 0 and r.lost is not None and r.held is not None
            and r.lost <= 0.03 * r.held)

def accounted(r, want):
    """What the step's own report says it reached, of the clock, is the step
    less the time it says it lost: the schedule is Diatom's clock, advanced
    a turn's frames at a time, so what is not lost is run, to within the one
    turn of debt it keeps (33 ms at 2x, half a percent of the run) and the
    report's rounding (0.05 s of the seconds held, and 0.005x). True of any
    run, a machine that stalls or not, and not of a loop that drops time and
    does not say: the stalled game's 1.88x against 2x with nothing said lost
    is 6% off, which this allowed until 2026-11-24"""
    return (r is not None and r.clock is not None and r.held and r.lost is not None
            and abs(r.clock - want * (r.held - r.lost) / r.held) <= 0.02 * want)

def reaches(r, want):
    """What a run that kept up shows: the step, to 1%, and what the step's
    own report says it reached"""
    return abs(r.speed - want) <= 0.01 * want and abs(r.speed - r.clock) <= 0.01 * want + 0.005

# That judgment, on runs made up from what the runner's log said, and from
# what a machine that keeps up writes: the runner's 2.0x, with no turn short
# and 1.5x of both the clock and the turns, is not judged
def made(speed, clock, turns, short, held=6.4, lost=0.0):
    r = Run()
    r.speed, r.clock, r.turns, r.short, r.held, r.lost = speed, clock, turns, short, held, lost
    return r
check("a run with no turn short and time lost inside the turns is not judged (the runner's 2.0x, twice)",
      not kept_up(made(1.5, 1.5, 1.5, 0, lost=1.6)) and not kept_up(made(1.52, 1.5, 2.1, 0, lost=1.5)), "judged")
check("a run with a turn short is not judged", not kept_up(made(1.9, 1.9, 1.9, 1)), "judged")
check("a run that kept up is judged, and 2.0x reaches 2.0x",
      kept_up(made(1.99, 1.98, 1.99, 0)) and reaches(made(1.99, 1.98, 1.99, 0), 2.0), "not judged or not reached")
check("a run that kept up and reached 1.5x of 2.0x is not near it",
      kept_up(made(1.5, 1.5, 1.5, 0)) and not reaches(made(1.5, 1.5, 1.5, 0), 2.0), "reached")
check("a run with no report is not judged",
      not kept_up(made(2.0, None, None, None, None, None)) and not kept_up(None), "judged")
check("a run's own report accounts for what it reached: the step less what it lost",
      accounted(made(1.5, 1.5, 1.5, 0, lost=1.6), 2.0) and accounted(made(1.99, 1.98, 1.99, 0), 2.0) and
      not accounted(made(1.5, 1.5, 1.5, 0), 2.0) and not accounted(made(1.99, 1.98, 1.99, 0, lost=1.6), 2.0),
      "not accounted")

# The control: a core that draws every frame it runs
r = run("plain", {})
say("plain", r)
check("a core that draws every frame is one frame to a picture, at 1x and 60 fps",
      per_picture(r, 1) and at(r, 1.0, CORE_FPS), r)

# 60 frames a second, a new picture every second one: 30 fps
r = run("dupe", {"STUBCORE_DUPE": "2"})
say("dupe", r)
check("a core that repeats every second picture is two frames to a picture, at 1x and 30 fps",
      per_picture(r, 2) and at(r, 1.0, CORE_FPS / 2), r)
check("and the now file says the same speed", same_now(r), r)

# A call two frames long, a picture and two frames of sound, taking two
# frames' time and a tenth of a millisecond: 30 fps at full speed, called 30
# times a second, but for that tenth
cost = int(2 * PERIOD_US) + 100
r = run("span", {"STUBCORE_SPAN": "2", "STUBCORE_FRAME_US": str(cost)})
say("span", r)
check("a core whose call is two frames long is two frames to a picture, at 1x and 30 fps, not 0.5x",
      per_picture(r, 2) and at(r, allows(2, cost), allows(2, cost) * CORE_FPS / 2), r)
check("and the now file says the same speed", same_now(r), r)

# The same, three frames long: a 20 fps game
cost = int(3 * PERIOD_US) + 100
r = run("span3", {"STUBCORE_SPAN": "3", "STUBCORE_FRAME_US": str(cost)})
say("span3", r)
check("a core whose call is three frames long is three frames to a picture, at 1x and 20 fps, not 0.33x",
      per_picture(r, 3) and at(r, allows(3, cost), allows(3, cost) * CORE_FPS / 3), r)

# A core too slow for its game reads under 1x, and so does a 30 fps game that
# is slow: a call of 3 frames' time that is 2 frames long. 25 ms a frame is
# 40 a second, 0.67x
r = run("slow", {"STUBCORE_FRAME_US": "25000"})
say("slow", r)
check("a core at 25 ms a frame is one frame to a picture, at 0.67x and 40 fps",
      per_picture(r, 1) and at(r, allows(1, 25000), 40.0), r)
check("and the now file says the same speed", same_now(r), r)
cost = int(3 * PERIOD_US)
r = run("slowspan", {"STUBCORE_SPAN": "2", "STUBCORE_FRAME_US": str(cost)})
say("slowspan", r)
check("a 30 fps game at two thirds of its speed is two frames to a picture, at 0.67x and 20 fps",
      per_picture(r, 2) and at(r, allows(2, cost), 1e6 / cost), r)
check("and the now file says the same speed", same_now(r), r)

# Fast forward: the step, dupes or not. A 1 ms core reaches it, and the
# overlay says the step and what the step's own report says it reached
for presses, want in ((2, 2.0), (3, 3.0)):
    for tag, stub_env, k in (("every frame drawn", {}, 1), ("dupe", {"STUBCORE_DUPE": "2"}, 2)):
        r = run(f"ff{presses}{k}", dict(stub_env, STUBCORE_FRAME_US="1000"), presses)
        say(f"{want}x, {tag}", r)
        check(f"fast forward at {want}x, {tag}: {k} frame(s) to a picture, at the step and "
              f"{want * 60 / k:.0f} fps", per_picture(r, k) and at(r, want, want * CORE_FPS / k), r)
        check(f"fast forward at {want}x, {tag}: the now file says the same speed", same_now(r), r)
        check(f"fast forward at {want}x, {tag}: it kept up, no turn short and no time lost, "
              f"and the overlay says what its report says", kept_up(r) and r.lost == 0 and reaches(r, want), r)
        check(f"fast forward at {want}x, {tag}: the step's own report accounts for what it reached, "
              f"the step less what it lost", accounted(r, want), r)

# A machine that loses time, made to: each of the loop's sleeps runs up to
# 90 ms over, by the test clock's fixed sequence, more than the turn of debt
# the schedule keeps at 2x (33 ms). It reads under the step, and the step's
# report says why (what it lost, not a turn short of the step, which is
# where a stop in the sleep leaves none: GitHub's macOS runner, 2.0x read
# 1.5x with none short, 2026-11-19); the check does not judge it against the
# step, and the overlay and the now file say the speed it ran at. Their
# stretch is the now file's two writings this script happened to read, and
# the speed is not steady, so they agree to the width of a window at each
# end of it, not to 1%
r = run("stalled", {"STUBCORE_FRAME_US": "1000"}, 2, clock="90000")
say("2.0x, each sleep up to 90 ms over", r)
check("a game that loses time says so: a tenth of a second lost at least", r is not None and r.lost is not None and r.lost >= 0.1, r)
check("and with no turn short, it is not judged against the step", r is not None and r.short == 0 and not kept_up(r), r)
check("and its own report accounts for what it reached, the step less what it lost", accounted(r, 2.0), r)
check("and the overlay says the speed it ran at, the report's", r is not None and r.clock is not None
      and abs(r.speed - r.clock) <= 0.05 * r.clock, r)
check("and the now file says the same speed", same_now(r, 0.05), r)

shutil.rmtree(TMP, ignore_errors=True)
if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: Speed is the game's speed against its own rate, FPS the pictures a second, dupes or long calls or not")
