#!/usr/bin/env python3
"""ZyperOS's own menu music: the free tracks in res/bgm/ (src/bgm.h).

    tools/bgm-tracks.py --check           the shipped tracks held to their
                                          manifest, their notices and the
                                          size cap (make check-bgm-tracks)
    tools/bgm-tracks.py fetch LIST CACHE  download every source LIST names
                                          into CACHE/<id>/, each checked
                                          against the SHA-256 it was chosen by
    tools/bgm-tracks.py encode CACHE      make res/bgm/*.mp3 from
                                          res/bgm/tracks.json's sources, and
                                          write what came out back into it
    tools/bgm-tracks.py lab LIST CACHE OUT
                                          the menu music lab's audio
                                          (OUT/audio/<id>.mp3) and its data
                                          (OUT/menu-music.js)

LIST is a manifest: res/bgm/tracks.json, the tracks ZyperOS ships, or
docs/design/mock/menu-music-lab.json, the ones the lab offered. Each track
names the exact file it was chosen from (source.url, source.sha256), its
author, its license and the page that grants it, so the shipped MP3 can
always be made again from the file that was reviewed, and its license read
again where it was given.

These play on a card with no music of its own, so a fresh download has
music, and with the card's own while MENU > Menu Music > ZyperOS Tracks is
on (src/bgm.c, bgm_scan_all).

Making a track, the same way for the lab and for the card: the source
decoded (a tracker module rendered with openmpt123 from the author's own
file) to 48 kHz stereo, the rate the system's mix runs at, so the player's
converter has nothing to do (src/bgm.h, "the rate"); silence trimmed from
both ends, since the player goes from one track straight into the next; and
one gain for the whole track to LUFS below, the same loudness for every
track so none jumps out of the shuffle. A track whose peaks would go over
CEILING_DBTP at that gain has them held down by a limiter rather than the
whole track turned down. Then MP3 at VBR (lame -V) for the card, with the
title, author, license and page in its ID3v2 tag. The card had Ogg Vorbis
until 2026-11-07: on the Pixel 2 the menu music thread spent most of its time
in stb_vorbis, which decodes at about twice minimp3's cost, so I took
MP3 at the quality the lab played him (src/bgm_dec.c plays it gapless from
LAME's own note of its delay and padding). The lab also makes the Vorbis
file, to show what it would weigh.

encode and lab need ffmpeg, oggenc (vorbis-tools), lame and openmpt123
(libopenmpt); --check needs nothing but Python.
"""
import array
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BGM = os.path.join(ROOT, "res", "bgm")
MANIFEST = os.path.join(BGM, "tracks.json")

RATE = 48000
LUFS = -16.0           # integrated loudness of every track
CEILING_DBTP = -1.0    # true peak after the gain
QUALITY = 4            # oggenc -q, for the lab's comparison
VBR = 2                # lame -V: what the lab played me
# Every shipped track together. The lab planned 15 MB for 3 to 5 tracks;
# I picked all ten originals and kept them at full quality (2026-11-06),
# 26 MB as Vorbis, 38 MB as MP3 at VBR (2026-11-07: the cap raised with it,
# not the quality lowered). The cap is there so the card and the repository
# do not grow without anyone deciding it, not to squeeze the music.
CAP_BYTES = 40_000_000
FILE_CAP_BYTES = 7_000_000   # any one of them (Ice Cave, 292 s, is 6.6 MB)
MIN_SECONDS = 45             # menu music, not a jingle played over and over
PEAKS = 160                  # bars in the lab's picture of a track

# Licenses that let ZyperOS ship a track on its card image and in
# its public repository: no NC, no ND, no "personal use". The name is how
# THIRD-PARTY-LICENSES.md writes it; the text is res/bgm/LICENSE-<id>.txt.
LICENSES = {
    "CC0-1.0": ("CC0 1.0", "https://creativecommons.org/publicdomain/zero/1.0/"),
    "CC-BY-3.0": ("CC BY 3.0", "https://creativecommons.org/licenses/by/3.0/"),
    "CC-BY-4.0": ("CC BY 4.0", "https://creativecommons.org/licenses/by/4.0/"),
}


