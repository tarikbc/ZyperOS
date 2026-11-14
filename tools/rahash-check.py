#!/usr/bin/env python3
"""Do the C and Python hashers agree, over every ROM in the library?

    make check-rahash

Two implementations of RetroAchievements' per-console hash rules exist because
one has to run on the device and one has to run on a host. That is a
duplication, and this is what stops it being a drift: they are both run over
the same ROMs and every hash has to match.

Getting a rule wrong does not crash anything. It produces a hash RA has never
seen, which looks exactly like a game RA does not know - and there are twelve
of those in this library already, so the wrong answer hides among real ones.
"""
import hashlib
import importlib.util
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build-native", "rahash-check")


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _rec(sector, size, name, flags=0):
    """One ISO 9660 directory record, as a disc writes it."""
    n = len(name)
    ln = 33 + n + (n + 1) % 2
    return (bytes([ln, 0]) + sector.to_bytes(4, "little") + sector.to_bytes(4, "big")
            + size.to_bytes(4, "little") + size.to_bytes(4, "big") + bytes(7)
            + bytes([flags, 0, 0]) + (1).to_bytes(2, "little") + (1).to_bytes(2, "big")
            + bytes([n]) + name + bytes(ln - 33 - n))


def _iso(path, root):
    """A 2048-byte-sector PlayStation image: a volume descriptor at 16, the
    root directory at 18, SYSTEM.CNF at 20 and a 4096-byte PS-X EXE at 21."""
    sec = [bytearray(2048) for _ in range(23)]
    pvd = sec[16]
    pvd[0:6] = b"\x01CD001"
    pvd[128:130] = (2048).to_bytes(2, "little")
    pvd[156:156 + 34] = _rec(18, 2048, b"\x00", flags=2)
    sec[18][:len(root)] = root
    sec[20][:29] = b"BOOT = cdrom:\\SLUS_000.01;1\r\n"
    exe = bytearray(4096)
    exe[0:8] = b"PS-X EXE"
    exe[28:32] = (2048).to_bytes(4, "little")
    exe[2048:] = bytes(range(256)) * 8
    sec[21], sec[22] = exe[:2048], exe[2048:]
    with open(path, "wb") as f:
        f.write(b"".join(sec))
    return b"SLUS_000.01" + bytes(exe)


def disc_fixtures(rac):
    """The PlayStation walk on two images made here, so it is checked with no
    library present: one well formed, whose hash is known independently of
    both hashers, and one whose last directory record names SYSTEM.CNF;1 with
    only its first five bytes inside the sector, and one whose record is
    shorter than its own name. That one must be refused by
    both, and the C side runs under AddressSanitizer where the compiler has
    it, so an over-read past the sector fails here rather than on a card."""
    import tempfile
    bad = 0
    tmp = tempfile.mkdtemp(prefix="rahash-")
    good = os.path.join(tmp, "good.iso")
    root = (_rec(18, 2048, b"\x00", 2) + _rec(18, 2048, b"\x01", 2)
            + _rec(20, 29, b"SYSTEM.CNF;1") + _rec(21, 4096, b"SLUS_000.01;1"))
    want = hashlib.md5(_iso(good, root)).hexdigest()
    crafted = os.path.join(tmp, "crafted.iso")
    def filler(n):                        # a well-formed record n long
        r = bytearray(_rec(19, 0, b"X").ljust(n, b"\0"))
        r[0] = n
        return bytes(r)
    # Seven of 254 and one of 232 reach byte 2010, so the scan gets there.
    trunc = bytearray((filler(254) * 7 + filler(232)).ljust(2048, b"\0"))
    trunc[2010] = 34                      # the last record the scan reads,
    trunc[2010 + 32] = 12                 # naming 12 bytes of which 5 fit
    trunc[2043:2048] = b"SYSTE"           # the sector: a match running off
                                          # its end
    _iso(crafted, bytes(trunc))
    short = os.path.join(tmp, "short.iso")
    r = bytearray(_rec(20, 29, b"SYSTEM.CNF;1"))
    r[32] = 40                            # a name longer than its record;
    _iso(short, bytes(r) + _rec(21, 4096, b"SLUS_000.01;1"))  # read on past
                                          # it, the image would hash

    asan = os.path.join(ROOT, "build-native", "rahash-check-asan")
    cc = os.environ.get("CC", "cc")
    built = subprocess.run([cc, "-std=gnu11", "-D_GNU_SOURCE", "-O1", "-g",
                            "-fsanitize=address", "-fno-omit-frame-pointer",
                            "-o", asan, os.path.join(ROOT, "tools", "rahash-check.c"),
                            os.path.join(ROOT, "src", "rahash.c"), "-lz"],
                           capture_output=True).returncode == 0
    binary = asan if built else BIN
    if not built:
        print("  note: no AddressSanitizer here; the crafted images run unsanitized")
    out = subprocess.run([binary, "PS1", good, crafted, short],
                         capture_output=True, text=True)
    got = dict(reversed(l.split("\t", 1)) for l in out.stdout.splitlines())
    if "AddressSanitizer" in out.stderr or out.returncode not in (0, 1):
        bad += 1
        print("    C hasher read out of bounds on a crafted image:\n" + out.stderr[-2000:])
    for path, expect in ((good, want), (crafted, None), (short, None)):
        py = rac.ra_md5("PS1", path)
        c = got.get(path)
        c = None if c in (None, "FAILED") else c
        name = os.path.basename(path)
        if py != expect or c != expect:
            bad += 1
            print(f"    {name}: want {expect}, C {c}, python {py}")
        else:
            print(f"  ok  {name:<12} {expect or 'refused by both'}")
    return bad


