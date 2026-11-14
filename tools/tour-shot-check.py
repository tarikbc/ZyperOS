#!/usr/bin/env python3
"""The tutorial as the launcher draws it, and the start that shows it.

    make check-tour-shots

tools/tour-check.c holds the tutorial's steps and Zyper's moves as numbers;
neither sees what src/tourdraw.c and src/main.c make of them, which is where
a second Zyper lands on the screen, a bubble runs into "START: skip", or the
first start forgets the tutorial. So this shoots the host build headless
(--tour) and reads the pixels back, in both themes:

  - a step waiting for its press has no Zyper in the corner and no check;
  - its reward has him there (his ears' pink) and the accent check disc;
  - a wrong press has him there with his tongue in, and his bubble;
  - the longest answer, "That's SELECT. Try VOL+ or VOL-.", keeps clear of
    "START: skip" at its right;
  - the power step's Zyper is the stage's while it waits, a wrong press
    there gets its bubble and no second Zyper, and its reward has the
    corner's and none on the stage: one Zyper at a time;
  - the finish inherits that corner Zyper, draws him during his exit, and
    then draws only the large hello;
  - on the Pixel 2 (--fn-volume) each lesson's handheld has its key lit on
    the side of it the key is on, the shoulders its note names (L1/R1 in
    Move Around, L2/R2 in Music) lit on its top, tilted back to show it, and
    no others, it stays in its kept texture's box in every frame of every
    turn (--handheld-box tour), the lesson block is the Brick's moved
    TOURDEV_SHIFT right, its words keep the screen's inset from the right
    edge, Zyper's corner and bubble keep their place, and the welcome and
    the finish are the Brick's, pixel for pixel;
  - the choices after the lessons (Theme, Home Screen, Sounds, Menu Music)
    ring and name the focused choice alone and nothing else, put the
    reward's check on the kept one, show the home screen previews (Light at
    the left and Dark at the right in Theme, whatever the screen's theme),
    and are the same on the Pixel 2 as on the Brick: no handheld, nothing
    moved;

and it starts the launcher itself, headless, on a fresh database, where the
tutorial must be drawn before the shelf (tour_run says "tour: the first
start's, on the screen" on stderr once it has presented a frame), and
again after `--set tour.done=1`, where the shelf's first frame must come and
no tutorial.

The card is a scratch tree under build-native/tour-shot-check, as check-zyper
makes one. It needs the launcher built, which needs SDL2, and skips cleanly
without it. The PNGs are read by check-zyper's reader, the standard library
alone.

Exits non-zero and says what failed.
"""
import importlib.util
import os
import select
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = os.path.join(ROOT, "build-native", "tour-shot-check")
BIN = os.path.join(ROOT, "build-native", "zyperos")

_spec = importlib.util.spec_from_file_location("zyper_check",
                                               os.path.join(ROOT, "tools", "zyper-check.py"))
zc = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(zc)

EAR, TONGUE = zc.EAR, zc.TONGUE
ACCENT = (190, 242, 60)                       # **accent**, both themes
TEXT = {"light": (26, 28, 26), "dark": (228, 226, 216)}         # **text**
TEXT_DIM = {"light": (90, 93, 90), "dark": (150, 153, 152)}     # **text-dim**
TEXT_SOFT = {"light": (61, 64, 62), "dark": (195, 195, 186)}    # **text-soft**: a button hint's word

CORNER = (20, 580, 172, 768)    # Zyper's corner, his hop and his bubble's start
STAGE = (172, 84, 852, 424)     # the stage
BUBBLE = (168, 600, 1000, 720)  # his bubble beside him, one line or two
LEGEND = (600, 700, 1024, 750)  # START's hint, "(START) skip"
CHECK = (852, 59)               # inside the check disc, above its tick
HELLO = (100, 180, 440, 570)    # the finish's large Zyper, away from the corner
GROUND = {"light": (230, 228, 218), "dark": (17, 19, 16)}         # **ground**

fails = 0


def ck(cond, what):
    global fails
    if not cond:
        print("  FAIL", what)
        fails += 1


