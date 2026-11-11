#!/bin/bash
# A ZyperOS release, from this Mac.
#
#   tools/release.sh            build the GKD Pixel 2's card image from this
#                               commit and check it; nothing leaves this
#                               computer (out/release/v<VERSION>/)
#   tools/release.sh --publish  the same, then a draft GitHub release
#                               v<VERSION> with the image, its sources, the
#                               System Update pack and signed list, and
#                               SHA256SUMS, and the tag pushed
#   tools/release.sh --publish-built
#                               publish what the first form built, without
#                               building again: the files tested on a device
#                               are the files released. Refused unless they
#                               were built from main as GitHub has it now and
#                               still match their SHA256SUMS
#
# (make release, and make release PUBLISH=1.) The tag starts GitHub's
# .github/workflows/release.yml, which builds ZyperOS Installer for macOS,
# Windows and Linux, adds the apps to the same release, puts them in its
# SHA256SUMS, and publishes it: every version has its image and its
# installer in one place.
#
# The version is the Makefile's VERSION (tools/set-version.sh sets it with the
# installer's). The release notes are docs/releases/v<VERSION>.md, committed.
#
# The image is built here, not on GitHub, because its inputs are here: the
# cores in the main checkout's vendor/ (ZYPEROS_VENDOR), which
# mk/fetch-vendor.sh can no longer fetch exactly as they are, and plastron's
# Docker volume. plastron's release build (make release-pixel2) starts from an
# empty output folder of its own, so it builds everything again, with the
# shared download cache: it takes a while, and this holds
# /tmp/plastron-build-lock all the time it runs. The TrimUI Brick's image
# needs its GPU files (BRICK_GPU in plastron), which this Mac doesn't have, so
# a release is the Pixel 2's until it does.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
REPO=tarikbc/ZyperOS
# The cores: vendor/ of the main checkout (git's first worktree), which a
# worktree like this one may not be
VENDOR=${ZYPEROS_VENDOR:-$(git worktree list --porcelain | sed -n '1s/^worktree //p')/vendor}
VERSION=$(sed -n 's/^VERSION ?= //p' Makefile)
TAG=v$VERSION
OUT=$ROOT/out/release/$TAG
NOTES=docs/releases/$TAG.md
LOCK=/tmp/plastron-build-lock
IMAGE=ZyperOS-$TAG-pixel2.img.xz
SOURCES=ZyperOS-$TAG-pixel2-sources.tar
# System Update (docs/updates.md): plastron packs the image's own ZyperOS
# folder and writes the list unsigned; it is signed here, on the Mac that
# holds the key (tools/update-key.sh), never in the container or on GitHub.
# The list's name has no version, so releases/latest/download/ finds it.
UPDATE_PACK=ZyperOS-$TAG-pixel2-update.zpk
UPDATE_LIST=ZyperOS-pixel2-update.txt
# And the system's (root, kernel, device tree) for a card with two system
# slots (docs/updates.md, layer 2), signed the same way
SYSTEM_PACK=ZyperOS-$TAG-pixel2-system.zpk
SYSTEM_LIST=ZyperOS-pixel2-system.txt
UPDATE_KEY=${ZYPEROS_UPDATE_KEY:-$HOME/.config/zyperos/update-signing-key.pem}

PUBLISH=0
BUILT=0
case "${1:-}" in
--publish) PUBLISH=1 ;;
--publish-built) PUBLISH=1; BUILT=1 ;;
"") ;;
*) echo "usage: tools/release.sh [--publish | --publish-built]" >&2; exit 2 ;;
esac

say() { printf '\n== %s\n' "$*"; }
stop() { echo "release.sh: $*" >&2; exit 1; }

