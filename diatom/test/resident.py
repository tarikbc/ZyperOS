"""Many resident cores, and zipped games, end to end against the stub core.

Two bugs found in QEMU on 2026-11-06, while ROGUE OS's library was moved onto
a ZyperOS card, each with a case here:

  - Options for every core a process keeps open. The option tables were eight
    while the resident registry was 32, so from the ninth core a run opened, a
    core got no options, its own defaults included: pcsx_rearmed then has no
    memory card, and a PlayStation game played after eight other systems kept
    no save. Here ten copies of the stub, at ten paths, are ten resident cores,
    and each must read its option's default.
  - A zipped game for a core that takes the bytes. GET_GAME_INFO_EXT was
    refused; Handy reads its cart from that answer or, refused, from the
    path, which is the zip's, so it read the zip and no zipped Lynx game
    started. Here the stub, told to let Diatom unzip for it, must be answered
    as RetroArch answers for content in an archive: "<zip>#<entry>", the
    entry's extension, the zip's folder and name, and the bytes. The path in
    retro_game_info stays the zip's (core.c says why: PokeMini names its save
    after it). A game that is no zip, and a zipped game for a core that wants
    a file (need_fullpath, staged in /tmp), must still be refused the call,
    as they always were.

And the file a zipped game is staged to for a core that wants a file: there
while the game runs, gone once it stops. /tmp is RAM on the devices, and the
staged file was never removed, so every game played that way kept its ROM's
size there until the next boot. Two games in a row stage the same name, and
the second must find its file as the first did.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-proto does. SDL's dummy drivers: no window, no sound.

    python3 test/resident.py
"""
import os, shutil, socket, subprocess, sys, tempfile, time, zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-resident-")
NCORES = 10                     # more than the eight tables there were

cores = []
for n in range(1, NCORES + 1):
    c = f"{TMP}/stub-{n}.so"
    shutil.copy(STUB, c)
    cores.append(c)
# A zipped game whose entry is named apart from the zip, as a set's often
# is, so the name the core is told can only have come from one of them. The
# process id in it keeps this run's staged file (/tmp, below) its own.
GAME = b"STUB" + bytes(range(60))
ENTRY = f"Game (World) {os.getpid()}.STUB"
with zipfile.ZipFile(f"{TMP}/Game.zip", "w") as z:
    z.writestr(ENTRY, GAME)
with open(f"{TMP}/Plain.stub", "wb") as f:
    f.write(GAME)
STAGED = f"/tmp/diatom-{ENTRY}"  # where core.c stages a zip for a need_fullpath core

fails = []
def check(name, ok, got):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def session(tag, runs, watch=None, seen=None, **env):
    """One Diatom process: each (core, rom) in turn, RUN then STOP. Returns
    whether each run got RUNNING, and Diatom's stderr as lines. With `watch`,
    appends to `seen` whether that file was there while each game ran and
    then once it had stopped."""
    sock, err_path = f"{TMP}/{tag}.sock", f"{TMP}/{tag}.err"
    err = open(err_path, "w")
    p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                         env=dict(os.environ, SDL_VIDEODRIVER="dummy",
                                  SDL_RENDER_DRIVER="software",
                                  SDL_AUDIODRIVER="dummy", **env),
                         stdout=subprocess.DEVNULL, stderr=err)
    for _ in range(100):
        if os.path.exists(sock): break
        time.sleep(0.05)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock)
    buf = b""

    def drain(timeout, until):
        """Lines until `until` arrives or the time is up."""
        nonlocal buf
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

    ran = []
    drain(2.0, "READY")
    for core, rom in runs:
        send("RUN\tcore=" + core + ("\trom=" + rom if rom else ""))
        got = drain(4.0, "RUNNING")
        ran.append(any(l.startswith("RUNNING") for l in got))
        if watch: seen.append(os.path.exists(watch))
        send("STOP")
        drain(3.0, "EXIT")
        if watch: seen.append(os.path.exists(watch))
    send("QUIT")
    time.sleep(0.5)
    p.terminate()
    try: p.wait(3)
    except subprocess.TimeoutExpired: p.kill()
    err.close()
    return ran, open(err_path, errors="replace").read().splitlines()

def lines(log, prefix):
    return [l for l in log if l.startswith(prefix)]

# Ten resident cores with no game, then a zipped game and a plain one
ran, log = session("buffer", [(c, None) for c in cores] +
                   [(cores[0], f"{TMP}/Game.zip"), (cores[1], f"{TMP}/Plain.stub")],
                   STUBCORE_EXTRACT="1")
check(f"{NCORES} resident cores and two games all run", ran == [True] * (NCORES + 2), ran)
check(f"each of {NCORES} resident cores, and both games after, reads its option's default",
      lines(log, "stubcore: probe=") == ["stubcore: probe=probe-default"] * (NCORES + 2),
      lines(log, "stubcore: probe="))
check("no core goes without an option table",
      not any("no free table" in l for l in log), [l for l in log if "no free table" in l])

exts, paths = lines(log, "stubcore: ext"), lines(log, "stubcore: path=")
check("a run with no game is refused GET_GAME_INFO_EXT",
      exts[:NCORES] == ["stubcore: ext none"] * NCORES, exts[:NCORES])
want = (f"stubcore: ext full={TMP}/Game.zip#{ENTRY} name=Game ext=stub dir={TMP} "
        f"archive={TMP}/Game.zip file={ENTRY} in_archive=1 size={len(GAME)} head=STUB")
check("a zipped game is described as RetroArch describes content in an archive",
      exts[NCORES:NCORES + 1] == [want], exts[NCORES:NCORES + 1])
check("and handed the unzipped bytes, at the zip's own path",
      paths[:1] == [f"stubcore: path={TMP}/Game.zip size={len(GAME)} head=STUB"], paths[:1])
check("a game that is no zip keeps its path and is refused GET_GAME_INFO_EXT, as before",
      paths[1:] == [f"stubcore: path={TMP}/Plain.stub size={len(GAME)} head=STUB"]
      and exts[NCORES + 1:] == ["stubcore: ext none"], (paths[1:], exts[NCORES + 1:]))

# A core that wants a file: the zip is staged in /tmp, as it always was
ran, log = session("fullpath", [(cores[0], f"{TMP}/Game.zip")],
                   STUBCORE_EXTRACT="1", STUBCORE_FULLPATH="1")
check("a zipped game for a need_fullpath core is handed the staged file, no bytes",
      ran == [True] and lines(log, "stubcore: path=") == [f"stubcore: path={STAGED} size=0 head="],
      (ran, lines(log, "stubcore: path=")))
check("and is refused GET_GAME_INFO_EXT, as before",
      lines(log, "stubcore: ext") == ["stubcore: ext none"], lines(log, "stubcore: ext"))

# The staged file: there while the game runs, gone once it stops, and the
# same name staged again for the next game
seen = []
ran, log = session("staged", [(cores[0], f"{TMP}/Game.zip")] * 2, watch=STAGED, seen=seen,
                   STUBCORE_EXTRACT="1", STUBCORE_FULLPATH="1")
check("two zipped games in a row for a need_fullpath core both run",
      ran == [True, True], ran)
check("each reads the game from its staged file",
      lines(log, "stubcore: file") == ["stubcore: file read head=STUB"] * 2,
      lines(log, "stubcore: file"))
check("the staged file is there while each game runs, and gone once it stops",
      seen == [True, False, True, False], seen)

shutil.rmtree(TMP, ignore_errors=True)
try: os.remove(STAGED)
except OSError: pass
if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: options for every resident core, zipped games described as RetroArch describes them, "
      "and a staged game's file removed when it stops")