def env():
    return dict(os.environ, ZYPEROS_HEADLESS="1", SDL_VIDEODRIVER="dummy",
                SDL_RENDER_DRIVER="software", ZYPEROS_ROOT=os.path.join(D, "root"),
                ZYPEROS_CARD=D, ZYPEROS_ROMS=os.path.join(D, "Roms"),
                ZYPEROS_USERDATA=os.path.join(D, "userdata"),
                ZYPEROS_SHARED=os.path.join(D, "shared"), ZYPEROS_WEB=D,
                ZYPEROS_FONT=os.path.join(ROOT, "res", "fonts", "menu.ttf"))


def shot(name, *args):
    path = os.path.join(D, name + ".png")
    with open(os.path.join(D, "log"), "a") as log:
        st = subprocess.call([BIN, "--shot", path] + list(args), env=env(), cwd=ROOT,
                             stdout=log, stderr=log)
    if st != 0 or not os.path.isfile(path):
        ck(False, "%s: the shot failed (exit %d), see %s" % (name, st, os.path.join(D, "log")))
        return None
    return zc.load(path, 768)


def count(img, col, box):
    return zc.count(img, col, box)


def xs(img, col, box):
    """The x of every pixel of `col` in `box`."""
    return [x for y in range(box[1], box[3]) for x in range(box[0], box[2])
            if zc.px(img, x, y) == col]


def first_start():
    """Start the launcher with no --shot and read stderr until it says which
    came first, the tutorial or the shelf; then stop it."""
    p = subprocess.Popen([BIN], env=env(), cwd=ROOT, stdout=subprocess.DEVNULL,
                         stderr=subprocess.PIPE)
    seen = None
    t0 = time.time()
    buf = b""
    try:
        while time.time() - t0 < 15 and seen is None:
            ready, _, _ = select.select([p.stderr], [], [], 0.1)
            if not ready:
                if p.poll() is not None:
                    break
                continue
            chunk = os.read(p.stderr.fileno(), 4096)
            if not chunk:
                break
            buf += chunk
            if b"tour: the first start's, on the screen" in buf:
                seen = "tour"
            elif b"first frame" in buf:
                seen = "shelf"
    finally:
        p.kill()
        p.wait()
    with open(os.path.join(D, "log"), "ab") as log:
        log.write(buf)
    return seen


# The handheld beside each lesson, on the Pixel 2 (--fn-volume): DESIGN.md 11.10
# and src/tourdevice.h. Its column is left of the moved stage; a lesson's key
# is lit in **accent** on the side of the column it is on.
SHIFT = 104                     # TOURDEV_SHIFT
COLUMN_L = (0, 82, 138, 442)    # the column's halves about the handheld's middle
COLUMN_R = (138, 82, 276, 442)
ON_ACCENT = (17, 19, 16)                                         # **on-accent**
MENU_DOT = {"light": (61, 64, 62), "dark": (150, 153, 152)}      # text-soft, text-dim
INSET = 24                      # **inset**: text from every screen edge
WORDS = (1024 - INSET, 480, 1024, 660)  # right of the title, line and note
LIT_SIDE = {"move": "lr", "a": "r", "b": "r", "y": "r", "x": "r", "menu": "r",
            "select": "lr", "vol": "l", "bright": "lr", "power": "r"}
# The shoulders over the top edge, facing front (10 degrees, 3.55 units a
# millimetre): L1/R1 outside, L2/R2 inside, where a lit one stands up out of
# the edge (y 109 to 118). Boxes inside each cap's fill and clear of the
# rings of its neighbours, which are **accent** too on Salt & Pepper.
SHOULDERS = {"l1": (30, 104, 70, 118), "r1": (206, 104, 246, 118),
             "l2": (95, 104, 107, 118), "r2": (172, 104, 186, 118)}