# The draft and the tag, from the files in $OUT
publish() {
	say "Publishing $TAG"
	# A draft first, with the image on it: the tag's workflow adds the installer
	# to this release and publishes it, and refuses a tag with no draft waiting
	[ $UPDATES = 0 ] || [ -n "$UPDATE_FILES" ] || stop "the update list is not signed"
	gh release create "$TAG" --repo $REPO --draft $PRERELEASE --target "$HEAD" --title "ZyperOS $VERSION" \
		--notes-file "$ROOT/$NOTES" "$IMAGE" "$SOURCES" $UPDATE_FILES SHA256SUMS
	cd "$ROOT"
	git tag -a "$TAG" -m "ZyperOS $VERSION" "$HEAD"
	git -c credential.helper= -c "credential.helper=!gh auth git-credential" push origin "refs/tags/$TAG"
	echo
	echo "The draft is up and $TAG is pushed. GitHub now builds ZyperOS Installer and publishes the release:"
	echo "  https://github.com/$REPO/actions/workflows/release.yml"
}

say "Checks"
sh tools/check-version.sh
# A version with a hyphen (0.2.0-beta.1) is a pre-release, which the
# installer never offers
case $VERSION in *-*) PRERELEASE=--prerelease ;; *) PRERELEASE= ;; esac
[ -z "$(git status --porcelain)" ] || stop "the working tree has changes: commit them first"
git fetch -q origin
HEAD=$(git rev-parse HEAD)
if [ $PUBLISH = 1 ]; then
	[ "$HEAD" = "$(git rev-parse origin/main)" ] ||
		stop "a release is made from main as GitHub has it: this is $(git rev-parse --short HEAD), origin/main is $(git rev-parse --short origin/main)"
	[ -f "$NOTES" ] || stop "write the release notes in $NOTES and commit them first"
	gh auth status >/dev/null 2>&1 || stop "gh isn't signed in"
	! git ls-remote --exit-code --tags origin "refs/tags/$TAG" >/dev/null 2>&1 || stop "$TAG is already tagged on GitHub"
	! gh release view "$TAG" --repo $REPO >/dev/null 2>&1 || stop "$REPO already has a $TAG release (a draft counts)"
