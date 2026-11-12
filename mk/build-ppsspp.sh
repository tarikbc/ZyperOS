#!/bin/sh
# Build the PSP shelf's core, ppsspp_libretro.so, and the files it reads from
# Bios/PPSSPP (PPSSPP's assets/: the PSP's fonts in flash0, the VFPU tables,
# the game fixes in compat.ini, the system dialogs' languages and atlas).
#
# PPSSPP v1.20.4, the release ROCKNIX's libretro core (ppsspp-lr) and ArkOS's
# standalone PPSSPP pin, and the base Deadstation Portable (the Brick's PSP
# emulator) is cut from, so its speedups can come over as patches later.
# Every RK3326 system runs the PSP on PPSSPP; ROGUE OS, ROCKNIX, Knulli and
# ArkOS each run it standalone by default. ZyperOS runs its libretro core in
# Diatom, which gives a core OpenGL ES in its GPU frames (diatom/src/hw.c).
# The notes on the four systems are in this change's PR.
#
# The flags are ROCKNIX's ppsspp-lr OpenGL ES set without its Vulkan and
# Wayland (the Pixel 2 has neither in Diatom): USING_GLES2, the one OpenGL ES
# build PPSSPP has (it uses OpenGL ES 3 when the context is 3.x, and Mesa's
# Mali is allowed it at this release, Common/GPU/OpenGL/GLFeatures.cpp),
# FBDEV without EGL, the libretro core. And ROCKNIX's two build patches as
# compiler flags: -Ofast -fno-tree-slp-vectorize, -mno-outline-atomics (the
# Cortex-A35 has no LSE atomics, so the inline LL/SC loops are what it runs
# anyway), with the RK3326's CPU, -mcpu=cortex-a35. FFmpeg (the PSP's movies
# and some of its sound) is the static aarch64 build in PPSSPP's own ffmpeg
# submodule, as Knulli and ArkOS link it: plastron has no FFmpeg.
#
# The source is PPSSPP's git tree at the release's commit with its
# submodules, at the commits the tree records: git checks every object by its
# hash, so the commit names all of it. The build itself reads nothing from
# the network.
#
# In mk/cmake-cores.Dockerfile, Debian bullseye's g++ 10 and CMake. Checked
# as the other OpenGL ES cores are: no unversioned undefined symbol but
# OpenGL ES's and EGL's own (it would not dlopen), then against the sha256s
# here, the core's and the assets' (every file's sha256, in path order).
#
#   vendor/cores/ppsspp_libretro.so
#   vendor/ppsspp/                  assets/, as PPSSPP ships it; mk/payload.sh
#                                   ships it as ZyperOS/ppsspp and launch.sh
#                                   puts it in Bios/PPSSPP
#
# Usage: mk/build-ppsspp.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-/tmp/zyperos-ppsspp}
PIN=fa50bb1976065c4f8b1b47af227d367fe9771555   # hrydgard/ppsspp v1.20.4
SHA=PENDING
DATA_SHA=PENDING
# What the libretro core builds from. Left out: the test suite, Discord's
# and UPnP's (both off), macOS's SDL, the web debugger's page, Android's
# Adreno tools, OpenXR and MoltenVK
SUBMODULES='libretro/libretro-common ffmpeg ext/armips ext/glslang ext/SPIRV-Cross
ext/rapidjson ext/zstd ext/cpu_features ext/rcheevos ext/naett ext/libchdr
ext/lua ext/freetype ext/nanosvg ext/aemu_postoffice'

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v git    >/dev/null || { echo "need git" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/cmake-cores.Dockerfile" -t zyperos-cmake-cores "$ROOT/mk" >/dev/null

SRC="$WORK/ppsspp"
mkdir -p "$WORK"
[ -d "$SRC/.git" ] || git clone -q --no-checkout https://github.com/hrydgard/ppsspp.git "$SRC"
git -C "$SRC" cat-file -e "$PIN^{commit}" 2>/dev/null || git -C "$SRC" fetch -q origin "$PIN"
git -C "$SRC" -c advice.detachedHead=false checkout -q --force "$PIN"
git -C "$SRC" clean -q -dffx
[ "$(git -C "$SRC" rev-parse HEAD)" = "$PIN" ] || { echo "!! PPSSPP is not at $PIN" >&2; exit 1; }
# shellcheck disable=SC2086
git -C "$SRC" submodule update -q --init --force --depth 1 --recursive -- $SUBMODULES

docker run --rm --platform linux/arm64 -v "$SRC:/src" zyperos-cmake-cores bash -c '
set -e
FLAGS="-Ofast -fno-tree-slp-vectorize -mno-outline-atomics -mcpu=cortex-a35"
mkdir -p /tmp/b && cd /tmp/b
cmake /src -DCMAKE_BUILD_TYPE=Release -DLIBRETRO=ON -DFORCED_CPU=aarch64 \
	-DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS" \
	-DUSING_GLES2=ON -DUSING_FBDEV=ON -DUSING_EGL=OFF \
	-DVULKAN=OFF -DUSE_VULKAN_DISPLAY_KHR=OFF -DUSING_X11_VULKAN=OFF -DUSE_WAYLAND_WSI=OFF \
	-DUSE_FFMPEG=ON -DUSE_SYSTEM_FFMPEG=OFF -DBUILD_SHARED_LIBS=OFF \
	-DANDROID=OFF -DWIN32=OFF -DAPPLE=OFF -DUSING_QT_UI=OFF -DUNITTEST=OFF \
	-DSIMULATOR=OFF -DHEADLESS=OFF -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF \
	> cmake.log 2>&1 || { tail -30 cmake.log >&2; exit 1; }
make -j3 ppsspp_libretro > make.log 2>&1 || { grep -i -B2 -A4 "error" make.log | head -40 >&2; exit 1; }
so=$(find . -name ppsspp_libretro.so | head -1)
strip --strip-unneeded $so
# OpenGL ES and EGL functions are unversioned and come from libGLESv2 and
# libEGL, which the core names (NEEDED); anything else would not resolve
[ "$(nm -D -u $so | grep -v " w " | grep -v "@" | grep -v " gl[A-Z]" | grep -vc " egl[A-Z]")" = 0 ] \
  || { nm -D -u $so | grep -v " w " | grep -v "@" | grep -v " gl[A-Z]" | grep -v " egl[A-Z]" >&2;
       echo "!! undefined symbols - the core will not dlopen" >&2; exit 1; }
! readelf -d $so | grep -q "libGL\." || { echo "!! $so links desktop OpenGL" >&2; exit 1; }
cp $so /src/ppsspp_libretro.so
'
SO="$SRC/ppsspp_libretro.so"
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
GOT_DATA=$(cd "$SRC/assets" && find . -type f ! -name .git | LC_ALL=C sort |
	while IFS= read -r f; do shasum -a 256 "$f"; done | shasum -a 256 | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ] || [ "$GOT_DATA" != "$DATA_SHA" ]; then
	echo "!! ppsspp sha256 $GOT, expected $SHA" >&2
	echo "!! ppsspp assets sha256 $GOT_DATA, expected $DATA_SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA and DATA_SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/ppsspp_libretro.so"
rm -rf "$ROOT/vendor/ppsspp"
mkdir -p "$ROOT/vendor/ppsspp"
( cd "$SRC/assets" && find . -type f ! -name .git | LC_ALL=C sort |
	while IFS= read -r f; do mkdir -p "$ROOT/vendor/ppsspp/${f%/*}"; cp "$f" "$ROOT/vendor/ppsspp/$f"; done )
echo "$PIN" > "$ROOT/vendor/ppsspp/.pin"
echo "  built  ppsspp ($PIN)"
echo "  sha256 $GOT"