def named_and_refused(rac):
    """An arcade or Neo Geo set hashes as its name, md5("mslug") for
    mslug.zip, whatever is in it; a Sega CD or Neo Geo CD disc, a PSP .cso
    or .pbp, a Dreamcast .cdi and a Saturn .mds are refused
    by both hashers without being read. Files made here, so this runs with
    no library."""
    import tempfile
    bad = 0
    tmp = tempfile.mkdtemp(prefix="rahash-")
    cases = []
    for tag, name, want in (("NEOGEO", "mslug.zip", hashlib.md5(b"mslug").hexdigest()),
                            ("ARCADE", "ssf2.7z", hashlib.md5(b"ssf2").hexdigest()),
                            ("SEGACD", "Sonic CD (USA).chd", None),
                            ("NEOCD", "Metal Slug.cue", None),
                            ("PSP", "LittleBigPlanet.cso", None),
                            ("PSP", "Patapon.pbp", None),
                            ("DC", "Shenmue.cdi", None),
                            ("SATURN", "Nights.mds", None)):
        p = os.path.join(tmp, name)
        with open(p, "wb") as f:
            f.write(b"not a set, not a disc")
        cases.append((tag, p, want))
    for tag, p, want in cases:
        out = subprocess.run([BIN, tag, p], capture_output=True, text=True)
        c = out.stdout.partition("\t")[0].strip() or None
        c = None if c == "FAILED" else c
        py = rac.ra_md5(tag, p)
        name = os.path.basename(p)
        if py != want or c != want:
            bad += 1
            print(f"    {tag} {name}: want {want}, C {c}, python {py}")
        else:
            print(f"  ok  {tag:<7} {name:<20} {want or 'refused by both'}")
    return bad


