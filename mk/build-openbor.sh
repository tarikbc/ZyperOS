#!/bin/sh
# Build OpenBOR, the beat-'em-up engine the OpenBOR shelf runs. OpenBOR has no
# libretro core, so it is a program of its own, run from the shelf by
# sd/pixel2/OpenBOR.sh as DraStic is run by its script, and shipped in
# ZyperOS/openbor (mk/payload.sh).
#
# DCurrent/openbor at b8303cc, ROCKNIX's pin for the same RK3326 (its
# standalone openbor package, whose sha256 for the archive is this one too),
# BSD-3-Clause. Built as ROCKNIX builds it: the engine Makefile's Linux target
# for ARM (BUILD_LINUX_LE_arm: SDL2, its own 2D scalers, Ogg Vorbis, WebM
# video, no OpenGL), with -Werror taken out of the Makefile as ROCKNIX takes it
# out (a newer gcc warns where OpenBOR's own did not). The version header is
# what version.sh writes from a git checkout, given the commit's build number
# (7144, the commit count at b8303cc) and hash here instead.
#
# In mk/openbor.Dockerfile: bullseye's gcc 10 on arm64, flycast2021's digest
# and snapshot. OpenBOR links SDL2, libpng, zlib, libogg and libvorbis from
# the device (plastron has each, under the same soname), and libvpx, which
# plastron does not have, in whole: a folder holding only libvpx.a comes first
# on the link's search path. Checked before it is installed: it needs no
# library plastron lacks, and asks for no glibc newer than 2.29, as the cores
# ask. Then against the sha256 here.
#
# Usage: mk/build-openbor.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-/tmp/zyperos-openbor}
PIN=b8303cce992a0db93c3a465df3c943942fe322f8   # DCurrent/openbor, ROCKNIX's openbor pin
TAR_SHA=8a21e40b31bf04fd5212d1bb86452f1843fcd0e8a54d073e474e51f4fc130973
BUILD_NO=7144
SHA=eead6978c04354419aefeadff3e36678b0ba7d7b04a5a7983836a5b308d18603

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/openbor.Dockerfile" -t zyperos-openbor "$ROOT/mk" >/dev/null

mkdir -p "$WORK"
TAR="$WORK/openbor-$PIN.tar.gz"
[ -f "$TAR" ] || curl -sSfL -o "$TAR" "https://github.com/DCurrent/openbor/archive/$PIN.tar.gz"
GOT=$(shasum -a 256 "$TAR" | cut -d' ' -f1)
[ "$GOT" = "$TAR_SHA" ] || { echo "!! source sha256 $GOT, expected $TAR_SHA" >&2; exit 1; }
SRC="$WORK/openbor-$PIN"
rm -rf "$SRC"
tar -xzf "$TAR" -C "$WORK"

docker run --rm --platform linux/arm64 -v "$SRC:/src" -w /src/engine \
	-e BUILD_NO="$BUILD_NO" -e COMMIT="$(echo $PIN | cut -c1-7)" zyperos-openbor bash -c '
set -e
sed -i "s|-Werror||g" Makefile
printf "%s\n%s\n" "$BUILD_NO" "$COMMIT" | LC_ALL=C bash version.sh > /dev/null 2>&1
mkdir -p /static && ln -sf /usr/lib/aarch64-linux-gnu/libvpx.a /static/libvpx.a
make -j6 BUILD_LINUX_LE_arm=1 SDKPATH= LNXDEV=/usr/bin PREFIX= CC="gcc -L/static" > build.log 2>&1 \
  || { tail -30 build.log >&2; exit 1; }
for lib in $(readelf -d OpenBOR | sed -n "s/.*Shared library: \[\(.*\)\]/\1/p"); do
	case $lib in
	libSDL2-2.0.so.0|libpng16.so.16|libz.so.1|libogg.so.0|libvorbis.so.0|libvorbisfile.so.3|libm.so.6|libpthread.so.0|libc.so.6|ld-linux-aarch64.so.1) ;;
	*) echo "!! OpenBOR needs $lib, which plastron does not have" >&2; exit 1 ;;
	esac
done
# What travels with the binary, as BSD-3-Clause asks of OpenBOR and of
# the libvpx linked into it: their notices
cp /usr/share/doc/libvpx-dev/copyright LICENSE.libvpx
newest=$(nm -D -u OpenBOR | grep -o "GLIBC_[0-9.]*" | sort -V | tail -1)
[ "$(printf "%s\nGLIBC_2.29\n" "$newest" | sort -V | tail -1)" = GLIBC_2.29 ] \
  || { echo "!! OpenBOR asks for $newest" >&2; exit 1; }
'
BIN="$SRC/engine/OpenBOR"
GOT=$(shasum -a 256 "$BIN" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! OpenBOR sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the program, then" >&2
	echo "  update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/openbor"
cp "$BIN" "$ROOT/vendor/openbor/OpenBOR"
cp "$SRC/LICENSE" "$ROOT/vendor/openbor/LICENSE"
cp "$SRC/engine/LICENSE.libvpx" "$ROOT/vendor/openbor/LICENSE.libvpx"
echo "  built  OpenBOR ($PIN, build $BUILD_NO)"
echo "  sha256 $GOT"
