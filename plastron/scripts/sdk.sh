#!/bin/sh
# make sdk: a cross SDK packed from a build in the build volume, for building
# ZyperOS's payload (its make px2-payload) without touching the volume.
#
#   sh scripts/sdk.sh VOLUME IMAGE OUTPUT NAME
#
# The SDK is OUTPUT's host/ folder - the cross-compiler, pkg-config and the
# sysroot with the system's libraries - copied into a Docker volume of its own,
# plastron-sdk-NAME-ID, and relocated there as Buildroot's own `make sdk`
# relocates its tarball (scripts/sdk-pack.sh). ID is a hash of what was
# copied, so the same build packs to the same name and any change to the
# toolchain or a library to a new one: an SDK never changes once packed, and
# what mounts one (read-only) knows exactly what it builds against.
#
# The build volume is only read, mounted read-only, under OUTPUT's shared
# lock (scripts/lock.sh): a build of OUTPUT waits until the copy is done, and
# a copy waits for a build. Buildroot's `make sdk` would write into the output
# (the relocation list, the tarball) and rebuild what it thinks is stale;
# this writes nothing there.
#
# A pack stopped halfway leaves a volume with no manifest
# (share/plastron/sdk, written last), which nothing uses and the next pack
# fills again. Old SDKs stay until removed: docker volume rm NAME.
set -eu

VOLUME=$1
IMAGE=$2
OUTPUT=$3
NAME=$4
SRC=$(cd "$(dirname "$0")/.." && pwd)

docker volume inspect "$VOLUME" > /dev/null 2>&1 ||
	{ echo "sdk: no volume $VOLUME: build plastron first (make)" >&2; exit 1; }

id=$(docker run --rm -v "$VOLUME":/work:ro -v "$SRC":/src:ro "$IMAGE" \
	sh /src/scripts/sdk-pack.sh id "/work/$OUTPUT")
sdk=plastron-sdk-$NAME-$id

if docker volume inspect "$sdk" > /dev/null 2>&1 &&
   docker run --rm -v "$sdk":/sdk:ro "$IMAGE" test -f /sdk/share/plastron/sdk; then
	echo "sdk: $sdk is already packed"
else
	docker volume inspect "$sdk" > /dev/null 2>&1 ||
		docker volume create --label plastron.sdk="$NAME" --label plastron.sdk.id="$id" "$sdk" > /dev/null
	# As root: the volume is made root's, and the copy keeps the build's owner
	docker run --rm --user root -v "$VOLUME":/work:ro -v "$SRC":/src:ro -v "$sdk":/sdk "$IMAGE" \
		sh /src/scripts/sdk-pack.sh pack "/work/$OUTPUT" "$id" /sdk
fi
docker run --rm -v "$sdk":/sdk:ro "$IMAGE" cat /sdk/share/plastron/sdk
echo "sdk: $sdk"
