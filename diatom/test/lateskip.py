"""frameskip=late and frameskip=steady at 1x, end to end against the stub core.

A DS game sent with frameskip=late (a PSP game too, its call two periods
long when it draws at 30) leaves out the drawing of a frame that starts
late, two in a row at most, so a slow stretch shows frames 16.7 and
33.3 ms apart in turn (Mario Kart DS on the Pixel 2: a median 41 fps, uneven,
2026-11-16). frameskip=steady, a RUN word the launcher no longer sends, draws
exactly every second frame while frames start late, and every frame again
once they have started on time for half a second (main.c, STEADY_HOLD).

The stub offers a frameskip (STUBCORE_SKIPPABLE), takes a set time in each
frame (STUBCORE_FRAME_US_AT: fast, then slower than a refresh for a stretch,
then fast again) and writes which frames Diatom had drawn, a 1 or a 0 each
(STUBCORE_SHOWN_LOG).

On Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK): time moves only
by the stub's frames and the loop's sleeps, so a fast frame starts on time
and a slow one late on any machine, however busy, and every frame is judged
exactly. On the wall clock this check had to let fast frames be left out
now and then, and judge them only when this machine had started them on
time by the times the stub wrote down; the merge gate's load of 12 still
had steady's drawn too few (2026-11-24). Two more runs have the loop's
sleeps overshoot by up to 6 ms, by a fixed sequence, as a busy machine's do:
a frame that late is less than half a period behind, no late frame, and
every one is drawn.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-hotkeys does. SDL's dummy drivers: no window, no sound.

    python3 test/lateskip.py
"""
import os, re, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-lateskip-")
fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

# Fast for 120 frames, 25 ms a frame (slower than a refresh) for 120, fast
# again. The log has a line for each of the stub's frames, the three of the
# warmup (main.c) first, so a frame's line is its number in the schedule.
SLOW_FROM, SLOW_TO = 120, 240
SCHEDULE = f"0:2000,{SLOW_FROM}:25000,{SLOW_TO}:2000"
STEADY_HOLD = 30   # main.c

def run(tag, skip, frames=420, schedule=SCHEDULE, extra=None, overshoot=0):
    """One game with frameskip=`skip` on the test clock, DIATOM_TEST_CLOCK
    `overshoot` (port/desktop.c: the most a sleep runs over, in us, and the
    sound device's lateness and slowness), until the stub has run `frames`
    frames. The frames
    it was told to draw, as a string of 1 and 0, and Diatom's output.
    `extra`: more of the stub's settings (a PSP game's, below)."""
    sock, out, log = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.out", f"{TMP}/{tag}.shown"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", DIATOM_TEST_CLOCK=str(overshoot),
               STUBCORE_SKIPPABLE="1", STUBCORE_FRAME_US_AT=schedule,
               STUBCORE_SHOWN_LOG=log, **(extra or {}))
    with open(out, "w") as f:
        p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                             env=env, stdout=f, stderr=subprocess.STDOUT)
    # The waits are the wall clock's, and only waits: a busy machine makes
    # them longer, never the frames different
    end = time.monotonic() + 30
    while not os.path.exists(sock) and time.monotonic() < end:
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.sendall(f"RUN\tcore={STUB}\tframeskip={skip}\n".encode())
    end = time.monotonic() + 120
    while time.monotonic() < end:
        time.sleep(0.1)
        try:
            with open(log) as f: n = sum(1 for _ in f)
        except OSError: n = 0
        if n >= frames: break
    s.sendall(b"STOP\n")
    end = time.monotonic() + 60
    while time.monotonic() < end and "frames in" not in open(out, errors="replace").read():
        time.sleep(0.1)
    s.sendall(b"QUIT\n")
    try: p.wait(60)
    except subprocess.TimeoutExpired: p.kill()
    lines = [l.split() for l in open(log)] if os.path.exists(log) else []
    shown = "".join(l[0] for l in lines if len(l) == 2)
    return shown, open(out, errors="replace").read()

def every(x):
    """Every frame drawn"""
    return len(x) > 20 and set(x) == {"1"}

