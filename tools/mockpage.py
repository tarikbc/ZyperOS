#!/usr/bin/env python3
"""Turn a mock-kit page into one self-contained HTML file.

    python3 tools/mockpage.py docs/design/mock/components.html OUT.html

A page in docs/design/mock/ links kit.css, tokens.js and kit.js beside it and
marks where Zyper goes with <!--ZYPER-->; an Artifact page has to carry all
of it inline. This puts kit.css in a <style>, the two scripts in <script>s
and res/web/zyper.svg in an SVG <symbol id="zyper">, and writes OUT. A page
that links sounds.js, the sound lab's WAVs (tools/gensounds.py --lab), or
menu-music.js, the menu music lab's tracks (tools/bgm-tracks.py lab), or
portal.js, the File Transfer page's stylesheet and icons (tools/mockportal.py),
gets that inlined too; the music lab's MP3s stay files beside the page.
Standard library only.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, out = sys.argv[1], sys.argv[2]
    here = os.path.dirname(os.path.abspath(src))
    with open(src) as f:
        page = f.read()

    def read(name):
        with open(os.path.join(here, name)) as f:
            return f.read()

    page = page.replace('<link rel="stylesheet" href="kit.css">', "<style>\n" + read("kit.css") + "</style>")
    for js in ("tokens.js", "kit.js", "sounds.js", "menu-music.js", "portal.js"):
        tag = '<script src="%s"></script>' % js
        if tag in page:
            page = page.replace(tag, "<script>\n" + read(js) + "</script>")
    with open(os.path.join(ROOT, "res", "web", "zyper.svg")) as f:
        svg = f.read()
    box = re.search(r'viewBox="([^"]+)"', svg).group(1)
    body = re.sub(r"^<svg[^>]*>|</svg>\s*$", "", svg.strip())
    symbol = ('<svg width="0" height="0" style="position:absolute" aria-hidden="true">'
              '<symbol id="zyper" viewBox="%s">%s</symbol></svg>' % (box, body))
    page = page.replace("<!--ZYPER-->", symbol)
    left = [m for m in ('href="kit.css"', 'src="tokens.js"', 'src="kit.js"', 'src="sounds.js"',
                        'src="menu-music.js"', 'src="portal.js"', "<!--ZYPER-->")
            if m in page]
    if left:
        raise SystemExit("not inlined: " + ", ".join(left))
    with open(out, "w") as f:
        f.write(page)
    print("wrote", out, len(page), "bytes")


if __name__ == "__main__":
    main()
