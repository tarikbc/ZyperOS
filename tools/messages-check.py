#!/usr/bin/env python3
"""Guard the 16 result sites against the old blocking holds."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import sys
import tempfile

# src/main.c and the files split from it
LAUNCHER = ("src/main.c", "src/aout_route.c", "src/bt_screen.c", "src/ctltab_screen.c",
            "src/place.c", "src/portload.c", "src/powerscreens.c", "src/raqueue.c",
            "src/shelfbuild.c", "src/stats_screen.c", "src/zyperdraw.c")
source = "\n".join(Path(p).read_text() for p in LAUNCHER)
source = re.sub(r'/\*.*?\*/|//[^\n]*', '', source, flags=re.S)
stale = list(re.finditer(r'\bSDL_Delay\s*\(\s*(900|1400|1600|1800|2200)\s*\)', source))
if stale:
    print(f"FAIL: {len(stale)} blocking result holds remain: " +
          ", ".join(m.group(1) for m in stale))
    sys.exit(1)
print("ok: no stale result-delay literals")

def function(name):
    # Braces inside strings and comments are not C block boundaries.
    tokens = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]', re.S)
    raw = Path("src/main.c").read_text()
    # Static, or not: src/panel.h shares some with the screens split off
    match = re.search(r'^(?:static )?[A-Za-z_][^;{(]*\b' + name + r'\([^;{]*\)\s*\{', raw, re.M)
    if not match:
        raise AssertionError(f"missing function: {name}")
    depth = 0
    for token in tokens.finditer(raw, match.end() - 1):
        if token.group() == '{':
            depth += 1
        elif token.group() == '}':
            depth -= 1
            if not depth:
                return raw[match.start():token.end()]
    raise AssertionError(f"unclosed function: {name}")

# The planned source guard holds coverage of all 16 sites. The C harness below
# then executes the actual loop, with the real timer, motion and snapshots.
sites = {
    "wifi_key": (0, 3), "ra_signin_screen": (2, 0), "ss_signin_screen": (2, 0),
    # File Transfer's session (main.c, xs_*): could not start and the hotspot's could
    # not start (xs_open), the hotspot stopped (xs_stop), and closed for a game
    # (xs_before_game)
    "xs_open": (2, 0), "xs_stop": (1, 0), "xs_before_game": (1, 0),
    "art_run": (2, 0), "album_art_screen": (1, 0),
    "launch_port": (0, 1), "launch": (0, 1),
}
for name, counts in sites.items():
    body = function(name)
    actual = tuple(len(re.findall(r'\b' + call + r'\(', body))
                   for call in ("timed_message", "result_message"))
    assert actual == counts, (name, actual, counts)
wait = function("wait_panel")
assert wait.count("plat_present()") == 1 and "SDL_Delay" not in wait
assert "timed_message" not in wait and "result_message" not in wait

with tempfile.TemporaryDirectory(prefix="zyperos-messages-check-") as scratch:
    out = Path(scratch)
    platform = Path("src/platform.h").read_text()
    # The buttons are src/inbutton.h's, which platform.h includes; in_state
    # follows that include there.
    inputs = platform[platform.index('#include "inbutton.h"'):platform.index("bool plat_video_init")]
    (out / "message-input.inc").write_text("typedef unsigned Uint32;\n" + inputs)
    names = ("snap_record", "snap_draw", "snap_shown", "snap_promote", "panel_show",
             "menu_draw_ex", "menu_leaving", "panel_settle", "panel_open", "panel_frame",
             "panel_close_at", "panel_close", "timed_message")
    (out / "message-loop.inc").write_text("\n\n".join(function(n) for n in names))
    binary = out / "message-loop-check"
    subprocess.run(shlex.split(os.environ.get("CC", "cc")) + [
        "-std=gnu11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-O1", "-g",
        "-Isrc", "-I" + scratch, "tools/message-loop-check.c", "src/message.c",
        "src/menumotion.c", "src/menusnap.c", "src/menu.c", "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
