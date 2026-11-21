#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The GKD Pixel 2's card payload, built from this working tree against
# plastron's cross SDK, in Docker, without plastron's build volume:
#
#   tools/px2-payload.sh             (make px2-payload)
#   tools/px2-payload.sh --programs  the programs alone (make px2-programs)
#
# --programs: build/px2/programs/ZyperOS with the launcher, MP4, portkeys,
# zyhome, System Update's program, p2d, p8fx.so and Diatom, and the card's
# scripts (launch.sh, device.sh, eq-port.sh, PICO-8's and OpenBOR's,
# mk/card-scripts.sh), and nothing else, for a change to the code: no cores,
# so no main checkout's vendor/ (whose patched cores' stamps must be fresh
# for a whole payload), and tools/px2-deploy.sh sends only those of them that
# changed. A change to res/, systems.cfg or a core needs the whole payload.
# Both builds also leave build/px2/system (the system image's files a deploy's
# --system stands in for until the next restart) and build/px2/initns.
#
# Out: build/px2/payload/ZyperOS - the launcher, MP4, portkeys, System
# Update's program and Diatom, with the cores, cards and fonts, as plastron's
# image carries it - for tools/px2-deploy.sh or `tools/qemu-run.sh deploy`.
# The objects stay in build/px2/ and diatom/build/pixel2/, unstripped, for
# addr2line against a crash on the device.
#
# The SDK is a Docker volume that plastron's `make sdk` packs from a build:
# its cross-compiler, pkg-config and the sysroot with the system's SDL2,
# FFmpeg, ALSA and the rest, so what links here is what runs on the device.
# Named by its contents (plastron-sdk-px2-<id>) and never changed once packed,
# it is mounted read-only; this tree is mounted read-only too, but for the two
# build folders. Nothing is shared but what cannot change, so builds from any
# number of worktrees run at once with no lock. PX2_SDK picks an SDK by name;
# without it, the newest one.
#
# The cores come from the main checkout's vendor/ (a worktree has none of its
# own), or ZYPEROS_VENDOR; the ScreenScraper pair from the main checkout's
# .screenscraper.env, or ZYPEROS_SS_ENV, as plastron's build reads it - none
# is a supported build. JOBS (4) is how many compilers run at once.
set -eu

PROGRAMS=
case ${1:-} in
--programs) PROGRAMS=1 ;;
'') ;;
*) echo "usage: tools/px2-payload.sh [--programs]" >&2; exit 2 ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/.." && pwd)
IMAGE=${PLASTRON_IMAGE:-plastron-builder}

die() { echo "px2-payload: $*" >&2; exit 1; }

command -v docker > /dev/null || die "needs Docker"
docker image inspect "$IMAGE" > /dev/null 2>&1 ||
	die "no $IMAGE image: run make builder in plastron"

sdk=${PX2_SDK:-}
if [ -z "$sdk" ]; then
	sdk=$(docker volume ls -q --filter label=plastron.sdk=px2 | while read -r v; do
		echo "$(docker volume inspect -f '{{.CreatedAt}}' "$v") $v"
	done | sort | tail -1 | cut -d' ' -f2)
fi
[ -n "$sdk" ] || die "no plastron SDK: run make sdk in plastron, after a build (or set PX2_SDK)"
docker volume inspect "$sdk" > /dev/null 2>&1 || die "no SDK volume $sdk"

common=$(git -C "$REPO" rev-parse --path-format=absolute --git-common-dir 2> /dev/null) || common=$REPO/.git
main=$(dirname "$common")
VENDOR=${ZYPEROS_VENDOR:-}
if [ -z "$VENDOR" ]; then
	if [ -f "$REPO/vendor/cores/fceumm_libretro.so" ]; then VENDOR=$REPO/vendor; else VENDOR=$main/vendor; fi
fi
[ -n "$PROGRAMS" ] || [ -f "$VENDOR/cores/fceumm_libretro.so" ] ||
	die "no cores in $VENDOR: make vendor in the main checkout (or set ZYPEROS_VENDOR, or --programs)"
# A small core systems.cfg names that $VENDOR lacks (mk/build-small-cores.sh)
# is built into it first; with none missing this only looks. --programs
# carries no cores
[ -n "$PROGRAMS" ] || ZYPEROS_VENDOR="$VENDOR" sh "$REPO/mk/build-small-cores.sh" --missing ||
	die "mk/build-small-cores.sh could not build the small cores $VENDOR lacks"
SS_ENV=${ZYPEROS_SS_ENV:-$main/.screenscraper.env}

# The string tables are the build's (src/i18n_tables.c, not committed), and
# the tree goes in read-only: written here first, as make would
command -v python3 > /dev/null || die "needs python3 for the string tables (tools/i18n.py)"
python3 "$REPO/tools/i18n.py" > /dev/null

mkdir -p "$REPO/build/px2" "$REPO/diatom/build/pixel2"
set -- --rm -e "JOBS=${JOBS:-4}" -e "PROGRAMS_ONLY=$PROGRAMS" \
	-v "$sdk:/sdk:ro" \
	-v "$REPO:/zyperos:ro" \
	-v "$REPO/build/px2:/zyperos/build/px2" \
	-v "$REPO/diatom/build/pixel2:/zyperos/diatom/build/pixel2"
[ -n "$PROGRAMS" ] || set -- "$@" -v "$VENDOR:/zyperos-vendor:ro"
[ -f "$SS_ENV" ] && set -- "$@" -v "$SS_ENV:/zyperos-ss.env:ro"
echo "px2-payload: against $sdk"
docker run "$@" "$IMAGE" sh /zyperos/mk/px2-sdk-build.sh
if [ -n "$PROGRAMS" ]; then OUTDIR=$REPO/build/px2/programs; else OUTDIR=$REPO/build/px2/payload; fi
# What it was built from and against, beside ZyperOS/ (not in it, so it never
# goes onto a card): tools/px2-deploy.sh says it, and its loader check on the
# device names the SDK when a program needs what the system does not have
{
	echo "zyperos=$(git -C "$REPO" describe --always --dirty --abbrev=12 2> /dev/null || echo unknown)"
	echo "sdk=$sdk"
	sed 's/^/sdk./' "$REPO/build/px2/sdk-manifest"
	echo "built=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
} > "$OUTDIR/BUILT"
echo "px2-payload: $OUTDIR/ZyperOS"
