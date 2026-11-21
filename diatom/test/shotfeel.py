"""A screenshot felt and heard as it is taken (ZyperOS's DESIGN.md 9 and
11.61), end to end against the stub core: the Screenshot hotkey on SELECT+Y
(RUN hotkeys= and shots=), the pad by script (DIATOM_INPUT_SCRIPT), the stub
showing a frame of our own (STUBCORE_IMAGE), the desktop port's pretend motor
writing down each level and when (DIATOM_RUMBLE_LOG, DIATOM_RUMBLE_LOG_US) and
each write of the game's sound (DIATOM_AUDIO_LOG).

  - One shot, one buzz: the motor at the game's rumble level (RUN rumble=,
    the player's Rumble times the system's share) as the shot is taken, and
    back to 0 a frame or so after its 40 ms, never a long rumble. Two shots,
    two buzzes. RUN rumble=0 (Rumble Off): none.
  - One shot, one HOTKEY key=shot, said as it is taken and before its SHOT
    path= (the launcher's shutter, which never waits on the file); at every
    level, Off too: the shutter follows UI Sounds, the launcher's to judge.
  - The file is the game's own frame, pixel for pixel, at every level: the
    buzz and the shutter put nothing in it.
  - The game's sound goes on through the shots as it was: every write the
    stub's 440 Hz tone at its own level, nothing added, none missing between
    the shots and after them.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/shotfeel.py
"""
import os, socket, struct, subprocess, sys, tempfile, time, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-shotfeel-")
W, H = 256, 224          # the stub's base frame, which STUBCORE_IMAGE fills
TONE = 2200              # the stub's 440 Hz tone, at its peak

fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

# A frame of our own, every pixel its own RGB565 value, so a shot of it can
# be held to it pixel for pixel
IMAGE = f"{TMP}/frame.rgb565"
px = [((x * 37 + y * 101) ^ (x << 5) ^ (y << 11)) & 0xFFFF for y in range(H) for x in range(W)]
with open(IMAGE, "wb") as f:
    f.write(struct.pack(f"<{W * H}H", *px))

def want_rgb(v):
    """Diatom's PNG of an RGB565 pixel (diatom/src/main.c, png_encode)"""
    return (((v >> 11) & 0x1f) * 255 // 31, ((v >> 5) & 0x3f) * 255 // 63, (v & 0x1f) * 255 // 31)

WANT = b"".join(bytes(want_rgb(v)) for v in px)

def png_pixels(path):
    """An 8-bit RGB PNG's pixels, every row filter none, as Diatom writes it"""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n": return None, None
    at, idat, w, h = 8, b"", 0, 0
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if kind == b"IHDR": w, h = struct.unpack(">II", body[:8])
        elif kind == b"IDAT": idat += body
        at += 12 + n
    raw = zlib.decompress(idat)
    rows = [raw[r * (w * 3 + 1):(r + 1) * (w * 3 + 1)] for r in range(h)]
    if any(r[0] != 0 for r in rows): return (w, h), None
    return (w, h), b"".join(r[1:] for r in rows)

# Two shots: SELECT, then Y, a frame or so each, the second long after the
# first's file is written (a press while one is written is dropped)
SCRIPT = "40:select;43:select+y;47:select;50:;160:select;163:select+y;167:select;170:"
FRAMES = 260

def run(tag, rumble):
    sock = f"{TMP}/{tag}.sock"
    rlog, alog, ilog = f"{TMP}/{tag}.rumble", f"{TMP}/{tag}.audio", f"{TMP}/{tag}.input"
    shots = f"{TMP}/{tag}/Screenshots/Stub/Game"
    p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                         env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                                  SDL_AUDIODRIVER="dummy", DIATOM_INPUT_SCRIPT=SCRIPT,
                                  STUBCORE_INPUT_LOG=ilog, STUBCORE_IMAGE=IMAGE,
                                  DIATOM_RUMBLE_LOG=rlog, DIATOM_RUMBLE_LOG_US="1",
                                  DIATOM_AUDIO_LOG=alog),
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.settimeout(0.05)
    buf, got = b"", []

    def pump(seconds):
        nonlocal buf
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
                got.append(l.decode().strip())

    def polls():
        try:
            with open(ilog) as f: return sum(1 for _ in f)
        except OSError: return 0

    pump(0.3)
    extra = f"\trumble={rumble}" if rumble is not None else ""
    s.sendall(f"RUN\tcore={STUB}\thotkeys=shot:y\tshots={shots}{extra}\n".encode())
    end = time.time() + 90
    while polls() < FRAMES and time.time() < end:
        pump(0.2)
    pump(0.5)
    s.sendall(b"STOP\n")
    pump(1.0)
    s.sendall(b"QUIT\n")
    try: p.wait(5)
    except subprocess.TimeoutExpired: p.kill()
    motor = [list(map(int, l.split())) for l in open(rlog)] if os.path.exists(rlog) else []
    sound = [list(map(int, l.split())) for l in open(alog)] if os.path.exists(alog) else []
    return got, motor, sound

def buzzes(motor):
    """Each run of the motor: (strength, ms on)"""
    out, on = [], None
    for strong, weak, us in motor:
        if strong and on is None: on = (strong, us)
        elif not strong and on is not None:
            out.append((on[0], (us - on[1]) / 1000.0))
            on = None
    if on is not None: out.append((on[0], None))
    return out

for tag, rumble, strength in (("medium", 65, round(0xFFFF * 0.65)), ("off", 0, 0), ("full", None, 0xFFFF)):
    level = "Rumble Off" if rumble == 0 else f"rumble={rumble}" if rumble else "no rumble= (Full)"
    print(f"{level}:")
    got, motor, sound = run(tag, rumble)
    said = [l for l in got if l == "HOTKEY\tkey=shot" or l.startswith("SHOT\tpath=")]
    check("each shot says HOTKEY key=shot as it is taken, then SHOT path= once written",
          ["taken" if l.startswith("HOTKEY") else "file" for l in said] == ["taken", "file", "taken", "file"],
          said)
    b = buzzes(motor)
    if strength:
        check(f"two shots, two buzzes, each at {strength} ({rumble or 100}% of full)",
              len(b) == 2 and all(s == strength for s, _ in b), b)
        check("each a short pulse: on 40 ms, off by a frame or so after, never a long rumble",
              len(b) == 2 and all(ms is not None and 40 <= ms < 150 for _, ms in b), b)
    else:
        check("Rumble Off: no buzz at all", not any(m[0] or m[1] for m in motor), motor)
    for i, l in enumerate(l for l in got if l.startswith("SHOT\tpath=")):
        size, pix = png_pixels(l[10:])
        check(f"shot {i + 1}: the game's own frame, pixel for pixel", size == (W, H) and pix == WANT,
              (size, pix is not None and sum(a != b for a, b in zip(pix, WANT))))
    tops = [t for n, t in sound if n]
    check("the game's sound goes on through the shots: written all along", len(tops) > 50, len(tops))
    # Past its first writes (a resampler settling), every one the tone as
    # the stub makes it: nothing added, nothing cut
    late = tops[10:]
    check(f"every write the stub's tone at its own level (peak about {TONE}), nothing added",
          late and max(late) <= TONE * 1.1 and min(late) >= TONE * 0.5, (min(late or [0]), max(late or [0])))

if fails:
    print(f"FAIL: {len(fails)} screenshot feel check(s)")
    sys.exit(1)
print("ok: a screenshot is felt and heard as it is taken, and the shot and the game's sound are as they were")
