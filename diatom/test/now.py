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

sock = f"{TMP}/d.sock"
err = open(f"{TMP}/d.err", "w")
p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy", DIATOM_NOW=NOW),
                     stdout=subprocess.DEVNULL, stderr=err)
for _ in range(100):
    if os.path.exists(sock): break
    time.sleep(0.05)
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
    drain(2.0, "READY")
    check("no file while no game is loaded", read_now() is None, read_now())
    send(f"RUN\tcore={STUB}\trom={ROM}")
    check("the game runs", drain(4.0, "RUNNING"), "no RUNNING")
    time.sleep(0.3)
    a = read_now() or {}
    check("written as the game starts: Diatom's pid, the core and the game",
          a.get("pid") == str(p.pid) and a.get("core") == STUB and a.get("rom") == ROM, a)
    check("once, with its counters 0",
          a.get("seq") == "1" and a.get("frames") == "0" and a.get("run_us") == "0", a)
    time.sleep(0.6)
    check("and not again while nobody asks", (read_now() or {}).get("seq") == "1", read_now())

    open(NOW + ".ask", "w").close()
    time.sleep(0.6)
    b = read_now() or {}
    time.sleep(2.0)
    c = read_now() or {}
    seq_b, seq_c = int(b.get("seq", 0)), int(c.get("seq", 0))
    check("asked, written again every quarter of a second",
          seq_b >= 2 and 6 <= seq_c - seq_b <= 10, (seq_b, seq_c))
    df = int(c.get("frames", 0)) - int(b.get("frames", 0))
    dr = int(c.get("run_us", 0)) - int(b.get("run_us", 0))
    fps = float(c.get("fps", 0) or 0)
    speed = df * 1e6 / fps / dr if fps and dr > 0 else 0
    check("frames and run_us move, and the stub runs near 1x of the clock",
          df > 0 and dr > 1500000 and 0.8 <= speed <= 1.2, (df, dr, fps, speed))
    check("at speed step 1", c.get("speed") == "1", c.get("speed"))

    os.remove(NOW + ".ask")
    time.sleep(0.4)
    d = (read_now() or {}).get("seq")
    time.sleep(0.8)
    check("the ask gone, written no more", (read_now() or {}).get("seq") == d, (d, read_now()))

    send("STOP")
    check("the game stops", drain(3.0, "EXIT"), "no EXIT")
    time.sleep(0.2)
    check("and the file is gone", read_now() is None and not os.path.exists(NOW + ".tmp"), read_now())
finally:
    send("QUIT")
    time.sleep(0.3)
    p.terminate()
    try: p.wait(3)
    except subprocess.TimeoutExpired: p.kill()
    err.close()
    shutil.rmtree(TMP, ignore_errors=True)

if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: what Diatom runs now, written as a game starts, again only while asked, gone when it stops")