def cycle(x, unit):
    """`x` is `unit` over and over, from wherever in it x starts"""
    return len(x) > 50 and x in unit * (len(x) // len(unit) + 2)

late, late_out = run("late", "late")
steady, steady_out = run("steady", "steady")
check("both ran past the slow stretch", len(late) > SLOW_TO + 120 and len(steady) > SLOW_TO + 120,
      (len(late), len(steady)))

# Fast, every frame starts on time: none left out
check("late, fast, before the stretch: every frame drawn", every(late[10:SLOW_FROM]), late[10:SLOW_FROM])
check("steady, fast, before the stretch: every frame drawn", every(steady[10:SLOW_FROM]), steady[10:SLOW_FROM])
# Slow, every frame starts late: late leaves out two and draws the third,
# steady draws every second
slow_late = late[SLOW_FROM + 10:SLOW_TO - 5]
slow_steady = steady[SLOW_FROM + 10:SLOW_TO - 5]
check("late, slow: two left out and one drawn, over and over", cycle(slow_late, "001"), slow_late)
check("late, slow: it starts within the stretch's first frames", "0" in late[SLOW_FROM:SLOW_FROM + 4],
      late[SLOW_FROM - 2:SLOW_FROM + 6])
check("steady, slow: exactly every second frame drawn", cycle(slow_steady, "01"), slow_steady)
# Fast again: the debt left from the stretch (cut to the sound queue's
# depth, pace.h) is repaid in a frame or two, and late draws every frame
check("late, fast again: every frame drawn, a few frames after the stretch",
      every(late[SLOW_TO + 4:]), late[SLOW_TO - 5:])
# Steady goes on drawing every second frame for STEADY_HOLD frames from the
# last that started late (the stretch's last, or one of the two after it
# that repay the debt), then every frame: its last left out is the hold's
# last frame or the one before
back = len(steady.rstrip("1"))   # one past the last left out
check("steady, fast again: every second frame for half a second after the stretch, then every frame",
      SLOW_TO + STEADY_HOLD - 1 <= back <= SLOW_TO + 2 + STEADY_HOLD and
      cycle(steady[SLOW_FROM + 10:back], "01") and every(steady[back:]),
      (back, steady[SLOW_TO - 5:back + 5]))
check("the summary names the mode", "(frameskip=steady)" in steady_out and "(frameskip=late)" in late_out,
      [l for l in (late_out + steady_out).splitlines() if "left undrawn" in l])

# A sleep that overshoots, as a busy machine's does: up to 6 ms, under half a
# period (8.4 ms), so no frame of these is late, and none is left out
FAST = "0:2000"
late_o, _ = run("late-over", "late", frames=300, schedule=FAST, overshoot=6000)
steady_o, _ = run("steady-over", "steady", frames=300, schedule=FAST, overshoot=6000)
check("late, sleeps up to 6 ms over: every frame drawn", every(late_o[10:300]), late_o[10:300])
check("steady, sleeps up to 6 ms over: every frame drawn", every(steady_o[10:300]), steady_o[10:300])

# A PSP game's, frameskip|PSP|late (config/systems.cfg): PPSSPP's retro_run
# runs until the game shows a new picture, so a game that draws at 30 brings
# two periods of sound with each call, and Diatom paces PPSSPP by its sound
# (main.c, paced_by_sound). The stub as that core: its library's name, two
# periods a call (STUBCORE_SPAN), on time at 2 ms a call, then 45 ms a call
# (more than the 33.5 ms its sound lasts: behind), then 2 ms again. On time,
# a call of two periods is no late frame: every one drawn, as the game draws
# today. Behind, frames left out, two in a row at most, as a DS game's.
#
# Such a core's frame is late by its work, not by the schedule (main.c,
# sound_debt_us), so a turn the loop itself started late is no late frame:
# here the sleeps run up to 15 ms over, past half a period, which a DS
# game's rule would count late, and the sound device is as SDL's dummy one
# was measured on a Mac (2026-11-24): its periods 22.5 ms apart where 21.3
# are due, so 5% slow, which rate control's 0.5% cannot follow and pushes
# wait for room (audio.c, HOLD), and each period up to 10 ms late. Judged by
# the schedule instead, 40% of the on-time frames were left out of this
# run.
PSP_FROM, PSP_TO = 80, 160
psp, psp_out = run("psp", "late", frames=280,
                   schedule=f"0:2000,{PSP_FROM}:45000,{PSP_TO}:2000",
                   extra={"STUBCORE_NAME": "PPSSPP", "STUBCORE_SPAN": "2"},
                   overshoot="15000:10000:50000")
slow_psp = psp[PSP_FROM + 10:PSP_TO - 5]
check("PSP: ran past the slow stretch", len(psp) > PSP_TO + 60, len(psp))
check("PSP: paced by its sound", "paced by the sound it made" in psp_out,
      [l for l in psp_out.splitlines() if "paced" in l])
held = re.search(r"audio waited [\d.]+ s for room, over (\d+) push", psp_out)
check("PSP: pushes waited for room on the slow device", held and int(held.group(1)) > 20,
      held and held.group(0))
# On time is judged by the work, not the schedule (main.c, sound_debt_us):
# a sound-paced turn whose push waited for room starts the next one late by
# the schedule while the game is ahead
check("PSP, on time (two periods a call): every frame drawn", every(psp[10:PSP_FROM]), psp[10:PSP_FROM])
check("PSP, behind: two left out and one drawn, over and over", cycle(slow_psp, "001"), slow_psp)
check("PSP, on time again: every frame drawn, a few frames after the stretch",
      every(psp[PSP_TO + 4:]), psp[PSP_TO - 5:])
check("PSP: the summary names the mode", "(frameskip=late)" in psp_out,
      [l for l in psp_out.splitlines() if "left undrawn" in l])

shutil.rmtree(TMP, ignore_errors=True)
if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: frameskip=late leaves late frames out two in three, frameskip=steady every second one "
      "and for half a second after, and both draw every frame on time, on the test clock")
