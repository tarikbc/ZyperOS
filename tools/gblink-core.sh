#!/bin/sh
# Gambatte for this computer, for the Link Cable's checks on it
# (tools/gblink-diatom.py, tools/gblink-loopback.c): the commit and the
# source sha256 mk/build-small-cores.sh pins for the handhelds, the same
# mk/patches/gambatte-*.patch, built with this computer's compiler
# (platform=osx on a Mac, unix elsewhere: HAVE_NETWORK=1, the network Game
# Link, on both). Not the release's core: that one is built for the devices
# in Docker and held to its own sha256.
#
# Usage: tools/gblink-core.sh [work dir]
# The core lands in build-native/gblink/gambatte_libretro.{dylib,so}.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=${1:-$ROOT/build-native/gblink/src}
OUT=$ROOT/build-native/gblink
line=$(grep '^gambatte|' "$ROOT/mk/build-small-cores.sh")
repo=$(echo "$line" | cut -d'|' -f2)
pin=$(echo "$line" | cut -d'|' -f3)
tar_sha=$(echo "$line" | cut -d'|' -f4)
case $(uname -s) in
	Darwin) platform=osx; ext=dylib ;;
	*)      platform=unix; ext=so ;;
esac
mkdir -p "$WORK" "$OUT"
tar="$WORK/gambatte-libretro-$pin.tar.gz"
[ -f "$tar" ] || curl -sSfL -o "$tar" "https://github.com/$repo/archive/$pin.tar.gz"
got=$(shasum -a 256 "$tar" | cut -d' ' -f1)
[ "$got" = "$tar_sha" ] || { echo "!! gambatte source sha256 $got, expected $tar_sha" >&2; exit 1; }
src="$WORK/gambatte-libretro-$pin"
rm -rf "$src"
tar -xzf "$tar" -C "$WORK"
for p in "$ROOT/mk/patches/gambatte-"*.patch; do
	[ -f "$p" ] && patch -s -p1 -d "$src" < "$p"
done
jobs=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
make -C "$src" -j"$jobs" platform=$platform GIT_VERSION="${pin%"${pin#???????}"}" \
	> "$WORK/build.log" 2>&1 || { tail -20 "$WORK/build.log" >&2; exit 1; }
cp "$src/gambatte_libretro.$ext" "$OUT/gambatte_libretro.$ext"
echo "  built  $OUT/gambatte_libretro.$ext ($pin, patched)"