def die(msg):
    raise SystemExit("bgm-tracks: " + msg)


def load(path):
    with open(path) as f:
        return json.load(f)


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def run(args, **kw):
    return subprocess.run(args, check=True, **kw)


# ---- the Ogg Vorbis file, read with the standard library ------------------------


def ogg_packets(data, want):
    """The first `want` packets of the first logical stream, and the last
    page's granule position (the stream's length in samples)."""
    packets, cur, pos, granule = [], b"", 0, 0
    while pos + 27 <= len(data):
        if data[pos:pos + 4] != b"OggS":
            raise ValueError("not an Ogg page at byte %d" % pos)
        nseg = data[pos + 26]
        lacing = data[pos + 27:pos + 27 + nseg]
        body = pos + 27 + nseg
        g = struct.unpack_from("<q", data, pos + 6)[0]
        if g >= 0:
            granule = g
        for n in lacing:
            if len(packets) < want:
                cur += data[body:body + n]
                if n < 255:
                    packets.append(cur)
                    cur = b""
            body += n
        pos = body
    return packets, granule


def vorbis_info(path):
    """(channels, rate, seconds, {COMMENT: value})"""
    with open(path, "rb") as f:
        data = f.read()
    (ident, comment), granule = ogg_packets(data, 2)
    if ident[:7] != b"\x01vorbis" or comment[:7] != b"\x03vorbis":
        raise ValueError("not Vorbis")
    channels = ident[11]
    rate = struct.unpack_from("<I", ident, 12)[0]
    p = 7
    vlen = struct.unpack_from("<I", comment, p)[0]
    p += 4 + vlen
    n = struct.unpack_from("<I", comment, p)[0]
    p += 4
    tags = {}
    for _ in range(n):
        ln = struct.unpack_from("<I", comment, p)[0]
        k, _, v = comment[p + 4:p + 4 + ln].decode("utf-8").partition("=")
        tags[k.upper()] = v
        p += 4 + ln
    return channels, rate, granule / rate, tags


# ---- the MP3 file, read with the standard library -----------------------------------


def syncsafe(b):
    return b[0] << 21 | b[1] << 14 | b[2] << 7 | b[3]


def id3_text(body):
    """An ID3v2 text field's bytes after its encoding byte, as a string"""
    enc, raw = body[0], body[1:]
    codec = {0: "latin-1", 1: "utf-16", 2: "utf-16-be", 3: "utf-8"}.get(enc, "latin-1")
    return raw.decode(codec).rstrip("\0")


def id3v2(data):
    """({TITLE, ARTIST, and TXXX descriptions: value}, the tag's length)"""
    if data[:3] != b"ID3":
        return {}, 0
    ver, flags = data[3], data[5]
    end = 10 + syncsafe(data[6:10]) + (10 if flags & 0x10 else 0)
    tags, pos = {}, 10
    while pos + 10 <= end:
        fid = data[pos:pos + 4]
        size = syncsafe(data[pos + 4:pos + 8]) if ver >= 4 else struct.unpack_from(">I", data, pos + 4)[0]
        if fid == b"\0\0\0\0" or size <= 0:
            break
        body = data[pos + 10:pos + 10 + size]
        if fid in (b"TIT2", b"TPE1"):
            tags[{b"TIT2": "TITLE", b"TPE1": "ARTIST"}[fid]] = id3_text(body)
        elif fid == b"TXXX":
            text = id3_text(body)
            desc, _, value = text.partition("\0")
            tags[desc.upper()] = value.lstrip("\ufeff")
        pos += 10 + size
    return tags, end


