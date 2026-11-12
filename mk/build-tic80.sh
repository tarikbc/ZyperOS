#!/bin/sh
# Build the TIC-80 shelf's core, tic80_libretro.so: TIC-80's own libretro
# build (src/system/libretro), at the v1.4.1 release.
#
# ROGUE OS runs TIC-80 on this core (its es_systems.cfg, tic-80), and ROCKNIX
# builds it from the same tree with CMake. The flags are ROCKNIX's
# (projects/ROCKNIX/packages/emulators/libretro/tic80-lr): the libretro core
# alone, no player, editors, SDL or Sokol, static. Where ROCKNIX turns most
# languages off, every one that builds here is on, so a cart in any of them
# plays: Lua, MoonScript, YueScript, Fennel, Wren, Squirrel, JavaScript
# (QuickJS), Python (pocketpy), Janet, Scheme and WebAssembly. Left out:
# Ruby (mruby's build wants Ruby and rake), Forth (TIC-80 calls it unsafe:
# no memory sandbox), Gravity (its CMake wants 3.19, bullseye has 3.18) and
# MiniScript (it does not compile with GCC 10).
#
# The source is TIC-80's git tree at the release's commit with the
# submodules those languages need, at the commits the tree records: git
# checks every object by its hash, so the commit names all of it. The build
# itself reads nothing from the network. One patch of ours,
# mk/patches/tic80-no-state.patch: the core reports no save state (the patch
# says why), as ScummVM's does, so the in-game menu offers none.
#
# In mk/cmake-cores.Dockerfile, Debian bullseye's g++ 10 and CMake.
# SOURCE_DATE_EPOCH is the release's date: TIC-80 stamps the year it was built
# into the core, and with it the core is the same file in any year. Checked as
# the other built cores are: no unversioned undefined symbol (it would not
# dlopen), then against the sha256 here.
#
# Usage: mk/build-tic80.sh [work dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-/tmp/zyperos-tic80}
PIN=b3dbd597eecfe6742f64001c2fe5ba6c1afe55d9   # nesbox/TIC-80 v1.4.1
EPOCH=1793124592                               # v1.4.1's commit time, 2026-10-27
SHA=c11a22767bece503a0e2d92c79ed59535490c6f0ea649a0951e21854a767a386
SUBMODULES='vendor/argparse vendor/blip-buf vendor/clay vendor/giflib vendor/janet
vendor/jsmn vendor/libpng vendor/lpeg vendor/lua vendor/moonscript vendor/naett
vendor/pocketpy vendor/quickjs vendor/squirrel vendor/wasm3 vendor/wren
vendor/yuescript vendor/zip vendor/zlib'

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
command -v git    >/dev/null || { echo "need git" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/cmake-cores.Dockerfile" -t zyperos-cmake-cores "$ROOT/mk" >/dev/null

SRC="$WORK/TIC-80"
mkdir -p "$WORK"
[ -d "$SRC/.git" ] || git clone -q --no-checkout https://github.com/nesbox/TIC-80.git "$SRC"
git -C "$SRC" cat-file -e "$PIN^{commit}" 2>/dev/null || git -C "$SRC" fetch -q origin "$PIN"
git -C "$SRC" -c advice.detachedHead=false checkout -q --force "$PIN"
git -C "$SRC" clean -q -dffx
[ "$(git -C "$SRC" rev-parse HEAD)" = "$PIN" ] || { echo "!! TIC-80 is not at $PIN" >&2; exit 1; }
# shellcheck disable=SC2086
git -C "$SRC" submodule update -q --init --force --depth 1 --recursive -- $SUBMODULES
git -C "$SRC" apply "$ROOT/mk/patches/tic80-no-state.patch"

docker run --rm --platform linux/arm64 -v "$SRC:/src" -e SOURCE_DATE_EPOCH="$EPOCH" \
	zyperos-cmake-cores bash -c '
set -e
mkdir -p /tmp/b && cd /tmp/b
cmake /src -DCMAKE_BUILD_TYPE=Release -DBUILD_LIBRETRO=ON \
	-DBUILD_PLAYER=OFF -DBUILD_SDL=OFF -DBUILD_SDLGPU=OFF -DBUILD_SOKOL=OFF \
	-DBUILD_EDITORS=OFF -DBUILD_SURF=OFF -DBUILD_TOOLS=OFF -DBUILD_PRO=OFF \
	-DBUILD_DEMO_CARTS=OFF -DBUILD_STATIC=ON -DBUILD_TOUCH_INPUT=OFF \
	-DBUILD_WITH_ALL=ON -DBUILD_WITH_RUBY=OFF -DBUILD_WITH_FORTH=OFF \
	-DBUILD_WITH_GRAVITY=OFF -DBUILD_WITH_MINISCRIPT=OFF -DBUILD_WITH_ZLIB=ON \
	> cmake.log 2>&1 || { tail -30 cmake.log >&2; exit 1; }
make -j3 tic80_libretro > make.log 2>&1 ||
	{ grep -E -B2 -A6 "error:|Error [0-9]|undefined reference" make.log | head -60 >&2; exit 1; }
so=bin/tic80_libretro.so
strip --strip-unneeded $so
[ "$(nm -D -u $so | grep -v " w " | grep -vc "@")" = 0 ] \
  || { nm -D -u $so | grep -v " w " | grep -v "@" >&2;
       echo "!! undefined symbols in $so - it will not dlopen" >&2; exit 1; }
cp $so /src/tic80_libretro.so
'
SO="$SRC/tic80_libretro.so"
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
if [ "$GOT" != "$SHA" ]; then
	echo "!! tic80 sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/tic80_libretro.so"
echo "  built  tic80 ($PIN)"
echo "  sha256 $GOT"
# Which patch is in it, for mk/payload.sh to hold it to (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" write tic80
