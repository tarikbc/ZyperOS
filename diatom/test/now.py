"""What Diatom runs now (src/now.h), end to end against the stub core.

The file is there exactly while a game is loaded, names the game and the
core, and holds counters that move only while someone asks: px2-snap (ZyperOS's
tools/px2-snap.sh) refuses a game that is open by it, and px2-perf reads the
speed against the clock from two of its writings.

  - written once as the game starts running: its pid, core and rom, seq 1,
    the counters 0
  - with <file>.ask there, written again every quarter of a second: seq up,
    frames and run_us moving, and the stub's speed against the clock near 1
  - the ask gone, written no more
  - the game stopped, the file gone

On Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK), and judged by
the times the file itself gives (t_us): a write a quarter of a second after
the last on Diatom's clock, at the first turn past it, and the stub at 1x of
that clock, exactly, on any machine. On the wall clock this counted the
writes in two seconds of this script's sleep and let the speed be anything
from 0.3x, which a busy CI runner came near (20 fps).

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-resident does. SDL's dummy drivers: no window, no sound.

    python3 test/now.py
"""
import os, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-now-")
NOW = f"{TMP}/now"
ROM = f"{TMP}/A Game (World).stub"
with open(ROM, "wb") as f:
    f.write(b"STUB" + bytes(range(60)))

fails = []
def check(name, ok, got):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def read_now():
    try:
        with open(NOW) as f:
            return dict(l.rstrip("\n").split("=", 1) for l in f if "=" in l)
    except OSError:
        return None

def soon(pred):
    """`pred` once it holds, or False after 30 s: a wait, and only that, so a
    busy machine makes it longer, never a check different"""
    end = time.time() + 30
    while time.time() < end:
        if pred(): return True
        time.sleep(0.02)
    return pred()

def seq():
    return int((read_now() or {}).get("seq", 0))

LOG = f"{TMP}/d.input"
def frames():
    """The frames the stub has run, each 1/fps of Diatom's clock"""
    try:
        with open(LOG) as f: return sum(1 for _ in f)
    except OSError: return 0

sock = f"{TMP}/d.sock"
err = open(f"{TMP}/d.err", "w")
p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy", DIATOM_TEST_CLOCK="0", DIATOM_NOW=NOW,
                              STUBCORE_INPUT_LOG=LOG),
                     stdout=subprocess.DEVNULL, stderr=err)
soon(lambda: os.path.exists(sock))
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
buf = b""

def drain(timeout, until):
    global buf
    end = time.time() + timeout
    s.settimeout(0.2)
    while time.time() < end:
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            if l.decode().startswith(until): return True
        try:
            d = s.recv(65536)
        except socket.timeout:
            continue
        if not d: break
        buf += d
    return False

def send(m):
    s.sendall((m + "\n").encode())

try:
    drain(30.0, "READY")
    check("no file while no game is loaded", read_now() is None, read_now())
    send(f"RUN\tcore={STUB}\trom={ROM}")
    check("the game runs", drain(30.0, "RUNNING"), "no RUNNING")
    soon(lambda: read_now() is not None)
    a = read_now() or {}
    check("written as the game starts: Diatom's pid, the core and the game",
          a.get("pid") == str(p.pid) and a.get("core") == STUB and a.get("rom") == ROM, a)
    check("once, with its counters 0",
          a.get("seq") == "1" and a.get("frames") == "0" and a.get("run_us") == "0", a)
    f0 = frames()
    soon(lambda: frames() >= f0 + 60)       # a second of Diatom's clock
    check("and not again while nobody asks, a second on", frames() >= f0 + 60 and
          (read_now() or {}).get("seq") == "1", read_now())

    # Until it has been written again, and then four times more, a second of
    # Diatom's clock: the waits are this script's, the times Diatom's own
    open(NOW + ".ask", "w").close()
    soon(lambda: seq() >= 2)
    b = read_now() or {}
    soon(lambda: seq() >= int(b.get("seq", 0)) + 4)
    c = read_now() or {}
    seq_b, seq_c = int(b.get("seq", 0)), int(c.get("seq", 0))
    t_b, t_c = int(b.get("t_us", 0)), int(c.get("t_us", 0))
    fps = float(c.get("fps", 0) or 0)
    # Asked at a turn a quarter of a second or more after the last, so each
    # write is at the first turn past it: 250 ms apart, and less than a
    # frame more
    per = (t_c - t_b) / (seq_c - seq_b) if seq_c > seq_b else 0
    check("asked, written again every quarter of a second of Diatom's clock",
          seq_b >= 2 and seq_c - seq_b >= 4 and fps and 250000 <= per < 250000 + 1e6 / fps,
          (seq_b, seq_c, per))
    df = int(c.get("frames", 0)) - int(b.get("frames", 0))
    dr = int(c.get("run_us", 0)) - int(b.get("run_us", 0))
    speed = df * 1e6 / fps / dr if fps and dr > 0 else 0
    check("frames and run_us move, and the stub runs at 1x of the clock",
          df > 0 and dr > 1000000 and abs(speed - 1) < 0.01, (df, dr, fps, speed))
    check("at speed step 1", c.get("speed") == "1", c.get("speed"))

    # The ask gone: a write the turn under way had begun is done a frame
    # later, so after a second of the stub's frames the count is what it
    # stays, and a second more, four writings' time, it has not moved. By
    # this script's time until 2026-11-24, 0.4 s then 0.8 s, which a busy
    # machine could stretch past a writing, or a Diatom run fast here cram
    os.remove(NOW + ".ask")
    f0 = frames()
    per_s = round(float(c.get("fps", 60) or 60))
    soon(lambda: frames() >= f0 + per_s)
    d = (read_now() or {}).get("seq")
    soon(lambda: frames() >= f0 + 2 * per_s)
    check("the ask gone, written no more, over a second of Diatom's clock",
          frames() >= f0 + 2 * per_s and (read_now() or {}).get("seq") == d, (d, read_now()))

    send("STOP")
    check("the game stops", drain(30.0, "EXIT"), "no EXIT")
    # Removed just after EXIT is said (main.c, run_session)
    check("and the file is gone", soon(lambda: read_now() is None) and not os.path.exists(NOW + ".tmp"),
          read_now())
finally:
    send("QUIT")
    try: p.wait(30)
    except subprocess.TimeoutExpired: p.kill()
    err.close()
    shutil.rmtree(TMP, ignore_errors=True)

if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: what Diatom runs now, written as a game starts, again only while asked, gone when it stops")