def mp3_info(path):
    """(channels, rate, seconds, {TITLE, ARTIST, LICENSE...}): the seconds
    from LAME's note in the first frame, its frames less the encoder's delay
    and padding, the length src/bgm_dec.c plays"""
    with open(path, "rb") as f:
        data = f.read()
    tags, pos = id3v2(data)
    h = data[pos:pos + 4]
    if len(h) < 4 or h[0] != 0xFF or (h[1] & 0xE0) != 0xE0:
        raise ValueError("no MP3 frame after the tag")
    mpeg1 = bool(h[1] & 0x08)
    if (h[1] >> 1) & 3 != 1:
        raise ValueError("not layer III")
    rates = {3: (44100, 48000, 32000), 2: (22050, 24000, 16000), 0: (11025, 12000, 8000)}[(h[1] >> 3) & 3]
    rate = rates[(h[2] >> 2) & 3]
    mono = (h[3] >> 6) == 3
    at = pos + 4 + (0 if h[1] & 1 else 2) + ((17 if mono else 32) if mpeg1 else (9 if mono else 17))
    x = data[at:at + 4]
    if x not in (b"Xing", b"Info"):
        raise ValueError("no Xing/Info note in the first frame")
    flags = struct.unpack_from(">I", data, at + 4)[0]
    p = at + 8
    frames = 0
    if flags & 1:
        frames = struct.unpack_from(">I", data, p)[0]
        p += 4
    p += (4 if flags & 2 else 0) + (100 if flags & 4 else 0) + (4 if flags & 8 else 0)
    if data[p:p + 4] != b"LAME":
        raise ValueError("no LAME extension, so no delay and padding to play it gapless")
    delay = data[p + 21] << 4 | data[p + 22] >> 4
    padding = (data[p + 22] & 0x0F) << 8 | data[p + 23]
    samples = frames * (1152 if mpeg1 else 576) - delay - padding
    return 1 if mono else 2, rate, samples / rate, tags


# ---- the check ---------------------------------------------------------------------


