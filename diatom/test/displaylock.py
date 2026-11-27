"""The display lock (src/pace.h), end to end against the stub core.

The stub runs at 59.7275 fps, a GBA's rate. With --pace smooth (RUN
pace=smooth, the launcher's Timing: Smooth) and a display rate within 1% of
it (DIATOM_REFRESH_HZ, the desktop port's stand-in for a panel's mode), the
game runs at the display's, one frame a refresh, and says so; without it
(Exact), or with no display rate, at its own, as before. Diatom's summary
gives the frames and the time.

  - --pace smooth, DIATOM_REFRESH_HZ=60: "locked to the display", 60 a second
  - the same with no --pace (Exact): not locked, the stub's 59.73
  - --pace exact: not locked either
  - --pace smooth, no display rate: not locked, 59.73
  - --pace smooth, a 50 Hz display, more than 1% from the core: not locked

The rates are measured on Diatom's test clock (port/desktop.c,
DIATOM_TEST_CLOCK), where time moves only by the stub's frames and the
loop's sleeps. The two rates are 0.46% apart, and on the wall clock a busy
machine moved them more than that: the merge gate's load of 12 had the
locked run at 59.66 fps and the unlocked at 59.71 (2026-11-24). Here every
sleep overshoots by up to 6 ms, by a fixed sequence, as a busy machine's
do, and the loop's schedule must absorb it: each rate within 0.1% of its
target (the last sleep's overshoot is 0.075% of 8 s), no resync and no
debt cut. A loop that slept a period at a time, not to its schedule, would
lose 3 ms a frame and run at 51.

Then a look on the wall clock, the real sleep, both ways: paced, never
faster than the target. Only that: a busy machine makes a loop slower, never
faster, so it holds under any load, and how much slower says nothing of the
lock. The rest is judged everywhere.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-now does. SDL's dummy drivers: no window, no sound.

    python3 test/displaylock.py
"""
import os, re, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-lock-")
ROM = f"{TMP}/A Game (World).stub"
with open(ROM, "wb") as f:
    f.write(b"STUB" + bytes(range(60)))

fails = []
def check(name, ok, got):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

OVERSHOOT = 6000   # us, the most a sleep runs over on the test clock

def run(refresh=None, extra=(), clock=True, frames=480):
    """One game of `frames` frames, on the test clock unless `clock` is
    False. Diatom's output, the rate it measured and its target, and its
    counts of resyncs and debts cut (pace.h)."""
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", STUBCORE_FRAME_US="2000")
    env.pop("DIATOM_REFRESH_HZ", None)
    env.pop("DIATOM_TEST_CLOCK", None)
    if refresh is not None:
        env["DIATOM_REFRESH_HZ"] = str(refresh)
    if clock:
        env["DIATOM_TEST_CLOCK"] = str(OVERSHOOT)
    out = subprocess.run([BIN, "--core", STUB, "--rom", ROM, "--save", TMP, "--system", TMP,
                          "--frames", str(frames), *extra], env=env, capture_output=True, text=True,
                         timeout=300).stdout
    m = re.search(r"(\d+) frames in ([\d.]+)s = ([\d.]+) fps \(target ([\d.]+)\)", out)
    fps = float(m.group(3)) if m else 0.0
    target = float(m.group(4)) if m else 0.0
    r = re.search(r"diatom: (\d+) resync\(s\)", out)
    c = re.search(r"diatom: (\d+) debt\(s\) cut", out)
    lost = (int(r.group(1)) if r else -1, int(c.group(1)) if c else -1)
    return out, fps, target, lost

def near(fps, want):
    return abs(fps - want) / want <= 0.001

SMOOTH = ("--pace", "smooth")
out, fps, target, lost = run(60, SMOOTH)
check("Smooth on a 60 Hz display: locked, and says so", "locked to the display, 60.000 Hz" in out, out[-600:])
check("  ...its frames' target the display's 60", abs(target - 60.0) < 0.001, target)
check("  ...and run at it, within 0.1%, its sleeps running over", near(fps, 60.0), fps)
check("  ...every overshoot absorbed: no resync, no debt cut", lost == (0, 0), lost)
out_c, fps_c, target_c, lost_c = run(60)
check("no --pace (Exact): not locked, and nothing said", "locked to the display" not in out_c and
      "not the display's" not in out_c, out_c[-600:])
check("  ...the core's own 59.7275", abs(target_c - 59.7275) < 0.001, target_c)
check("  ...and run at it, within 0.1%", near(fps_c, 59.7275), fps_c)
check("  ...every overshoot absorbed: no resync, no debt cut", lost_c == (0, 0), lost_c)
check("  ...slower than locked, by the 0.46%", fps - fps_c >= 0.2, (fps, fps_c))
out_e, fps_e, target_e, _ = run(60, ("--pace", "exact"))
check("--pace exact: not locked either", "locked to the display" not in out_e and abs(target_e - 59.7275) < 0.001
      and near(fps_e, 59.7275), (target_e, fps_e, out_e[-300:]))
out_n, fps_n, target_n, _ = run(None, SMOOTH)
check("Smooth, no display rate: not locked, as before", "locked to the display" not in out_n and
      abs(target_n - 59.7275) < 0.001 and near(fps_n, 59.7275), (target_n, fps_n, out_n[-300:]))
out_p, fps_p, target_p, _ = run(50, SMOOTH)
check("Smooth on a 50 Hz display, 16% from the core: not locked", "more than 1% apart" in out_p and
      abs(target_p - 59.7275) < 0.001 and near(fps_p, 59.7275), (target_p, fps_p, out_p[-300:]))

# The wall clock: the real sleep paces both ways. Never over the target by
# more than the summary's rounding: the loop's last turn sleeps to its
# deadline, so the time is at least the frames' periods
out_w, fps_w, target_w, _ = run(60, SMOOTH, clock=False, frames=240)
out_wc, fps_wc, target_wc, _ = run(60, clock=False, frames=240)
check("wall clock, locked: the target 60, and paced, never faster",
      abs(target_w - 60.0) < 0.001 and 0 < fps_w <= 60.0 * 1.002, (target_w, fps_w))
check("wall clock, not locked: the target 59.7275, and paced, never faster",
      abs(target_wc - 59.7275) < 0.001 and 0 < fps_wc <= 59.7275 * 1.002, (target_wc, fps_wc))
print(f"  --   on the wall clock this machine ran them at {fps_w:.2f} and {fps_wc:.2f} fps (not judged)")

if fails:
    print(f"displaylock: {len(fails)} FAILED")
    sys.exit(1)
print("ok: with pace=smooth a core within 1% of the display's rate runs at it and says so; without it, no rate or a far one, its own")
