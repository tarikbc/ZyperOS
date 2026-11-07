#!/bin/sh
# Build the Sega Saturn core, yabasanshiro_libretro.so: YabaSanshiro, the
# Saturn emulator with an ARM64 dynarec for its two SH-2s and an OpenGL ES 3
# renderer, which is what makes the Saturn run on an RK3326 at all. Beetle
# Saturn, the other libretro Saturn core, interprets everything and wants a
# desktop CPU several times this one's speed. Both need the Saturn's BIOS:
# this YabaSanshiro has a high-level one, but refuses to start a disc
# without the real file (config/systems.cfg).
#
# libretro/yabause at 39535a6 (its yabasanshiro branch, 2024-05-23, gcc 14
# fixes), ROCKNIX's yabasanshiro-lr pin, GPL-2.0-or-later. libretro's buildbot
# builds it for aarch64 with desktop OpenGL, which the devices do not have, so
# it is built here for OpenGL ES 3, drawn in Diatom's GPU frames as the N64
# core is: platform=arm64_cortex_a53_gles3, the core's own target for a small
# ARM64 device (a Cortex-A35 runs what an A53 does), with the aarch64 dynarec
# (USE_AARCH64_DRC, DYNAREC_DEVMIYAX). ROCKNIX's speed flags too: -Ofast
# -ffast-math where the Makefile says -O3, link-time optimization, and no
# outline atomics (gcc 10's default on aarch64, a call per atomic).
#
# In mk/flycast2021.Dockerfile: bullseye's gcc 10 on arm64, libGLESv2 to link
# against. Checked as build-mupen64plus-next.sh checks its core: no unversioned
# undefined symbol but OpenGL ES's and EGL's own. Then against the sha256 here.
#
# Usage: mk/build-yabasanshiro.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-/tmp/zyperos-yabasanshiro}
PIN=39535a6abcad5abf9f71c8b2a7975f005ee12ed6   # libretro/yabause, ROCKNIX's yabasanshiro-lr pin
TAR_SHA=56aa4dc17f1c9e61e60274a66c71b33371786688df001ac0ca56569d94cc8b3b
SHA=94fb651ee18711d862d58e26c6403bd6106f0223932a2a30e7229f7142326f71

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/flycast2021.Dockerfile" -t zyperos-flycast2021 "$ROOT/mk" >/dev/null

mkdir -p "$SRC"
TAR="$SRC/yabause-$PIN.tar.gz"
[ -f "$TAR" ] || curl -sSfL -o "$TAR" "https://github.com/libretro/yabause/archive/$PIN.tar.gz"
GOT=$(shasum -a 256 "$TAR" | cut -d' ' -f1)
[ "$GOT" = "$TAR_SHA" ] || { echo "!! source sha256 $GOT, expected $TAR_SHA" >&2; exit 1; }
rm -rf "$SRC/yabause-$PIN"
tar -xzf "$TAR" -C "$SRC"

docker run --rm --platform linux/arm64 -v "$SRC/yabause-$PIN:/src" -w /src/yabause/src/libretro \
	zyperos-flycast2021 bash -c '
set -e
sed -i "s/-O[23]/-Ofast -ffast-math/" Makefile
export CFLAGS="-flto -fipa-pta -mno-outline-atomics"
export CXXFLAGS="$CFLAGS" LDFLAGS="-flto -fipa-pta"
make -j6 platform=arm64_cortex_a53_gles3 > build.log 2>&1 || { tail -30 build.log >&2; exit 1; }
so=yabasanshiro_libretro.so
strip --strip-unneeded $so
[ "$(nm -D -u $so | grep -v " w " | grep -v "@" | grep -vc " e\{0,1\}gl[A-Z]")" = 0 ] \
  || { nm -D -u $so | grep -v " w " | grep -v "@" | grep -v " e\{0,1\}gl[A-Z]" >&2;
       echo "!! undefined symbols - the core will not dlopen" >&2; exit 1; }
! readelf -d $so | grep -q "libGL\." \
  || { echo "!! $so links desktop OpenGL" >&2; exit 1; }
'
SO="$SRC/yabause-$PIN/yabause/src/libretro/yabasanshiro_libretro.so"
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! yabasanshiro sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/yabasanshiro_libretro.so"
echo "  built  yabasanshiro ($PIN)"
echo "  sha256 $GOT"