LIT_SHOULDERS = {"move": ("l1", "r1"), "select": ("l2", "r2")}
# ...and where the lessons that light shoulders have them: tilted back 25
# degrees (TD_PITCH_DEG), the top showing with the caps standing on it, each
# box inside a cap's face and top, clear of its neighbours' rings
TILTED_SHOULDERS = {"l1": (38, 110, 64, 122), "r1": (214, 124, 240, 136),
                    "l2": (99, 118, 106, 127), "r2": (168, 124, 176, 133)}


def shifted_same(p2, brick, box):
    """How many pixels of `box` on the Brick are not where the Pixel 2 has
    them, SHIFT to the right; every fourth pixel each way."""
    return sum(1 for y in range(box[1], box[3], 4) for x in range(box[0], box[2], 4)
               if zc.px(p2, x + SHIFT, y) != zc.px(brick, x, y))


def handheld_box():
    """--handheld-box tour: the handheld measured in every lesson and every
    frame of the turn into it against its kept texture's box"""
    path = os.path.join(D, "box.png")
    p = subprocess.run([BIN, "--shot", path, "--theme", "light", "--handheld-box", "tour"],
                       env=env(), cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    err = p.stderr.decode("utf-8", "replace")
    with open(os.path.join(D, "log"), "a") as log:
        log.write(err)
    out = [l for l in err.splitlines() if l.startswith("HANDHELD-BOX:")]
    for l in out[:5]:
        print("  " + l)
    measured = [l for l in err.splitlines() if l.startswith("handheld-box:") and "measured" in l]
    n = int(measured[-1].split(" and ")[1].split()[0]) if measured else 0
    ck(p.returncode == 0 and not out and n >= 400,
       "the tutorial's handheld stays in its kept box: %d poses out of %d measured"
       % (len(out), n))


def pixel2():
    print("on the Pixel 2: the handheld beside each lesson, its key lit on the side it is "
          "on, the lesson block moved right and the rest where it was")
    handheld_box()
    for th in ("light", "dark"):
        for step, side in LIT_SIDE.items():
            img = shot("p2-%s-%s" % (step, th), "--theme", th, "--fn-volume", "--tour", step, "2000")
            if img is None:
                continue
            left, right = count(img, ACCENT, COLUMN_L), count(img, ACCENT, COLUMN_R)
            ck(("l" in side) == (left > 30) and ("r" in side) == (right > 30),
               "%s %s: the lit key is on the %s of the handheld (lit pixels left %d, right %d)"
               % (th, step, {"l": "left", "r": "right", "lr": "both sides"}[side], left, right))
            # The shoulders its note names stand up lit over the top edge,
            # and no others (Move Around's L1/R1, Music's L2/R2)
            boxes = TILTED_SHOULDERS if step in LIT_SHOULDERS else SHOULDERS
            lit = {k: count(img, ACCENT, box) for k, box in boxes.items()}
            want = LIT_SHOULDERS.get(step, ())
            ck(all((lit[k] > 20) == (k in want) for k in SHOULDERS),
               "%s %s: lit shoulders %s (accent pixels %s)"
               % (th, step, "/".join(want).upper() or "none", lit))
            # Moved right, a line wider than the stage (Music's) stays the
            # inset from the edge: nothing but the ground there.
            ground = zc.px(img, 1023, WORDS[1] - 10)
            near = sum(1 for y in range(WORDS[1], WORDS[3]) for x in range(WORDS[0], WORDS[2])
                       if zc.px(img, x, y) != ground)
            ck(near == 0, "%s %s: the lesson's words keep the inset from the right edge "
               "(%d pixels in it)" % (th, step, near))
        # MENU's three dots read as three, lit (the Settings lesson) and at
        # rest (Power), in both themes: down the key's middle, which turned
        # 52 degrees at 3.95 units a millimetre is x 210, y 180 to 210.
        for step, dot in (("menu", ON_ACCENT), ("power", MENU_DOT[th])):
            img = shot("p2-%s-%s" % (step, th), "--theme", th, "--fn-volume", "--tour", step, "2000")
            if img is not None:
                down = [zc.px(img, 210, y) == dot for y in range(180, 210)]
                runs = sum(1 for i, d in enumerate(down) if d and (i == 0 or not down[i - 1]))
                ck(runs == 3, "%s %s: MENU on the handheld shows its three dots (%d)"
                   % (th, step, runs))
        # The same step, the Brick's way and the Pixel 2's: the stage and
        # what plays on it, the glyph and the words, moved and otherwise the
        # same; the Brick has no handheld.
        for step in ("menu", "power"):
            brick = shot("brick-%s-%s" % (step, th), "--theme", th, "--tour", step, "2000")
            p2 = shot("p2-%s-%s" % (step, th), "--theme", th, "--fn-volume", "--tour", step, "2000")
            if None in (brick, p2):
                continue
            ck(count(brick, ACCENT, (0, 82, 172, 442)) == 0,
               "%s %s: the Brick has no handheld at the left" % (th, step))
            off = shifted_same(p2, brick, (STAGE[0], 20, STAGE[2] - 4, 660))
            ck(off == 0, "%s %s: the lesson block is the Brick's moved %d right (%d pixels "
               "differ)" % (th, step, SHIFT, off))
        # Zyper's corner, his bubble and START: skip stay where they were.
        done = shot("p2-a-done-" + th, "--theme", th, "--fn-volume", "--tour", "a", "4000", "300")
        longest = shot("p2-vol-wrong-" + th, "--theme", th, "--fn-volume", "--tour", "vol",
                       "3000", "--tour-wrong", "select", "420")
        if None not in (done, longest):
            ck(count(done, EAR, CORNER) > 40, "%s: the Pixel 2's reward has Zyper in the corner" % th)
            bub, leg = xs(longest, TEXT[th], BUBBLE), xs(longest, TEXT_SOFT[th], LEGEND)
            ck(bub and leg and max(bub) + 8 <= min(leg),
               "%s: on the Pixel 2 the longest answer stays clear of START: skip" % th)
        # Not on the welcome or the finish: those are the Brick's, pixel for pixel.
        for step in ("welcome", "finish"):
            brick = shot("brick-%s-%s" % (step, th), "--theme", th, "--tour", step, "2000")
            p2 = shot("p2-%s-%s" % (step, th), "--theme", th, "--fn-volume", "--tour", step, "2000")
            if None not in (brick, p2):
                off = sum(1 for y in range(0, 768, 4) for x in range(0, 1024, 4)
                          if zc.px(p2, x, y) != zc.px(brick, x, y))
                ck(off == 0, "%s %s: no handheld and nothing moved (%d pixels differ)"
                   % (th, step, off))


# The choices, as src/tourdraw.c lays them out: two 380 x 285 at y 96, 56
# apart, or four 180 x 180 tiles at y 130, 32 apart; a name 48 under each.
def choice_box(n, i):
    if n > 2:
        x0 = (1024 - (n * 180 + (n - 1) * 32)) // 2
        return (x0 + i * 212, 130, 180, 180)
    return (104 + i * 436, 96, 380, 285)


CHOICE_N = {"theme": 2, "home": 2, "sounds": 4, "music": 2}


def ring_px(img, b):
    """Accent pixels in the band 10 to 16 above a choice, where its ring is."""
    x, y, w, h = b
    return count(img, ACCENT, (x, y - 16, x + w, y - 10))


def pill_px(img, b):
    x, y, w, h = b
    cx, cy = x + w // 2, y + h + 48
    return count(img, ACCENT, (cx - 30, cy - 20, cx + 30, cy + 20))


def choices():
    print("the choices: the focused one ringed and named, the kept one checked, the "
          "previews, and the Pixel 2 the Brick's")
    for th in ("light", "dark"):
        for step, n in CHOICE_N.items():
            for pick in range(n):
                img = shot("c-%s-%d-%s" % (step, pick, th), "--theme", th, "--tour", step,
                           "2000", "--tour-music", "--tour-pick", str(pick))
                p2 = shot("c-%s-%d-%s-p2" % (step, pick, th), "--theme", th, "--tour", step,
                          "2000", "--tour-music", "--tour-pick", str(pick), "--fn-volume")
                if None in (img, p2):
                    continue
                for i in range(n):
                    b = choice_box(n, i)
                    ring, pill = ring_px(img, b), pill_px(img, b)
                    if i == pick:
                        ck(ring > b[2] and pill > 400, "%s %s: choice %d looked at is ringed "
                           "and its name on the pill (%d, %d)" % (th, step, i, ring, pill))
                    else:
                        ck(ring == 0 and pill == 0, "%s %s: choice %d not looked at has no ring "
                           "and no pill (%d, %d)" % (th, step, i, ring, pill))
                ck(zc.px(img, *CHECK) != ACCENT and count(img, EAR, CORNER) == 0,
                   "%s %s: no check and no Zyper while it waits" % (th, step))
                off = sum(1 for y in range(0, 768, 4) for x in range(0, 1024, 4)
                          if zc.px(p2, x, y) != zc.px(img, x, y))
                ck(off == 0, "%s %s: the Pixel 2's is the Brick's, no handheld (%d pixels "
                   "differ)" % (th, step, off))
        # Theme: the home screen in Light at the left and in Dark at the right,
        # whatever the screen is in; Home Screen in the screen's theme
        img = shot("c-theme-%s" % th, "--theme", th, "--tour", "theme", "2000", "--tour-music")
        home = shot("c-home-%s" % th, "--theme", th, "--tour", "home", "2000", "--tour-music")
        if None not in (img, home):
            for i, want in enumerate(("light", "dark")):
                x, y, w, h = choice_box(2, i)
                ck(zc.px(img, x + 190, y + 40) == GROUND[want],
                   "%s theme: preview %d is the home screen in %s" % (th, i, want))
                ck(zc.px(home, x + 190, y + 40) == GROUND[th],
                   "%s home: preview %d is the home screen in the screen's theme" % (th, i))
            ck(any(zc.px(home, 540 + 190, 96 + y) != zc.px(home, 104 + 190, 96 + y)
                   for y in range(60, 240, 4)), "%s home: Spotlight and Gallery differ" % th)
        # The reward: the check on the kept one, and Zyper up with "Saved!"
        done = shot("c-sounds-done-%s" % th, "--theme", th, "--tour", "sounds", "3000", "400",
                    "--tour-music", "--tour-pick", "2")
        if done is not None:
            x, y, w, h = choice_box(4, 2)
            ck(zc.px(done, x + w, y - 25) == ACCENT and zc.px(done, *CHECK) != ACCENT,
               "%s sounds: the reward's check is on the kept one" % th)
            ck(count(done, EAR, CORNER) > 40 and count(done, TEXT[th], BUBBLE) > 1000,
               "%s sounds: and Zyper is up with his bubble" % th)


def main():
    if subprocess.call(["pkg-config", "--exists", "sdl2", "SDL2_image", "SDL2_ttf"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) != 0:
        print("skipped: no SDL2 here to build the launcher with")
        return 0
    if not os.access(BIN, os.X_OK):
        print("FAIL no build-native/zyperos: make native first", file=sys.stderr)
        return 1

    shutil.rmtree(D, ignore_errors=True)
    for sub in ("root", "Roms/Game Boy", "userdata", "shared"):
        os.makedirs(os.path.join(D, sub))
    shutil.copy(os.path.join(ROOT, "config", "systems.cfg"), os.path.join(D, "root"))
    open(os.path.join(D, "Roms", "Game Boy", "Tetris.gb"), "w").close()
    os.makedirs(os.path.join(D, "root", "res"))
    os.symlink(os.path.join(ROOT, "res", "tour"), os.path.join(D, "root", "res", "tour"))

    print("Zyper only for the reward and a wrong press, one at a time, the check, "
          "and his bubble clear of START: skip")
    for th in ("light", "dark"):
        wait = shot("a-wait-" + th, "--theme", th, "--tour", "a", "2000")
        done = shot("a-done-" + th, "--theme", th, "--tour", "a", "4000", "300")
        wrong = shot("y-wrong-" + th, "--theme", th, "--tour", "y", "3000",
                     "--tour-wrong", "x", "420")
        longest = shot("vol-wrong-" + th, "--theme", th, "--tour", "vol", "3000",
                       "--tour-wrong", "select", "420")
        pwait = shot("power-wait-" + th, "--theme", th, "--tour", "power", "1800")
        pwrong = shot("power-wrong-" + th, "--theme", th, "--tour", "power", "1800",
                      "--tour-wrong", "a", "420")
        pdone = shot("power-done-" + th, "--theme", th, "--tour", "power", "4000", "300")
        fstart = shot("finish-start-" + th, "--theme", th, "--tour", "finish", "0")
        fleave = shot("finish-leave-" + th, "--theme", th, "--tour", "finish", "70")
        fhello = shot("finish-hello-" + th, "--theme", th, "--tour", "finish", "500")
        if None in (wait, done, wrong, longest, pwait, pwrong, pdone, fstart, fleave, fhello):
            continue
        ck(count(wait, EAR, CORNER) == 0, "%s: no Zyper while a step waits" % th)
        ck(zc.px(wait, *CHECK) != ACCENT, "%s: no check while a step waits" % th)
        ck(count(done, EAR, CORNER) > 40, "%s: Zyper in the corner for the reward" % th)
        ck(zc.px(done, *CHECK) == ACCENT, "%s: the reward's check disc" % th)
        ck(count(wrong, EAR, CORNER) > 40 and count(wrong, TONGUE, CORNER) == 0,
           "%s: a wrong press has him up with his tongue in" % th)
        ck(count(wrong, TEXT[th], BUBBLE) > 2000, "%s: and his bubble" % th)
        bub, leg = xs(longest, TEXT[th], BUBBLE), xs(longest, TEXT_SOFT[th], LEGEND)
        ck(bub and leg and max(bub) + 8 <= min(leg),
           "%s: the longest answer ends at %s, clear of START: skip at %s"
           % (th, max(bub) if bub else None, min(leg) if leg else None))
        ck(count(pwait, EAR, CORNER) == 0 and count(pwait, EAR, STAGE) > 20,
           "%s: the power step's Zyper is the stage's" % th)
        ck(count(pwrong, EAR, CORNER) == 0 and count(pwrong, TEXT[th], BUBBLE) > 2000,
           "%s: a wrong press there gets its bubble and no second Zyper" % th)
        ck(count(pdone, EAR, CORNER) > 40 and count(pdone, EAR, STAGE) == 0,
           "%s: its reward has the corner's Zyper and none on the stage" % th)
        ck(count(fstart, EAR, CORNER) > 40 and count(fstart, EAR, HELLO) == 0,
           "%s: the finish starts with the reward's corner Zyper alone" % th)
        ck(count(fleave, EAR, CORNER) > 40 and count(fleave, EAR, HELLO) == 0,
           "%s: the corner Zyper remains visible during his exit" % th)
        ck(count(fhello, EAR, CORNER) == 0 and count(fhello, EAR, HELLO) > 200,
           "%s: the large hello follows the corner's exit, one Zyper at a time" % th)

    print("the first start shows the tutorial, and not once tour.done is set")
    ck(first_start() == "tour", "a fresh database starts with the tutorial")
    with open(os.path.join(D, "log"), "a") as log:
        st = subprocess.call([BIN, "--set", "tour.done=1"], env=env(), cwd=ROOT,
                             stdout=log, stderr=log)
    ck(st == 0, "--set tour.done=1 is a setting it knows")
    ck(first_start() == "shelf", "with tour.done set, the shelf comes first")

    pixel2()
    choices()

    if fails:
        print("\n%d tutorial shot check(s) FAILED; the shots are in %s" % (fails, D))
        return 1
    print("ok: the tutorial draws one Zyper, for the reward and a wrong press, hands him "
          "over to the finish, draws its check and its bubble clear of the legend, "
          "and the first start shows it once")
    return 0


if __name__ == "__main__":
    sys.exit(main())
