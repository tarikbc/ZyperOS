# SPDX-License-Identifier: GPL-3.0-only
"""Committed art against its generator, for a generator's --check.

The generator writes into a temp folder of its own, and this compares what it
wrote with the files of the same names in the committed folder. A PNG by its
pixels, every band of them: a Pillow whose zlib packs the same picture into
other bytes is no drift, and a color moved under the same alpha is. Anything
else byte for byte. A file the generator owns in the committed folder and no
longer writes is drift too: `owned(rel)` says which names are its.

Needs Pillow, as the generators do.
"""
import os

from PIL import Image, ImageChops


def _files(root):
    """Every file under `root`, relative to it; a dotfile is the host's (a
    .DS_Store a Finder window leaves), and is passed over."""
    out = set()
    for d, ds, fs in os.walk(root):
        ds[:] = [x for x in ds if not x.startswith(".")]
        out |= {os.path.relpath(os.path.join(d, f), root) for f in fs if not f.startswith(".")}
    return out


def _same_png(a, b):
    x, y = Image.open(a), Image.open(b)
    if x.size != y.size or x.mode != y.mode:
        return False
    diff = ImageChops.difference(x.convert("RGBA"), y.convert("RGBA"))
    # Band by band: an RGBA image's getbbox looks at its alpha alone.
    return not any(band.getbbox() for band in diff.split())


def drift(fresh, committed, owned):
    """What differs, a line each, empty when the committed files are what the
    generator writes"""
    bad = []
    made = _files(fresh)
    for rel in sorted(made):
        a, b = os.path.join(fresh, rel), os.path.join(committed, rel)
        if not os.path.exists(b):
            bad.append("%s: written, and not committed" % rel)
        elif rel.endswith(".png"):
            if not _same_png(a, b):
                bad.append("%s: not what the generator draws" % rel)
        else:
            with open(a, "rb") as fa, open(b, "rb") as fb:
                if fa.read() != fb.read():
                    bad.append("%s: not what the generator writes" % rel)
    bad += ["%s: committed, and no longer written" % rel
            for rel in sorted(_files(committed) - made) if owned(rel)]
    return bad
