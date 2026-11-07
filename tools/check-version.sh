#!/bin/sh
# Whether ZyperOS's version is the same everywhere it is written: the
# Makefile's VERSION, and ZyperOS Installer's Cargo.toml and Cargo.lock (the
# installer ships in each release with ZyperOS's version). tools/set-version.sh
# writes all three.
set -eu
cd "$(dirname "$0")/.."
MAKE_V=$(sed -n 's/^VERSION ?= //p' Makefile)
CARGO_V=$(sed -n 's/^version = "\(.*\)"$/\1/p' installer/src-tauri/Cargo.toml | head -1)
LOCK_V=$(sed -n '/^name = "zyperos-installer"$/{n;s/^version = "\(.*\)"$/\1/p;}' installer/src-tauri/Cargo.lock)
if [ -n "$MAKE_V" ] && [ "$MAKE_V" = "$CARGO_V" ] && [ "$MAKE_V" = "$LOCK_V" ]; then
	echo "check-version: $MAKE_V everywhere"
else
	echo "check-version: Makefile $MAKE_V, installer Cargo.toml $CARGO_V, Cargo.lock $LOCK_V" >&2
	echo "check-version: tools/set-version.sh <version> sets them all" >&2
	exit 1
fi
