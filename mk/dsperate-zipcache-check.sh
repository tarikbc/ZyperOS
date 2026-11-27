#!/bin/sh
# The cap on the zipped games a built DSperate core unpacks into .dsperate
# (tools/dsperate-zipcache-check.c): beside 2.5 GB of unpacked games, a zip's
# load takes the oldest launch's copy out and keeps the rest, 2 GB with the
# new copy, as DSperate's own frontend does (mk/patches/
# dsperate-lr-zip-cache.patch). The launcher holds the same cap after each
# game (src/dscache.h, make check-dscache).
#
#   sh mk/dsperate-zipcache-check.sh [core.so]    default vendor/cores/dsperate_libretro.so
#
# mk/build-dsperate.sh runs it on each core it builds, before installing it.
# Compiled and run as mk/dsperate-state-check.sh is: the build's own image
# (dsperate-lr-cross) compiles, ubuntu:24.04 for arm64 runs it, on a folder in
# the container (the images are sparse: they take no room). A few seconds.
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SO=${1:-$ROOT/vendor/cores/dsperate_libretro.so}
IMAGE=dsperate-lr-cross

command -v docker >/dev/null || { echo "need docker" >&2; exit 1; }
[ -f "$SO" ] || { echo "no core at $SO" >&2; exit 1; }
docker image inspect $IMAGE >/dev/null 2>&1 ||
	{ echo "no $IMAGE image: mk/build-dsperate.sh builds it" >&2; exit 1; }

W=$(mktemp -d "${TMPDIR:-/tmp}/dsperate-zipcache.XXXXXX")
trap 'rm -rf "$W"' EXIT
cp "$SO" "$W/core.so"
docker run --rm --platform linux/amd64 -v "$ROOT:/src:ro" -v "$W:/w" $IMAGE \
	aarch64-linux-gnu-gcc -std=gnu11 -O1 -Wall -Wextra -Werror -I/src/diatom/src \
	-o /w/check /src/tools/dsperate-zipcache-check.c -ldl
docker run --rm --platform linux/arm64 -v "$W:/w:ro" ubuntu:24.04 \
	/w/check /w/core.so /tmp/card