def headers_and_orders(rac):
    """The container rules, on files made here: an A78 header, a Lynx header
    and an FDS header are not hashed; an N64 ROM is hashed in its .z64 order
    whichever of the three it is in, and a file that is none of them gets no
    hash; a MAME set hashes as its name. Each case's hash is known without
    either hasher (rcheevos's rc_hash_7800, _lynx, _nes, _n64, _arcade)."""
    import tempfile
    bad = 0
    tmp = tempfile.mkdtemp(prefix="rahash-")
    body = bytes(range(256)) * 16
    z64 = b"\x80\x37\x12\x40" + body
    v64 = bytearray(z64)
    v64[0::2], v64[1::2] = z64[1::2], z64[0::2]
    n64 = bytearray(z64)
    for k in range(4):
        n64[k::4] = z64[3 - k::4]
    md5 = lambda b: hashlib.md5(b).hexdigest()
    cases = (
        ("A7800", "Pole Position II.a78", b"\x01ATARI7800" + bytes(118) + body, md5(body)),
        ("A7800", "Headerless.a78", body, md5(body)),
        ("LYNX", "Chips Challenge.lnx", b"LYNX\0" + bytes(59) + body, md5(body)),
        ("NES", "Zelda no Densetsu.fds", b"FDS\x1a" + bytes(12) + body, md5(body)),
        ("N64", "Mario Kart 64.z64", z64, md5(z64)),
        ("N64", "Mario Kart 64.v64", bytes(v64), md5(z64)),
        ("N64", "Mario Kart 64.n64", bytes(n64), md5(z64)),
        ("N64", "Not a ROM.n64", b"\x00" + body, None),
        ("MAME", "mslug.zip", b"not read", md5(b"mslug")),
    )
    for tag, name, data, want in cases:
        p = os.path.join(tmp, name)
        with open(p, "wb") as f:
            f.write(data)
        out = subprocess.run([BIN, tag, p], capture_output=True, text=True)
        c = out.stdout.partition("\t")[0].strip() or None
        c = None if c == "FAILED" else c
        py = rac.ra_md5(tag, p)
        if py != want or c != want:
            bad += 1
            print(f"    {tag} {name}: want {want}, C {c}, python {py}")
        else:
            print(f"  ok  {tag:<7} {name:<22} {want or 'refused by both'}")
    return bad


# The shelves whose every kind of file is a disc with no walk here: a game of
# one has no hash, so its Achievements rows say "not for this system" rather
# than "not checked yet" (src/rahash.h, ra_hash_support). A systems.cfg line
# that changes the list has to change it here, on purpose.
NO_SYSTEM = {"PSP", "SEGACD", "3DO", "SATURN", "NEOCD", "DC"}


def support(rac):
    """ra_hash_support, which reads no file, against systems.cfg: which
    shelves have no hash at all, what a refused file on a shelf that has one
    is, and that each kind it refuses both hashers refuse as a real file, so
    a row never says no hash for a game one of them would hash."""
    import tempfile
    bad = 0
    systems = rac.systems_from_cfg(os.path.join(ROOT, "config", "systems.cfg"))
    tmp = tempfile.mkdtemp(prefix="rahash-")
    found = set()
    for folder, tag, exts in systems:
        names = [f"Game.{e}" for e in sorted(exts)]
        if not names:
            continue
        out = subprocess.run([BIN, "--support", tag, ",".join(sorted(exts))] + names,
                             capture_output=True, text=True)
        said = dict(reversed(l.split("\t", 1)) for l in out.stdout.splitlines())
        if set(said.values()) == {"system"}:
            found.add(tag)
        # Each name it said has no hash, as a real file: both must refuse it
        refused = [n for n in names if said.get(n) in ("file", "system")]
        files = []
        for n in refused:
            p = os.path.join(tmp, tag, n)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, "wb") as f:
                f.write(b"neither a disc nor a cartridge")
            files.append(p)
        if not files:
            continue
        hashed = subprocess.run([BIN, tag] + files, capture_output=True, text=True)
        c = {pp: h for h, _, pp in (l.partition("\t") for l in hashed.stdout.splitlines())}
        for n, p in zip(refused, files):
            py = rac.ra_md5(tag, p)
            if c.get(p) != "FAILED" or py is not None:
                bad += 1
                print(f"    {tag} {n}: said {said[n]}, yet C {c.get(p)}, python {py}")
    if found != NO_SYSTEM:
        bad += 1
        print(f"    no hash for the shelf: {sorted(found)}, want {sorted(NO_SYSTEM)}")
    else:
        print(f"  ok  not for this system: {' '.join(sorted(found))}")
    cases = (("PSP", "iso,cso,chd,pbp", "LittleBigPlanet.chd", "system"),
             ("PSP", "iso,cso,chd,pbp", "Patapon.cso", "system"),
             ("PS1", "cue,chd,pbp,m3u,ccd,toc,iso,mdf,cbn,exe", "Spyro.chd", "file"),
             ("PS1", "cue,chd,pbp,m3u,ccd,toc,iso,mdf,cbn,exe", "Spyro.pbp", "file"),
             ("PS1", "cue,chd,pbp,m3u,ccd,toc,iso,mdf,cbn,exe", "Spyro.cue", "ok"),
             ("PCE", "pce,sgx,cue,ccd,chd,toc,m3u,zip", "Ys I & II.cue", "file"),
             ("PCE", "pce,sgx,cue,ccd,chd,toc,m3u,zip", "Bonk.pce", "ok"),
             ("NES", "nes,fds,unf,unif,zip", "Contra.nes", "ok"),
             ("NEOGEO", "zip,7z", "mslug.zip", "ok"),
             ("SEGACD", "", "Sonic CD.chd", "file"))
    for tag, exts, name, want in cases:
        out = subprocess.run([BIN, "--support", tag, exts, name],
                             capture_output=True, text=True)
        got = out.stdout.partition("\t")[0]
        if got != want:
            bad += 1
            print(f"    {tag} {name} ({exts or 'any file'}): want {want}, got {got}")
        else:
            print(f"  ok  {tag:<7} {name:<20} {want}")
    return bad


