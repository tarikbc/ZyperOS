#!/usr/bin/env python3
"""Pack a built ZyperOS folder as a System Update.

    tools/update-pack.py --payload DIR --version 0.2.0 --device pixel2 --out OUTDIR
        [--card-extra DIR] [--min-system 0.2.0] [--notes docs/releases/v0.2.0.md]

DIR is a payload's ZyperOS folder (mk/payload.sh's, or the one in the system
image at /usr/share/zyperos/card/ZyperOS). Writes, in OUTDIR:

    ZyperOS-v<version>-<device>-update.zpk   every file, one after the other,
                                             each a gzip member where that is
                                             smaller, else as it is
    ZyperOS-<device>-update.txt.unsigned     the list: path, size, SHA-256 and
                                             place in the pack of each file,
                                             up to the "end" line

tools/update-key.sh sign turns the .unsigned list into ZyperOS-<device>-
update.txt by adding the "sign" line. The format is docs/updates.md's (and
update/manifest.c's header). --card-extra is the payload's card root (the
folder above DIR): its Licenses/ files and its empty Roms folders go in under
".card/", which the switch adds to the card. --min-system defaults to
res/update/min-system. --notes takes the release notes' first eight bullet
points ("- " lines, markdown taken out) as the list's "note" lines, which
the device shows as What's New: signed with the rest.

Standard library only: it runs on the Mac and in plastron's build container.
"""
import argparse
import gzip
import hashlib
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RESERVED = {".files", ".update", ".ready", ".trial", ".staged"}
# The launcher's own state in its folder, never part of a release
STATE = {".last", ".playing"}
VERSION_RE = re.compile(r"^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$")


def path_ok(p):
    if not p or len(p) > 400 or p.startswith("/") or "\\" in p or ":" in p:
        return False
    if any(ord(c) < 0x20 or ord(c) == 0x7F for c in p):
        return False
    return all(part not in ("", ".", "..") for part in p.split("/"))


def walk(top):
    """Every file under top, relative, sorted; and the empty folders."""
    files, empty = [], []
    for d, dirs, names in os.walk(top):
        dirs.sort()
        rel = os.path.relpath(d, top)
        rel = "" if rel == "." else rel.replace(os.sep, "/")
        names = [n for n in names if n not in (".DS_Store",) and not n.startswith("._")]
        if not names and not dirs and rel:
            empty.append(rel)
        for n in sorted(names):
            files.append((rel + "/" + n) if rel else n)
    return files, empty


