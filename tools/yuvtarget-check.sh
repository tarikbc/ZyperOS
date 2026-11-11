#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# A video picture's colors through PLAT_TARGET_FORMAT, with the device's own
# SDL and its GLES2 renderer (tools/yuvtarget-check.c, src/platform.h):
#
#   tools/yuvtarget-check.sh             (make yuvtarget)
#
# Not part of `make check`: it needs Docker, a few minutes and the network.
# SDL is built in a Debian container from the Pixel 2's own SDL source tree,
# with plastron's patches applied, as plastron's last build left it in its
# build volume (plastron-work, release-output-pixel2/build/sdl2-*), mounted
# read-only and copied; SDL_SRC=DIR takes a source tree on the Mac instead.
# The program runs on SDL's offscreen video and Mesa's llvmpipe through EGL:
# the same SDL renderer code and shaders as on the Pixel 2's Mali, which is
# where the swap was (SDL's choice of shader, not the GPU's). Run it again
# after SDL is upgraded in plastron, or before a render target changes format.
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
VOLUME=${PLASTRON_VOLUME:-plastron-work}
JOBS=${JOBS:-3}

if [ -n "${SDL_SRC:-}" ]; then
	src=$(cd "$SDL_SRC" && pwd)
	mount="-v $src:/sdl-src:ro"
	from=/sdl-src
else
	mount="-v $VOLUME:/work:ro"
	from='$(ls -d /work/release-output-pixel2/build/sdl2-[0-9]* | tail -n 1)'
fi

# shellcheck disable=SC2086
exec docker run --rm $mount -v "$REPO/src:/zyperos/src:ro" -v "$REPO/tools:/zyperos/tools:ro" \
	-e JOBS="$JOBS" debian:bookworm sh -c '
set -eu
apt-get update -qq >/dev/null
DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends \
	build-essential pkg-config libegl-dev libgles-dev libegl-mesa0 libgl1-mesa-dri >/dev/null
from='"$from"'
[ -d "$from" ] || { echo "yuvtarget: no SDL source tree at $from" >&2; exit 1; }
echo "SDL from $from"
cp -a "$from" /tmp/sdl && cd /tmp/sdl
make distclean >/dev/null 2>&1 || true
./configure -q --prefix=/opt/sdl --disable-video-x11 --disable-video-wayland \
	--disable-video-vulkan --disable-video-kmsdrm --enable-video-offscreen --enable-video-opengles \
	--disable-video-opengl --disable-audio --disable-joystick --disable-haptic --disable-sensor \
	--disable-hidapi >/dev/null 2>&1
make -j"$JOBS" >/dev/null 2>&1 && make install >/dev/null 2>&1
cc -O1 -Wall -o /tmp/yuvtarget-check /zyperos/tools/yuvtarget-check.c \
	$(/opt/sdl/bin/sdl2-config --cflags --libs)
LD_LIBRARY_PATH=/opt/sdl/lib SDL_VIDEODRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
	EGL_PLATFORM=surfaceless /tmp/yuvtarget-check
'
