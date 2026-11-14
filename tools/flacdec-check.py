#!/usr/bin/env python3
"""Does src/flacdec.c give back, to the byte, the PCM the flac encoder was given?

    python3 tools/flacdec-check.py

A CHD disc image codes its CD audio as bare FLAC frames and src/flacdec.c is
what reads them. A decoder that is wrong about one coding does not fail on
every disc: it fails on the frames where the encoder happened to pick that
coding, and plays a track with a click in it. So the PCM is made here, the
real encoder (flac, from Homebrew) codes it under every setting that changes
what the frames hold, and each decode has to match the input exactly.

The frame headers are then rewritten in the ways flac never writes them but
the format allows (variable block size, 7-byte sample numbers, sizes left to
STREAMINFO), and broken in the ways a damaged file breaks them: cut short,
one byte flipped, garbage. Those run under AddressSanitizer where the
compiler has it, because the decoder's promise there is not an answer but
never touching a byte outside its buffers.
"""
import array
import concurrent.futures
import math
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = [os.path.join(ROOT, "tools", "flacdec-check.c"),
       os.path.join(ROOT, "src", "flacdec.c")]
JOBS = 4


# ------------------------------------------------------------------ build ---

def build(tmp):
    """The driver twice: as it ships (-O2, warnings as errors) and with
    AddressSanitizer, plus UndefinedBehaviorSanitizer where it builds too."""
    cc = os.environ.get("CC", "cc")
    plain = os.path.join(tmp, "flacdec-check")
    r = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
                        "-o", plain] + SRC, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        return None, None
    asan = os.path.join(tmp, "flacdec-check-asan")
    for san in ("address,undefined", "address"):
        if subprocess.run([cc, "-std=gnu11", "-O1", "-g", f"-fsanitize={san}",
                           "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
                           "-o", asan] + SRC, capture_output=True).returncode == 0:
            return plain, asan
    return plain, None


# ---------------------------------------------------------------- signals ---

