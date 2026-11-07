#!/usr/bin/env python3
"""The Chinese and Japanese UI's fonts: Noto Sans SC and JP cut to what
ZyperOS writes.

    python3 tools/cjkfont.py DIR     res/fonts/zh-menu.otf, zh-title.otf,
                                     ja-menu.otf and ja-title.otf from
                                     DIR/NotoSansSC-Regular.otf, -Bold.otf,
                                     NotoSansJP-Regular.otf and -Bold.otf
                                     (make cjk-font CJK_SRC=DIR; needs fontTools)

Lexend, the UI's face (DESIGN.md 4), has neither, so in 简体中文 and 日本語
the launcher draws every role in Noto Sans SC or JP: Regular for all but the
title, Bold for the title (the static cuts have no SemiBold). Two faces, not
one: Chinese and Japanese write some of the same characters differently. The
button letters and the cards the launcher draws stay Lexend in every
language: they are marks, not words.

The whole fonts are 4.6 to 8.5 MB a weight; each cut keeps

  - its language's most used characters, so a new string seldom needs a new
    cut: GB 2312's first level (3,755) for Chinese, JIS X 0208's first level
    of kanji (2,965), the kana and its symbols for Japanese,
  - every character of every string in res/strings, in every language, that
    the font has,
  - ASCII, Latin-1, Latin Extended-A, general and CJK punctuation and the
    full-width forms,

and drops the hinting and the layout features the launcher never asks for:
about 830 KB a weight for Chinese, and as much for Japanese. Measured on an
M3 against Lexend (2026-11-07): about 1 ms to open the launcher's eight
sizes, and 0.5 MB more memory. tools/i18n.py --check fails when a string has
a character its cut lacks, and says to run this.

The sources are Google's Noto Sans SC 2.002 and Noto Sans JP 2.001
(github.com/notofonts/noto-cjk, the Google Fonts static cuts), under the SIL
Open Font License 1.1, res/fonts/OFL-NotoSansSC.txt and
OFL-NotoSansJP.txt. They are not kept here: DIR is wherever a copy is.
"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n  # noqa: E402

ROOT = i18n.ROOT
OUT = os.path.join(ROOT, "res", "fonts")
# Each cut: its files from the sources, and the encoding rows of its
# language's common characters (the first byte's range, as EUC writes them)
CUTS = {
    "zh-Hans": ({"zh-menu.otf": "NotoSansSC-Regular.otf", "zh-title.otf": "NotoSansSC-Bold.otf"},
                "gb2312", [(0xB0, 0xD7)]),
    "ja": ({"ja-menu.otf": "NotoSansJP-Regular.otf", "ja-title.otf": "NotoSansJP-Bold.otf"},
           "euc_jp", [(0xA1, 0xA1), (0xA4, 0xA5), (0xB0, 0xCF)]),
}


def common(codec, rows):
    """The characters of an EUC encoding's rows"""
    chars = set()
    for first, last in rows:
        for hi in range(first, last + 1):
            for lo in range(0xA1, 0xFF):
                try:
                    chars.add(bytes([hi, lo]).decode(codec))
                except UnicodeDecodeError:
                    pass
    return chars


def wanted(codec, rows):
    """Every character a cut keeps"""
    chars = common(codec, rows)
    for lo, hi in ((0x20, 0x7E), (0xA0, 0x17F), (0x2000, 0x206F), (0x3000, 0x303F), (0xFF00, 0xFFEF)):
        chars.update(chr(c) for c in range(lo, hi + 1))
    errors = []
    for s in i18n.parse(errors):
        for v in s.text.values():
            for t in (v if isinstance(v, tuple) else (v,)):
                chars.update(t or "")
    for _, name, _, _ in i18n.LANGS:
        chars.update(name)
    chars.discard("\n")
    return "".join(sorted(chars))


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    src = sys.argv[1]
    text = os.path.join(ROOT, "build", "cjkfont-chars.txt")
    os.makedirs(os.path.dirname(text), exist_ok=True)
    for code, (files, codec, rows) in CUTS.items():
        with open(text, "w", encoding="utf-8") as f:
            f.write(wanted(codec, rows))
        for out, name in files.items():
            subprocess.check_call([sys.executable, "-m", "fontTools.subset", os.path.join(src, name),
                                   "--text-file=" + text, "--output-file=" + os.path.join(OUT, out),
                                   "--no-hinting", "--layout-features=kern",
                                   "--name-IDs=*", "--name-legacy", "--name-languages=*"])
            print("res/fonts/%s: %d KB" % (out, os.path.getsize(os.path.join(OUT, out)) // 1024))


if __name__ == "__main__":
    main()
