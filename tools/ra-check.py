#!/usr/bin/env python3
"""Report which games RetroAchievements recognizes, and which have a set.

    tools/ra-check.py [--roms DIR] [--system NAME] [--verbose]

RA identifies a game by an MD5 over the ROM, but not always over the WHOLE
ROM - each console has its own rule about what to skip. This implements the
rules for the nine systems ZyperOS ships and asks RA what it makes of each.

Needs no credentials. `r=gameid` is unauthenticated; it does need a
User-Agent, because RA refuses curl's default with `unsupported_client`,
which reads like a permissions problem and is not one.

SEQUENTIALLY, on purpose. A first version ran six requests at a time, got
rate-limited, counted the failures as "no achievements" and reported 48%
coverage. The real figure was 93%. Anything that turns a network failure into
a negative result will eventually tell you a confident lie; failures are
retried here and counted separately.

Measured 2026-08-29 over 180 ROMs: 168 recognized. Of the 12 that are not,
8 are fan translations - romhacks are in no No-Intro-derived database under
those names, so RA has never seen their hashes and never will.
"""
import argparse
import hashlib
import json
import os
import sys
import time
import urllib.parse
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
UA = "ZyperOS/1.0 (+achievement id check)"

# systems.cfg tag -> how much of the ROM RA hashes.
#   NES   an iNES header is 16 bytes of container, not game
#   SFC   copier headers are 512 bytes and only present sometimes, which is
#         what the size test detects
#   PCE   the same idea against a different block size
#   MD/SMS/GG  an SMD-style header, likewise conditional
#   GB/GBC/GBA  the whole file; there is no container to strip
#   NES (FDS)  fwNES's 16-byte "FDS\x1a" header, rc_hash_nes's second case
#   A7800  a 128-byte A78 header, "ATARI7800" at byte 1 (rc_hash_7800)
#   LYNX   a 64-byte "LYNX\0" header (rc_hash_lynx)
#   N64    the big-endian (.z64) order whatever the file's: 0x37 first is
#          byte-swapped (.v64), 0x40 little-endian words (.n64); any other
#          first byte is not an N64 ROM, and None (rc_hash_n64)
def ra_body(tag, d):
    if tag == "NES":
        return d[16:] if d[:4] in (b"NES\x1a", b"FDS\x1a") else d
    if tag == "A7800":
        return d[128:] if d[1:10] == b"ATARI7800" else d
    if tag == "LYNX":
        return d[64:] if d[:5] == b"LYNX\0" else d
    if tag == "N64":
        if not d:
            return None
        if d[0] == 0x80:
            return d
        if d[0] == 0x37:
            b = bytearray(d)
            b[0:len(b) & ~1:2], b[1:len(b) & ~1:2] = d[1:len(d) & ~1:2], d[0:len(d) & ~1:2]
            return bytes(b)
        if d[0] == 0x40:
            b = bytearray(d)
            m = len(d) & ~3
            for k in range(4):
                b[k:m:4] = d[3 - k:m:4]
            return bytes(b)
        return None
    if tag == "SFC":
        return d[512:] if len(d) % 1024 == 512 else d
    if tag == "PCE":
        return d[512:] if len(d) % 131072 == 512 else d
    if tag in ("MD", "SMS", "GG"):
        return d[512:] if len(d) % 16384 == 512 else d
    return d


# A PlayStation disc is not hashed whole: RA hashes the boot executable that
# SYSTEM.CNF names (PSX.EXE without one), prefixed with that name. rcheevos's
# rc_hash_psx and its ISO 9660 walk, restated - and restated again in C in
# src/rahash.c, which check-rahash holds to this one. It reads sectors, not the
# whole image. .cue, a bare .bin/.iso/.img and the first disc of an .m3u; .chd
# needs a CHD decoder, and RA does not hash PlayStation .pbp files at all
# (rcheevos files the extension under the PSP), so both give None.
DISC_TAGS = ("PS1",)
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


def _sibling(path, name):
    return name if os.path.isabs(name) else os.path.join(os.path.dirname(path), name)


