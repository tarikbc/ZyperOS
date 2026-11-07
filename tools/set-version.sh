#!/bin/sh
# tools/set-version.sh 0.2.0 sets ZyperOS's version everywhere it is written:
# the Makefile's VERSION (plastron reads it for the card and the About page),
# and ZyperOS Installer's Cargo.toml and Cargo.lock, since the installer has
# ZyperOS's version and ships in the same release. tools/check-version.sh
# (make check-installer) says when they disagree.
set -eu
V=${1:-}
case $V in
[0-9]*.[0-9]*.[0-9]*) ;;
*) echo "usage: tools/set-version.sh <major.minor.patch>" >&2; exit 2 ;;
esac
cd "$(dirname "$0")/.."
python3 - "$V" <<'PY'
import re, sys
v = sys.argv[1]
def edit(path, pattern, repl):
    s = open(path).read()
    t, n = re.subn(pattern, repl, s, count=1, flags=re.M)
    if n != 1:
        sys.exit(f"set-version: no version line in {path}")
    open(path, "w").write(t)
edit("Makefile", r"^VERSION \?= .*$", f"VERSION ?= {v}")
edit("installer/src-tauri/Cargo.toml", r'^version = ".*"$', f'version = "{v}"')
edit("installer/src-tauri/Cargo.lock", r'(name = "zyperos-installer"\nversion = )".*"', rf'\1"{v}"')
PY
sh tools/check-version.sh