def check():
    fails = []

    def ck(cond, what):
        if not cond:
            fails.append(what)

    if not os.path.isfile(MANIFEST):
        print("  FAIL res/bgm/tracks.json is missing\nbgm-tracks: 1 FAILED")
        return 1
    m = load(MANIFEST)
    tracks = m["tracks"]
    third = open(os.path.join(ROOT, "THIRD-PARTY-LICENSES.md")).read()
    readme = open(os.path.join(ROOT, "README.md")).read()
    payload = open(os.path.join(ROOT, "mk", "payload.sh")).read()

    on_disk = sorted(f for f in os.listdir(BGM) if not f.startswith("."))
    audio = [f for f in on_disk if re.search(r"\.(ogg|mp3|wav|flac)$", f, re.I)]
    named = [t.get("file", "") for t in tracks]
    ck(len(tracks) > 0, "res/bgm/tracks.json names no track")
    ck(sorted(audio) == sorted(named),
       "res/bgm holds %s but tracks.json names %s" % (audio, sorted(named)))
    ck(len(set(named)) == len(named), "a file is named twice")
    total = 0
    for t in tracks:
        f = t.get("file", "")
        who = f or t.get("id", "?")
        for k in ("id", "title", "author", "license", "page"):
            ck(bool(t.get(k)), "%s: no %s" % (who, k))
        src = t.get("source", {})
        ck(bool(src.get("url")) and re.fullmatch(r"[0-9a-f]{64}", src.get("sha256", "")) is not None,
           "%s: no source file and SHA-256 it was chosen by" % who)
        lic = t.get("license")
        ck(lic in LICENSES, "%s: license %r is not one a ZyperOS card may ship" % (who, lic))
        ck(f.endswith(".mp3") and os.path.splitext(f)[0] == t.get("title"),
           "%s: the file is not the title + .mp3 (the Track row shows the name)" % who)
        path = os.path.join(BGM, f)
        if not os.path.isfile(path):
            ck(False, "%s: missing" % who)
            continue
        size = os.path.getsize(path)
        total += size
        made = t.get("mp3", {})
        ck(size <= FILE_CAP_BYTES, "%s: %d bytes, over the %d a track may take" % (who, size, FILE_CAP_BYTES))
        ck(sha256(path) == made.get("sha256"), "%s: not the file tracks.json records (encode again)" % who)
        try:
            ch, rate, secs, tags = mp3_info(path)
        except (ValueError, struct.error, IndexError, KeyError, UnicodeDecodeError) as e:
            ck(False, "%s: not an MP3 the player plays gapless: %s" % (who, e))
            continue
        ck(ch == 2 and rate == RATE, "%s: %d ch at %d Hz, not stereo at %d" % (who, ch, rate, RATE))
        ck(secs >= MIN_SECONDS, "%s: %.0f s long, under %d" % (who, secs, MIN_SECONDS))
        ck(abs(secs - made.get("seconds", -1)) < 0.5, "%s: %.1f s, tracks.json says %s" % (who, secs, made.get("seconds")))
        ck(tags.get("TITLE") == t.get("title") and tags.get("ARTIST") == t.get("author")
           and tags.get("LICENSE") == lic, "%s: its ID3 tag does not name its title, author and license" % who)
        # Credited where the card and the repository say who made what
        name, url = LICENSES.get(lic, ("?", "?"))
        entry = next((ln for ln in third.split("\n- ") if t["page"] in ln), "")
        ck(bool(entry), "%s: THIRD-PARTY-LICENSES.md does not link %s" % (who, t["page"]))
        for word in (t["title"], t["author"], name):
            ck(word in " ".join(entry.split()), "%s: THIRD-PARTY-LICENSES.md's entry lacks %r" % (who, word))
        ck(t["title"] in readme and t["author"] in readme, "%s: not in the README's credits" % who)
        ck(os.path.isfile(os.path.join(BGM, "LICENSE-%s.txt" % lic)), "%s: no res/bgm/LICENSE-%s.txt" % (who, lic))
    ck(total <= CAP_BYTES, "the tracks take %d bytes, over the %d cap" % (total, CAP_BYTES))
    ck('cp "$ROOT/res/bgm/"*.mp3 "$P/res/bgm/"' in payload, "mk/payload.sh does not put res/bgm on the card")
    ck('"$ROOT"/res/bgm/LICENSE-*.txt' in payload, "mk/payload.sh does not put the tracks' licenses in Licenses/")
    for f in fails:
        print("  FAIL", f)
    if fails:
        print("bgm-tracks: %d FAILED" % len(fails))
        return 1
    print("bgm-tracks: ok, %d tracks, %.1f of %.1f MB" % (len(tracks), total / 1e6, CAP_BYTES / 1e6))
    return 0


# ---- making the audio ----------------------------------------------------------------


def fetch(tracks, cache):
    for t in tracks:
        src = t["source"]
        d = os.path.join(cache, t["id"])
        os.makedirs(d, exist_ok=True)
        path = os.path.join(d, urllib.request.unquote(src["url"].rsplit("/", 1)[1]))
        if not os.path.isfile(path) or sha256(path) != src["sha256"]:
            print("fetch", src["url"])
            req = urllib.request.Request(src["url"], headers={"User-Agent": "ZyperOS bgm-tracks"})
            with urllib.request.urlopen(req, timeout=600) as r, open(path + ".part", "wb") as f:
                shutil.copyfileobj(r, f)
            os.replace(path + ".part", path)
        got = sha256(path)
        if got != src["sha256"]:
            die("%s is %s, not the %s it was chosen by" % (path, got, src["sha256"]))
    print("fetch: %d sources, every one the file recorded" % len(tracks))


