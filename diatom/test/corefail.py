"""A core that would not load says why, end to end: the real dlopen, against
cores built here (src/corefail.h).

Diatom answered a core dlopen refused with ERROR code=core_missing and the
path, and the launcher put the game back on the shelf with nothing said. Now
the line carries why (machine, library, version, symbol, other) and the name
dlerror gave, which the launcher draws in the player's language
(src/corefail.c in ZyperOS). The cores:

  - a file with an ELF header of another machine (an x86-64 build on an
    aarch64 device, Davistoa's bsnes): why=machine. glibc says ENOENT for it,
    the name of a file that is there, so the header is what is read
  - a core that needs a library that is not there: why=library, name its file
  - a core that calls a function nothing has: why=symbol, name it (dlopen is
    RTLD_NOW, so it fails at the load)
  - a file that loads and has no retro_run: why=other
  - a core that is not there: why=other

and the full dlerror in the log, and a good core after all of them, still run.

Builds the cores with the C compiler (cc) and SKIPS without it, and without
build/desktop (diatom and stubcore.so), as check-resident does. A Mac's dlerror
words are not Linux's (a library is a .dylib, a function's name has an
underscore before it); the names come out the same.

    python3 test/corefail.py
"""
import os, shutil, socket, struct, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = f"{ROOT}/build/desktop/diatom"
STUB = f"{ROOT}/build/desktop/stubcore.so"
if not (os.path.exists(BIN) and os.path.exists(STUB)):
    print("skipped: no build/desktop/diatom and stubcore.so (make; make stub)")
    sys.exit(0)
CC = shutil.which(os.environ.get("CC", "cc"))
if not CC:
    print("skipped: no C compiler")
    sys.exit(0)

TMP = tempfile.mkdtemp(prefix="diatom-corefail-")
fails = []
def check(name, ok, got=None):
    print(("  ok   " if ok else "  FAIL ") + name + ("" if ok else f"\n         got  {got}"))
    if not ok: fails.append(name)

MAC = sys.platform == "darwin"
LIBEXT = "dylib" if MAC else "so"

def build(args, src_name, src):
    path = f"{TMP}/{src_name}.c"
    with open(path, "w") as f: f.write(src)
    r = subprocess.run([CC] + args + [path], capture_output=True, text=True, cwd=TMP)
    if r.returncode != 0:
        print(f"  --   {src_name}: the compiler said no: {r.stderr.strip().splitlines()[-1:]}")
        return False
    return True

def foreign_elf(path):
    """A file that begins as an ELF of another machine than this host's: an
    x86-64 one, or an aarch64 one where the host is x86-64"""
    import platform
    machine = 183 if platform.machine().lower() in ("x86_64", "amd64") else 62
    h = bytearray(64)
    h[0:4] = b"\x7fELF"
    h[4], h[5], h[6] = 2, 1, 1
    struct.pack_into("<H", h, 16, 3)        # ET_DYN
    struct.pack_into("<H", h, 18, machine)
    with open(path, "wb") as f: f.write(h + bytes(256))

cores = {}

foreign_elf(f"{TMP}/x86_libretro.so")
cores["x86"] = f"{TMP}/x86_libretro.so"

# A core that needs a library, which is then taken away
if build(["-shared", "-fPIC", "-o", f"libzyperos_gone.{LIBEXT}"] +
         (["-install_name", f"@loader_path/libzyperos_gone.{LIBEXT}"] if MAC else []),
         "dep", "int zy_dep(void) { return 1; }\n") and \
   build(["-shared", "-fPIC", "-o", "needs_libretro.so", f"-L{TMP}", "-lzyperos_gone"],
         "needs", "int zy_dep(void);\nint retro_api_version(void) { return zy_dep(); }\n"):
    os.remove(f"{TMP}/libzyperos_gone.{LIBEXT}")
    cores["library"] = f"{TMP}/needs_libretro.so"

# A core that calls what nothing has
if build(["-shared", "-fPIC", "-o", "symbol_libretro.so"] + (["-undefined", "dynamic_lookup"] if MAC else []),
         "symbol", "int zy_missing_function(void);\nint retro_api_version(void) { return zy_missing_function(); }\n"):
    cores["symbol"] = f"{TMP}/symbol_libretro.so"

