#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Every picture Diatom's GPU ports draw, the right way up (make check-gpu-upright).

    python3 tools/gpu-upright-check.py            on Linux with Mesa's EGL and GLES
    python3 tools/gpu-upright-check.py --docker   anywhere with Docker: the same,
                                                  in tools/p8fx-gl.Dockerfile's Linux

The shared presenting code (diatom/port/gles_*.h) built twice by
diatom/test/upright_test.c, once with the TrimUI Brick's hooks and once with
the GKD Pixel 2's, each port's PRESENT_TOP_DOWN taken from its own file
(diatom/port/brick.c, diatom/port/pixel2.c), and run on Mesa's llvmpipe with
no display: each frame read back from the target as that device's display
reads it, the Brick's page of fb0 from its first byte, the Pixel 2's window
from its top row, the portrait panel turned. This fails when, on either:
  - a frame at Nearest is not, to the pixel, the picture the right way up;
    at Sharp, Sharp-Shimmerless, CRT, CRT Curved, Scanlines, LCD Grid and
    LCD No Backlight, and under 100%, its bright quarter is not top left;
  - a DS frame's top screen is not at the top, or a GPU core's frame, drawn
    GL's way up or top down, is not the picture to the pixel;
  - the Border, a notice, the level bar, On-Screen Stats or the low-battery
    disc is drawn anywhere but in its place, or a notice upside down;
  - a capture (a screenshot) of a frame or of a GPU core's frame is not the
    picture the right way up;
  - the launcher's CRT Curved, drawn from a target texture as the Brick's is
    (tools/gpu-upright-bend.c, src/screenfx_draw.c) on SDL's OpenGL ES 2
    renderer, does not keep the picture's bright quarter top left.
Skips, saying so, where it cannot run: not Linux and no --docker, no EGL,
GLES or SDL2 to build with, no OpenGL ES 3 on EGL's surfaceless platform.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# The image check-p8fx-gl builds: the same Ubuntu, SDL2 and Mesa
IMAGE = "zyperos-p8fx-gl"
PORTS = [("brick", "diatom/port/brick.c", "UPRIGHT_BRICK"),
         ("pixel2", "diatom/port/pixel2.c", "UPRIGHT_PIXEL2")]


def skip(why):
    print("skipped: " + why)
    return 0


def docker():
    if not shutil.which("docker"):
        return skip("no Docker for --docker")
    if subprocess.run(["docker", "image", "inspect", IMAGE], capture_output=True).returncode:
        print("gpu-upright: building %s (tools/p8fx-gl.Dockerfile, once)" % IMAGE)
        if subprocess.run(["docker", "build", "-q", "-t", IMAGE, "-f",
                           str(ROOT / "tools/p8fx-gl.Dockerfile"), str(ROOT / "tools")]).returncode:
            return skip("could not build the %s image" % IMAGE)
    return subprocess.run(["docker", "run", "--rm", "-v", "%s:/z:ro" % ROOT, "-w", "/z", IMAGE,
                           "python3", "tools/gpu-upright-check.py"]).returncode


def sh(cmd, **kw):
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, **kw)


def top_down(port_file):
    """The port's PRESENT_TOP_DOWN as -D, if it defines one before the
    shared drawing (gles_draw.h), else None"""
    src = (ROOT / port_file).read_text()
    cut = src.find('#include "gles_draw.h"')
    m = re.search(r"^#define PRESENT_TOP_DOWN\b[ \t]*(\S*)", src[:cut if cut >= 0 else len(src)], re.M)
    if not m:
        return None
    return "-DPRESENT_TOP_DOWN" + ("=" + m.group(1) if m.group(1) else "")


def main():
    if sys.argv[1:] == ["--docker"]:
        return docker()
    if sys.platform != "linux":
        return skip("needs Linux (Mesa's EGL and OpenGL ES); python3 tools/gpu-upright-check.py --docker")
    if sh("pkg-config --exists egl glesv2 sdl2").returncode:
        return skip("no EGL, GLES and SDL2 to build with (pkg-config egl glesv2 sdl2)")
    cc = os.environ.get("CC", "cc")
    tmp = Path(tempfile.mkdtemp(prefix="gpu-upright."))
    try:
        return run(cc, tmp)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def run(cc, B):
    failed = False
    for name, port_file, model in PORTS:
        flag = top_down(port_file)
        exe = B / ("upright-" + name)
        build = ("%s -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -D%s %s "
                 "-Idiatom/include -Idiatom/src -Idiatom/port $(pkg-config --cflags egl glesv2 sdl2) "
                 "-o %s diatom/test/upright_test.c diatom/port/osdbar.c diatom/port/hud.c "
                 "$(pkg-config --libs egl glesv2 sdl2) -lm -lpthread"
                 % (cc, model, flag or "", exe))
        r = sh(build, cwd=ROOT)
        if r.returncode:
            print("FAIL could not build %s's half: %s\n%s" % (name, build, r.stderr))
            return 1
        if r.stderr.strip():
            print(r.stderr.strip())
        r = subprocess.run([str(exe), str(B)], cwd=ROOT, capture_output=True, text=True)
        out = r.stdout.strip()
        if r.returncode == 77:
            return skip(out.replace("skipped: ", "", 1) or "no OpenGL ES 3 here")
        if out:
            print(out)
        if r.returncode:
            failed = True
            if r.stderr.strip():
                print(r.stderr.strip())
            print("FAIL %s: %s, %s" % (name, port_file,
                                       "with " + flag if flag else "no PRESENT_TOP_DOWN in it"))
    # The launcher's bend, on SDL's offscreen video and its OpenGL ES 2
    # renderer, which is the Brick's
    exe = B / "bend"
    build = ("%s -std=gnu11 -O2 -Wall -Wextra -D_GNU_SOURCE -Isrc $(pkg-config --cflags sdl2) -o %s "
             "tools/gpu-upright-bend.c src/screenfx.c src/screenfx_draw.c src/canvas.c $(pkg-config --libs sdl2) -lm"
             % (cc, exe))
    r = sh(build, cwd=ROOT)
    if r.returncode:
        print("FAIL could not build the launcher's bend: %s\n%s" % (build, r.stderr))
        return 1
    r = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True,
                       env=dict(os.environ, SDL_VIDEODRIVER="offscreen", SDL_RENDER_DRIVER="opengles2"))
    if r.stdout.strip():
        print(r.stdout.strip())
    if r.returncode not in (0, 77):
        failed = True
        if r.stderr.strip():
            print(r.stderr.strip())
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
