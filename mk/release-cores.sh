#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The release mk/fetch-vendor.sh fetches the cores from: the exact files in
# vendor/cores that its pins name, with the source of each copyleft one, put
# on github.com/tarikbc/ZyperOS under its CORES_TAG.
#
#   sh mk/release-cores.sh             what it would publish, nothing sent
#   sh mk/release-cores.sh --publish   publish it (gh, signed in)
#
# Every pinned core must be in vendor/cores with the pin's sha256, or nothing
# is published. Each core whose license in THIRD-PARTY-LICENSES.md is a GPL or
# an LGPL gets its source beside it, <core>-<commit>-source.tar.gz: GitHub's
# archive of the repository at the commit its row names, the source of the
# binary shipped. The release gets the cores, those archives, a SHA256SUMS of
# all of them, and notes naming each core's source. It is a pre-release and
# never "latest", and its tag is not v<number>: System Update reads
# releases/latest, the installer takes only v<number> releases that are not
# pre-releases, so neither ever takes it for a ZyperOS version.
#
# A tag is published once. A core that changes goes into a new release: a new
# dated CORES_TAG in mk/fetch-vendor.sh, its pin, its row, then this.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FETCH=$ROOT/mk/fetch-vendor.sh
LICENSES=$ROOT/THIRD-PARTY-LICENSES.md
# vendor/ is ignored by git, so a worktree has none: the main checkout's, as
# plastron's Makefile takes it, or ZYPEROS_VENDOR
MAIN=$(git -C "$ROOT" worktree list --porcelain 2>/dev/null | sed -n '1s/^worktree //p')
CORES=${ZYPEROS_VENDOR:-${MAIN:-$ROOT}/vendor}/cores
REPO=tarikbc/ZyperOS
TAG=$(sed -n 's/^CORES_TAG=//p' "$FETCH")
OUT=$ROOT/build-native/release-cores/$TAG
publish=
case ${1:-} in
--publish) publish=1 ;;
'') ;;
*) sed -n '7,8p' "$0" | sed 's/^# //' >&2; exit 2 ;;
esac

[ -n "$TAG" ] || { echo "release-cores: no CORES_TAG in $FETCH" >&2; exit 1; }
case $TAG in v[0-9]*) echo "release-cores: $TAG looks like a ZyperOS version" >&2; exit 1 ;; esac
command -v gh > /dev/null || { echo "release-cores: need gh (the sources' commits)" >&2; exit 1; }
rm -rf "$OUT"
mkdir -p "$OUT"
: > "$OUT/cores.txt"
: > "$OUT/sources.txt"

# The cores, each its pin
grep '^fetch_core [a-z0-9_]* *[0-9a-f]\{64\}' "$FETCH" | while read -r _ name sha; do
	so=${name}_libretro.so
	[ -f "$CORES/$so" ] || { echo "  MISSING  $so (vendor/cores)" >&2; exit 1; }
	got=$(shasum -a 256 "$CORES/$so" | cut -d' ' -f1)
	[ "$got" = "$sha" ] ||
		{ echo "  MISMATCH $so: vendor/cores has $got, the pin is $sha" >&2; exit 1; }
	grep -q "^| \`$so\` |" "$LICENSES" ||
		{ echo "  $so has no row in THIRD-PARTY-LICENSES.md" >&2; exit 1; }
	cp "$CORES/$so" "$OUT/$so"
	echo "$so" >> "$OUT/cores.txt"
done