# A file that loads and is no libretro core
if build(["-shared", "-fPIC", "-o", "plain_libretro.so"], "plain", "int zy_nothing(void) { return 0; }\n"):
    cores["plain"] = f"{TMP}/plain_libretro.so"

cores["gone"] = f"{TMP}/not_there_libretro.so"

ROM = f"{TMP}/A Game (World).stub"
with open(ROM, "wb") as f:
    f.write(b"STUB" + bytes(range(60)))

sock = f"{TMP}/d.sock"
log = open(f"{TMP}/d.err", "w")
p = subprocess.Popen([BIN, "--socket", sock, "--save", TMP, "--system", TMP],
                     env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
                              SDL_AUDIODRIVER="dummy"),
                     stdout=subprocess.DEVNULL, stderr=log)
# The waits are the wall clock's, and only waits: each ends as soon as what
# it waits for is there (3 to 8 s until 2026-11-24)
end = time.time() + 30
while not os.path.exists(sock) and time.time() < end:
    time.sleep(0.05)
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.connect(sock)
s.settimeout(0.2)
buf = b""

def lines_until(prefix, timeout):
    """Lines read until one starts with prefix; (that line, all of them)"""
    global buf
    seen, end = [], time.time() + timeout
    while time.time() < end:
        while b"\n" in buf:
            l, buf = buf.split(b"\n", 1)
            l = l.decode().rstrip("\r")
            seen.append(l)
            if l.startswith(prefix): return l, seen
        try:
            d = s.recv(65536)
            if not d: break
            buf += d
        except socket.timeout:
            pass
    return None, seen

def fields(line):
    return dict(kv.split("=", 1) for kv in line.split("\t")[1:] if "=" in kv)

def refused(core):
    s.sendall(f"RUN\tcore={core}\trom={ROM}\n".encode())
    line, seen = lines_until("ERROR", 30.0)
    return fields(line) if line else None

try:
    lines_until("READY", 30.0)

    f = refused(cores["x86"])
    check("an ELF of another machine: why=machine", f and f.get("code") == "core_load" and f.get("why") == "machine", f)
    check("and the core's path is said", f and f.get("msg") == cores["x86"], f)

    if "library" in cores:
        f = refused(cores["library"])
        check("a core that needs a library that is gone: why=library, the library's file",
              f and f.get("why") == "library" and f.get("name") == f"libzyperos_gone.{LIBEXT}", f)
    if "symbol" in cores:
        f = refused(cores["symbol"])
        check("a core that calls a function nothing has: why=symbol, its name",
              f and f.get("why") == "symbol" and f.get("name") == "zy_missing_function", f)
    if "plain" in cores:
        f = refused(cores["plain"])
        check("a file that loads and is no libretro core: why=other", f and f.get("why") == "other", f)
    f = refused(cores["gone"])
    check("a core that is not there: why=other, not a library", f and f.get("why") == "other", f)

    # After all of them a good core still runs
    s.sendall(f"RUN\tcore={STUB}\trom={ROM}\n".encode())
    line, _ = lines_until("RUNNING", 30.0)
    check("and a good core after them runs", line is not None, line)
    s.sendall(b"STOP\n")
    lines_until("EXIT", 30.0)
finally:
    try: s.sendall(b"QUIT\n")
    except OSError: pass
    try: p.wait(30)
    except subprocess.TimeoutExpired: p.kill()
    log.close()

text = open(f"{TMP}/d.err", errors="replace").read()
check("the log keeps dlerror's own text for the core of another machine",
      f"diatom: dlopen {cores['x86']}: " in text, text[-400:])
check("and says what it made of each",
      "core load failed: machine" in text and "core load failed: other" in text, text[-400:])
if "library" in cores:
    check("the log names the library", f"core load failed: library libzyperos_gone.{LIBEXT}" in text, text[-400:])

shutil.rmtree(TMP, ignore_errors=True)
if fails:
    print(f"{len(fails)} failed")
    sys.exit(1)
print("ok: a core that would not load says why - another machine, a library, a function - and the log keeps dlerror")
