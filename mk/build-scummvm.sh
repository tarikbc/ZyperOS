#!/bin/sh
# Build the ScummVM core, scummvm_libretro.so, and the data files it reads
# from Bios/scummvm/ (engine data and the GUI theme).
#
# ScummVM's own libretro backend (backends/platform/libretro, in ScummVM's
# tree since 2023), at the 2026.4.0 release. libretro's buildbot has an
# aarch64 Linux build, but one that links desktop OpenGL, which the devices
# do not have (Panfrost: OpenGL ES alone), so it is built here without any:
# FORCE_OPENGLNONE, the backend's switch for a software-only core. Every 2D
# engine draws in software anyway; the 3D ones (Grim, Myst III and the like)
# are left out by the backend's own engine configuration when there is no GL,
# and would not run at speed on an RK3326 if they were in.
#
# The backend's Makefile clones its two dependencies (libretro-deps: zlib,
# libpng, FreeType, the audio decoders; libretro-common) at commits it pins.
# Here they come as archives of those same commits, checked by sha256 like
# ScummVM's own, and DEPS_SUBMODULES= tells the Makefile not to clone: the
# build reads nothing from the network.
#
# The core links none of the system's libraries but glibc and libstdc++,
# and the image is bullseye's g++ 10, mk/scummvm.Dockerfile (flycast2021's
# digest and snapshot plus zip), so it asks plastron for no newer symbols than
# the other cores do. Checked as build-small-cores.sh checks its cores: no
# unversioned undefined symbol (it would not dlopen) and no libGL. Then
# against the sha256 here, so a changed toolchain or source cannot slip a
# different binary into vendor/cores.
#
#   vendor/cores/scummvm_libretro.so
#   vendor/scummvm/extra, vendor/scummvm/theme   the bundle ScummVM builds
#                                                (make datafiles), unpacked;
#                                                mk/payload.sh ships it and
#                                                launch.sh puts it in Bios/
#
# Usage: mk/build-scummvm.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-/tmp/zyperos-scummvm}

PIN=7d2709da5136866589f7602f5f5a7b17de03420e      # scummvm/scummvm v2026.4.0
TAR_SHA=99c7de379c652a913fa5ab2407086f71cdd015b0f87b2f18af253c83397e0907
# backends/platform/libretro/dependencies.mk's own pins, at that tag
DEPS_PIN=bab7d258c451c0e7cba4b6a79f1b062c13efff38   # libretro/libretro-deps
DEPS_SHA=5adffd4f94d21d7efb6c6a224e34f1921faeda3f46083f36d158493dd4c952dd
COMMON_PIN=879c8d507b0b52e77e27d759239c2b5df1e26dfd # libretro/libretro-common
COMMON_SHA=b464f796ff9752f9fb53c4ced9a0346e428fdbf7786032abac79d106d7170fcd
SHA=93b10ce060eca3198528d4a53b4b78557ef57f9dd7ed7712eae30a123ba16226
# The data files' contents, not the zip's: the bundle stamps each entry with
# the time it was zipped. The sha256 of `shasum -a 256` over every file of the
# unpacked scummvm/ folder, in byte order of their paths.
DATA_SHA=9963089b5a25d85d352e59aec90cfd7f9d6d286576a545106189bf057d797251

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v curl   >/dev/null || { echo "need curl" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/scummvm.Dockerfile" -t zyperos-scummvm "$ROOT/mk" >/dev/null
mkdir -p "$WORK"

# fetch REPO COMMIT SHA256: the archive of that commit, checked
fetch() {
	tar="$WORK/${1#*/}-$2.tar.gz"
	[ -f "$tar" ] || curl -sSfL -o "$tar" "https://github.com/$1/archive/$2.tar.gz"
	got=$(shasum -a 256 "$tar" | cut -d' ' -f1)
	[ "$got" = "$3" ] || { echo "!! $1 source sha256 $got, expected $3" >&2; exit 1; }
}
fetch scummvm/scummvm "$PIN" "$TAR_SHA"
fetch libretro/libretro-deps "$DEPS_PIN" "$DEPS_SHA"
fetch libretro/libretro-common "$COMMON_PIN" "$COMMON_SHA"

SRC="$WORK/scummvm-$PIN"
rm -rf "$SRC"
tar -xzf "$WORK/scummvm-$PIN.tar.gz" -C "$WORK"
DEPS="$SRC/backends/platform/libretro/deps"
mkdir -p "$DEPS"
tar -xzf "$WORK/libretro-deps-$DEPS_PIN.tar.gz" -C "$DEPS"
tar -xzf "$WORK/libretro-common-$COMMON_PIN.tar.gz" -C "$DEPS"
mv "$DEPS/libretro-deps-$DEPS_PIN" "$DEPS/libretro-deps"
mv "$DEPS/libretro-common-$COMMON_PIN" "$DEPS/libretro-common"

docker run --rm --platform linux/arm64 -v "$SRC:/src" -w /src/backends/platform/libretro \
	zyperos-scummvm bash -c '
set -e
ARGS="platform=unix FORCE_OPENGLNONE=1 DEPS_SUBMODULES="
make -j6 $ARGS > build.log 2>&1 || { tail -40 build.log >&2; exit 1; }
make $ARGS datafiles > data.log 2>&1 || { tail -20 data.log >&2; exit 1; }
so=scummvm_libretro.so
strip --strip-unneeded $so
[ "$(nm -D -u $so | grep -v " w " | grep -vc "@")" = 0 ] \
  || { nm -D -u $so | grep -v " w " | grep -v "@" >&2;
       echo "!! undefined symbols in $so - it will not dlopen" >&2; exit 1; }
! readelf -d $so | grep -q "libGL" \
  || { echo "!! $so links OpenGL" >&2; exit 1; }
'
SO="$SRC/backends/platform/libretro/scummvm_libretro.so"
ZIP="$SRC/backends/platform/libretro/scummvm.zip"
rm -rf "$WORK/data"
mkdir -p "$WORK/data"
( cd "$WORK/data" && unzip -q "$ZIP" )
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
GOT_DATA=$(cd "$WORK/data/scummvm" && find . -type f | LC_ALL=C sort |
	while IFS= read -r f; do shasum -a 256 "$f"; done | shasum -a 256 | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ] || [ "$GOT_DATA" != "$DATA_SHA" ]; then
	echo "!! scummvm sha256 $GOT, expected $SHA" >&2
	echo "!! scummvm data sha256 $GOT_DATA, expected $DATA_SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA and DATA_SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/scummvm_libretro.so"
rm -rf "$ROOT/vendor/scummvm"
mv "$WORK/data/scummvm" "$ROOT/vendor/scummvm"
echo "$PIN" > "$ROOT/vendor/scummvm/.pin"
echo "  built  scummvm ($PIN)"
echo "  sha256 $GOT"