# The source of each copyleft one, at the commit its row names
while read -r so; do
	row=$(grep "^| \`$so\` |" "$LICENSES")
	license=$(printf '%s\n' "$row" | awk -F'|' '{print $4}')
	case $license in *GPL*) ;; *) continue ;; esac
	src=$(printf '%s\n' "$row" | sed -n 's#.*https://github.com/\([^ ]*\) at \([0-9a-f]\{7,40\}\).*#\1 \2#p' | head -n 1)
	[ -n "$src" ] || { echo "  $so: its row names no repository and commit to take its source from" >&2; exit 1; }
	repo=${src% *}
	commit=$(gh api "repos/$repo/commits/${src#* }" --jq .sha) ||
		{ echo "  $so: no commit ${src#* } in $repo" >&2; exit 1; }
	short=$(printf '%.7s' "$commit")
	archive=${so%_libretro.so}-$short-source.tar.gz
	curl -sSfL -o "$OUT/$archive" "https://codeload.github.com/$repo/tar.gz/$commit" ||
		{ echo "  $so: could not download $repo at $commit" >&2; exit 1; }
	tar -tzf "$OUT/$archive" > /dev/null || { echo "  $so: $archive is no archive" >&2; exit 1; }
	echo "$so $archive https://github.com/$repo/tree/$commit" >> "$OUT/sources.txt"
	echo "  source  $archive"
	# A submodule is not in GitHub's archive: each one its own, at the
	# commit the core's tree pins (pcsx_rearmed's libpicofe)
	gh api "repos/$repo/git/trees/$commit?recursive=1" \
	   --jq '.tree[] | select(.type=="commit") | .path + " " + .sha' |
	while read -r path sub; do
		url=$(gh api "repos/$repo/contents/.gitmodules?ref=$commit" --jq .content | base64 -d |
		      awk -v p="$path" '$1=="path" && $3==p {f=1; next} f && $1=="url" {print $3; exit}')
		subrepo=$(printf '%s\n' "$url" | sed -n 's#^https://github.com/\(.*\)$#\1#p' | sed 's#\.git$##')
		[ -n "$subrepo" ] || { echo "  $so: submodule $path is not on GitHub ($url)" >&2; exit 1; }
		subarchive=${so%_libretro.so}-$(basename "$path")-$(printf '%.7s' "$sub")-source.tar.gz
		curl -sSfL -o "$OUT/$subarchive" "https://codeload.github.com/$subrepo/tar.gz/$sub" ||
			{ echo "  $so: could not download $subrepo at $sub" >&2; exit 1; }
		echo "$so $subarchive https://github.com/$subrepo/tree/$sub ($path)" >> "$OUT/sources.txt"
		echo "  source  $subarchive"
	done
done < "$OUT/cores.txt"

( cd "$OUT" && shasum -a 256 $(cat cores.txt) $(awk '{print $2}' sources.txt) > SHA256SUMS )
n=$(wc -l < "$OUT/cores.txt" | tr -d ' ')
s=$(wc -l < "$OUT/sources.txt" | tr -d ' ')

{
	echo "The libretro cores ZyperOS ships, as \`mk/fetch-vendor.sh\` fetches them, each pinned there by its sha256 (also in SHA256SUMS here). This is not a ZyperOS version: install ZyperOS from its own releases."
	echo
	echo "Every core is the work of its authors and is under its own license. libretro's buildbot built them; the source of each is below, as THIRD-PARTY-LICENSES.md in the repository gives it."
	echo
	echo "## Sources"
	echo
	echo "Each core under a GPL or an LGPL has its source here, at the commit of the binary in this release: \`<core>-<commit>-source.tar.gz\`, GitHub's archive of that commit, also in SHA256SUMS."
	echo
	echo "| Core | Source archive | At |"
	echo "|------|----------------|----|"
	while read -r so archive url; do
		echo "| \`$so\` | \`$archive\` | $url |"
	done < "$OUT/sources.txt"
	echo
	echo "## Licenses"
	echo
	echo "| Core | Systems | License | Source |"
	echo "|------|---------|---------|--------|"
	while read -r so; do
		grep "^| \`$so\` |" "$LICENSES"
	done < "$OUT/cores.txt"
} > "$OUT/NOTES.md"

echo "release-cores: $TAG, $n cores and $s sources, in $OUT"
if [ -z "$publish" ]; then
	echo "release-cores: not published (--publish does it)"
	exit 0
fi
if gh release view "$TAG" -R "$REPO" > /dev/null 2>&1; then
	echo "release-cores: $TAG is already published; a changed core needs a new tag" >&2
	exit 1
fi
cd "$OUT"
# The tag on the commit main is at: it marks no ZyperOS version
# shellcheck disable=SC2046
gh release create "$TAG" -R "$REPO" --target main --prerelease --latest=false \
	--title "Cores $TAG" --notes-file NOTES.md \
	SHA256SUMS $(cat cores.txt) $(awk '{print $2}' sources.txt)
echo "release-cores: published https://github.com/$REPO/releases/tag/$TAG"
