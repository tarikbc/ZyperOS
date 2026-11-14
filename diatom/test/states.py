"""Save states as Diatom writes them since 2026-11-12, end to end against the
stub core with a state big enough to pack (STUBCORE_STATE_PAD).

  - the state a game leaves as it stops is version 2, packed: a header with
    the core's name where version 1 had it, the LZ4 block, its sizes right,
    and a log line that says both sizes and what each half cost
  - a game resumed from it carries on where it stopped: the core is given
    every byte it serialized (the stub refuses a state with one changed)
  - a quick SAVE and LOAD while the game runs, on Diatom's thread, the same
  - a version 1 state, as every Diatom before wrote it, resumes as it did
  - a state damaged or cut short is said and refused, and the game starts
    from its beginning, as for any state that will not load

Python has no LZ4 to unpack with, so the bytes' round trip is the stub's
to judge, and the frame it carries on from. test/statefile_test.c holds the
format itself.

Needs build/desktop (diatom and stubcore.so) and SKIPS without it, as
check-now does. SDL's dummy drivers: no window, no sound.

    python3 test/states.py
"""
import os, re, shutil, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)

PAD = 1 << 20
TMP = tempfile.mkdtemp(prefix="diatom-states-")
ROM, LOG, ERR = f"{TMP}/A Game.stub", f"{TMP}/input.log", f"{TMP}/d.err"
with open(ROM, "wb") as f:
    f.write(b"STUB" + bytes(range(60)))

# The stub's pad (test/stubcore.c pad_byte): it repeats every 4 KB
BLOCK = bytes(((i * 31) >> 3) & 0xFF if i < 512 else 0 for i in range(4096))
PADDING = (BLOCK * (PAD // 4096 + 1))[:PAD]

fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

def header(path):
    with open(path, "rb") as f: b = f.read()
    h = {"magic": b[:8], "version": struct.unpack("<I", b[8:12])[0],
         "payload": struct.unpack("<I", b[12:16])[0],
         "core": b[16:80].split(b"\0")[0].decode(), "len": len(b)}
    if h["version"] == 2:
        h["codec"], h["stored"] = struct.unpack("<II", b[112:120])
    return h

def frames():
    try:
        with open(LOG) as f: return [int(l.split()[0]) for l in f if l.strip()]
    except OSError:
        return []

def log():
    with open(ERR, errors="replace") as f: return f.read()

sock = f"{TMP}/d.sock"
err = open(ERR, "w")
p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy", STUBCORE_STATE_PAD=str(PAD),
                              STUBCORE_INPUT_LOG=LOG),
                     stdout=err, stderr=err)
for _ in range(100):
    if os.path.exists(sock): break
    time.sleep(0.05)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
buf = b""

def drain(timeout, until):
    """The first line that starts with `until`, or None"""
    global buf
    end = time.time() + timeout
    s.settimeout(0.2)
    while time.time() < end:
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            if l.decode().startswith(until): return l.decode()
        try:
            d = s.recv(65536)
        except socket.timeout:
            continue
        if not d: break
        buf += d
    return None

def send(m):
    s.sendall((m + "\n").encode())

def run(extra, secs):
    """A game run for `secs`, then stopped: the first frame it ran"""
    before = len(frames())
    send(f"RUN\tcore={STUB}\trom={ROM}" + extra)
    ok = drain(4.0, "RUNNING") is not None
    time.sleep(secs)
    f = frames()[before:]
    return ok, (f[0] if f else None)

