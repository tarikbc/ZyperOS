#!/bin/sh
# The idle-loop yield in a built DSperate core: tools/dsperate-yield-check.c
# loads the core as Diatom does, with the recompiler on, boots a ROM it
# writes itself in which the ARM9 polls a word the ARM7 sets after a delay
# and both CPUs then count VBlanks by polling DISPSTAT (the ARM9 in ARM code,
# the ARM7 in Thumb), and runs it with dsperate_idle_yield disabled and
# enabled: each poll still sees each change, the counts are the same, and
# the frames cost no more with the yield (mk/patches/dsperate-lr-idle-yield.patch).
#
#   sh mk/dsperate-yield-check.sh [core.so]    default vendor/cores/dsperate_libretro.so
#
# mk/build-dsperate.sh runs it on each core it builds, before installing it.
# Compiled with the build's own image (dsperate-lr-cross) and run in
# ubuntu:24.04 for arm64, as mk/dsperate-rtc-check.sh is. A few seconds.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SO=${1:-$ROOT/vendor/cores/dsperate_libretro.so}
IMAGE=dsperate-lr-cross

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -f "$SO" ] || { echo "no core at $SO" >&2; exit 1; }
docker image inspect $IMAGE >/dev/null 2>&1 ||
	{ echo "no $IMAGE image: mk/build-dsperate.sh builds it" >&2; exit 1; }
W=$(mktemp -d "${TMPDIR:-/tmp}/dsperate-yield.XXXXXX")
trap 'rm -rf "$W"' EXIT
cp "$SO" "$W/core.so"
docker run --rm --platform linux/amd64 -v "$ROOT:/src:ro" -v "$W:/w" $IMAGE \
	aarch64-linux-gnu-gcc -std=gnu11 -O1 -Wall -Wextra -Werror -I/src/diatom/src \
	-o /w/check /src/tools/dsperate-yield-check.c -ldl
docker run --rm --platform linux/arm64 -v "$W:/w" ubuntu:24.04 /w/check /w/core.so /w
