#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every texture ZyperOS draws into is PLAT_TARGET_FORMAT, and that is a format
SDL's GLES2 renderer keeps unswapped (make check-targets).

On the Pixel 2 a video snap drawn into an ARGB8888 target came out with red and
blue changed over: SDL's GLES2 renderer swaps red and blue inside such a
target for RGB textures and draw colors, but not for a YUV texture's shader
(src/platform.h, PLAT_TARGET_FORMAT, has the whole story). A new target made
ARGB8888 again would bring it back for whatever video is drawn into it, and
nothing on the Mac would show it: Metal holds both formats as they are. So
every SDL_CreateTexture with SDL_TEXTUREACCESS_TARGET in src/ is held to the
one name, and the name to a format whose bytes are GLES2's own order.
tools/yuvtarget-check.sh runs the real thing against the device's SDL."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
# SDL's names for the formats whose bytes are R, G, B, A in memory on a
# little-endian CPU (the Pixel 2's and the Brick's): GLES2's GL_RGBA as it is
UNSWAPPED = {'SDL_PIXELFORMAT_ABGR8888', 'SDL_PIXELFORMAT_RGBA32',
             'SDL_PIXELFORMAT_XBGR8888', 'SDL_PIXELFORMAT_RGBX32',
             'SDL_PIXELFORMAT_BGR888'}
failures = 0


def fail(msg):
    global failures
    print(f'FAIL {msg}')
    failures += 1


def code(text):
    """The text with comments and strings blanked, its line breaks kept."""
    blank = lambda m: re.sub(r'[^\n]', ' ', m.group())
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\\n])*"', blank, text, flags=re.S)


def calls(text, name):
    """(line, [arguments]) for each call of `name` in code(text)."""
    for m in re.finditer(r'\b' + name + r'\s*\(', text):
        depth, args, start = 1, [], m.end()
        for i in range(m.end(), len(text)):
            c = text[i]
            if c in '([{':
                depth += 1
            elif c in ')]}':
                depth -= 1
                if depth == 0:
                    args.append(text[start:i].strip())
                    break
            elif c == ',' and depth == 1:
                args.append(text[start:i].strip())
                start = i + 1
        yield text.count('\n', 0, m.start()) + 1, [' '.join(a.split()) for a in args]


platform = code((ROOT / 'src/platform.h').read_text())
d = re.search(r'^\s*#\s*define\s+PLAT_TARGET_FORMAT\s+(\S+)\s*$', platform, re.M)
if not d:
    fail('src/platform.h does not define PLAT_TARGET_FORMAT')
elif d.group(1) not in UNSWAPPED:
    fail(f'PLAT_TARGET_FORMAT is {d.group(1)}: SDL\'s GLES2 renderer swaps red and blue '
         f'in a target of that format for everything but YUV, so a video in it comes out '
         f'swapped; one of {", ".join(sorted(UNSWAPPED))}')

targets = 0
for path in sorted((ROOT / 'src').rglob('*.c')):
    rel = path.relative_to(ROOT)
    for line, args in calls(code(path.read_text()), 'SDL_CreateTexture'):
        if len(args) != 5:
            fail(f'{rel}:{line}: SDL_CreateTexture with {len(args)} arguments, not 5')
            continue
        if args[2] != 'SDL_TEXTUREACCESS_TARGET':
            if 'TARGET' in args[2]:
                fail(f'{rel}:{line}: access {args[2]}, which this check cannot read')
            continue
        targets += 1
        if args[1] != 'PLAT_TARGET_FORMAT':
            fail(f'{rel}:{line}: a render target made {args[1]}, not PLAT_TARGET_FORMAT '
                 f'(src/platform.h: a video drawn into an ARGB8888 one on the Pixel 2 '
                 f'has red and blue swapped)')

if targets < 9:
    fail(f'only {targets} render targets found in src/; the check is not reading them')
if failures:
    sys.exit(1)
print(f'ok: all {targets} render targets in src/ are PLAT_TARGET_FORMAT, {d.group(1)}')
