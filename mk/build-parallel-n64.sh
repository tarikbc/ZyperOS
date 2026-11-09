#!/bin/sh
# Build the Nintendo 64's default core, parallel_n64 (libretro's ParallelN64,
# Glide64 for the picture), for OpenGL ES 2.
#
# ROGUE OS runs the N64 on it, in RetroArch with Glide64 at medium accuracy
# and its "fullspeed" frame rate, and Super Mario 64 and Mario Kart 64 ran
# smoothly there. Under Diatom on the Pixel 2 they ran at their full speed
# too, where Mupen64Plus-Next's GLideN64 crashed Mesa's panfrost at that
# speed (config/systems.cfg says more). This is the commit ROGUE's core says
# it is ("2.0-rc2 334998e").
#
# libretro's buildbot has an aarch64 Linux build, but for desktop OpenGL,
# which Diatom does not give a core (its GPU frames are OpenGL ES, src/hw.c).
# So it is built here: platform=unix-gles (GLES 2, libGLESv2), with the
# CPUFLAGS of the core's own armv8 target, which that target cannot be asked
# for directly: it turns GLES off. -DCLASSIC makes 320x240 the first
# resolution, the N64's own. The aarch64 dynarec comes from the machine the
# image runs on (uname -m).
#
# In the image mk/build-flycast2021.sh builds the Dreamcast core in
# (mk/flycast2021.Dockerfile): Debian bullseye's GCC 10 and libGLESv2.
#
# The result is checked before it is installed, as build-flycast2021.sh does:
# no unversioned undefined symbol (it would not dlopen) but OpenGL ES's own.
# No patch of ours; mk/core-stamp.sh stamps it all the same, so a core built
# from another commit is never put on a card.
#
# Usage: mk/build-parallel-n64.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-/tmp/zyperos-parallel-n64}
PIN=334998e6129debe50f7ef9c5cd1e995460ae2da8   # libretro/parallel-n64, ROGUE OS's "2.0-rc2 334998e"
TAR_SHA=8fa8b47f718c8f8d003108c22c67b906995c8315aa9dd385eb413bb70b68d7d7
SHA=ecbf976ef2c1c0b88ab92ed8cc8f2c5cf4afc3293ed6ba27559be5a621bcbaba

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/flycast2021.Dockerfile" -t zyperos-flycast2021 "$ROOT/mk" >/dev/null

mkdir -p "$SRC"
TAR="$SRC/parallel-n64-$PIN.tar.gz"
[ -f "$TAR" ] || curl -sSfL -o "$TAR" "https://github.com/libretro/parallel-n64/archive/$PIN.tar.gz"
GOT=$(shasum -a 256 "$TAR" | cut -d' ' -f1)
[ "$GOT" = "$TAR_SHA" ] || { echo "!! source sha256 $GOT, expected $TAR_SHA" >&2; exit 1; }
rm -rf "$SRC/parallel-n64-$PIN"
tar -xzf "$TAR" -C "$SRC"

docker run --rm --platform linux/arm64 -v "$SRC/parallel-n64-$PIN:/src" -w /src \
	zyperos-flycast2021 bash -c '
set -e
make -j3 platform=unix-gles GIT_VERSION=" 334998e" \
	CPUFLAGS="-DNO_ASM -DARM -DARM_ASM -DDONT_WANT_ARM_OPTIMIZATIONS -DARM_FIX -DCLASSIC -DARM64 -march=armv8-a+crc" >/dev/null 2>&1
so=parallel_n64_libretro.so
strip --strip-unneeded $so
# OpenGL ES functions are unversioned and come from libGLESv2, which the
# core names (NEEDED); anything else unversioned would not resolve
[ "$(nm -D -u $so | grep -v " w " | grep -v "@" | grep -vc " gl[A-Z]")" = 0 ] \
  || { nm -D -u $so | grep -v " w " | grep -v "@" | grep -v " gl[A-Z]" >&2;
       echo "!! undefined symbols - the core will not dlopen" >&2; exit 1; }
'
SO="$SRC/parallel-n64-$PIN/parallel_n64_libretro.so"
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! parallel_n64 sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/parallel_n64_libretro.so"
echo "  built  parallel_n64 ($PIN)"
echo "  sha256 $GOT"
# Which commit it is, for mk/payload.sh to hold it to (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" write parallel_n64
