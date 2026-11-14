#!/usr/bin/env python3
"""The same pixels, proved: every shot the launcher takes, at a base and here.

    make pixel-gate                         against origin/main, both themes,
                                            every language
    python3 tools/pixel-gate.py --base REF  against REF
    python3 tools/pixel-gate.py --lang en --theme light menu usb-ask
                                            one language, one theme, some shots

A change that moves code and means to change nothing on the screen (a part
moved into src/ds/, a number that became a token) is held to it: the
launcher is built at the base in a worktree of its own and here, and both
take every shot of tools/i18n-shots.py (the README's screens and the rest:
check-i18n-shots's list) in each language and theme asked for, from the same
scratch cards, made from this tree's res/. The frames are compared as decoded
pixels, not as PNG bytes. It prints "pixel gate: K of N frames changed", lists
each one, and for the changed ones writes sheets of the base, this tree and the
pixels that differ (build-native/pixel-gate/sheet-*.png): the before and after
of a change that means to change something, for a review.

The base's frames are kept by its commit (build-native/pixel-gate/base-<sha>),
so a second run against the same base shoots only this tree. A shot that only
one side has is listed as new or gone, not as a change. Art in res/ is not
compared: both sides draw this tree's, and its own checks hold it.

Exit 0 when no frame changed, 1 when one did (or with --allow-changes, 0 and
the list), 2 when a build or a side's shots failed to run at all.

Not in `make check`: it builds the launcher twice and takes about 9,200 frames
a side. CPU: --jobs (3) launchers at once.
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build-native" / "pixel-gate"
SHEET_ROWS = 10


def run(cmd, cwd, **kw):
    return subprocess.run(cmd, cwd=cwd, text=True, **kw)


def rev(ref):
    r = run(["git", "rev-parse", "--verify", ref + "^{commit}"], ROOT, stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL)
    if r.returncode:
        sys.exit("pixel gate: no commit %s" % ref)
    return r.stdout.strip()


def build(tree, jobs):
    """The host launcher in `tree`, as make native builds it"""
    r = run(["make", "-s", "-j%d" % jobs, "native"], tree, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, errors="replace")
    binary = tree / "build-native" / "zyperos"
    if r.returncode or not binary.is_file():
        print(r.stdout[-3000:], file=sys.stderr)
        sys.exit("pixel gate: the launcher did not build in %s" % tree)
    return binary


def shoot(binary, out, langs, themes, only, jobs):
    """Every shot into out/<theme>/<lang>/<name>.png, with this tree's
    i18n-shots (its list, its cards), whatever the binary is"""
    for theme in themes:
        cmd = [sys.executable, str(ROOT / "tools" / "i18n-shots.py"), "--binary", str(binary),
               "--theme", theme, "--jobs", str(jobs), "--keep", str(out / theme)]
        for code in langs:
            cmd += ["--lang", code]
        cmd += only
        # Its own verdict (what does not fit) is check-i18n-shots's business:
        # here only the frames count
        r = run(cmd, ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, errors="replace")
        if not any((out / theme).glob("*/*.png")):
            print(r.stdout[-3000:], file=sys.stderr)
            sys.exit("pixel gate: no frames from %s in %s" % (binary, theme))


def frames(top):
    return {p.relative_to(top).as_posix(): p for p in top.glob("*/*/*.png")}


def decode(path):
    from PIL import Image
    with Image.open(path) as im:
        im = im.convert("RGBA")
        return im.size, im.tobytes()


def same(a, b):
    if a.read_bytes() == b.read_bytes():
        return True
    try:
        return decode(a) == decode(b)
    except ImportError:
        return False


def sheets(changed, base, here, out):
    """Rows of base | here | the differing pixels (magenta over the base,
    dimmed), half size, SHEET_ROWS a sheet"""
    try:
        from PIL import Image, ImageChops, ImageDraw, ImageFont
    except ImportError:
        print("pixel gate: no Pillow, so no sheets")
        return []
    try:
        font = ImageFont.load_default(size=20)   # Pillow 10.1+
    except TypeError:
        font = ImageFont.load_default()
    made = []
    for start in range(0, len(changed), SHEET_ROWS):
        part = changed[start:start + SHEET_ROWS]
        w, h, label = 512, 384, 28
        sheet = Image.new("RGB", (3 * w + 4 * 8, len(part) * (h + label + 8) + 8), (40, 42, 40))
        draw = ImageDraw.Draw(sheet)
        for i, name in enumerate(part):
            a = Image.open(base[name]).convert("RGB")
            b = Image.open(here[name]).convert("RGB")
            if a.size != b.size:
                b = b.resize(a.size)
            diff = ImageChops.difference(a, b).convert("L").point(lambda v: 255 if v else 0)
            mark = Image.blend(a, Image.new("RGB", a.size, (0, 0, 0)), 0.6)
            mark.paste(Image.new("RGB", a.size, (255, 0, 200)), mask=diff)
            y = 8 + i * (h + label + 8)
            draw.text((8, y + 6), "%s   (before | after | changed pixels: %d)" % (
                name, sum(1 for v in diff.getdata() if v)), fill=(228, 226, 216), font=font)
            for k, im in enumerate((a, b, mark)):
                sheet.paste(im.resize((w, h)), (8 + k * (w + 8), y + label))
        path = out / ("sheet-%02d.png" % (start // SHEET_ROWS + 1))
        sheet.save(path)
        made.append(path)
    return made


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--base", default="origin/main", help="the commit to compare with (origin/main)")
    ap.add_argument("--lang", action="append", help="this language only (more than once for more)")
    ap.add_argument("--theme", action="append", choices=["light", "dark"], help="this theme only")
    ap.add_argument("--jobs", type=int, default=3, help="launchers at once (3)")
    ap.add_argument("--allow-changes", action="store_true", help="exit 0 with changes: a change meant")
    ap.add_argument("--fresh", action="store_true", help="shoot the base again even if kept")
    ap.add_argument("only", nargs="*", help="shot names (default: all)")
    args = ap.parse_args()

    sys.path.insert(0, str(ROOT / "tools"))
    import i18n
    langs = args.lang or (i18n.CODES + [i18n.PSEUDO])
    themes = args.theme or ["light", "dark"]
    sha = rev(args.base)
    OUT.mkdir(parents=True, exist_ok=True)
    # The base's frames are kept by commit and by what was asked: a run of
    # some shots or languages keeps its own
    key = "%s-%s-%s%s" % (sha[:12], "+".join(themes), "+".join(langs),
                          ("-" + "+".join(sorted(args.only))) if args.only else "")
    base_out = OUT / ("base-" + key)
    here_out = OUT / "here"

    if args.fresh or not any(base_out.glob("*/*/*.png")):
        shutil.rmtree(base_out, ignore_errors=True)
        with tempfile.TemporaryDirectory(prefix="zyperos-pixel-gate-") as tmp:
            tree = Path(tmp) / "base"
            run(["git", "worktree", "add", "--detach", "-q", str(tree), sha], ROOT, check=True)
            try:
                print("pixel gate: building the base, %s" % sha[:12], flush=True)
                binary = build(tree, args.jobs)
                print("pixel gate: the base's shots", flush=True)
                shoot(binary, base_out, langs, themes, args.only, args.jobs)
            finally:
                run(["git", "worktree", "remove", "--force", str(tree)], ROOT)
    else:
        print("pixel gate: the base's shots kept from %s" % base_out.name)

    print("pixel gate: building this tree", flush=True)
    binary = build(ROOT, args.jobs)
    shutil.rmtree(here_out, ignore_errors=True)
    print("pixel gate: this tree's shots", flush=True)
    shoot(binary, here_out, langs, themes, args.only, args.jobs)

    base, here = frames(base_out), frames(here_out)
    both = sorted(set(base) & set(here))
    changed = [n for n in both if not same(base[n], here[n])]
    new = sorted(set(here) - set(base))
    gone = sorted(set(base) - set(here))
    for old in OUT.glob("sheet-*.png"):
        old.unlink()
    report = ["pixel gate: %d of %d frames changed (base %s, %s, %s)" % (
        len(changed), len(both), sha[:12], " ".join(themes), " ".join(langs))]
    report += ["changed: " + n for n in changed]
    report += ["new: " + n for n in new]
    report += ["gone: " + n for n in gone]
    made = sheets(changed, base, here, OUT) if changed else []
    report += ["sheet: " + str(p) for p in made]
    (OUT / "report.txt").write_text("\n".join(report) + "\n")
    print("\n".join(report))
    if changed and not args.allow_changes:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