def _psx_track1(path):
    """(file, byte offset of track 1) for a disc, or None."""
    ext = os.path.splitext(path)[1].lower()
    if ext == ".m3u":
        for line in open(path, encoding="utf-8", errors="replace"):
            line = line.strip()
            if line and not line.startswith("#"):
                first = _sibling(path, line.split("|")[0])
                return None if first.lower().endswith(".m3u") else _psx_track1(first)
        return None
    if ext == ".cue":
        cur, ss, in1 = None, 2352, False
        for line in open(path, encoding="utf-8", errors="replace"):
            t = line.strip()
            u = t.upper()
            if u.startswith("FILE"):
                q = t.split('"')
                if len(q) >= 3:
                    cur = q[1]
            elif u.startswith("TRACK"):
                f = t.split()
                in1 = len(f) > 1 and f[1].isdigit() and int(f[1]) == 1
                if in1:
                    ss = 2048 if "/2048" in t else 2336 if "/2336" in t else 2352
            elif in1 and u.startswith("INDEX 01") and cur:
                mm, sec, fr = (int(x) for x in t[8:].strip().split(":")[:3])
                return _sibling(path, cur), ((mm * 60 + sec) * 75 + fr) * ss
        return None
    if ext in (".bin", ".iso", ".img"):
        return path, 0
    return None


class _Track:
    def __init__(self, f, offset):
        self.f, self.offset = f, offset
        self.ss = None
        for ss in (2352, 2336):
            f.seek(16 * ss + offset)
            h = f.read(32)
            if len(h) == 32 and h[:12] == SYNC:
                bcd = lambda b: (b >> 4) * 10 + (b & 15)
                self.ss, self.hdr = ss, (24 if h[25:30] == b"CD001" else 16)
                self.first = (bcd(h[12]) * 60 + bcd(h[13])) * 75 + bcd(h[14]) - 150 - 16
                return
        f.seek(16 * 2048 + offset)
        h = f.read(32)
        if h[1:6] == b"CD001":
            self.ss, self.hdr, self.first = 2048, 0, 0

    def read(self, sector, n=2048):
        if sector < self.first:
            return b""
        self.f.seek((sector - self.first) * self.ss + self.hdr + self.offset)
        return self.f.read(min(n, 2048))

    def find(self, path):
        """(sector, size) of a file by backslashed path, or (0, 0). The root's
        length keeps counting down inside a subdirectory, as in rcheevos."""
        pvd = self.read(self.first + 16, 256)
        if pvd[:6] != b"\x01CD001":
            return 0, 0
        lbs = int.from_bytes(pvd[128:130], "little")
        sector = int.from_bytes(pvd[158:161], "little")
        remaining = int.from_bytes(pvd[166:170], "little")
        parts = path.lstrip("\\").split("\\")
        for i, part in enumerate(parts):
            last, want = i == len(parts) - 1, part.encode().lower()
            hit = None
            while hit is None:
                sec = self.read(sector)
                if len(sec) < 2048:
                    return 0, 0
                r = 0
                while r < 2048 - 34 and sec[r]:
                    n = sec[32 + r]
                    # The same bounds the C side holds: a name longer than its
                    # record, or than the sector, ends the scan. Python would
                    # not over-read, but the two must stop at the same record.
                    if r + sec[r] > 2048 or 33 + n > sec[r] or r + 33 + n > 2048:
                        break
                    name = sec[r + 33:r + 33 + n]
                    if n >= len(want) and name[:len(want)].lower() == want and (
                            n == len(want) or (last and name[len(want):len(want) + 1] == b";")):
                        hit = (int.from_bytes(sec[r + 2:r + 5], "little"),
                               int.from_bytes(sec[r + 10:r + 14], "little"))
                        break
                    r += sec[r]
                if hit is None:
                    if remaining <= lbs:
                        return 0, 0
                    remaining -= lbs
                    sector += 1
            if last:
                return hit
            sector = hit[0]
        return 0, 0


def psx_hash_input(path):
    """The bytes RA's PlayStation hash is the MD5 of, or None."""
    t1 = _psx_track1(path)
    if not t1 or not os.path.isfile(t1[0]):
        return None
    with open(t1[0], "rb") as f:
        t = _Track(f, t1[1])
        if not t.ss:
            return None
        name, (sector, size) = b"", t.find("SYSTEM.CNF")
        if sector:
            cnf = t.read(sector, 2047).split(b"\0")[0]
            sector = 0
            for line in cnf.split(b"\n"):
                if not line.startswith(b"BOOT"):
                    continue
                v = line[4:].lstrip()
                if not v.startswith(b"="):
                    continue
                v = v[1:].lstrip()
                if v.startswith(b"cdrom:"):
                    v = v[6:]
                v = v.lstrip(b"\\")
                end = 0
                while end < len(v) and not chr(v[end]).isspace() and v[end:end + 1] != b";":
                    end += 1
                name = v[:end][:63]
                sector, size = t.find(name.decode("latin-1"))
                break
        if not sector:
            sector, size = t.find("PSX.EXE")
            name = b"PSX.EXE" if sector else b""
        head = t.read(sector, 32) if sector else b""
        if len(head) < 32:
            return None
        if head[:7] == b"PS-X EXE":
            size = int.from_bytes(head[28:32], "little") + 2048
        size = min(size, 64 * 1024 * 1024)
        out = [name]
        while size > 0:
            d = t.read(sector, min(size, 2048))
            if not d:
                break
            out.append(d)
            size -= len(d)
            sector += 1
        return b"".join(out)


