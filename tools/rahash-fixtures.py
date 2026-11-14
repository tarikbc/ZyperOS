#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Make tools/fixtures/rahash/: the .chd files check-rahash reads, and the
hashes it expects of every container it builds (tools/discfix.py).

    python3 tools/rahash-fixtures.py --ref <rhashref> [--chdman chdman]

Not part of make check: it needs chdman (MAME's, Homebrew's rom-tools) and a
reference hasher, `rhashref`, built from upstream rcheevos (MIT,
github.com/RetroAchievements/rcheevos) - a few lines around
rc_hash_generate_from_file with rc_hash_init_default_cdreader:

    rhashref <console id> <file>...   ->  <hash>\\t<file>

The expected hashes come from that reference on the plain images only - the
.cue/.bin, the .iso, the PSN .pbp. rcheevos reads no .chd, .cso or PSone .pbp
by itself; the frontends that do (RetroArch's CHD reader, PPSSPP's block
devices, DuckStation) hand it the plain image's sectors, so the plain image's
hash is what each container must give. chdman packs the CHDs here once, and
they are committed: the check has no chdman.
"""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import discfix  # noqa: E402

OUT = os.path.join(HERE, "fixtures", "rahash")

PSX, PSP = 12, 41                 # rcheevos console ids

# name, source, chdman command, extra arguments
CHDS = (
    ("ps1-cd.chd", "ps1.cue", "createcd", []),                  # cdlz,cdzl,cdfl
    ("ps1-cdfl.chd", "ps1.cue", "createcd", ["-c", "cdfl"]),
    ("ps1-cdzl.chd", "ps1.cue", "createcd", ["-c", "cdzl"]),
    ("ps1sub-cd.chd", "ps1sub.cue", "createcd", []),
    ("psp-dvd.chd", "psp.iso", "createdvd", []),                # lzma,zlib,huff,flac:
                                                                # stored, self, LZMA,
                                                                # Deflate and FLAC hunks
    ("psp-huff.chd", "psp.iso", "createdvd", ["-c", "huff"]),
    ("psp-cd.chd", "psp.iso", "createcd", []),                  # a cooked MODE1 track
)


def sources(d):
    """The plain images, written into d; their md5s, for the check to see
    that discfix still builds the same bytes"""
    img, _ = discfix.ps1_iso(small=True)
    raw = discfix.raw_track(img, 2, discfix.xa_ranges(img))
    sub, _ = discfix.ps1_iso(subdir=True, seed="ps1b", small=True)
    rawsub = discfix.raw_track(sub, 2)
    psp = discfix.psp_iso(small=True)
    files = {
        "ps1.bin": raw, "ps1.cue": discfix.cue("ps1.bin").encode(),
        "ps1sub.bin": rawsub, "ps1sub.cue": discfix.cue("ps1sub.bin").encode(),
        "psp.iso": psp, "psn.pbp": discfix.pbp_psp(),
    }
    for n, b in files.items():
        with open(os.path.join(d, n), "wb") as f:
            f.write(b)
    return {n: hashlib.md5(b).hexdigest() for n, b in files.items() if not n.endswith(".cue")}


def ref(binary, console, path):
    out = subprocess.run([binary, str(console), path], capture_output=True, text=True)
    h = out.stdout.partition("\t")[0].strip()
    if out.returncode or len(h) != 32:
        sys.exit(f"rhashref failed on {path}: {out.stdout}{out.stderr}")
    return h


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", required=True, help="rhashref, built from upstream rcheevos")
    ap.add_argument("--chdman", default="chdman")
    a = ap.parse_args()

    ver = subprocess.run([a.chdman], capture_output=True, text=True).stdout.splitlines()
    ver = next((l for l in ver if "chdman" in l), "chdman")
    tmp = tempfile.mkdtemp(prefix="rahash-fix-")
    md5s = sources(tmp)
    hashes = {
        "ps1": ref(a.ref, PSX, os.path.join(tmp, "ps1.cue")),
        "ps1sub": ref(a.ref, PSX, os.path.join(tmp, "ps1sub.cue")),
        "psp": ref(a.ref, PSP, os.path.join(tmp, "psp.iso")),
        "psn": ref(a.ref, PSP, os.path.join(tmp, "psn.pbp")),
    }
    os.makedirs(OUT, exist_ok=True)
    chds = {}
    for name, src, cmd, extra in CHDS:
        dst = os.path.join(OUT, name)
        r = subprocess.run([a.chdman, cmd, "-f", "-i", os.path.join(tmp, src), "-o", dst] + extra,
                           capture_output=True, text=True)
        if r.returncode:
            sys.exit(f"chdman {cmd} {name}: {r.stdout}{r.stderr}")
        system = "PS1" if name.startswith("ps1") else "PSP"
        game = "ps1sub" if name.startswith("ps1sub") else "ps1" if system == "PS1" else "psp"
        info = subprocess.run([a.chdman, "info", "-i", dst], capture_output=True, text=True).stdout
        codecs = next((l.split(":", 1)[1].strip() for l in info.splitlines()
                       if l.startswith("Compression")), "")
        chds[name] = {"system": system, "hash": hashes[game], "codecs": codecs,
                      "md5": hashlib.md5(open(dst, "rb").read()).hexdigest()}
        print(f"  {name:<15} {os.path.getsize(dst):>7} bytes  {codecs}")
    manifest = {
        "made_with": ver.strip(),
        "reference": "upstream rcheevos rc_hash_generate_from_file, default cdreader",
        "sources": md5s,
        "hashes": hashes,
        "chd": chds,
    }
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
