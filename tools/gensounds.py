#!/usr/bin/env python3
"""Synthesize the interface sounds (DESIGN.md 9) as WAV files.

    tools/gensounds.py              write res/sounds/<voice>/<sound>.wav
                                    (make sounds)
    tools/gensounds.py --check      say whether they are what would be written
                                    (check-sounds)
    tools/gensounds.py --lab OUT.js write every voice as data: URIs, for the
                                    sound lab (docs/design/mock/sounds.js)

Every sound is synthesized here from its tones in tools/designdef.py's
VOICES, and nothing is recorded or downloaded. A tone is (wave, Hz, ms, peak,
start ms): a sine, a triangle or a square, band-limited (a sum of its
harmonics up to 20 kHz, so a square at 330 Hz does not alias), at one pitch,
or gliding from the first of two pitches to the second; a chord is several
tones, each from its own start. Each rises in min(4 ms, a quarter of its
length) to its peak and falls exponentially from there to 1/10000 of full
scale at its end - the envelope the mock kit's ZK.sound gave the design lab.
A glide runs from the first pitch to the second in log frequency over the
first third of the tone, with a quadratic ease-out, and holds the second
after: a tone is loudest at its start, so that is where its pitch has to move
to be heard moving, as a struck bar or a drop settles on its note.

The files are what the launcher plays (src/uisound.c): 48 kHz, the rate the
system's mixed output runs at, so nothing resamples them; 16-bit; one channel,
which the launcher sends to both. A sound is as long as its last tone.

--check renders every sound again and compares it with the file sample for
sample, allowing one step either way, since libm's sin need not round alike
on every machine.
"""
import base64
import io
import json
import math
import os
import struct
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import designdef  # noqa: E402

RATE = 48000
OUT = os.path.join(ROOT, "res", "sounds")
TOP_HZ = 20000.0
FLOOR = 1e-4


def harmonics(shape, hz):
    """(multiple, amplitude) for a band-limited wave whose highest pitch is hz."""
    if shape == "sine":
        return [(1, 1.0)]
    out, k = [], 1
    while k * hz < TOP_HZ:
        if shape == "triangle":
            out.append((k, 8 / math.pi ** 2 * (-1) ** ((k - 1) // 2) / (k * k)))
        elif shape == "square":
            out.append((k, 4 / math.pi / k))
        else:
            raise SystemExit("unknown wave %r" % shape)
        k += 2
    return out


def render(tones, rate=RATE):
    """The sound's samples, as floats of full scale."""
    end = max(at + ms for _, _, ms, _, at in tones)
    out = [0.0] * int(round(end * rate / 1000.0))
    for shape, hz, ms, peak, at in tones:
        f0, f1 = (float(hz), float(hz)) if isinstance(hz, (int, float)) else map(float, hz)
        first = int(round(at * rate / 1000.0))
        n = int(round(ms * rate / 1000.0))
        rise = min(4.0, ms / 4.0) * rate / 1000.0
        parts = harmonics(shape, max(f0, f1))
        phase = [0.0] * len(parts)
        for i in range(n):
            u = min(1.0, 3.0 * i / n)
            f = f0 * (f1 / f0) ** (1 - (1 - u) ** 2)
            if i < rise:
                env = peak * i / rise
            else:
                env = peak * (FLOOR / peak) ** ((i - rise) / (n - rise))
            s = 0.0
            for j, (k, a) in enumerate(parts):
                s += a * math.sin(phase[j])
                phase[j] += 2 * math.pi * k * f / rate
            if first + i < len(out):
                out[first + i] += env * s
    return out


def pcm(samples):
    return [max(-32768, min(32767, int(round(s * 32767)))) for s in samples]


def wav_bytes(samples):
    buf = io.BytesIO()
    w = wave.open(buf, "wb")
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(struct.pack("<%dh" % len(samples), *samples))
    w.close()
    return buf.getvalue()


def read_wav(path):
    w = wave.open(path, "rb")
    try:
        if (w.getnchannels(), w.getsampwidth(), w.getframerate()) != (1, 2, RATE):
            return None
        raw = w.readframes(w.getnframes())
    finally:
        w.close()
    return list(struct.unpack("<%dh" % (len(raw) // 2), raw))


def files():
    """(path under res/sounds, tones) for every sound of every voice."""
    for vid, _, tones in designdef.VOICES:
        for name, _ in designdef.SOUNDS:
            yield os.path.join(vid, name + ".wav"), tones[name]


def write():
    for rel, tones in files():
        path = os.path.join(OUT, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        data = wav_bytes(pcm(render(tones)))
        with open(path, "wb") as f:
            f.write(data)
        print("wrote", os.path.relpath(path, ROOT), len(data), "bytes")


def check():
    bad, want_files = [], set()
    for rel, tones in files():
        want_files.add(rel)
        path = os.path.join(OUT, rel)
        want = pcm(render(tones))
        have = read_wav(path) if os.path.exists(path) else None
        if have is None:
            bad.append("%s: missing, or not 48 kHz 16-bit mono" % rel)
        elif len(have) != len(want):
            bad.append("%s: %d samples, want %d" % (rel, len(have), len(want)))
        elif any(abs(a - b) > 1 for a, b in zip(have, want)):
            bad.append("%s: not what tools/designdef.py's tones make" % rel)
    # A dotfile is the host's (a .DS_Store a Finder window leaves), not a
    # sound: mk/payload.sh ships only the WAVs, and this looks past it too.
    have_files = set()
    for d, ds, fs in os.walk(OUT):
        ds[:] = [x for x in ds if not x.startswith(".")]
        have_files |= {os.path.relpath(os.path.join(d, f), OUT) for f in fs if not f.startswith(".")}
    bad += ["%s: no such sound in tools/designdef.py" % e for e in sorted(have_files - want_files)]
    if bad:
        for b in bad:
            print("check-sounds: res/sounds/" + b, file=sys.stderr)
        print("check-sounds: run `make sounds`", file=sys.stderr)
        return 1
    print("check-sounds: %d sounds in %d voices match tools/designdef.py"
          % (len(want_files), len(designdef.VOICES)))
    return 0


def lab(path):
    voices, tones = {}, {}
    for vid, _, ts in designdef.VOICES:
        voices[vid] = {}
        tones[vid] = {}
        for name, _ in designdef.SOUNDS:
            data = wav_bytes(pcm(render(ts[name])))
            voices[vid][name] = "data:audio/wav;base64," + base64.b64encode(data).decode()
            tones[vid][name] = [[w, list(hz) if isinstance(hz, tuple) else hz, ms, pk, at]
                                for w, hz, ms, pk, at in ts[name]]
    with open(path, "w") as f:
        f.write("/* Generated by tools/gensounds.py --lab: every voice's sounds, as WAVs. */\n")
        f.write("window.ZK_SOUNDLAB = %s;\n" % json.dumps({"rate": RATE, "voices": voices, "tones": tones}))
    print("wrote", os.path.relpath(path, ROOT), os.path.getsize(path), "bytes")


def main(argv):
    if argv == ["--check"]:
        return check()
    if len(argv) == 2 and argv[0] == "--lab":
        lab(argv[1])
        return 0
    if argv:
        raise SystemExit(__doc__)
    write()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
