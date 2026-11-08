#!/bin/sh
# Build the Nintendo 64 core, mupen64plus_next (libretro's Mupen64Plus-Next,
# GLideN64 for the picture), for OpenGL ES 3.
#
# libretro's buildbot has an aarch64 Linux build of it, but for desktop
# OpenGL, which Diatom does not give a core: its GPU frames are OpenGL ES
# (src/hw.c), as Panfrost's are on the Mali-G31. So it is built here, as
# ROCKNIX builds it for the same RK3326 (its mupen64plus-nx-lr package, GLES3
# and libGLESv2), from the commit ROCKNIX pins.
#
# platform=arm64_cortex_a53_gles3, the core's own target for a small ARM64
# device on OpenGL ES 3: the aarch64 dynarec, GLideN64 on GLES 3. A Cortex-A35
# runs what an A53 does. HLE RSP (no paraLLEl, which wants Vulkan).
#
# One patch of ours, mk/patches/mupen64plus-next-new-frame-swap.patch:
# GLideN64 hands over a frame only when there is a new picture, not at every
# vertical interrupt (the patch says why).
#
# In the image mk/build-flycast2021.sh builds the Dreamcast core in
# (mk/flycast2021.Dockerfile): Debian bullseye's GCC 10, libGLESv2 to link
# against and zlib, which is all this core needs too.
#
# The result is checked before it is installed, as build-flycast2021.sh does:
# no unversioned undefined symbol (it would not dlopen) but OpenGL ES's and
# EGL's own.
#
# Usage: mk/build-mupen64plus-next.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-/tmp/zyperos-mupen64plus-next}
PIN=f275caf4b2bfa1e6d1c51636746ea793f3d80320   # libretro/mupen64plus-libretro-nx, ROCKNIX's mupen64plus-nx-lr pin
TAR_SHA=1810b7bbdc4abfdeee8a9f7f99c4a91dab601a228935802317c25a43d7cf9dbb
SHA=4d54a1878e98a4d2ea6ca3795762d3994a772440e2ea44558f1594913d156b45

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/flycast2021.Dockerfile" -t zyperos-flycast2021 "$ROOT/mk" >/dev/null

mkdir -p "$SRC"
TAR="$SRC/mupen64plus-libretro-nx-$PIN.tar.gz"
[ -f "$TAR" ] || curl -sSfL -o "$TAR" "https://github.com/libretro/mupen64plus-libretro-nx/archive/$PIN.tar.gz"
GOT=$(shasum -a 256 "$TAR" | cut -d' ' -f1)
[ "$GOT" = "$TAR_SHA" ] || { echo "!! source sha256 $GOT, expected $TAR_SHA" >&2; exit 1; }
rm -rf "$SRC/mupen64plus-libretro-nx-$PIN"
tar -xzf "$TAR" -C "$SRC"
(cd "$SRC/mupen64plus-libretro-nx-$PIN" &&
 patch -s -p1 < "$ROOT/mk/patches/mupen64plus-next-new-frame-swap.patch")

docker run --rm --platform linux/arm64 -v "$SRC/mupen64plus-libretro-nx-$PIN:/src" -w /src \
	zyperos-flycast2021 bash -c '
set -e
make -j3 platform=arm64_cortex_a53_gles3 GIT_VERSION=" f275caf" >/dev/null
so=mupen64plus_next_libretro.so
strip --strip-unneeded $so
# OpenGL ES and EGL functions are unversioned and come from libGLESv2 and
# libEGL, which the core names (NEEDED: GLideN64 looks its GL functions up
# with eglGetProcAddress); anything else unversioned would not resolve
[ "$(nm -D -u $so | grep -v " w " | grep -v "@" | grep -vc " e\{0,1\}gl[A-Z]")" = 0 ] \
  || { nm -D -u $so | grep -v " w " | grep -v "@" | grep -v " e\{0,1\}gl[A-Z]" >&2;
       echo "!! undefined symbols - the core will not dlopen" >&2; exit 1; }
'
SO="$SRC/mupen64plus-libretro-nx-$PIN/mupen64plus_next_libretro.so"
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! mupen64plus_next sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/mupen64plus_next_libretro.so"
echo "  built  mupen64plus_next ($PIN)"
echo "  sha256 $GOT"
# Which patch is in it, for mk/payload.sh to hold it to (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" write mupen64plus_next
