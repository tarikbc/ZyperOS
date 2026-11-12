#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Which of ZyperOS's patches are in a vendored core, so that a core built
# before one of them changed is never put on a card.
#
# vendor/ is not in git: each checkout keeps the cores its build scripts made,
# whenever they were run. On 2026-11-08 the Nintendo DS core in the 1.0.1
# images and in that night's test build had been built before
# mk/patches/dsperate-lr-language.patch existed, so the Game Language row sent
# an option the core had never heard of, and every game started in the
# language of the card's firmware.bin. Nothing said so: a core without an
# option ignores it.
#
# So each build script that applies patches of ours writes a stamp beside the
# core it installs, vendor/cores/<core>_libretro.stamp: the core's sha256 and
# the name and sha256 of every patch it applied. mk/payload.sh, and
# tools/release.sh before its long build, refuse a core whose stamp is missing
# or names other patches, or another core, than the ones here now.
#
#   sh mk/core-stamp.sh write NAME [VENDOR]   the stamp for VENDOR/cores/NAME_libretro.so,
#                                              from the patches in mk/patches now (the
#                                              build scripts, after their build)
#   sh mk/core-stamp.sh check [VENDOR]        every patched core in VENDOR against its
#                                              stamp; 1, and what to run, if one is stale
#   sh mk/core-stamp.sh adopt NAME... [-v VENDOR]
#                                             a stamp for a core built before stamps,
#                                             when its sha256 is the one its build
#                                             script pins (that script's build of these
#                                             patches), so it need not be built again
#   sh mk/core-stamp.sh list                  the patched cores, their patches, their scripts
#
# VENDOR is vendor/ beside this script's checkout unless given. A core that is
# not in VENDOR is passed over here: mk/payload.sh has its own check for that.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PATCHES=$ROOT/mk/patches

# The cores ZyperOS patches: the name of the core (NAME_libretro.so), the
# prefix every one of its patches in mk/patches has, and its build script in
# mk/. A patch that no prefix here claims fails the check, so a new patched
# core cannot be left out of it.
CORES='dsperate dsperate-lr- build-dsperate.sh
fake08 fake08- build-fake08.sh
flycast2021 flycast2021- build-flycast2021.sh
freej2me_plus freej2me-plus- build-freej2me.sh
mupen64plus_next mupen64plus-next- build-mupen64plus-next.sh
parallel_n64 parallel-n64- build-parallel-n64.sh
ppsspp ppsspp- build-ppsspp.sh
tic80 tic80- build-tic80.sh'

die() { echo "core-stamp: $*" >&2; exit 1; }

sha() {
	if command -v sha256sum > /dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1
}

# field NAME N: the Nth field of NAME's line in CORES
field() {
	echo "$CORES" | awk -v n="$1" -v f="$2" '$1 == n { print $f }'
}

# The stamp NAME's core in VENDOR would have now: its own line, then one a
# patch, in name order
expected() {
	so=$2/cores/$1_libretro.so
	echo "core $1_libretro.so $(sha "$so")"
	for p in "$PATCHES/$(field "$1" 2)"*.patch; do
		if [ -f "$p" ]; then echo "patch ${p##*/} $(sha "$p")"; fi
	done
}

# The patches in mk/patches that no core claims
unclaimed() {
	for p in "$PATCHES"/*.patch; do
		[ -f "$p" ] || continue
		b=${p##*/}
		claimed=
		for pre in $(echo "$CORES" | awk '{ print $2 }'); do
			case $b in "$pre"*) claimed=1 ;; esac
		done
		[ -n "$claimed" ] || echo "$b"
	done
}

cmd=${1:-}
[ $# -gt 0 ] && shift
case $cmd in
write)
	name=${1:-}
	vendor=${2:-$ROOT/vendor}
	[ -n "$(field "$name" 1)" ] || die "$name is not a core ZyperOS patches (mk/core-stamp.sh CORES)"
	[ -f "$vendor/cores/${name}_libretro.so" ] || die "no $vendor/cores/${name}_libretro.so"
	expected "$name" "$vendor" > "$vendor/cores/${name}_libretro.stamp"
	echo "  stamp  $vendor/cores/${name}_libretro.stamp"
	;;
check)
	vendor=${1:-$ROOT/vendor}
	bad=0
	for b in $(unclaimed); do
		echo "core-stamp: mk/patches/$b belongs to no core in mk/core-stamp.sh's CORES" >&2
		bad=1
	done
	for name in $(echo "$CORES" | awk '{ print $1 }'); do
		so=$vendor/cores/${name}_libretro.so
		stamp=$vendor/cores/${name}_libretro.stamp
		[ -f "$so" ] || continue
		build=mk/$(field "$name" 3)
		if [ ! -f "$stamp" ]; then
			echo "core-stamp: ${name}_libretro.so has no stamp, so nothing says which patches are in it" >&2
			echo "core-stamp:   run sh $build (or sh mk/core-stamp.sh adopt $name, for a core $build already made)" >&2
			bad=1
			continue
		fi
		want=$(expected "$name" "$vendor")
		if [ "$want" != "$(cat "$stamp")" ]; then
			echo "core-stamp: ${name}_libretro.so is not the build of the patches here:" >&2
			printf '%s\n' "$want" | diff "$stamp" - |
				sed -n 's/^< /core-stamp:   stamp  /p; s/^> /core-stamp:   now    /p' >&2 || true
			echo "core-stamp:   run sh $build" >&2
			bad=1
		fi
	done
	[ $bad = 0 ] || exit 1
	;;
adopt)
	vendor=$ROOT/vendor
	names=
	while [ $# -gt 0 ]; do
		case $1 in
		-v) vendor=$2; shift 2 ;;
		*) names="$names $1"; shift ;;
		esac
	done
	[ -n "$names" ] || die "adopt which core? ($(echo "$CORES" | awk '{ printf "%s%s", s, $1; s = " " }'))"
	for name in $names; do
		build=$(field "$name" 3)
		[ -n "$build" ] || die "$name is not a core ZyperOS patches (mk/core-stamp.sh CORES)"
		so=$vendor/cores/${name}_libretro.so
		[ -f "$so" ] || die "no $so"
		pin=$(sed -n 's/^SHA=\([0-9a-f]\{64\}\).*/\1/p' "$ROOT/mk/$build")
		[ -n "$pin" ] || die "mk/$build pins no SHA= for its core"
		got=$(sha "$so")
		[ "$got" = "$pin" ] ||
			die "${name}_libretro.so is $got, and mk/$build makes $pin: run sh mk/$build"
		expected "$name" "$vendor" > "$vendor/cores/${name}_libretro.stamp"
		echo "  stamp  $vendor/cores/${name}_libretro.stamp (the core mk/$build pins)"
	done
	;;
list)
	echo "$CORES" | while read -r name pre build; do
		echo "$name (mk/$build):"
		for p in "$PATCHES/$pre"*.patch; do if [ -f "$p" ]; then echo "  ${p##*/}"; fi; done
	done
	;;
*)
	echo "usage: sh mk/core-stamp.sh write NAME [VENDOR] | check [VENDOR] | adopt NAME... [-v VENDOR] | list" >&2
	exit 2
	;;
esac
