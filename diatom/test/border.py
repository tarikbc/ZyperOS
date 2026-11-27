"""The Border drawn, end to end, on the desktop port against the stub core.

  - A picture (a DTOV file, --border): on a 640x480 surface at Sharp Pixels
    the stub's 256x224 is 512x448 in the middle, and every pixel around it
    is the picture's; the game's own pixels are on top of it.
  - Blur (--border blur): around the game, the game's own colors, darkened:
    not black, and darker than the game.
  - None: black around, as before.
  - SETBORDER to a resident Diatom: a good file BORDER ok=1, a file that is
    no picture ERROR code=bad_border, blur=1 ok, and no field ok (black).

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/border.py
"""
import os, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-border-")
ENV = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software", SDL_AUDIODRIVER="dummy",
           DIATOM_SURFACE="640x480")
fails = []


def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok:
        fails.append(name)


def dtov(path, w, h, bgr):
    with open(path, "wb") as f:
        f.write(b"DTOV" + struct.pack("<HH", w, h) + bytes([bgr[0], bgr[1], bgr[2], 0]) * (w * h))


def bmp(path):
    """A 24-bit BMP's pixels as {(x, y): (r, g, b)} reader"""
    data = open(path, "rb").read()
    off, = struct.unpack_from("<I", data, 10)
    w, h = struct.unpack_from("<ii", data, 18)
    row = (w * 3 + 3) & ~3

    def px(x, y):
        yy = (h - 1 - y) if h > 0 else y
        b, g, r = data[off + yy * row + x * 3: off + yy * row + x * 3 + 3]
        return (r, g, b)
    return abs(w), abs(h), px


def shot(name, border=None):
    out = f"{TMP}/{name}.bmp"
    cmd = [BIN, "--core", STUB, "--frames", "40", "--display", "integer", "--shot", out,
           "--save", TMP, "--system", TMP]
    if border:
        cmd += ["--border", border]
    subprocess.run(cmd, env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
    return bmp(out) if os.path.exists(out) else (0, 0, None)


# ---- a picture, blur, none ----------------------------------------------------
pic = f"{TMP}/teal.dtov"
dtov(pic, 4, 3, (200, 120, 40))               # BGR: teal-blue (40, 120, 200)
w, h, px = shot("picture", pic)
check("a 640x480 capture", (w, h) == (640, 480), (w, h))
if px:
    around = [px(5, 5), px(634, 5), px(5, 474), px(634, 474), px(32, 240), px(607, 240), px(320, 8), px(320, 470)]
    check("every pixel around the game is the picture's", all(p == (40, 120, 200) for p in around), around)
    inside = [px(64, 16), px(575, 463), px(320, 240)]
    check("and the game's own pixels are on top of it", all(p != (40, 120, 200) for p in inside), inside)
    check("the game's edge where Sharp Pixels puts it (2x, 512x448 at 64,16)",
          px(63, 240) == (40, 120, 200) and px(64, 240) != (40, 120, 200) and
          px(576, 240) == (40, 120, 200) and px(575, 240) != (40, 120, 200), [px(63, 240), px(64, 240)])

# A frame's window (rect=): the game where the frame says, whatever the
# mode (Sharp Pixels would center it at 2x), at the size it says
out = f"{TMP}/window.bmp"
subprocess.run([BIN, "--core", STUB, "--frames", "40", "--display", "integer", "--shot", out, "--save", TMP,
                "--system", TMP, "--border", pic, "--border-rect", "256x224+20+30"],
               env=ENV, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=60)
if os.path.exists(out):
    w, h, px = bmp(out)
    check("a window: the game at 256x224 at 20,30, the picture around it",
          px(19, 100) == (40, 120, 200) and px(20, 100) != (40, 120, 200) and
          px(275, 100) != (40, 120, 200) and px(276, 100) == (40, 120, 200) and
          px(100, 29) == (40, 120, 200) and px(100, 30) != (40, 120, 200) and
          px(100, 253) != (40, 120, 200) and px(100, 254) == (40, 120, 200),
          [px(19, 100), px(20, 100), px(275, 100), px(276, 100), px(100, 29), px(100, 30)])
else:
    check("a window: a capture", False, "no capture")

w, h, px = shot("blur", "blur")
if px:
    c = px(10, 10)
    game = [px(x, y) for x in range(72, 568, 16) for y in range(24, 456, 16)]
    side = [px(x, y) for x in range(2, 62, 6) for y in range(20, 460, 20)]
    gm = sum(sum(p) for p in game) / len(game)
    sm = sum(sum(p) for p in side) / len(side)
    check("blur: around the game, not black", c != (0, 0, 0), c)
    check("and darker than the game on the whole", sm < gm, (round(sm), round(gm)))

w, h, px = shot("none")
if px:
    check("no border: black around, as before", px(5, 5) == (0, 0, 0) and px(634, 474) == (0, 0, 0),
          (px(5, 5), px(634, 474)))

# ---- SETBORDER's answers ----------------------------------------------------------
sock = f"{TMP}/b.sock"
p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP], env=ENV,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
# The waits are the wall clock's, and only waits: each ends as soon as what
# it waits for is there (5 s until 2026-11-24)
end = time.time() + 30
while not os.path.exists(sock) and time.time() < end:
    time.sleep(0.05)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
s.settimeout(0.1)
buf, got = b"", []


def until(prefix, limit=30.0):
    global buf
    end = time.time() + limit
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            got.append(l.decode().strip())
            if got[-1].startswith(prefix):
                return got[-1]
    return None


def send(m):
    s.sendall((m + "\n").encode())


until("READY")
send(f"RUN\tcore={STUB}")
until("RUNNING")
bad = f"{TMP}/bad.dtov"
open(bad, "wb").write(b"not a picture")
for line, want, name in ((f"SETBORDER\tpath={pic}\trect=999x10+0+0", "ERROR\tcode=bad_border",
                          "a window off the surface"),
                         (f"SETBORDER\tpath={pic}\trect=160x144+10+10", "DISPLAY", "a window: the rect said"),
                         (f"SETBORDER\tpath={pic}", "BORDER\tok=1", "a good file"),
                         (f"SETBORDER\tpath={bad}", "ERROR\tcode=bad_border", "a file that is no picture"),
                         ("SETBORDER\tblur=1", "BORDER\tok=1", "blur"),
                         ("SETBORDER", "BORDER\tok=1", "black again")):
    send(line)
    a = until(want.split("\t")[0])
    if want == "DISPLAY":
        check(f"SETBORDER, {name}", a is not None and "rect=160x144+10+10" in a, a)
    else:
        check(f"SETBORDER, {name}: {want.split(chr(9))[0]}", a is not None and a.startswith(want), a)
send("STOP")
until("EXIT")
send("QUIT")
try:
    p.wait(30)
except subprocess.TimeoutExpired:
    p.kill()

if fails:
    print(f"border: {len(fails)} failed")
    sys.exit(1)
print("border: ok")
