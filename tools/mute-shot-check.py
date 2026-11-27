#!/usr/bin/env python3
"""The Brick's mute switch shows on the screen and in the log (#482).

ZyperOS reads the Brick's side switch as mute and silences every output,
headphones too. The volume bar used to move while nothing played, and nothing
on the screen or in the log said why. This runs the host launcher as a Brick
(--device brick) with the switch forced down (ZYPEROS_MUTE_SWITCH=on, read by
the host's plat_mute_poll) and up, in both themes, with the volume bar up
(--osd volume 12):

- the log says "brick: mute switch on" once, or "off" once;
- the top bar's corner has the crossed speaker in **text-soft** only muted;
- the volume bar writes "Muted" in **text** after the crossed speaker, and
  shows no step, in a pill only as wide as the two, centered, at the bar's
  height and place; not muted, it shows the steps and no word, 452 wide;
- a profile with no switch (--device desktop, the Smart Pro's case) says
  nothing of it and shows the steps, the variable set or not.
"""
import importlib.util
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
spec = importlib.util.spec_from_file_location("tour_shots", os.path.join(ROOT, "tools", "tour-shot-check.py"))
t = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t)
t.D = os.path.join(ROOT, "build-native", "mute-shot-check")

# The bar's steps on the screen: the buffer's OSDBAR_WORD_X0..X1 (156 to 494)
# from its left at (1024 - 596) / 2, its middle at 10 + 44
STEPS = (214 + 156, 54 - 10, 214 + 494, 54 + 10)
# The corner, from the fan's left to the battery's tip at 960
CORNER = (800, 52, 962, 77)


def run(name, device, muted, *args):
    """A shot, and the lines it said about the switch."""
    path = os.path.join(t.D, name + ".png")
    env = t.env()
    env.pop("ZYPEROS_MUTE_SWITCH", None)
    if muted:
        env["ZYPEROS_MUTE_SWITCH"] = "on"
    p = subprocess.run([t.BIN, "--device", device, "--shot", path] + list(args), env=env,
                       cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with open(os.path.join(t.D, "log"), "a") as log:
        log.write(p.stdout.decode("utf-8", "replace"))
    if p.returncode != 0 or not os.path.isfile(path):
        t.ck(False, "%s: the shot failed (exit %d), see %s" % (name, p.returncode, os.path.join(t.D, "log")))
        return None, []
    said = [l for l in p.stdout.decode("utf-8", "replace").splitlines() if "mute switch" in l]
    return t.zc.load(path, 100), said


if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_ttf", "SDL2_image"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL):
    print("skipped: no SDL2 for the mute switch's shots")
    sys.exit(0)
if not os.access(t.BIN, os.X_OK):
    print("FAIL no %s: make native first" % t.BIN)
    sys.exit(1)
shutil.rmtree(t.D, ignore_errors=True)
for sub in ("root", "Roms/NES", "userdata", "shared"):
    os.makedirs(os.path.join(t.D, sub))
shutil.copy(os.path.join(ROOT, "config", "systems.cfg"), os.path.join(t.D, "root"))
open(os.path.join(t.D, "Roms/NES/Contra.nes"), "w").close()

for theme in ("light", "dark"):
    text, soft = t.TEXT[theme], t.TEXT_SOFT[theme]
    on, said_on = run("brick-muted-" + theme, "brick", True, "--theme", theme, "--osd", "volume", "12")
    off, said_off = run("brick-" + theme, "brick", False, "--theme", theme, "--osd", "volume", "12")
    desk, said_desk = run("desktop-" + theme, "desktop", True, "--theme", theme, "--osd", "volume", "12")
    if None in (on, off, desk):
        continue
    print("%s:" % theme)
    t.ck(said_on == ["brick: mute switch on"], "muted, the log says it once: %r" % said_on)
    t.ck(said_off == ["brick: mute switch off"], "not muted, the log says it once: %r" % said_off)
    t.ck(said_desk == [], "no switch, no word of one: %r" % said_desk)
    soft_on, soft_off = t.count(on, soft, CORNER), t.count(off, soft, CORNER)
    t.ck(soft_on >= soft_off + 40,
         "the corner's crossed speaker in text-soft, muted only (%d pixels against %d)" % (soft_on, soft_off))
    t.ck(t.count(desk, soft, CORNER) == soft_off, "no switch: the corner as not muted")
    t.ck(t.count(on, text, STEPS) > 60, "muted, the bar writes Muted where the steps were")
    t.ck(t.count(off, text, STEPS) == 0, "not muted, no word in the bar")
    t.ck(t.count(desk, text, STEPS) == 0, "no switch, no word in the bar")
    lit = sum(1 for x in range(STEPS[0], STEPS[2]) if t.zc.px(off, x, 54) != t.zc.px(on, x, 54))
    t.ck(lit > 200, "and the steps the bar shows not muted are gone muted (%d pixels differ)" % lit)
    # The muted pill hugs the crossed speaker and the word, centered, as tall
    # as the bar; the bar of steps keeps its 452 (286 to 738)
    ground = t.GROUND[theme]
    def span(img, y):
        xs = [x for x in range(250, 775) if t.zc.px(img, x, y) != ground]
        return (xs[0], xs[-1]) if xs else (0, 0)
    l_on, r_on = span(on, 54)
    l_off, r_off = span(off, 54)
    t.ck(abs(l_off - 286) <= 6 and abs(r_off - 737) <= 6, "not muted, the pill 452 wide (its shadow too): %d to %d" % (l_off, r_off))
    t.ck(r_on - l_on < 340 and l_on > l_off + 40 and r_on < r_off - 40,
         "muted, the pill only as wide as the speaker and the word: %d to %d" % (l_on, r_on))
    t.ck(abs((l_on + r_on) - (l_off + r_off)) <= 3, "and centered as the bar is: %d to %d" % (l_on, r_on))
    words = [x for x in range(l_on, r_on) for y in range(STEPS[1], STEPS[3]) if t.zc.px(on, x, y) == text]
    t.ck(words and min(words) > l_on + 60 and max(words) < r_on - 20,
         "the word inside the pill, after the speaker, with the end's room after it")
    top_on = [y for y in range(0, 100) if t.zc.px(on, 512, y) != ground]
    top_off = [y for y in range(0, 100) if t.zc.px(off, 512, y) != ground]
    t.ck(top_on[:1] == top_off[:1], "as high from the top as the bar of steps")

if t.fails:
    print("mute-shot-check: %d FAILED (shots in %s)" % (t.fails, t.D))
    sys.exit(1)
print("ok: the mute switch is said in the log, in the corner and on the volume bar")
