#!/bin/sh
# Runs inside the build container for make sdk (scripts/sdk.sh), with the
# build volume mounted read-only at /work.
#
#   sdk-pack.sh id   /work/OUTPUT              the SDK's id: a hash of host/
#   sdk-pack.sh pack /work/OUTPUT ID DEST      host/ copied into DEST and
#                                              relocated there, if its id is
#                                              still ID
#
# Both hold OUTPUT's shared lock (scripts/lock.sh), so no build of it runs
# meanwhile, and pack hashes its copy before relocating it: a build between
# the two steps, or one that took no lock, would otherwise leave a volume
# holding something other than what it is named for.
#
# The relocation is Buildroot's (its prepare-sdk and support/misc/
# relocate-sdk.sh): every text file naming the build's host/ is listed and
# rewritten to DEST. The toolchain's programs find their libraries through
# $ORIGIN already, and Buildroot's pkg-config wrapper finds the sysroot beside
# itself, so nothing else names the old place.
set -eu

. /src/scripts/lock.sh

MODE=$1
OUT=$2
HOST=$OUT/host

[ -d "$HOST/bin" ] || { echo "sdk: no build in $OUT (no $HOST)" >&2; exit 1; }
plastron_lock "$OUT" -s

# Contents, names, links and modes, in a fixed order, with no times or owners
sdk_id() {
	tar -C "$1" --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner -cf - . |
		sha256sum | cut -c1-12
}

case $MODE in
id)
	sdk_id "$HOST"
	;;
pack)
	ID=$3
	DEST=$4
	gcc=$(cd "$HOST/bin" && ls ./*-buildroot-linux-*-gcc | head -1)
	[ -n "$gcc" ] || { echo "sdk: no cross-compiler in $HOST/bin" >&2; exit 1; }
	target=$(basename "$gcc" -gcc)
	# The Buildroot this output was made by, for its relocation script
	br=$(sed -n 's|^MAKEARGS := -C \([^ ]*\).*|\1|p' "$OUT/Makefile")
	[ -f "$br/support/misc/relocate-sdk.sh" ] ||
		{ echo "sdk: no Buildroot for $OUT (looked in '$br')" >&2; exit 1; }

	echo "sdk: copying $HOST ($(du -sh "$HOST" | cut -f1))"
	find "$DEST" -mindepth 1 -delete
	cp -a "$HOST/." "$DEST/"
	[ "$(sdk_id "$DEST")" = "$ID" ] ||
		{ echo "sdk: $OUT changed while being packed: run make sdk again" >&2; exit 1; }

	cd "$DEST"
	mkdir -p share/buildroot share/plastron
	export LC_ALL=C
	grep -lrF "$HOST" . | while read -r f; do
		case $f in ./share/buildroot/sdk-*) continue ;; esac
		if file -b --mime-type "$f" | grep -q '^text/'; then echo "$f"; fi
	done > share/buildroot/sdk-relocs
	echo "$HOST" > share/buildroot/sdk-location
	install -m 755 "$br/support/misc/relocate-sdk.sh" relocate-sdk.sh
	./relocate-sdk.sh

	# Last, so an SDK with a manifest is a whole one
	{
		echo "id=$ID"
		echo "target=$target"
		echo "gcc=$("$DEST/bin/$target-gcc" -dumpfullversion)"
		echo "buildroot=$(basename "$br" | sed 's/^buildroot-//')"
		echo "plastron=$(cat "$OUT/plastron-commit" 2>/dev/null || echo unknown)"
		echo "from=$OUT"
		echo "packed=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	} > share/plastron/sdk.new
	mv share/plastron/sdk.new share/plastron/sdk
	;;
*)
	echo "usage: sdk-pack.sh id|pack ..." >&2
	exit 1
	;;
esac