fi
[ -d "$VENDOR/cores" ] || stop "no cores in $VENDOR/cores (ZYPEROS_VENDOR)"
# The patched cores against their stamps now, not an hour into the build,
# where mk/payload.sh asks the same (mk/core-stamp.sh)
sh mk/core-stamp.sh check "$VENDOR" || stop "a patched core in $VENDOR is stale or unstamped"
# With no public key in res/update/keys the image has System Update off, and
# the release has no update files. With one, a published release must carry a
# signed list: the private key is checked for now, before the long build.
UPDATES=0
ls res/update/keys/*.pem > /dev/null 2>&1 && UPDATES=1
if [ $UPDATES = 1 ]; then
	if [ -f "$UPDATE_KEY" ]; then
		KEY_ID=$(sh tools/update-key.sh id "$UPDATE_KEY")
		[ -f "res/update/keys/$KEY_ID.pem" ] ||
			stop "$UPDATE_KEY is key $KEY_ID, which res/update/keys does not have: no image would trust its list"
	elif [ $PUBLISH = 1 ]; then
		stop "no System Update signing key at $UPDATE_KEY (ZYPEROS_UPDATE_KEY): a release needs its list signed"
	else
		echo "release.sh: no signing key at $UPDATE_KEY; the update list stays unsigned (not publishable)"
	fi
else
	echo "release.sh: no key in res/update/keys: System Update is off in this image, and the release has no update files"
fi
echo "ZyperOS $VERSION from $(git rev-parse --short HEAD), plastron/ with it"

# --publish-built: the files of an earlier build, as they are
if [ $BUILT = 1 ]; then
	[ -f "$OUT/BUILT-FROM" ] || stop "nothing built in $OUT: run tools/release.sh first"
	grep -q "($HEAD)" "$OUT/BUILT-FROM" || stop "$OUT is from another commit: $(cat "$OUT/BUILT-FROM")"
	cd "$OUT"
	shasum -a 256 -c SHA256SUMS > /dev/null || stop "a file in $OUT no longer matches SHA256SUMS"
	UPDATE_FILES=
	if [ $UPDATES = 1 ]; then
		for f in "$UPDATE_PACK" "$UPDATE_LIST" "$SYSTEM_PACK" "$SYSTEM_LIST"; do
			[ -s "$f" ] || stop "$OUT has no signed $f"
			UPDATE_FILES="$UPDATE_FILES $f"
		done
	fi
	xz -t "$IMAGE"
	publish
	exit 0
fi

# The source as committed, never the working tree: a clean worktree of HEAD
SRC=$(mktemp -d)/zyperos
HAVE_LOCK=0
cleanup() {
	git worktree remove --force "$SRC" 2>/dev/null || true
	if [ $HAVE_LOCK = 1 ]; then rm -rf $LOCK; echo "release.sh: $LOCK released"; fi
}
trap cleanup EXIT
git worktree add -q --detach "$SRC" "$HEAD"
# plastron/, the system, from the same commit
PLASTRON=$SRC/plastron

say "Waiting for $LOCK"
until mkdir $LOCK 2>/dev/null; do
	echo "  held by $(cat $LOCK/owner 2>/dev/null || echo '?'); trying again in 30 s"
	sleep 30
done
HAVE_LOCK=1
echo "ZyperOS release $TAG (tools/release.sh)" > $LOCK/owner

say "The GKD Pixel 2's image: plastron's release build (everything, from empty; this takes a while)"
date
make -C "$PLASTRON" builder volume
docker run --rm -v plastron-work:/work --user root plastron-builder rm -rf /work/release
make -C "$PLASTRON" release-pixel2 VERSION="$VERSION" ZYPEROS_SRC="$SRC" ZYPEROS_VENDOR="$VENDOR"
rm -rf "$OUT"
mkdir -p "$OUT"
docker run --rm -v plastron-work:/work -v "$OUT":/out --user root plastron-builder \
	sh -c "cp /work/release/$IMAGE /work/release/$SOURCES /out/ &&
	       { [ $UPDATES = 0 ] || cp /work/release/$UPDATE_PACK /work/release/$UPDATE_LIST.unsigned \
	                               /work/release/$SYSTEM_PACK /work/release/$SYSTEM_LIST.unsigned /out/; } &&
	       chown -R $(id -u):$(id -g) /out"
rm -rf $LOCK
HAVE_LOCK=0
echo "release.sh: $LOCK released"
date

say "Checking the image"
cd "$OUT"
[ -s "$IMAGE" ] && [ -s "$SOURCES" ] || stop "plastron's release didn't make $IMAGE and $SOURCES"
xz -t "$IMAGE"
UPDATE_FILES=
# A pack and its list: the list describes this pack and this version, and is
# signed here with the release key
sign_list() { # sign_list PACK LIST
	[ -s "$1" ] && [ -s "$2.unsigned" ] || stop "plastron's release didn't make $1 and $2.unsigned"
	PACK_LINE=$(sed -n 's/^pack //p' "$2.unsigned")
	[ "$PACK_LINE" = "$1 $(wc -c < "$1" | tr -d ' ') $(shasum -a 256 "$1" | cut -c1-64)" ] ||
		stop "$2.unsigned does not describe $1"
	grep -qx "version $VERSION" "$2.unsigned" || stop "$2.unsigned is not for $VERSION"
	if [ -f "$UPDATE_KEY" ]; then
		sh "$ROOT/tools/update-key.sh" sign "$2.unsigned" "$2" "$UPDATE_KEY"
		rm -f "$2.unsigned"
		UPDATE_FILES="$UPDATE_FILES $1 $2"
	fi
}
if [ $UPDATES = 1 ]; then
	sign_list "$UPDATE_PACK" "$UPDATE_LIST"
	sign_list "$SYSTEM_PACK" "$SYSTEM_LIST"
fi
shasum -a 256 "$IMAGE" "$SOURCES" $UPDATE_FILES > SHA256SUMS
printf 'ZyperOS %s (%s), plastron/ included\n' "$VERSION" "$HEAD" > BUILT-FROM
cat SHA256SUMS
ls -l

if [ $PUBLISH = 0 ]; then
	echo
	echo "Built in $OUT. Nothing was published: tools/release.sh --publish does that."
	exit 0
fi
publish
