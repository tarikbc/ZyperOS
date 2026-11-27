"""Clock ceilings in a resident Diatom, end to end against the stub core.

The launcher has the clock files whenever Diatom runs no game: between games
and while its menu is up. On the Pixel 2 it puts the DDR's ceiling at 328 MHz
on a screen left still for 2 s, and each game's start (RUN), and each RESUME,
is where Diatom's level must be written again. A resident Diatom remembered
FULL from its start, wrote nothing at the next game's FULL, and ran it under
the launcher's ceiling: Mario Kart DS at 20 to 28 fps instead of 59.8 on the
Pixel 2 (2026-11-16).

The desktop port writes two files in DIATOM_POWER_DIR as the Pixel 2 writes
its DDR's and GPU's ceilings (port/desktop.c), and this plays the launcher:
it puts the DDR's ceiling down between games and in the menu, and reads what
Diatom left there.

On Diatom's test clock (port/desktop.c, DIATOM_TEST_CLOCK), whose sound
device plays the stub's sound as it comes and whose frames are never late:
on the wall clock SDL's dummy device counted the stub's sound as dropped and
a busy machine made frames late, and each raised a light game's level
(main.c, power_trouble) a second after its start, so its ceilings had to be
read within that second. Here nothing raises it, and every wait is only a
wait (2026-11-24).

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-resident does. SDL's dummy drivers: no window, no sound.

    python3 test/power.py
"""
import os, shutil, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-power-")
RAM, GPU = f"{TMP}/ram_max_freq", f"{TMP}/gpu_max_freq"
SLOW = "328000000"              # the launcher's ceiling on a still screen
for f in (RAM, GPU):
    open(f, "w").close()

fails = []
def check(name, ok, got):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def read(path):
    with open(path) as f:
        return f.read()

def launcher_slows():
    """What ZyperOS's plat_ram_idle(true) writes"""
    with open(RAM, "w") as f:
        f.write(SLOW)

def lifted():
    """Diatom's "0" written over the launcher's ceiling. Diatom writes as sysfs
    wants, with no truncation (port_power.h), and a plain file is not sysfs:
    "0" over "328000000" leaves "028000000". The first byte says whose write
    was last."""
    return read(RAM)[:1] == "0"

def soon(pred, timeout=30.0):
    """`pred` once it holds, or False after `timeout`. Diatom says RUNNING
    before it writes the game's ceilings (main.c: the level comes after the
    load, before the warmup), and after a RESUME the caller writes them once
    the menu's loop has returned."""
    end = time.time() + timeout
    while time.time() < end:
        if pred(): return True
        time.sleep(0.02)
    return pred()

sock, err_path = f"{TMP}/power.sock", f"{TMP}/power.err"
err = open(err_path, "w")
p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy", DIATOM_TEST_CLOCK="0",
                              DIATOM_POWER_DIR=TMP),
                     stdout=subprocess.DEVNULL, stderr=err)
end = time.time() + 30
while not os.path.exists(sock) and time.time() < end:
    time.sleep(0.05)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
buf = b""

def drain(timeout, until):
    """Lines until one starting with `until` arrives or the time is up"""
    global buf
    out, end = [], time.time() + timeout
    s.settimeout(0.2)
    while time.time() < end:
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            out.append(l.decode().strip())
            if out[-1].startswith(until): return out
        try:
            d = s.recv(65536)
        except socket.timeout:
            continue
        if not d: break
        buf += d
    return out

def send(m):
    s.sendall((m + "\n").encode())

def run(power=None):
    send(f"RUN\tcore={STUB}" + (f"\tpower={power}" if power else ""))
    return any(l.startswith("RUNNING") for l in drain(30.0, "RUNNING"))

def stop():
    send("STOP")
    drain(30.0, "EXIT")

drain(30.0, "READY")
check("at its start, Diatom lifts every ceiling", (read(RAM), read(GPU)) == ("0", "0"),
      (read(RAM), read(GPU)))

launcher_slows()
ok = run()
check("the first game, at full power, lifts the launcher's ceiling",
      ok and soon(lifted), (ok, read(RAM)))
send("PAUSE")
paused = any(l == "PAUSED" for l in drain(30.0, "PAUSED"))
launcher_slows()
send("RESUME")
drain(30.0, "RUNNING")
check("back from the launcher's menu, the game lifts it again",
      paused and soon(lifted), (paused, read(RAM)))
stop()

launcher_slows()
ok = run()
check("a second game in the same process lifts the launcher's ceiling",
      ok and soon(lifted), (ok, read(RAM)))
stop()

ok = run("light")
check("a light game writes its own ceilings",
      ok and soon(lambda: (read(RAM), read(GPU)) == (SLOW, "200000000")),
      (ok, read(RAM), read(GPU)))
stop()
check("and its end lifts them for the launcher", soon(lambda: lifted() and read(GPU)[:1] == "0"),
      (read(RAM), read(GPU)))

send("QUIT")
try: p.wait(30)
except subprocess.TimeoutExpired: p.kill()
err.close()

shutil.rmtree(TMP, ignore_errors=True)
if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: a resident Diatom writes its clock ceilings at each game's start and after its menu, "
      "over whatever the launcher left")