def release_notes(path, most=12, longest=500):
    """What's New for the device's System Update page: the points under the
    release notes' "## What's new" heading (every point of the file when it
    has none), each whole, as plain text: links to their words, the
    Markdown marks gone, one line each. The page wraps them and scrolls
    (src/main.c, update_page_draw); a point longer than `longest` is cut at a
    word with an ellipsis, which no release has needed. Until 2026-11-11 each
    was cut at 110 characters, and the "Good to know" points came too."""
    if not path or not os.path.isfile(path):
        return []
    with open(path, encoding="utf-8") as f:
        text = f.read().splitlines()
    heads = [i for i, l in enumerate(text) if re.match(r"^#{1,6}\s", l)]
    new = [i for i in heads if re.match(r"^#{1,6}\s+what'?s new\s*$", text[i], re.I)]
    if new:
        start = new[0] + 1
        end = next((i for i in heads if i > new[0]), len(text))
        text = text[start:end]
    notes = []
    for raw in text:
        m = re.match(r"^\s*[-*] +(.*)$", raw)
        if not m:
            continue
        t = re.sub(r"\[([^\]]*)\]\([^)]*\)", r"\1", m.group(1))   # [text](url)
        t = re.sub(r"\*\*|__|`", "", t)
        t = re.sub(r"(?<![\w*])[*_](\S[^*_]*)[*_](?!\w)", r"\1", t)   # *emphasis*
        t = " ".join(t.split())
        t = "".join(c for c in t if ord(c) >= 0x20 and ord(c) != 0x7F)
        if len(t) > longest:
            t = t[:longest].rsplit(" ", 1)[0] + "\u2026"
        if t:
            notes.append(t)
        if len(notes) == most:
            break
    return notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--payload", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--device", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--card-extra")
    ap.add_argument("--min-system")
    ap.add_argument("--notes")
    # system: the payload is the system (root.squashfs, boot/Image, boot/<dtb>)
    # for a card with two system slots (docs/updates.md, layer 2)
    ap.add_argument("--kind", choices=("folder", "system"), default="folder")
    a = ap.parse_args()

    if not VERSION_RE.match(a.version):
        sys.exit("update-pack: %s is not a version" % a.version)
    if not re.match(r"^[a-z0-9]+$", a.device):
        sys.exit("update-pack: %s is not a device name" % a.device)
    min_system = a.min_system
    if not min_system:
        with open(os.path.join(ROOT, "res", "update", "min-system")) as f:
            min_system = f.read().strip()
    if not VERSION_RE.match(min_system):
        sys.exit("update-pack: min-system %s is not a version" % min_system)
    word = "system" if a.kind == "system" else "update"
    if a.kind == "system" and not os.path.isfile(os.path.join(a.payload, "root.squashfs")):
        sys.exit("update-pack: a system has root.squashfs")
    vfile = os.path.join(a.payload, "VERSION")
    if a.kind == "folder" and os.path.isfile(vfile):
        with open(vfile) as f:
            have = f.read().strip()
        if have != a.version:
            sys.exit("update-pack: %s says %s, not %s" % (vfile, have, a.version))

    entries = []  # (path in the list, file on disk)
    files, empty = walk(a.payload)
    for rel in files:
        top = rel.split("/")[0]
        if top in RESERVED or top in STATE:
            continue
        entries.append((rel, os.path.join(a.payload, rel)))
    dirs = list(empty)
    if a.card_extra:
        lic = os.path.join(a.card_extra, "Licenses")
        if os.path.isdir(lic):
            for rel in walk(lic)[0]:
                entries.append((".card/Licenses/" + rel, os.path.join(lic, rel)))
        roms = os.path.join(a.card_extra, "Roms")
        if os.path.isdir(roms):
            for d, sub, _ in os.walk(roms):
                # A dot folder (.media, where covers go) is made by whatever
                # puts the first file in it, never by an update
                sub[:] = sorted(x for x in sub if not x.startswith("."))
                rel = os.path.relpath(d, a.card_extra).replace(os.sep, "/")
                if not sub:
                    dirs.append(".card/" + rel)
    for p, _ in entries:
        if not path_ok(p):
            sys.exit("update-pack: %r cannot go in an update" % p)
    for p in dirs:
        if not path_ok(p):
            sys.exit("update-pack: folder %r cannot go in an update" % p)

    os.makedirs(a.out, exist_ok=True)
    pack_name = "ZyperOS-v%s-%s-%s.zpk" % (a.version, a.device, word)
    pack_path = os.path.join(a.out, pack_name)
    lines = []
    offset = 0
    whole = hashlib.sha256()
    with open(pack_path, "wb") as pack:
        for rel, src in sorted(entries):
            with open(src, "rb") as f:
                data = f.read()
            sha = hashlib.sha256(data).hexdigest()
            stored, method = data, "r"
            if data:
                buf = io.BytesIO()
                # mtime 0 and no name: the same file packs to the same bytes
                with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0, filename="") as z:
                    z.write(data)
                if len(buf.getvalue()) < len(data) * 95 // 100:
                    stored, method = buf.getvalue(), "g"
            pack.write(stored)
            whole.update(stored)
            lines.append("file %s %d %d %d %s %s" % (sha, len(data), offset, len(stored), method, rel))
            offset += len(stored)

    head = [
        "zyperos-update 1",
        "kind " + a.kind,
        "device " + a.device,
        "version " + a.version,
        "min-system " + min_system,
        "pack %s %d %s" % (pack_name, offset, whole.hexdigest()),
    ]
    notes = ["note " + t for t in release_notes(a.notes)] if a.notes else []
    body = head + notes + ["dir " + d for d in sorted(set(dirs))] + lines + ["end %d" % len(lines)]
    unsigned = os.path.join(a.out, "ZyperOS-%s-%s.txt.unsigned" % (a.device, word))
    with open(unsigned, "w", newline="\n") as f:
        f.write("\n".join(body) + "\n")
    total = sum(os.path.getsize(s) for _, s in entries)
    print("update-pack: %d files, %.1f MB, packed %.1f MB: %s" % (len(lines), total / 1e6, offset / 1e6, pack_name))


if __name__ == "__main__":
    main()
