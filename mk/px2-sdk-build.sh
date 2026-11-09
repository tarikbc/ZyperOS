#!/bin/sh
# Runs inside plastron's build image for tools/px2-payload.sh: plastron's SDK
# at /sdk and this tree at /zyperos, both read-only but for build/px2 and
# diatom/build/pixel2, and the cores at /zyperos-vendor. The same steps as
# plastron's zyperos and diatom packages (buildroot/package/zyperos,
# buildroot/package/diatom), with the SDK's compiler and sysroot in place of
# Buildroot's: mk/cross.mk for the launcher, Muse, portkeys and System
# Update's program, Diatom's own Makefile, then mk/payload.sh.
set -eu

MANIFEST=/sdk/share/plastron/sdk
[ -f "$MANIFEST" ] || { echo "px2-payload: /sdk is not a whole plastron SDK (no $MANIFEST)" >&2; exit 1; }
target=$(sed -n 's/^target=//p' "$MANIFEST")
sed 's/^/px2-payload: sdk /' "$MANIFEST"

# As Buildroot's TARGET_MAKE_ENV: its pkg-config, which answers for the sysroot
export PATH=/sdk/bin:$PATH
CC=/sdk/bin/$target-gcc
SYSROOT=/sdk/$target/sysroot
STRIP=/sdk/bin/$target-strip
JOBS=${JOBS:-4}

cd /zyperos
VERSION=$(sed -n 's/^VERSION ?= //p' Makefile)
B=build/px2

# The ScreenScraper pair into a header, read in the shell so make prints
# neither (mk/cross.mk, creds)
SS_DEVID="$(sed -n 's/^SS_DEVID=//p' /zyperos-ss.env 2> /dev/null | head -1)" \
SS_DEVPASS="$(sed -n 's/^SS_DEVPASS=//p' /zyperos-ss.env 2> /dev/null | head -1)" \
	make -s -f mk/cross.mk BUILD=$B creds
make -j"$JOBS" -f mk/cross.mk CC="$CC" SYSROOT="$SYSROOT" DEVICE=pixel2 BUILD=$B \
	VERSION="$VERSION" $B/zyperos.elf $B/muse $B/portkeys $B/zyperos-update $B/zyhome
make -j"$JOBS" -C diatom PORT=pixel2 CC="$CC"

DEVICE=pixel2 OUT=/zyperos/$B/payload VERSION="$VERSION" \
	ZYPEROS_ELF=$B/zyperos.elf MUSE_ELF=$B/muse PORTKEYS_ELF=$B/portkeys ZYHOME_ELF=$B/zyhome \
	UPDATE_ELF=$B/zyperos-update DIATOM_ELF=diatom/build/pixel2/diatom \
	VENDOR_DIR=/zyperos-vendor sh mk/payload.sh
# The image's tools for SSH (plastron's zyperos.mk builds them for /usr/bin),
# beside the payload rather than in it: tools/px2 copies px2-perf to a device
# whose image is older than it
for t in px2-shot px2-input px2-perf; do
	"$CC" -O2 -Wall -Wextra -o "$B/$t" "tools/$t.c" -lm
	"$STRIP" "$B/$t"
done
# Stripped, as plastron's image strips every program it carries
for f in zyperos.elf muse portkeys zyperos-update zyhome diatom; do
	"$STRIP" "$B/payload/ZyperOS/$f"
done