def main():
    rac = load("ra_check", os.path.join(ROOT, "tools", "ra-check.py"))
    if headers_and_orders(rac):
        print("\nFAIL: cartridge headers, N64 byte orders and MAME names")
        return 1
    if disc_fixtures(rac):
        print("\nFAIL: the PlayStation disc walk")
        return 1
    if named_and_refused(rac):
        print("\nFAIL: arcade names and refused discs")
        return 1
    if support(rac):
        print("\nFAIL: which games have no hash, by name (ra_hash_support)")
        return 1
    roms = sys.argv[1] if len(sys.argv) > 1 else \
        os.path.join(ROOT, "ZyperOS-Test-Set", "Roms")
    if not os.path.isdir(roms):
        print(f"  skip: no ROM library at {roms}")
        return 0

    systems = rac.systems_from_cfg(os.path.join(ROOT, "config", "systems.cfg"))
    total = mismatched = failed = 0
    for folder, tag, exts in systems:
        d = os.path.join(roms, folder)
        if not os.path.isdir(d):
            continue
        files = [os.path.join(d, f) for f in sorted(os.listdir(d))
                 if not f.startswith(".")
                 and os.path.isfile(os.path.join(d, f))
                 and (not exts or os.path.splitext(f)[1][1:].lower() in exts)]
        if not files:
            continue

        out = subprocess.run([BIN, tag] + files, capture_output=True, text=True)
        got = {}
        for line in out.stdout.splitlines():
            h, _, p = line.partition("\t")
            got[p] = h

        n = 0
        for p in files:
            want = rac.ra_md5(tag, p)
            have = got.get(p)
            if want is None:
                # Nothing to hash - a .chd or .pbp disc. The C side has to
                # agree there is nothing, rather than hash something else.
                if have not in (None, "FAILED"):
                    mismatched += 1
                    print(f"    C hashed what Python cannot: {os.path.basename(p)}")
                continue
            total += 1
            n += 1
            if have is None or have == "FAILED":
                failed += 1
                print(f"    C hasher failed: {os.path.basename(p)}")
            elif have != want:
                mismatched += 1
                print(f"    MISMATCH {os.path.basename(p)}\n"
                      f"      C      {have}\n      python {want}")
        print(f"  {folder:<20} {n:>3}")

    print(f"\n  {total} ROMs, {mismatched} mismatched, {failed} unreadable")
    if mismatched or failed:
        return 1
    print("\nok: both hashers agree on every ROM")
    return 0


if __name__ == "__main__":
    sys.exit(main())