def source_path(t, cache, tmp):
    src = t["source"]
    path = os.path.join(cache, t["id"], urllib.request.unquote(src["url"].rsplit("/", 1)[1]))
    if not os.path.isfile(path) or sha256(path) != src["sha256"]:
        die("%s: fetch it first (tools/bgm-tracks.py fetch)" % t["id"])
    if src.get("member"):
        # Only the one file read out of the archive, to a name of our own
        out = os.path.join(tmp, t["id"] + "-member" + os.path.splitext(src["member"])[1])
        with zipfile.ZipFile(path) as z, open(out, "wb") as f:
            f.write(z.read(src["member"]))
        path = out
    if src.get("render") == "openmpt":
        # --render writes beside the module, as its name + .wav
        run(["openmpt123", "--quiet", "--render", "--force", "--samplerate", str(RATE),
             "--channels", "2", path], stdout=subprocess.DEVNULL)
        path += ".wav"
    return path


def loudness(path):
    """(integrated LUFS, true peak dBTP)"""
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", path, "-af", "ebur128=peak=true",
                        "-f", "null", "-"], capture_output=True, text=True, check=True).stderr
    i = float(re.findall(r"I:\s+(-?[\d.]+) LUFS", r)[-1])
    tp = float(re.findall(r"Peak:\s+(-?[\d.]+) dBFS", r)[-1])
    return i, tp


def master(t, cache, tmp):
    """The track at 48 kHz stereo, trimmed and at LUFS: (WAV path, facts)"""
    src = source_path(t, cache, tmp)
    trimmed = os.path.join(tmp, t["id"] + "-48k.wav")
    trim = ("silenceremove=start_periods=1:start_threshold=-70dB,areverse,"
            "silenceremove=start_periods=1:start_threshold=-70dB,areverse")
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", src, "-vn", "-ac", "2",
         "-ar", str(RATE), "-af", trim, "-c:a", "pcm_f32le", trimmed])
    i, tp = loudness(trimmed)
    gain = LUFS - i
    limited = tp + gain > CEILING_DBTP
    chain = "volume=%.2fdB" % gain
    if limited:
        # Sample peaks held a dB under the ceiling, so the true peak
        # between samples stays under it too
        chain += ",alimiter=limit=%.4f:level=disabled:attack=2:release=60" % (10 ** ((CEILING_DBTP - 1.0) / 20))
    out = os.path.join(tmp, t["id"] + "-master.wav")
    run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", trimmed, "-af", chain,
         "-map_metadata", "-1", "-fflags", "+bitexact", "-c:a", "pcm_s24le", out])
    i2, tp2 = loudness(out)
    secs = float(subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", out],
                                capture_output=True, text=True, check=True).stdout)
    return out, {"gain_db": round(gain, 2), "limited": limited, "lufs": round(i2, 1),
                 "true_peak": round(tp2, 1), "seconds": round(secs, 2)}


def vorbis(t, wav, out):
    # The stream's serial number from the track, not oggenc's random one, so
    # the same source makes the same file, byte for byte, every time
    serial = int(hashlib.sha256(t["id"].encode()).hexdigest()[:8], 16) & 0x7FFFFFFF
    run(["oggenc", "--quiet", "-q", str(QUALITY), "-s", str(serial), "-t", t["title"], "-a", t["author"],
         "-c", "LICENSE=" + t["license"], "-c", "CONTACT=" + t["page"],
         "-c", "COMMENT=ZyperOS menu music, from " + t["source"]["url"], "-o", out, wav])