def signals():
    """Stereo test signals, as lists of interleaved ints. Lengths are odd on
    purpose: none is a multiple of any block size, so every stream ends in a
    short frame, and that frame's size is coded at the end of its header."""
    rnd = random.Random(9639)

    def clamp(v):
        return max(-32768, min(32767, int(round(v))))

    def stereo(n, f):
        out = []
        for i in range(n):
            out.extend(f(i))
        return out

    tau = 2 * math.pi
    rails = ((32767, -32768), (-32768, 32767), (32767, 32767), (-32768, -32768))
    sig = {
        "silence": stereo(6000, lambda i: (0, 0)),
        "sine": stereo(12345, lambda i: (
            clamp(12000 * math.sin(tau * 440 * i / 44100)),
            clamp(9000 * math.sin(tau * 660 * i / 44100 + 1)))),
        "noise": stereo(7001, lambda i: (rnd.randint(-32768, 32767),
                                         rnd.randint(-32768, 32767))),
        # Every value a multiple of 8, so the encoder finds wasted bits
        "square": stereo(10007, lambda i: (
            3000 + (8000 if (i // 50) % 2 else -8000),
            -1024 + (4096 if (i // 73) % 2 else -4096))),
        # Full scale both ways: a side channel of 32767 - -32768 needs 17 bits
        "clip": stereo(1500, lambda i: rails[(i // 37) % 4] if (i // 150) % 2 == 0
                       else (clamp(40000 * math.sin(i * 0.07)),
                             clamp(-40000 * math.sin(i * 0.07)))),
        "hiss": stereo(9001, lambda i: (clamp(rnd.gauss(0, 2)), clamp(rnd.gauss(0, 40)))),
    }
    mix = []
    for i in range(17003):
        left = 6000 * math.sin(i * 0.013) + 3000 * math.sin(i * i * 1e-6) + rnd.gauss(0, 300)
        mix.extend((clamp(left), clamp(0.9 * left + rnd.gauss(0, 50))))
    sig["mix"] = mix
    return sig


def pcm(samples, endian):
    a = array.array("h", samples)
    if (endian == "be") != (sys.byteorder == "big"):
        a.byteswap()
    return a.tobytes()


# ------------------------------------------------------------------- FLAC ---

def strip(flac):
    """The frames of a .flac file: past "fLaC" and every metadata block, each
    a 4-byte header (last-block flag, type, 24-bit length) and its body."""
    if flac[:4] != b"fLaC":
        raise ValueError("no fLaC marker")
    i = 4
    while True:
        last, n = flac[i] & 0x80, int.from_bytes(flac[i + 1:i + 4], "big")
        i += 4 + n
        if last:
            return flac[i:]


def encode(data, endian, channels, rate, args):
    r = subprocess.run([FLAC, "--force-raw-format", "--sign=signed", "--bps=16",
                        "--endian=" + ("big" if endian == "be" else "little"),
                        f"--channels={channels}", f"--sample-rate={rate}",
                        "--silent", "--stdout"] + list(args) + ["-"],
                       input=data, capture_output=True)
    if r.returncode:
        raise RuntimeError(f"flac {' '.join(args)}: {r.stderr.decode(errors='replace')}")
    return strip(r.stdout)


def _table(poly, width):
    top, mask = 1 << (width - 1), (1 << width) - 1
    out = []
    for i in range(256):
        c = i << (width - 8)
        for _ in range(8):
            c = ((c << 1) ^ poly if c & top else c << 1) & mask
        out.append(c)
    return out


CRC8, CRC16 = _table(0x07, 8), _table(0x8005, 16)


def crc8(b):
    c = 0
    for x in b:
        c = CRC8[c ^ x]
    return c


def crc16(b):
    c = 0
    for x in b:
        c = ((c << 8) & 0xffff) ^ CRC16[(c >> 8) ^ x]
    return c


def header(f):
    """The fields of the frame header at the start of f, or None."""
    if len(f) < 6 or f[0] != 0xff or f[1] & 0xfe != 0xf8:
        return None
    bs, sr, c = f[2] >> 4, f[2] & 15, f[4]
    ones = 0
    while ones < 8 and c & (0x80 >> ones):
        ones += 1
    if ones == 1 or ones == 8:
        return None
    num_end = 5 + max(ones - 1, 0)
    bsx = {6: 1, 7: 2}.get(bs, 0)
    srx = {12: 1, 13: 2, 14: 2}.get(sr, 0)
    hlen = num_end + bsx + srx
    if len(f) <= hlen or crc8(f[:hlen]) != f[hlen]:
        return None
    ext = f[num_end:num_end + bsx]
    if bs == 6 or bs == 7:
        size = int.from_bytes(ext, "big") + 1
    else:
        size = {1: 192}.get(bs) or (576 << (bs - 2) if bs <= 5 else 256 << (bs - 8))
    return dict(var=f[1] & 1, bs=bs, sr=sr, ch=f[3] >> 4, ss=(f[3] >> 1) & 7,
                bsbytes=ext, srbytes=f[num_end + bsx:hlen], hlen=hlen, size=size)


def split(raw):
    """The frames of a stream, found the way a decoder finds them again after
    losing its place: a sync code with a good header CRC, where the CRC-16 of
    everything since the last frame began comes out 0 (as a CRC run on through
    its own value does)."""
    cands = {m.start() for m in re.finditer(b"\xff[\xf8\xf9]", raw)}
    out, start, c, n = [], 0, 0, len(raw)
    for i in range(n):
        c = ((c << 8) & 0xffff) ^ CRC16[(c >> 8) ^ raw[i]]
        j = i + 1
        if c == 0 and j - start > 8 and (j == n or (j in cands and header(raw[j:j + 24]))):
            out.append(raw[start:j])
            start = j
    if start != n or not out:
        raise ValueError("stream does not split into frames")
    return out


def utf8(v):
    """FLAC's frame and sample numbers: UTF-8's coding, stretched to 36 bits."""
    if v < 0x80:
        return bytes([v])
    for n, width in ((2, 11), (3, 16), (4, 21), (5, 26), (6, 31), (7, 36)):
        if v < 1 << width:
            tail = []
            for _ in range(n - 1):
                tail.append(0x80 | (v & 0x3f))
                v >>= 6
            return bytes([((0xff << (8 - n)) & 0xff) | v] + tail[::-1])
    raise ValueError("number too big")


def rewrite(raw, fn):
    """Every frame given a new header by fn(fields, frame index, first
    sample), which returns the fields changed; both CRCs are made again
    (crc8, when given, is xored into the header's)."""
    out, sample = bytearray(), 0
    for k, f in enumerate(split(raw)):
        h = header(f)
        n = dict(var=h["var"], bs=h["bs"], sr=h["sr"], ch=h["ch"], ss=h["ss"],
                 num=sample if h["var"] else k, srbytes=h["srbytes"], low=0, crc8=0)
        n.update(fn(h, k, sample))
        head = (bytes([0xff, 0xf8 | n["var"], n["bs"] << 4 | n["sr"],
                       n["ch"] << 4 | n["ss"] << 1 | n["low"]])
                + utf8(n["num"]) + h["bsbytes"] + n["srbytes"])
        frame = head + bytes([crc8(head) ^ n["crc8"]]) + f[h["hlen"] + 1:-2]
        out += frame + crc16(frame).to_bytes(2, "big")
        sample += h["size"]
    return bytes(out)


# ------------------------------------------------------- frames made here ---

# libFLAC never escapes a partition, and for a 16-bit stream it never uses
# 5-bit Rice parameters either, so those two codings, which RFC 9639 allows
# and another encoder may use, are written here, along with frames that are
# wrong in one way each but have good CRCs, so the decoder has to see the
# fault itself rather than be saved by the checksum.

class BitWriter:
    def __init__(self):
        self.parts, self.n = [], 0

    def put(self, v, width):
        if width:
            self.parts.append(format(v & ((1 << width) - 1), f"0{width}b"))
            self.n += width

    def unary(self, q):
        self.parts.append("0" * q + "1")
        self.n += q + 1

    def bytes(self, fill=0):
        pad = -self.n % 8
        if fill and not pad:
            raise ValueError("no padding bits to set")
        self.put(fill, pad)
        s = "".join(self.parts)
        return int(s, 2).to_bytes(len(s) // 8, "big") if s else b""


FIXED = ([], [1], [2, -1], [3, -3, 1], [4, -6, 4, -1])


def width_of(values):
    """The fewest bits holding every value in two's complement; 0 for zeros."""
    lo, hi = min(values, default=0), max(values, default=0)
    if lo == hi == 0:
        return 0
    w = 1
    while lo < -(1 << (w - 1)) or hi >= 1 << (w - 1):
        w += 1
    return w


def rice_k(values, limit):
    if not values:
        return 0
    mean = sum(2 * v if v >= 0 else -2 * v - 1 for v in values) / len(values)
    k = 0
    while k < limit and 1 << (k + 1) <= mean:
        k += 1
    return k


def subframe(w, s, bps, sp):
    """Samples s at bps bits, coded as sp says: kind, order, wasted, and for
    the residual method, porder and parts, a list of (how, arg) used in turn
    for the partitions: ("rice", k), ("esc", width) or ("huge", k), where a
    None arg is worked out. pad, type, prec_code and shift_code override what
    is written, to make a wrong frame."""
    kind, order, wasted = sp["kind"], sp.get("order", 0), sp.get("wasted", 0)
    depth = bps - wasted
    s = [v >> wasted for v in s]
    code = {"constant": 0, "verbatim": 1, "fixed": 8 + order, "lpc": 31 + order}[kind]
    w.put(sp.get("pad", 0), 1)
    w.put(sp.get("type", code), 6)
    w.put(1 if wasted else 0, 1)
    if wasted:
        w.unary(wasted - 1)
    if depth <= 0:
        return
    if kind == "constant":
        w.put(s[0], depth)
        return
    if kind == "verbatim":
        for v in s:
            w.put(v, depth)
        return
    for v in s[:order]:
        w.put(v, depth)
    if kind == "fixed":
        coefs, shift = FIXED[order], 0
    else:
        coefs, shift, prec = sp["coefs"], sp["shift"], sp["prec"]
        w.put(sp.get("prec_code", prec - 1), 4)
        w.put(sp.get("shift_code", shift), 5)
        for c in coefs:
            w.put(c, prec)
    res = [s[i] - (sum(c * s[i - 1 - j] for j, c in enumerate(coefs)) >> shift)
           for i in range(order, len(s))]
    method, porder = sp.get("method", 0), sp.get("porder", 0)
    pbits = 5 if method else 4
    esc = (1 << pbits) - 1
    plan = sp.get("parts", [("rice", None)])
    w.put(method, 2)
    w.put(porder, 4)
    part, i = len(s) >> porder, 0
    for p in range(1 << porder):
        chunk = res[i:i + max(part - (order if p == 0 else 0), 0)]
        i += len(chunk)
        how, arg = plan[p % len(plan)]
        if how == "esc":
            width = width_of(chunk) if arg is None else arg
            w.put(esc, pbits)
            w.put(width, 5)
            for r in chunk:
                w.put(r, width)
            continue
        k = rice_k(chunk, esc - 1) if arg is None else arg
        w.put(k, pbits)
        for j, r in enumerate(chunk):
            u = 2 * r if r >= 0 else -2 * r - 1
            w.unary(((1 << 32) >> k) if how == "huge" and j == 0 else u >> k)
            w.put(u, k)


def frame(chans, assign, subs, num=0, size=None, numbytes=None, fill=0):
    """One variable-block-size frame. chans are the channels as they sound
    (left and right, for a stereo pair); assign is the channel code and subs
    one subframe spec for each channel as coded."""
    n = len(chans[0])
    head = (bytes([0xff, 0xf9, 7 << 4 | 9, assign << 4 | 4 << 1])
            + (numbytes or utf8(num)) + ((size or n) - 1).to_bytes(2, "big"))
    head += bytes([crc8(head)])
    if assign < 8:
        coded, bps = chans, [16] * len(chans)
    else:
        left, right = chans
        side = [a - b for a, b in zip(left, right)]
        mid = [(a + b) >> 1 for a, b in zip(left, right)]
        coded, bps = {8: ([left, side], [16, 17]), 9: ([side, right], [17, 16]),
                      10: ([mid, side], [16, 17])}[assign]
    w = BitWriter()
    for c, b, sp in zip(coded, bps, subs):
        subframe(w, c, b, sp)
    body = head + w.bytes(fill)
    return chans, body + crc16(body).to_bytes(2, "big")


def made_here():
    """(name, frames, good) for streams of frames written by frame()."""
    rnd = random.Random(1952)

    def walk(n, step=200, amp=20000, mul=1):
        v, out = 0, []
        for _ in range(n):
            v = max(-amp, min(amp, v + rnd.randint(-step, step)))
            out.append(v * mul)
        return out

    def noise(n, amp=32767):
        return [rnd.randint(-amp - 1, amp) for _ in range(n)]

    def lpc(order, prec, shift, span=None, **kw):
        """Random coefficients: they need not predict well, only be
        undone exactly. A small span keeps an unshifted residual in 32 bits."""
        top = span or 1 << (prec - 1)
        return dict(kind="lpc", order=order, prec=prec, shift=shift,
                    coefs=[rnd.randint(-top, top - 1) for _ in range(order)], **kw)

    fixed = lambda order, **kw: dict(kind="fixed", order=order, **kw)
    out = []

    m = (walk(512) + [0] * 512 + noise(512) + walk(512, 3, 50) + noise(512, 100)
         + [0] * 512 + walk(1024))
    out.append(("escaped partitions, 4-bit parameters", [
        frame([m], 0, [fixed(0, porder=3, parts=[
            ("rice", None), ("esc", 0), ("esc", None), ("rice", None),
            ("esc", 31), ("esc", 0), ("rice", 14), ("esc", None)])]),
        frame([walk(4096)], 0, [fixed(4, porder=2, parts=[("esc", None), ("rice", None)])])],
        True))
    out.append(("5-bit parameters", [
        frame([noise(4096), walk(4096)], 1, [
            fixed(0, method=1, porder=2),
            fixed(2, method=1, porder=3, parts=[("rice", 20), ("rice", None),
                                                ("esc", None), ("rice", 30)])]),
        frame([walk(1000, 4), walk(1000)], 1, [
            fixed(1, method=1, parts=[("rice", 0)]),
            fixed(3, method=1, porder=3, parts=[("esc", None)])])], True))
    l, r = noise(2048), noise(2048)
    out.append(("stereo pairs, escaped side channels", [
        frame([l, r], 10, [fixed(1), fixed(0, method=1, porder=1, parts=[("esc", None)])]),
        frame([l, r], 8, [fixed(0), dict(kind="verbatim")]),
        frame([l, r], 9, [fixed(2, method=1, porder=4, parts=[("esc", None), ("rice", None)]),
                          fixed(0)])], True))
    r = walk(2000, amp=3000, mul=8)
    out.append(("wasted bits", [
        frame([walk(2000, amp=8000, mul=4), walk(2000, amp=8000, mul=4)], 8, [
            fixed(2, wasted=2), dict(kind="verbatim", wasted=2)]),
        frame([[v + 1024 for v in r], r], 9, [
            dict(kind="constant", wasted=10), fixed(3, wasted=3)]),
        frame([[-4096] * 300, [12288] * 300], 1, [dict(kind="constant", wasted=12)] * 2)],
        True))
    out.append(("LPC orders, precisions and shifts", [
        frame([walk(4096, 50)], 0, [lpc(32, 15, 15, method=1, porder=4,
                                         parts=[("rice", None), ("esc", None)])]),
        frame([walk(4096, 50)], 0, [lpc(32, 15, 0, span=3, method=1, porder=2)]),
        frame([walk(1024)], 0, [dict(kind="lpc", order=1, prec=1, shift=0, coefs=[-1],
                                     method=1)]),
        frame([walk(4096)], 0, [lpc(12, 12, 9, porder=5)])], True))
    sizes = []
    for chans, assign, subs in (([walk(1), walk(1)], 1, [dict(kind="verbatim")] * 2),
                                ([walk(2), walk(2)], 10, [fixed(1), fixed(1)]),
                                ([walk(16), walk(16)], 8, [fixed(4, porder=2)] * 2),
                                ([walk(65535), walk(65535)], 9, [fixed(1), fixed(2)]),
                                ([[-77] * 3, walk(3)], 1,
                                 [dict(kind="constant"), dict(kind="verbatim")])):
        sizes.append(frame(chans, assign, subs, num=sum(len(c[0]) for c, _ in sizes)))
    out.append(("block sizes 1, 2, 16, 65535, 3", sizes, True))
    out.append(("eight channels", [
        frame([walk(1152) for _ in range(8)], 7, [fixed(k % 5) for k in range(8)])], True))

    # One fault each, behind a good frame with as many channels
    ok = {1: frame([walk(256)], 0, [fixed(2)]),
          2: frame([walk(256), walk(256)], 1, [fixed(2)] * 2)}
    w = walk(256)

    def bad(name, *frames):
        out.append((name, [ok[len(frames[0][0])]] + list(frames), False))

    bad("a negative LPC shift", frame([w], 0, [lpc(2, 4, 0, shift_code=-1)]))
    bad("16-bit LPC coefficients", frame([w], 0, [lpc(2, 4, 0, prec_code=15)]))
    for t in (2, 7, 13, 31):
        bad(f"reserved subframe type {t}", frame([w], 0, [dict(kind="verbatim", type=t)]))
    for method in (2, 3):
        bad(f"residual method {method}", frame([w], 0, [fixed(1, method=method)]))
    bad("partitions that do not split the block", frame([walk(4095)], 0, [fixed(1, porder=1)]))
    bad("a first partition shorter than the order", frame([walk(16)], 0, [fixed(4, porder=3)]))
    bad("a FIXED warm-up longer than the block", frame([walk(3)], 0, [fixed(4)]))
    bad("an LPC warm-up longer than the block", frame([walk(16)], 0, [lpc(32, 4, 0)]))
    bad("as many wasted bits as the depth", frame([w], 0, [dict(kind="constant", wasted=16)]))
    bad("a subframe padding bit", frame([w], 0, [fixed(1, pad=1)]))
    for n in range(100, 108):
        try:
            bad("frame padding that is not zero", frame([walk(n)], 0, [fixed(1)], fill=1))
            break
        except ValueError:
            continue
    bad("a sample past 16 bits", frame([[32767, 40000, 40001]], 0, [fixed(1)]))
    bad("a side channel that makes right 65535",
        frame([[32767] * 4, [65535] * 4], 8, [dict(kind="verbatim")] * 2))
    bad("block size 65536", frame([[0] * 65536], 0, [dict(kind="constant")]))
    bad("a Rice code past 32 bits", frame([w], 0, [fixed(1, method=1, parts=[("huge", 30)])]))
    bad("a frame number with a broken second byte",
        frame([w], 0, [fixed(1)], numbytes=bytes([0xc2, 0x41])))
    bad("a frame number starting 0x80", frame([w], 0, [fixed(1)], numbytes=bytes([0x80])))
    bad("a frame number starting 0xff", frame([w], 0, [fixed(1)], numbytes=bytes([0xff] + [0x80] * 7)))
    return out


# ------------------------------------------------------------------- cases ---

SANITIZER = re.compile(r"AddressSanitizer|UndefinedBehaviorSanitizer|runtime error")


def decode(binary, raw, frames, channels, endian, path):
    with open(path, "wb") as f:
        f.write(raw)
    r = subprocess.run([binary, path, str(frames), str(channels), endian],
                       capture_output=True)
    os.unlink(path)
    if SANITIZER.search(r.stderr.decode(errors="replace")) or r.returncode not in (0, 1):
        raise RuntimeError(f"crashed (exit {r.returncode}):\n"
                           + r.stderr.decode(errors="replace")[-1500:])
    return r.returncode, r.stdout


class Case:
    """One stream and one decode of it. want is the PCM it must give back,
    or None when the decoder must say false."""

    def __init__(self, name, make, frames, channels, endian, want, binary=None):
        self.name, self.make, self.frames = name, make, frames
        self.channels, self.endian, self.want, self.binary = channels, endian, want, binary

    def run(self, plain, path):
        raw = self.make()
        code, out = decode(self.binary or plain, raw, self.frames, self.channels,
                           self.endian, path)
        if self.want is None:
            return None if code == 1 else "decoded, though it must not"
        if code != 0:
            return "flac_decode_s16 said false"
        if out != self.want:
            at = next((i for i in range(min(len(out), len(self.want)))
                       if out[i] != self.want[i]), min(len(out), len(self.want)))
            return f"differs from byte {at} of {len(self.want)}"
        return None


def stream(sig, endian, channels, args, rate=44100):
    return lambda: encode(pcm(sig, endian), endian, channels, rate, args)


def groups(sig, asan):
    mono = {k: v[0::2] for k, v in sig.items()}
    out = {}

    def ok(sigs, endian, channels, args, rate=44100, tag=""):
        return [Case(f"{k} {endian} {channels}ch {' '.join(args)} {tag}".strip(),
                     stream(v, endian, channels, args, rate),
                     len(v) // channels, channels, endian, pcm(v, endian))
                for k, v in sigs.items()]

    g = out["levels"] = []
    for lvl in range(9):
        for endian in ("le", "be"):
            g += ok(sig, endian, 2, [f"-{lvl}"])

    # 100 and 2352 are not one of the coded sizes, so every frame's header
    # spells its size out in 8 or 16 bits; 16384 is past the subset.
    g = out["blocksizes"] = []
    for bs in (100, 192, 256, 576, 1152, 2048, 2304, 2352, 4096, 4608, 16384):
        for lvl, endian in (("-5", "le"), ("-8", "be")):
            g += ok(sig, endian, 2, [lvl, f"--blocksize={bs}", "--lax"])

    g = out["stereo"] = []
    for mode in ("--no-mid-side", "--mid-side", "--adaptive-mid-side"):
        for lvl in ("-1", "-3", "-5", "-8"):
            g += ok(sig, "le", 2, [lvl, mode])

    g = out["search"] = []
    for args in (["-5", "-e"], ["-5", "-p"], ["-8", "-e", "-p"],
                 ["-8", "-l", "32", "--lax"], ["-8", "-l", "32", "-e", "-p", "--lax"],
                 ["-8", "-q", "15"], ["-8", "-q", "5"], ["-8", "-l", "1"],
                 ["-8", "-l", "0", "-e"], ["-8", "-r", "0,8"], ["-0", "-r", "0"],
                 ["-8", "--blocksize=4608", "-r", "0,15", "--lax"],
                 ["-8", "--blocksize=16384", "-r", "15", "-l", "32", "--lax"],
                 ["-8", "-A", "rectangle"], ["-8", "-A", "subdivide_tukey(5)"]):
        g += ok(sig, "be" if len(args) % 2 else "le", 2, args)

    g = out["mono"] = []
    for args in (["-0"], ["-2"], ["-5"], ["-8"], ["-8", "--blocksize=192"],
                 ["-8", "--blocksize=4608"], ["-8", "-l", "32", "-e", "--lax"]):
        for endian in ("le", "be"):
            g += ok(mono, endian, 1, args)

    # 22000 is said in kHz, 37800 in tens of Hz and 11025 in Hz, each at
    # the end of the header; the rest have codes of their own.
    g = out["rates"] = []
    pick = {k: sig[k] for k in ("sine", "mix", "clip")}
    for rate in (8000, 11025, 16000, 22000, 22050, 32000, 37800, 48000,
                 60000, 88200, 96000, 176400, 192000):
        g += ok(pick, "le", 2, ["-5"], rate, f"@{rate}")

    # Headers flac never writes, though the format allows them
    def rw(signame, channels, args, fn):
        s = sig[signame] if channels == 2 else mono[signame]
        make = stream(s, "le", channels, args)
        return lambda: rewrite(make(), fn)

    g = out["headers"] = []
    sources = (("mix", 2, ["-8", "--blocksize=2352"]), ("sine", 2, ["-5"]),
               ("noise", 1, ["-0"]), ("mix", 2, ["-5", "--blocksize=100"]),
               ("clip", 2, ["-8", "--blocksize=192"]))
    good = (("variable", lambda h, k, s: dict(var=1, num=s)),
            ("variable, 7-byte numbers", lambda h, k, s: dict(var=1, num=s + (1 << 35) + 12345)),
            ("6-byte frame numbers", lambda h, k, s: dict(num=k + (1 << 30) + 7)),
            ("sizes from STREAMINFO", lambda h, k, s: dict(ss=0, sr=0, srbytes=b"")),
            ("rate in kHz", lambda h, k, s: dict(sr=12, srbytes=bytes([44]))),
            ("rate in Hz", lambda h, k, s: dict(sr=13, srbytes=(44100).to_bytes(2, "big"))),
            ("rate in tens of Hz", lambda h, k, s: dict(sr=14, srbytes=(4410).to_bytes(2, "big"))))
    bad = (("7-byte frame number", lambda h, k, s: dict(num=k + (1 << 35))),
           ("20-bit samples", lambda h, k, s: dict(ss=5)),
           ("8-bit samples", lambda h, k, s: dict(ss=1)),
           ("reserved sample size", lambda h, k, s: dict(ss=3)),
           ("reserved bit", lambda h, k, s: dict(low=1)),
           ("reserved rate", lambda h, k, s: dict(sr=15, srbytes=b"")),
           ("reserved channels", lambda h, k, s: dict(ch=11)),
           # The CRC-16 covers the header too, so it alone would catch this
           ("header CRC-8 off, CRC-16 good", lambda h, k, s: dict(crc8=0x10)))
    for signame, channels, args in sources:
        s = sig[signame] if channels == 2 else mono[signame]
        n = len(s) // channels
        for what, fn in good:
            g.append(Case(f"{signame} {' '.join(args)}: {what}",
                          rw(signame, channels, args, fn), n, channels, "le",
                          pcm(s, "le")))
        for what, fn in bad:
            g.append(Case(f"{signame} {' '.join(args)}: {what}",
                          rw(signame, channels, args, fn), n, channels, "le", None))
        # A frame is dropped from the middle: still good frames, but too few
        g.append(Case(f"{signame} {' '.join(args)}: frame left out",
                      (lambda m: lambda: b"".join(f for i, f in enumerate(split(m())) if i != 1))(
                          stream(s, "le", channels, args)), n, channels, "le", None))

    # What a caller can ask: fewer samples than the stream has, junk after
    # the last frame needed, more than there is, the wrong channel count.
    g = out["requests"] = []
    s = sig["mix"]
    n = len(s) // 2
    make = stream(s, "be", 2, ["-8", "--blocksize=1152"])
    junk = bytes(random.Random(1).randrange(256) for _ in range(3000))
    for frames in (1, 1151, 1152, 1153, 5000, n - 1, n):
        g.append(Case(f"mix: first {frames}", make, frames, 2, "be",
                      pcm(s[:2 * frames], "be")))
    g.append(Case("mix: junk after the last frame", lambda m=make: m() + junk, n, 2, "be",
                  pcm(s, "be")))
    g.append(Case("mix: one more than there is", make, n + 1, 2, "be", None))
    g.append(Case("mix: as mono", make, n, 1, "be", None))
    g.append(Case("mix: as three channels", make, n, 3, "be", None))
    g.append(Case("mix: no channels", make, n, 0, "be", None))
    g.append(Case("nothing asked of nothing", lambda: b"", 0, 2, "le", b""))
    g.append(Case("something asked of nothing", lambda: b"", 1, 2, "le", None))

    g = out["made here"] = []
    for i, (name, frames, good) in enumerate(made_here()):
        chans = len(frames[0][0])
        samples = []
        for c, _ in frames:
            for k in range(len(c[0])):
                samples.extend(ch[k] for ch in c)
        endian = "be" if i % 2 else "le"
        g.append(Case(name, (lambda fr: lambda: b"".join(b for _, b in fr))(frames),
                      len(samples) // chans, chans, endian,
                      pcm(samples, endian) if good else None, asan))

    # Damage, under the sanitizers where they build: every one must say false
    g = out["truncated"] = []
    for name, s, ch, args in (("mix", sig["mix"], 2, ["-8"]),
                              ("noise", sig["noise"], 2, ["-0", "--blocksize=4608"]),
                              ("square", mono["square"], 1, ["-5"]),
                              ("clip", sig["clip"], 2, ["-8", "--blocksize=100"])):
        make = stream(s, "le", ch, args)
        for cut in ("1", "5", "6", "header", "half", "last frame", "-200", "-3", "-2", "-1"):
            g.append(Case(f"{name} {' '.join(args)} cut at {cut}",
                          (lambda m, cut: lambda: truncate(m(), cut))(make, cut),
                          len(s) // ch, ch, "le", None, asan))
    g = out["flipped"] = []
    for name, s, ch, args in (("sine", sig["sine"], 2, ["-8"]),
                              ("hiss", sig["hiss"], 2, ["-5", "--blocksize=576"]),
                              ("noise", mono["noise"], 1, ["-0"])):
        make = stream(s, "le", ch, args)
        for k in range(24):
            g.append(Case(f"{name} {' '.join(args)} flip {k}",
                          (lambda m, k: lambda: flip(m(), k))(make, k),
                          len(s) // ch, ch, "le", None, asan))
    g = out["garbage"] = []
    for k in range(24):
        g.append(Case(f"garbage {k}", (lambda k: lambda: garbage(k))(k),
                      4096, 2 if k % 3 else 1, "le", None, asan))
    return out


def truncate(raw, cut):
    frames = split(raw)
    last = len(raw) - len(frames[-1])
    at = {"header": 9, "half": len(raw) // 2, "last frame": last,
          }.get(cut) or (int(cut) if int(cut) > 0 else len(raw) + int(cut))
    return raw[:at]


def flip(raw, k):
    """One byte flipped at the k-th of 24 places, header bytes first."""
    rnd = random.Random(k)
    frames = split(raw)
    at = sum(len(f) for f in frames[:k % len(frames)])
    f = frames[k % len(frames)]
    off = k % 6 if k < 8 else rnd.randrange(6, len(f))
    b = bytearray(raw)
    b[at + off] ^= 1 << rnd.randrange(8) if k % 2 else rnd.randrange(1, 256)
    return bytes(b)


def garbage(k):
    """Random bytes, the later ones behind a well-formed header, so that the
    subframe readers see them rather than the sync check alone."""
    rnd = random.Random(1000 + k)
    body = bytes(rnd.randrange(256) for _ in range(rnd.choice((0, 3, 40, 900, 20000))))
    if k < 4:
        return body
    ch = 0 if k % 3 == 0 else rnd.choice((1, 8, 9, 10))   # mono when asked for 1
    head = bytes([0xff, 0xf8, rnd.choice((0x19, 0x59, 0xc9, 0x69)), ch << 4 | 8, 0])
    if head[2] >> 4 == 6:
        head += bytes([rnd.randrange(256)])
    head += bytes([crc8(head)])
    return head + body


# -------------------------------------------------------------------- main ---

def main():
    global FLAC
    FLAC = shutil.which("flac")
    if not FLAC:
        print("  skip: no flac encoder on PATH (brew install flac)")
        return 0
    start = time.time()
    tmp = tempfile.mkdtemp(prefix="flacdec-")
    try:
        plain, asan = build(tmp)
        if not plain:
            print("\nFAIL: src/flacdec.c does not build cleanly")
            return 1
        if not asan:
            print("  note: no AddressSanitizer here; damaged streams run unsanitized")
        sig = signals()
        total = failed = 0
        with concurrent.futures.ThreadPoolExecutor(JOBS) as pool:
            for name, cases in groups(sig, asan).items():
                def one(ic):
                    i, c = ic
                    try:
                        return c.run(plain, os.path.join(tmp, f"{name}-{i}.bin"))
                    except Exception as e:  # an encoder error, or a crash
                        return str(e)
                errs = list(pool.map(one, enumerate(cases)))
                bad = [(c, e) for c, e in zip(cases, errs) if e]
                total += len(cases)
                failed += len(bad)
                for c, e in bad[:8]:
                    print(f"    {name}: {c.name}: {e}")
                if len(bad) > 8:
                    print(f"    {name}: ... and {len(bad) - 8} more")
                print(f"  {'ok  ' if not bad else 'FAIL'} {name:<11} {len(cases) - len(bad)}/{len(cases)}")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"\n  {total} cases, {failed} failed, {time.time() - start:.1f} s")
    if failed:
        print("\nFAIL: src/flacdec.c")
        return 1
    print("\nok: every stream decodes to its PCM, and every broken one is refused")
    return 0


if __name__ == "__main__":
    sys.exit(main())
