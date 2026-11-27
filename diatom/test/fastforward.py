"""Fast forward's speed, end to end against the stub core.

A fast turn stands for two refreshes. Until 2026-11-16 it was also paced as
two whatever ran in it, so a core whose frames took more than a third of the
turn ran 2 frames in it and then slept: Pokemon Black on the Pixel 2, at
11 to 12 ms a frame, went 1.0x at every step past 1.5x, with the emulation
thread idle a fifth of the time. A turn is now paced as the time its frames
stand for at the step chosen, so a core that cannot reach the step runs
flat out, and one that can is held to it, never over.

The stub takes STUBCORE_FRAME_US in each frame, as a slow core does, and
with STUBCORE_SKIPPABLE offers a frameskip, as the DS core does. The speed is
Diatom's own figure at STOP (step_report), "of the clock": frames run, as the
game's time, over Diatom's clock's.

Every run but one is on Diatom's test clock (port/desktop.c,
DIATOM_TEST_CLOCK): time moves only by the stub's frames (STUBCORE_FRAME_US,
spent on it) and the loop's sleeps, so each run's speed is what its pacing
gives, the same on an idle Mac, a loaded one or CI, and judged exactly every
run. On the wall clock a CI runner busy with other checks drew slower than
the core ran (a 1 ms core at 1.5x reached 1.31x), and a run the machine
could not keep up was not judged at all; make check's load of 12 left the
9 ms core at 1.5x unjudged most runs (2026-11-24). So here:

  - a 1 ms core reaches 1.5x and 3x, to 1%, with no turn short of the step
    and no time lost
  - a 9 ms core at 3x, with a frameskip or not, runs flat out: every turn
    short, no sleep after one, and 1/(9 ms) of the clock, 1.86x, to 1%,
    where the old pacing ran 3 frames a turn and slept the rest, 1.5x
  - the 9 ms core at 1.5x reaches it, to 1%, no turn short

Then one look on the wall clock, the real sleep: a 1 ms core at 3x never
over the step. Only that: a busy machine makes a run slower, never faster,
so it holds under any load, and how much slower says nothing of the pacing.

Sound is not judged here: SDL's dummy driver drains its queue slower than
the clock, and drops at 1x too. The silence fast forward pushes (one turn's
wall-clock time) is checked on a device, where step_report's count is real.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-hotkeys does. SDL's dummy drivers: no window, no sound.

    python3 test/fastforward.py
"""
import os, re, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-ff-")
fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def run(tag, frame_us, presses, seconds=4.0, skippable=False, clock=True):
    """One game: SELECT+R2 `presses` times from poll 30, then `seconds` at that
    step, STOP. On the test clock unless `clock` is False, its seconds then
    Diatom's own, counted in the stub's frames (STUBCORE_SHOWN_LOG). Returns
    (step, of the clock, audio frames dropped, a frame's cost in ms, seconds
    held, seconds slept after short turns, short turns, turns, of the turns,
    seconds lost) from the step's report, or None."""
    steps = ["30:select"]
    poll = 33
    for _ in range(presses):
        steps += [f"{poll}:select+r2", f"{poll + 3}:select"]
        poll += 6
    steps.append(f"{poll}:")
    sock, out, shown = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.out", f"{TMP}/{tag}.shown"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", DIATOM_INPUT_SCRIPT=";".join(steps),
               STUBCORE_FRAME_US=str(frame_us), STUBCORE_SHOWN_LOG=shown)
    env.pop("DIATOM_TEST_CLOCK", None)
    if clock: env["DIATOM_TEST_CLOCK"] = "0"
    if skippable: env["STUBCORE_SKIPPABLE"] = "1"
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
    s.settimeout(0.1)
    buf, speeds = b"", []
    s.sendall(f"RUN\tcore={STUB}\n".encode())

    def pump(until, done=lambda: False):
        nonlocal buf
        while time.time() < until and not done():
            try:
                d = s.recv(65536)
                if not d: return
                buf += d
            except socket.timeout:
                pass
            while b"\n" in buf:
                l, buf = buf.split(b"\n", 1)
                l = l.decode().strip()
                if l.startswith("SPEED\tx="): speeds.append(l.split("=", 1)[1])
                if l == "PAUSED": s.sendall(b"RESUME\n")

    def frames():
        try:
            with open(shown) as f: return sum(1 for _ in f)
        except OSError: return 0

    # Until the last step is said, then the time at it: the stub's frames
    # for `seconds` of the step on the test clock, as many as the step's
    # speed or the core's cost allows, at least; the wall's on the wall clock
    pump(time.time() + 30, lambda: len(speeds) >= presses)
    if clock:
        want = frames() + int(seconds * 59.7275 * min(presses_step(presses), limit_of(frame_us)))
        pump(time.time() + 120, lambda: frames() >= want)
    else:
        pump(time.time() + seconds)
    s.sendall(b"STOP\n")
    pump(time.time() + 30, lambda: "frames in" in open(out, errors="replace").read())
    s.sendall(b"QUIT\n")
    try: p.wait(30)
    except subprocess.TimeoutExpired: p.kill()
    text = open(out, errors="replace").read()
    reps = re.findall(r"diatom: speed ([\d.]+x|max) held ([\d.]+)s, reached ([\d.]+)x "
                      r"\(([\d.]+)x of the clock\), (\d+) audio frame\(s\) dropped, "
                      r"slept [\d.]+s \(([\d.]+)s after (\d+) of (\d+) turns short of the step\), "
                      r"lost ([\d.]+)s", text)
    # What a frame really cost in this run, drawn or not: on the wall clock,
    # and a busy machine, more than the stub spends
    costs = [float(c) for c in re.findall(r"diatom: the core took ([\d.]+) ms a drawn frame", text)]
    costs += [float(c) for c in re.findall(r"diatom: and ([\d.]+) ms a frame run undrawn", text)]
    if not reps or not costs: return None
    step, held, turn_speed, clock_x, dropped, short_s, short_n, turns, lost = reps[-1]
    # Max has no step: said "max", kept here as SPEED_MAX_HALVES' ceiling
    step = 32.0 if step == "max" else float(step[:-1])
    return (step, float(clock_x), int(dropped), sum(costs) / len(costs),
            float(held), float(short_s), int(short_n), int(turns), float(turn_speed), float(lost))

