#!/bin/sh
# Build the PICO-8 core, fake08, from jtothebell's tree.
#
# Adapted from plorpOS's mk/build-fake08.sh (https://github.com/thwonp/plorpOS
# at 5461fc8, MIT, Copyright (c) 2026 Thwonp), whose three patches it applies
# unchanged from mk/patches/.
#
# Built here because libretro's buildbot has no aarch64 fake08 (404 on
# 2026-11-03), and because without the three patches save states and resume
# do not work in Diatom. Thwonp's reasons, which hold for Diatom as they do
# for plorpOS's diatom:
#
#   fake08-load.patch - upstream's retro_load_game only queues the cart and
#   loads it on the first retro_run, but Diatom restores the autosave before
#   that first frame. A reused resident core crashed on the restore, a fresh
#   one had the late cart load wipe it. The patch loads and starts the cart
#   inside retro_load_game; a cart that will not load refuses the game. It
#   also keeps a copy of the cart for retro_reset, which reloaded by file
#   name and fell into the fake08 shell for a cart loaded from memory.
#
#   fake08-states.patch - a 2024 debugging commit (47c48ad) disabled eris's
#   init_persist_all, so every state silently saved no Lua at all. Re-enabled,
#   the restore corrupted the heap (eris hooks half-built protos into objects
#   the incremental GC has already marked); the patch holds the GC for the
#   restore and then runs a full collection. It also points the registry's
#   __PICO8_SANDBOX at the restored sandbox, so a resumed cart in a reused
#   core runs its own _update60 and not the previous cart's.
#
#   fake08-bound-lua.patch - the Lua state was copied into a fixed 1 MB
#   buffer with no bound. The budget is PICO-8's 2 MB; a state that does not
#   fit fails the save before anything is written, and a load checks every
#   size the state claims before restoring any of it.
#
# The pin is upstream master on 2026-11-03, unchanged since 2026-06-13 and
# the commit ROCKNIX ships. z8lua is a submodule.
#
# The build is native arm64 g++ 10.2 in mk/fake08.Dockerfile, so the bytes
# are the same on any host; SHA below is what it made on 2026-11-03.
#
# Run in a linux/arm64 container under Diatom's desktop port, 2026-11-03:
# Celeste Classic 2, POOM and Porklike at 60 fps, a state saved on stop and
# resumed after another cart in the same resident core. Resident it costs
# about 1 to 2 MB, flat over eight loads of two carts.
#
# Needs: docker that can run linux/arm64 containers.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${1:-/tmp/zyperos-fake08}
PIN=814991a2571ad3970e386cef48f3b148aa1c27b9   # jtothebell/fake-08 master, 2026-06-13
SHA=663747345933a1e87d2e8ad2af82b269f0988ef48e72589b5648662fd11b50da

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
docker build --platform linux/arm64 -q -f "$ROOT/mk/fake08.Dockerfile" -t zyperos-fake08 "$ROOT/mk" >/dev/null

if [ ! -d "$SRC/.git" ]; then
	git clone -q https://github.com/jtothebell/fake-08.git "$SRC"
fi
cd "$SRC"
git fetch -q origin "$PIN" 2>/dev/null || git fetch -q origin
git checkout -q -f "$PIN"
git submodule update -q --init --recursive
git apply "$ROOT/mk/patches/fake08-load.patch"
git apply "$ROOT/mk/patches/fake08-states.patch"
git apply "$ROOT/mk/patches/fake08-bound-lua.patch"
EPOCH=$(git log -1 --format=%ct)

# Flags through the environment: the Makefile appends to CFLAGS and CXXFLAGS.
# -j3, not nproc: the build shares its host.
H="-D_FORTIFY_SOURCE=2 -fstack-protector-strong"
docker run --rm --platform linux/arm64 -v "$SRC:/src" -w /src/platform/libretro \
	-e SOURCE_DATE_EPOCH="$EPOCH" -e CFLAGS="$H" -e CXXFLAGS="$H" \
	zyperos-fake08 bash -c '
set -e
make clean >/dev/null 2>&1 || true
make platform=unix -j3 >/dev/null
so=fake08_libretro.so
strip --strip-unneeded $so
# Never ship one that cannot load: an unversioned undefined symbol cannot
# resolve at dlopen.
[ "$(nm -D -u $so | grep -v " w " | grep -vc "@")" = 0 ] \
  || { echo "!! undefined symbols - the core will not dlopen" >&2; exit 1; }
[ "$(objdump -T $so | grep -cE "__stack_chk|_chk$")" -ge 9 ] \
  || { echo "!! not hardened" >&2; exit 1; }
'
SO=platform/libretro/fake08_libretro.so
GOT=$(shasum -a 256 "$SO" | cut -d' ' -f1)
git checkout -q -- .   # leave the clone clean for the next run
if [ "$GOT" != "$SHA" ]; then
	echo "!! fake08 sha256 $GOT, expected $SHA" >&2
	echo "  The toolchain image or the source changed. Verify the core, then" >&2
	echo "  update SHA here." >&2
	exit 1
fi
mkdir -p "$ROOT/vendor/cores"
cp "$SO" "$ROOT/vendor/cores/"
echo "  built  fake08 ($PIN)"
echo "  sha256 $GOT"
# Which patches are in it, for mk/payload.sh to hold it to (mk/core-stamp.sh)
sh "$ROOT/mk/core-stamp.sh" write fake08
