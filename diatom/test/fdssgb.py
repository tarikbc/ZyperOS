"""The Famicom Disk System's disk messages and the Super Game Boy per game,
end to end, against the stub core.

  - A core's message that is FCEUmm's word on an FDS disk (src/fdsdisk.h)
    reaches the launcher as a DISK line, each of its kinds; any other
    message sends nothing.
  - RUN sgb=games (src/sgb.h): a Game Boy ROM whose header says it is made
    for the Super Game Boy loads with mgba_gb_model=Super Game Boy and
    mgba_sgb_borders=ON over what the launcher sent; one that is not, with
    the launcher's values, and so has a Color only one. No sgb=: the
    launcher's values. And a game after one that was set loads with the
    launcher's values again, not the one before's.

The stub declares mGBA's two options as libretro/mgba e31759b does
(STUBCORE_MGBA) and writes what each game loaded with to stderr; it says a
message on a frame of its choosing (STUBCORE_MESSAGE), and a line for each
frame it runs (STUBCORE_INPUT_LOG): each game runs 30 frames, past frame
20's message, before its STOP, and a DISK line is sent as the core says it,
so one is in before EXIT or never. Until 2026-11-24 each game ran 1.2 s of
this script's time, which a busy machine could end before frame 20.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/fdssgb.py
"""
import os, socket, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-fdssgb-")
fails = []


def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok:
        fails.append(name)


def gb_rom(name, cgb, sgb, licensee):
    """A 32 KB ROM with the three header bytes that matter"""
    data = bytearray(0x8000)
    data[0x143], data[0x146], data[0x14B] = cgb, sgb, licensee
    path = f"{TMP}/{name}.gb"
    with open(path, "wb") as f:
        f.write(bytes(data))
    return path


def session(tag, runs, **env):
    """One Diatom: each (rom, extra RUN keys, SETOPTs) in turn, RUN, a while,
    then STOP. Returns the lines it sent and its stderr's lines. The waits
    are the wall clock's, and only waits: each ends as soon as what it waits
    for is there."""
    sock, err_path, log = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.err", f"{TMP}/{tag}.input"
    err = open(err_path, "w")
    p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                         env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                                  SDL_AUDIODRIVER="dummy", STUBCORE_INPUT_LOG=log, **env),
                         stdout=subprocess.DEVNULL, stderr=err)
    end = time.time() + 30
    while not os.path.exists(sock) and time.time() < end:
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    s.settimeout(0.1)
    buf, got = b"", []

    def pump(seconds, until=None):
        nonlocal buf
        end = time.time() + seconds
        while time.time() < end:
            try:
                d = s.recv(65536)
                if not d:
                    return
                buf += d
            except socket.timeout:
                pass
            while b"\n" in buf:
                l, buf = buf.split(b"\n", 1)
                got.append(l.decode().strip())
                if until and got[-1].startswith(until):
                    return

    def send(m):
        s.sendall((m + "\n").encode())

    def frames():
        try:
            with open(log) as f:
                return sum(1 for _ in f)
        except OSError:
            return 0

    pump(30.0, "READY")
    for rom, keys, opts in runs:
        for k, v in opts:
            send(f"SETOPT\tkey={k}\tvalue={v}")
        n = frames()
        send(f"RUN\tcore={STUB}" + (f"\trom={rom}" if rom else "") + "".join(f"\t{k}" for k in keys))
        pump(30.0, "RUNNING")
        end = time.time() + 30
        while frames() < n + 30 and time.time() < end:
            pump(0.05)
        send("STOP")
        pump(30.0, "EXIT")
    send("QUIT")
    try:
        p.wait(30)
    except subprocess.TimeoutExpired:
        p.kill()
    err.close()
    return got, open(err_path, errors="replace").read().splitlines()


# ---- the disk's messages ----------------------------------------------------
for text, want in (("Disk 1 of 2 Side B Selected", "DISK\tstate=selected\tdisk=1\tof=2\tside=B"),
                   ("Disk 2 of 2 Side A Inserted", "DISK\tstate=in\tdisk=2\tof=2\tside=A"),
                   ("Disk 1 of 1 Side A Ejected", "DISK\tstate=out\tdisk=1\tof=1\tside=A"),
                   ("Eject disk before selecting", "DISK\tstate=eject-first")):
    got, _ = session("disk", [(None, [], [])], STUBCORE_MESSAGE="20:" + text)
    disk = [l for l in got if l.startswith("DISK")]
    check(f"\"{text}\" reaches the launcher as one DISK line", disk == [want], disk)
got, _ = session("other", [(None, [], [])], STUBCORE_MESSAGE="20:System: NTSC")
check("any other message sends nothing", not [l for l in got if l.startswith("DISK")], got)

# ---- the Super Game Boy, per game ----------------------------------------------
made_for = gb_rom("made-for", 0x00, 0x03, 0x33)
color_too = gb_rom("color-too", 0x80, 0x03, 0x33)
plain = gb_rom("plain", 0x00, 0x00, 0x01)
color_only = gb_rom("color-only", 0xC0, 0x03, 0x33)
SHELF = [("mgba_gb_model", "Game Boy"), ("mgba_sgb_borders", "OFF")]   # the Game Boy shelf's
LAUNCHER, SGB = "stubcore: model=Game Boy borders=OFF", "stubcore: model=Super Game Boy borders=ON"
runs = [(made_for, ["sgb=games"], SHELF), (color_too, ["sgb=games"], SHELF), (plain, ["sgb=games"], SHELF),
        (plain, ["sgb=every"], SHELF), (color_only, ["sgb=games"], SHELF), (made_for, [], SHELF),
        (plain, [], SHELF)]
got, err = session("sgb", runs, STUBCORE_MGBA="1")
loaded = [l for l in err if l.startswith("stubcore: model=")]
check("seven games loaded", len(loaded) == 7, loaded)
if len(loaded) == 7:
    check("Its Games: a game made for it loads as a Super Game Boy, its border on", loaded[0] == SGB, loaded[0])
    check("and one with Color colors too, as a Super Game Boy rather than a Color", loaded[1] == SGB, loaded[1])
    check("a game not made for it: the launcher's values", loaded[2] == LAUNCHER, loaded[2])
    check("sgb=every is no word (no Every Game): the launcher's values", loaded[3] == LAUNCHER, loaded[3])
    check("a Color only cartridge with the flag never", loaded[4] == LAUNCHER, loaded[4])
    check("no sgb=: the launcher's values, even for a game made for it", loaded[5] == LAUNCHER, loaded[5])
    check("the game after a Super Game Boy one loads with the launcher's values", loaded[6] == LAUNCHER,
          loaded[6])

if fails:
    print(f"fdssgb: {len(fails)} failed")
    sys.exit(1)
print("fdssgb: ok")