def presses_step(presses):
    """The step SELECT+R2 pressed `presses` times reaches: 1.5x, 2x, 3x"""
    return (1.0, 1.5, 2.0, 3.0)[min(presses, 3)]

def limit_of(frame_us):
    """The most a core that takes frame_us in each frame can run at: on the
    test clock exactly, the frame loop's own work being free; on the wall
    clock never more, its sleep never shorter than it asks"""
    return 1e6 / frame_us / 59.7275

def short_of(r):
    return f"{r[6]} of {r[7]} turns short of the step, {r[5]:.2f}s slept after them, {r[9]:.2f}s lost"

def near(x, want):
    """Within 1% and the report's rounding, the test clock's all"""
    return abs(x - want) <= want * 0.01 + 0.005

# A core that reaches every step: held to it, exactly, no turn short of it
# and no time lost
for presses, want in ((1, 1.5), (3, 3.0)):
    r = run(f"fast{presses}", 1000, presses)
    check(f"a 1 ms core at {want}x reaches it, and holds it: no turn short, no time lost",
          r is not None and r[0] == want and near(r[1], want) and r[6] == 0 and r[9] == 0,
          r and (r, short_of(r)))
    if r: print(f"       {r[1]:.2f}x, {short_of(r)}")

# Max (2026-11-23): no step, so every turn is short of the ceiling and the
# loop never sleeps after one; on the test clock a 1 ms core runs past 3x
r = run("max", 1000, 4)
check("a 1 ms core at Max never sleeps, and runs past 3x",
      r is not None and r[0] == 32.0 and r[5] == 0 and r[1] > 3.3, r and (r, short_of(r)))
if r: print(f"       {r[1]:.2f}x at Max, {short_of(r)}")

# A slow core, flat out short of the step. The old pacing ran whole frames in
# a two-refresh turn and slept the rest, so its speed came in steps: at 9 ms
# a frame 3 a turn, 1.5x; a little over 10.5 ms, 2 a turn, 1.0x (Pokemon
# Black on the Pixel 2, 11 to 12 ms, went 1.0x): every turn short, and a
# sleep after each. Flat out, with no sleep after a short turn, it is what the
# frame cost allows, 1.86x at 9 ms.
for tag, skippable in (("slow", True), ("slow-noskip", False)):
    r = run(tag, 9000, 3, skippable=skippable)
    how = "with a frameskip" if skippable else "with no frameskip"
    check(f"a 9 ms core {how} at 3x runs flat out, not whole frames a turn: "
          f"{limit_of(9000):.2f}x, every turn short and no sleep after one",
          r is not None and r[0] == 3.0 and near(r[1], limit_of(9000)) and r[6] == r[7] > 0
          and r[5] == 0, r and (r, short_of(r)))
    if r: print(f"       {r[1]:.2f}x, {short_of(r)}")

# At a step it can reach it is held to the step
r = run("slow15", 9000, 1, skippable=True)
check("the 9 ms core at 1.5x reaches it, and holds it: no turn short, no time lost",
      r is not None and r[0] == 1.5 and near(r[1], 1.5) and r[6] == 0 and r[9] == 0,
      r and (r, short_of(r)))
if r: print(f"       {r[1]:.2f}x, {short_of(r)}")

# The wall clock, the real sleep: never over the step, a busy machine or
# not, to the report's rounding and the one turn of debt the loop keeps
r = run("wall3", 1000, 3, clock=False)
check("on the wall clock, a 1 ms core at 3x is never over the step",
      r is not None and r[0] == 3.0 and 0 < r[1] <= 3.0 * 1.03, r and (r, short_of(r)))
if r: print(f"  --   on the wall clock this machine ran it at {r[1]:.2f}x ({short_of(r)}; not judged)")

shutil.rmtree(TMP, ignore_errors=True)
if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: fast forward runs a slow core flat out, and a fast one at its step, never over it")