A, B, C = f"{TMP}/A.state", f"{TMP}/B.state", f"{TMP}/C.state"
try:
    drain(2.0, "READY")

    ok, _ = run(f"\texit_state={A}", 1.0)
    send("STOP")
    check("a game runs and stops", ok and drain(3.0, "EXIT") is not None)
    last = frames()[-1] if frames() else 0
    h = header(A)
    check("its state is version 2, packed with LZ4, the core named where version 1 had it",
          h["magic"] == b"DIATOMST" and h["version"] == 2 and h["codec"] == 1
          and h["core"] == "diatom-stub", h)
    check("its sizes: the core's bytes in full, the packed block, nothing else",
          h["payload"] == 4 + PAD and h["len"] == 120 + h["stored"], h)
    check(f"packed to a fraction: {h['len']} bytes for {120 + 4 + PAD}",
          h["stored"] < PAD // 4, h)
    check("the log says both sizes and what serialize, pack and write cost",
          re.search(rf"state: wrote {h['len']} bytes to {re.escape(A)} \({4 + PAD} from the core; "
                    r"serialize [\d.]+ ms, pack [\d.]+ ms, write [\d.]+ ms\)", log()),
          [l for l in log().splitlines() if "state:" in l])

    ok, first = run(f"\tresume={A}\texit_state={B}", 0.5)
    check(f"resumed, the game carries on from frame {last}",
          ok and first is not None and last - 1 <= first <= last + 2, (ok, last, first))
    check("the log says what was read, its sizes and what each half cost",
          re.search(rf"state: restored {re.escape(A)} \({h['len']} bytes, {4 + PAD} from the core; "
                    r"read [\d.]+ ms, unpack [\d.]+ ms, unserialize [\d.]+ ms\)", log()),
          [l for l in log().splitlines() if "state:" in l])

    send(f"SAVE\tpath={C}")
    check("a quick SAVE while the game runs is answered SAVED", drain(4.0, "SAVED") is not None)
    hc = header(C)
    check("and written packed", hc["version"] == 2 and hc["codec"] == 1
          and hc["len"] == 120 + hc["stored"] < PAD // 4, hc)
    send(f"LOAD\tpath={C}")
    check("a quick LOAD of it is answered LOADED", drain(4.0, "LOADED") is not None)
    send("STOP")
    drain(3.0, "EXIT")

    # Version 1, as every Diatom before 2026-11-12 wrote it: 112 bytes of
    # header, the core's bytes as they are
    old = f"{TMP}/old.state"
    with open(old, "wb") as f:
        f.write(b"DIATOMST" + struct.pack("<II", 1, 4 + PAD) + b"diatom-stub".ljust(64, b"\0")
                + b"1".ljust(32, b"\0") + struct.pack("<I", 5000) + PADDING)
    ok, first = run(f"\tresume={old}", 0.5)
    check("a version 1 state resumes as it did: the game carries on from frame 5000",
          ok and first is not None and 5000 <= first <= 5002, (ok, first))
    check("and the log says it was version 1",
          f"state: restored {old} ({112 + 4 + PAD} bytes, {4 + PAD} from the core, version 1;" in log(),
          [l for l in log().splitlines() if "state:" in l])
    send("STOP")
    drain(3.0, "EXIT")

    # Damaged in the packed block, and cut short
    with open(A, "rb") as f: good = bytearray(f.read())
    bad, cut = f"{TMP}/bad.state", f"{TMP}/cut.state"
    bad_bytes = bytearray(good)
    for i in range(120 + 40, len(bad_bytes) - 40, 97):
        bad_bytes[i] ^= 0x5A
    open(bad, "wb").write(bad_bytes)
    open(cut, "wb").write(good[:-10])
    for path, why in ((bad, "damaged"), (cut, "cut short")):
        ok, first = run(f"\tresume={path}", 0.4)
        said = (f"state: {path} is damaged" in log() or f"state: {path} is truncated" in log()
                or f"REFUSED by core: {path}" in log())
        check(f"a state {why} is said and refused, and the game starts from its beginning",
              ok and said and first is not None and first <= 2,
              (ok, first, [l for l in log().splitlines() if path in l]))
        send("STOP")
        drain(3.0, "EXIT")

finally:
    send("QUIT")
    time.sleep(0.3)
    p.terminate()
    try: p.wait(3)
    except subprocess.TimeoutExpired: p.kill()
    err.close()
    shutil.rmtree(TMP, ignore_errors=True)

if fails:
    print(f"states: {len(fails)} failed")
    sys.exit(1)
print("ok: states packed, resumed whole, version 1 still read, broken ones refused")
