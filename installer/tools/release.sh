#!/bin/bash
# Builds ZyperOS Installer for macOS, Windows and Linux, all from a Mac, into
# dist/<version>/ with SHA256SUMS. The version is Cargo.toml's, which is
# ZyperOS's (the Makefile's VERSION; tools/set-version.sh sets both).
#
# Nothing leaves this computer. A ZyperOS release carries these files too,
# built by GitHub on the release's tag (.github/workflows/release.yml, after
# tools/release.sh at the top of the repository): this script is for trying
# the apps from a Mac, or building them without GitHub.
#
# Needs Rust through rustup with the x86_64-apple-darwin target, the Tauri CLI
# (cargo's or npm's), and Docker for the Windows and Linux builds.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
VERSION=$(sed -n 's/^version = "\(.*\)"$/\1/p' src-tauri/Cargo.toml | head -1)
DIST=dist/$VERSION
NAME=ZyperOS-Installer-$VERSION

case "${1:-}" in
	"") ;;
	*) echo "usage: tools/release.sh" >&2; exit 2 ;;
esac

say() { printf '\n== %s\n' "$*"; }
stop() { echo "release.sh: $*" >&2; exit 1; }

# The Tauri CLI: cargo install tauri-cli's, or npm's prebuilt one
if cargo tauri --version >/dev/null 2>&1; then TAURI="cargo tauri"; else TAURI="npx --yes @tauri-apps/cli@2"; fi

say "ZyperOS's design"
tools/check-design.sh

# THIRD-PARTY.md names what the apps are built from: made again from
# Cargo.lock, it must be the committed file, or the release would ship a list
# of something else
say "The list of what the installer is built from"
LIST=$(mktemp)
trap 'rm -f "$LIST"' EXIT
python3 tools/third-party-list.py --output "$LIST"
if ! cmp -s "$LIST" THIRD-PARTY.md; then
	diff -u THIRD-PARTY.md "$LIST" | head -n 40 >&2 || true
	stop "THIRD-PARTY.md is out of date with src-tauri/Cargo.lock. Make it again with 'python3 tools/third-party-list.py' in installer/, and commit it."
fi
if git rev-parse --is-inside-work-tree >/dev/null 2>&1 &&
	! { git ls-files --error-unmatch THIRD-PARTY.md && git diff --quiet HEAD -- THIRD-PARTY.md; } >/dev/null 2>&1; then
	stop "THIRD-PARTY.md is up to date but not committed. Commit it."
fi
echo "THIRD-PARTY.md is up to date"

say "Tests"
(cd src-tauri && cargo test --quiet)

mkdir -p "$DIST"

say "macOS: one app for Apple Silicon and Intel"
(cd src-tauri && $TAURI build --target universal-apple-darwin --bundles app)
APP="src-tauri/target/universal-apple-darwin/release/bundle/macos/ZyperOS Installer.app"
# ditto keeps the app's links and permissions, as Finder's own zip does
ditto -c -k --keepParent "$APP" "$DIST/$NAME-macOS.zip"

say "Windows: a portable exe, cross-built with cargo-xwin"
docker run --rm --platform linux/amd64 -v "$ROOT":/src \
	-v zyperos-installer-cargo:/usr/local/cargo/registry \
	-v zyperos-installer-xwin:/root/.cache/cargo-xwin \
	-v zyperos-installer-rustup:/usr/local/rustup \
	-w /src/src-tauri -e CARGO_TARGET_DIR=/src/src-tauri/target/xwin \
	messense/cargo-xwin cargo xwin build --release --target x86_64-pc-windows-msvc
cp src-tauri/target/xwin/x86_64-pc-windows-msvc/release/zyperos-installer.exe "$DIST/$NAME-Windows.exe"

say "Linux: a .deb and an .rpm, built on Ubuntu 22.04"
if [ -z "$(docker images -q zyperos-installer-linux)" ]; then
	docker build --platform linux/amd64 -t zyperos-installer-linux -f tools/linux.Dockerfile tools
fi
# By its ID: OrbStack won't run an amd64 image by name on an Apple Silicon Mac
IMAGE=$(docker images --no-trunc -q zyperos-installer-linux | head -1)
docker run --rm -v "$ROOT":/src -v zyperos-installer-cargo-linux:/root/.cargo/registry \
	-w /src/src-tauri -e CARGO_TARGET_DIR=target/linux \
	"$IMAGE" cargo tauri build --bundles deb,rpm
BUNDLE=src-tauri/target/linux/release/bundle
# Named zyperos-installer by tauri.linux.conf.json: Tauri makes the package's
# name from the product's, and "ZyperOS" would come out as zyper-os
cp "$BUNDLE/deb/zyperos-installer_${VERSION}_amd64.deb" "$DIST/$NAME-amd64.deb"
cp "$BUNDLE/rpm/zyperos-installer-$VERSION-1.x86_64.rpm" "$DIST/$NAME-x86_64.rpm"

say "Checksums"
FILES="$NAME-macOS.zip $NAME-Windows.exe $NAME-amd64.deb $NAME-x86_64.rpm"
(cd "$DIST" && shasum -a 256 $FILES > SHA256SUMS && cat SHA256SUMS)
ls -l "$DIST"

echo
echo "Built in $DIST. Nothing was published: a ZyperOS release builds and carries these (tools/release.sh at the top)."
