#!/bin/sh
# SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0
# The GKD Pixel 2's card payload, built from this working tree against
# plastron's cross SDK, in Docker, without plastron's build volume:
#
#   tools/px2-payload.sh             (make px2-payload)
#
# Out: build/px2/payload/ZyperOS - the launcher, Muse, portkeys, System
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
[ -f "$VENDOR/cores/fceumm_libretro.so" ] ||
	die "no cores in $VENDOR: make vendor in the main checkout (or set ZYPEROS_VENDOR)"
SS_ENV=${ZYPEROS_SS_ENV:-$main/.screenscraper.env}

mkdir -p "$REPO/build/px2" "$REPO/diatom/build/pixel2"
set -- --rm -e "JOBS=${JOBS:-4}" \
	-v "$sdk:/sdk:ro" \
	-v "$REPO:/zyperos:ro" \
	-v "$REPO/build/px2:/zyperos/build/px2" \
	-v "$REPO/diatom/build/pixel2:/zyperos/diatom/build/pixel2" \
	-v "$VENDOR:/zyperos-vendor:ro"
[ -f "$SS_ENV" ] && set -- "$@" -v "$SS_ENV:/zyperos-ss.env:ro"
echo "px2-payload: against $sdk"
docker run "$@" "$IMAGE" sh /zyperos/mk/px2-sdk-build.sh
echo "px2-payload: $REPO/build/px2/payload/ZyperOS"