def mp3(t, wav, out):
    """MP3 for the card, its title, author, license and page in an ID3v2 tag.
    The same WAV makes the same file, byte for byte, every time."""
    run(["lame", "--quiet", "-V", str(VBR), "--resample", str(RATE // 1000), "--id3v2-only",
         "--tt", t["title"], "--ta", t["author"], "--tv", "TXXX=LICENSE=" + t["license"],
         "--tv", "TXXX=CONTACT=" + t["page"], "--tc", "ZyperOS menu music, from " + t["source"]["url"],
         wav, out])


def peaks(wav):
    raw = subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-i", wav, "-ac", "1", "-ar", "4000",
                          "-f", "s16le", "-"], capture_output=True, check=True).stdout
    a = array.array("h")
    a.frombytes(raw[:len(raw) // 2 * 2])
    if sys.byteorder == "big":
        a.byteswap()
    n = len(a) // PEAKS or 1
    vals = [max((abs(x) for x in a[k * n:(k + 1) * n]), default=0) / 32768 for k in range(PEAKS)]
    top = max(vals) or 1
    return [round(v / top, 3) for v in vals]


def encode(cache):
    m = load(MANIFEST)
    with tempfile.TemporaryDirectory() as tmp:
        for t in m["tracks"]:
            wav, facts = master(t, cache, tmp)
            old = os.path.join(BGM, t.get("file", ""))
            t["file"] = t["title"] + ".mp3"
            out = os.path.join(BGM, t["file"])
            if old != out and os.path.isfile(old):
                os.remove(old)
            mp3(t, wav, out)
            facts.update(sha256=sha256(out), bytes=os.path.getsize(out))
            t.pop("ogg", None)
            t["mp3"] = facts
            print("encode %-28s %5.1f s  %7d bytes  gain %+.1f dB%s" % (
                t["file"], facts["seconds"], facts["bytes"], facts["gain_db"], ", limited" if facts["limited"] else ""))
    m.pop("quality", None)
    m.update(lufs=LUFS, ceiling_dbtp=CEILING_DBTP, vbr=VBR, rate=RATE)
    with open(MANIFEST, "w") as f:
        json.dump(m, f, indent=2, ensure_ascii=False)
        f.write("\n")


def lab(listfile, cache, out):
    m = load(listfile)
    os.makedirs(os.path.join(out, "audio"), exist_ok=True)
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for t in m["tracks"]:
            wav, facts = master(t, cache, tmp)
            mp3 = os.path.join(out, "audio", t["id"] + ".mp3")
            run(["lame", "--quiet", "-V", "2", "--tt", t["title"], "--ta", t["author"], wav, mp3])
            ogg = os.path.join(tmp, t["id"] + ".ogg")
            vorbis(t, wav, ogg)
            row = {k: t[k] for k in ("id", "title", "author", "kind", "style", "license", "page")}
            for k in ("work", "notice", "also"):
                if t.get(k):
                    row[k] = t[k]
            row.update(facts, audio="audio/%s.mp3" % t["id"], ogg_bytes=os.path.getsize(ogg),
                       source_bytes=t["source"]["bytes"], peaks=peaks(wav))
            rows.append(row)
            print("lab %-20s %5.1f s  ogg %7d  gain %+.1f%s" % (
                t["id"], facts["seconds"], row["ogg_bytes"], facts["gain_db"], " limited" if facts["limited"] else ""))
    data = {"lufs": LUFS, "quality": QUALITY, "rate": RATE, "cap_bytes": CAP_BYTES, "tracks": rows,
            "licenses": {k: {"name": v[0], "url": v[1]} for k, v in LICENSES.items()}}
    with open(os.path.join(out, "menu-music.js"), "w") as f:
        f.write("/* The menu music lab's tracks: written by tools/bgm-tracks.py lab, never by hand. */\n")
        f.write("window.ZK_MUSICLAB = " + json.dumps(data, ensure_ascii=False, separators=(",", ":")) + ";\n")


def main(argv):
    if argv[1:] == ["--check"]:
        return check()
    if len(argv) == 4 and argv[1] == "fetch":
        fetch(load(argv[2])["tracks"], argv[3])
    elif len(argv) == 3 and argv[1] == "encode":
        encode(argv[2])
    elif len(argv) == 5 and argv[1] == "lab":
        lab(argv[2], argv[3], argv[4])
    else:
        raise SystemExit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