# Disc containers with no walk here, refused unread as src/rahash.c refuses
# them. Not .bin: a Genesis or 2600 cartridge is one too.
DISC_EXTS = ("chd", "cue", "ccd", "toc", "iso", "img", "m3u", "pbp", "mdf", "cbn")


def ra_md5(tag, path):
    """RA's hash of one game, or None when there is none to make."""
    if tag in DISC_TAGS:
        body = psx_hash_input(path)
        return hashlib.md5(body).hexdigest() if body is not None else None
    if tag in ("ARCADE", "NEOGEO", "MAME"):
        # rcheevos's rc_hash_arcade: the set's name, not its chips.
        name = os.path.splitext(os.path.basename(path))[0]
        return hashlib.md5(name.encode()).hexdigest()
    if os.path.splitext(path)[1][1:].lower() in DISC_EXTS:
        return None
    data = rom_bytes(path)
    body = ra_body(tag, data) if data is not None else None
    return hashlib.md5(body).hexdigest() if body is not None else None


def systems_from_cfg(path):
    out = []
    for line in open(path, encoding="utf-8"):
        f = line.rstrip("\n").split("|")
        if len(f) > 7 and f[0] == "sys":
            exts = {e.strip().lower() for e in f[7].split(",") if e.strip()}
            out.append((f[2], f[4], exts))
    return out


def rom_bytes(path):
    """The ROM itself. The library is zipped one game per archive, and RA
    hashes what is inside, not the container.

    THE LARGEST ENTRY, not the first, because that is the one Diatom loads
    (src/zip.c). For one-ROM-per-archive the two are the same file and the
    distinction never shows; for anything else this would hash one game and
    run another, and the achievements would silently be for something else."""
    if path.lower().endswith(".zip"):
        with zipfile.ZipFile(path) as z:
            infos = [i for i in z.infolist() if not i.is_dir() and i.file_size]
            if not infos:
                return None
            return z.read(max(infos, key=lambda i: i.file_size))
    return open(path, "rb").read()


def gameid(md5, tries=3):
    """RA's id for a hash. 0 means RA does not know it; -1 means the question
    could not be asked, which is a different thing and must not be counted as
    an answer."""
    for k in range(tries):
        try:
            body = urllib.parse.urlencode({"r": "gameid", "m": md5}).encode()
            req = urllib.request.Request("https://retroachievements.org/dorequest.php",
                                         data=body, headers={"User-Agent": UA})
            return json.load(urllib.request.urlopen(req, timeout=25)).get("GameID", 0)
        except Exception:
            time.sleep(1.5 * (k + 1))
    return -1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", default=os.path.join(ROOT, "ZyperOS-Test-Set", "Roms"))
    ap.add_argument("--system")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()

    systems = systems_from_cfg(os.path.join(ROOT, "config", "systems.cfg"))
    if a.system:
        systems = [s for s in systems if s[0] == a.system]
        if not systems:
            sys.exit(f"ra-check: {a.system} is not in systems.cfg")

    tot = hit = failed = 0
    misses = []
    for folder, tag, exts in systems:
        d = os.path.join(a.roms, folder)
        if not os.path.isdir(d):
            continue
        k = n = 0
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if f.startswith(".") or not os.path.isfile(p):
                continue
            if exts and os.path.splitext(f)[1][1:].lower() not in exts:
                continue
            h = ra_md5(tag, p)
            if h is None:
                continue
            n += 1
            g = gameid(h)
            if g > 0:
                k += 1
                if a.verbose:
                    print(f"    {g:>6}  {f}")
            elif g < 0:
                failed += 1
                print(f"    ASK FAILED: {folder}/{f}")
            else:
                misses.append((folder, f))
            time.sleep(0.12)          # civility, and it is what keeps this honest
        hit += k
        tot += n
        print(f"  {folder:<20} {k:>3}/{n}")

    if tot:
        print(f"  {'TOTAL':<20} {hit:>3}/{tot}  ({100 * hit // tot}%)"
              f"   unanswered: {failed}")
    tr = [m for m in misses if "Translated" in m[1]]
    if misses:
        print(f"\n  not recognized: {len(misses)}, of which fan translations: {len(tr)}")
        for folder, f in misses:
            if "Translated" not in f:
                print(f"    {folder}: {f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
