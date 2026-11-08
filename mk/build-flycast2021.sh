#!/bin/sh
# Build the Dreamcast core, flycast2021, the libretro Flycast of 2021.
#
# It is what ROGUE OS ran Crazy Taxi 2 on, smoothly, on this same handheld.
# The standalone Flycast 2.7 (Emulators/Flycast, the shelf's second core) is
# newer and heavier: its emulation thread sat at a whole core and Crazy Taxi 2
# showed about 21 frames a second, at 240 lines too (2026-11-04). flycast2021
# draws with OpenGL ES, which Diatom takes as GPU frames (its src/hw.c).
#
# The source is ROCKNIX's pin for its flycast2021-lr package, metallic77's
# fork (GPL-2.0-or-later), which builds with newer compilers than
# libretro/flycast at AmberELEC's 45bd2f4e does. One patch of ours,
# mk/patches/flycast2021-postprocess-render.patch: the fork's GLES2 build does
# not link without it (the patch says why).
#
# platform=arm64_cortex_a53_gles2, the fork's own target for a small ARM64
# device on OpenGL ES 2: LOW_END, the arm64 dynarec. A Cortex-A35 runs what
# an A53 does.
#
# Measured with this build on a Pixel 2, through ZyperOS (2026-11-04), Crazy
# Taxi 2's attract mode at the 320x240 its options set (src/db.c): full speed,
# its sound at 44.1 kHz, 25 to 28 new frames a second on the panel. At 640x480
# the Mali-G31 under Panfrost held the game to 65-75% of its speed.
#
# The result is checked before it is installed, as build-dsperate.sh does: no
# unversioned undefined symbol (it would not dlopen) but OpenGL ES's own.
#
# Usage: mk/build-flycast2021.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-/tmp/zyperos-flycast2021}
PIN=603814c9f73b773c455d9a497f389d2f93a257fd   # metallic77/flycast, ROCKNIX's flycast2021-lr pin
TAR_SHA=8aa94bdd669bab05a10ff03c8a977eccd41a30063271da3f81a7a50b1a72f4ca
SHA=fda9a598d6b3350f2ba4b226b20bebf521fb06cced0edfcaa7db0288e8a6e93a

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/flycast2021.Dockerfile" -t zyperos-flycast2021 "$ROOT/mk" >/dev/null

mkdir -p "$SRC"
TAR="$SRC/flycast-$PIN.tar.gz"
[ -f "$TAR" ] || curl -sSfL -o "$TAR" "https://github.com/metallic77/flycast/archive/$PIN.tar.gz"
GOT=$(shasum -a 256 "$TAR" | cut -d' ' -f1)
[ "$GOT" = "$TAR_SHA" ] || { echo "!! source sha256 $GOT, expected $TAR_SHA" >&2; exit 1; }
rm -rf "$SRC/flycast-$PIN"
tar -xzf "$TAR" -C "$SRC"
cd "$SRC/flycast-$PIN"
patch -s -p1 < "$ROOT/mk/patches/flycast2021-postprocess-render.patch"

docker run --rm --platform linux/arm64 -v "$SRC/flycast-$PIN:/src" -w /src \
	zyperos-flycast2021 bash -c '
set -e
make -j3 platform=arm64_cortex_a53_gles2 HAVE_OPENMP=0 GIT_VERSION=603814c >/dev/null
so=flycast_libretro.so
strip --strip-unneeded $so
# OpenGL ES functions are unversioned and come from libGLESv2, which the
# core names (NEEDED); anything else unversioned would not resolve
[ "$(nm -D -u $so | grep -v " w " | grep -v "@" | grep -vc " gl[A-Z]")" = 0 ] \
  || { echo "!! undefined symbols - the core will not dlopen" >&2; exit 1; }
'
SO=flycast_libretro.so
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! flycast2021 sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/flycast2021_libretro.so"
echo "  built  flycast2021 ($PIN)"
echo "  sha256 $GOT"
# Which patch is in it, for mk/payload.sh to hold it to (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" write flycast2021
