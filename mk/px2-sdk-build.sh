#!/bin/sh
# Runs inside plastron's build image for tools/px2-payload.sh: plastron's SDK
# at /sdk and this tree at /zyperos, both read-only but for build/px2 and
# diatom/build/pixel2, and the cores at /zyperos-vendor. The same steps as
# plastron's zyperos and diatom packages (buildroot/package/zyperos,
# buildroot/package/diatom), with the SDK's compiler and sysroot in place of
# Buildroot's: mk/cross.mk for the launcher, MP4, portkeys, System
# Update's program, p2d (NES Player 2's server) and p8fx.so (your PICO-8's
# Shader and Border), Diatom's own Makefile, then mk/payload.sh.
#
# Beside them, for tools/px2-deploy.sh: build/px2/sdk-manifest, the SDK's
# manifest (which SDK, which plastron); build/px2/initns (tools/initns.c);
# and build/px2/system, the system image's files ZyperOS's fixes change, as
# the image has them, for a deploy's --system to stand in for the image's
# own until the next restart: the ports' equalizer (plastron's zyperos-eq,
# from src/eqdsp.h), the side lights' gauge and the sound's ALSA config.
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
# Stripped as Buildroot strips what an image carries (its STRIPCMD), the
# compiler's .comment and .note gone too: a program built here from the same
# source against the SDK of the image's own build is then the image's byte
# for byte, and tools/px2 deploy sends only what a change changed
strip_as_image() { "$STRIP" --remove-section=.comment --remove-section=.note "$@"; }
JOBS=${JOBS:-4}

cd /zyperos
VERSION=$(sed -n 's/^VERSION ?= //p' Makefile)
B=build/px2

# The launcher's objects stay between builds (mk/cross.mk, one a source
# file), and the compile line is the same for every SDK (/sdk), so objects
# from another SDK would be linked against this one's libraries: a build
# against another SDK than the last starts from nothing
sdk_id=$(sed -n 's/^id=//p' "$MANIFEST")
if [ "$(cat "$B/sdk-id" 2> /dev/null)" != "$sdk_id" ]; then
	find "$B" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
	printf '%s\n' "$sdk_id" > "$B/sdk-id"
fi
cp "$MANIFEST" "$B/sdk-manifest"

# The ScreenScraper pair into a header, read in the shell so make prints
# neither (mk/cross.mk, creds)
SS_DEVID="$(sed -n 's/^SS_DEVID=//p' /zyperos-ss.env 2> /dev/null | head -1)" \
SS_DEVPASS="$(sed -n 's/^SS_DEVPASS=//p' /zyperos-ss.env 2> /dev/null | head -1)" \
	make -s -f mk/cross.mk BUILD=$B creds
# The Pixel 2's programs (mk/cross.mk's device-programs: the launcher, MP4
# and the helpers config/device/pixel2.mk lists), and Diatom for its port
make -j"$JOBS" -f mk/cross.mk CC="$CC" SYSROOT="$SYSROOT" DEVICE=pixel2 BUILD=$B \
	VERSION="$VERSION" device-programs
PORT=$(sed -n 's/^ZYDEV_DIATOM_PORT = *//p' config/device/pixel2.mk)
make -j"$JOBS" -C diatom PORT="$PORT" CC="$CC"

# The system's files a deploy can stand in for (tools/px2-deploy.sh
# --system), where the image has them, built as plastron's packages build
# them: the ports' equalizer (package/zyperos-eq, its arithmetic
# src/eqdsp.h), the side lights' gauge (package/gauge), and the sound's ALSA
# config (board/px2's /etc/asound.conf). Stripped as the image strips them.
S=$B/system
PL=plastron/buildroot
rm -rf "$S"
mkdir -p "$S/usr/lib/ladspa" "$S/usr/sbin" "$S/etc"
"$CC" -O2 -fPIC -shared -Wall -Wextra -Isrc -o "$S/usr/lib/ladspa/zyperos_eq.so" \
	$PL/package/zyperos-eq/src/zyperos_eq.c -lm
strip_as_image --strip-unneeded "$S/usr/lib/ladspa/zyperos_eq.so"
"$CC" -O2 -Wall -Wextra -o "$S/usr/sbin/gauge" $PL/package/gauge/src/gauge.c $PL/package/gauge/src/lights.c
strip_as_image "$S/usr/sbin/gauge"
cp $PL/board/px2/rootfs-overlay/etc/asound.conf "$S/etc/asound.conf"
# and the one call a deploy needs from a release image's Developer SSH, whose
# mount namespace is its own, to bind them where the rest of the device
# looks (tools/initns.c)
"$CC" -O2 -Wall -Wextra -o "$B/initns" tools/initns.c
strip_as_image "$B/initns"

# The programs alone (tools/px2-payload.sh --programs): stripped, as the
# payload's are, where ZyperOS/ has them, with the card's scripts as the
# payload has them (mk/card-scripts.sh): launch.sh, device.sh, eq-port.sh,
# your PICO-8's and OpenBOR's
if [ -n "${PROGRAMS_ONLY:-}" ]; then
	P=$B/programs/ZyperOS
	rm -rf "$B/programs"
	mkdir -p "$P"
	for f in zyperos.elf mp4d portkeys zyperos-update zyhome p2d; do
		[ -f "$B/$f" ] && strip_as_image -o "$P/$f" "$B/$f" && chmod +x "$P/$f"
	done
	strip_as_image -o "$P/diatom" "diatom/build/$PORT/diatom"
	chmod +x "$P/diatom"
	# Your PICO-8's library, where the payload has it, its exported calls kept
	mkdir -p "$P/pico8"
	strip_as_image --strip-unneeded -o "$P/pico8/p8fx.so" "$B/p8fx.so"
	DEVICE=pixel2 sh mk/card-scripts.sh "$P"
	exit 0
fi

DEVICE=pixel2 OUT=/zyperos/$B/payload VERSION="$VERSION" \
	ZYPEROS_ELF=$B/zyperos.elf MP4D_ELF=$B/mp4d PORTKEYS_ELF=$B/portkeys ZYHOME_ELF=$B/zyhome \
	UPDATE_ELF=$B/zyperos-update P2D_ELF=$B/p2d P8FX_SO=$B/p8fx.so DIATOM_ELF="diatom/build/$PORT/diatom" \
	VENDOR_DIR=/zyperos-vendor sh mk/payload.sh
# The image's tools for SSH (plastron's zyperos.mk builds them for /usr/bin),
# beside the payload rather than in it: tools/px2 copies px2-perf to a device
# whose image is older than it
for t in px2-shot px2-input px2-perf; do
	"$CC" -O2 -Wall -Wextra -o "$B/$t" "tools/$t.c" -lm
	strip_as_image "$B/$t"
done
# Stripped, as plastron's image strips every program it carries
for f in zyperos.elf mp4d portkeys zyperos-update zyhome p2d diatom; do
	strip_as_image "$B/payload/ZyperOS/$f"
done
strip_as_image --strip-unneeded "$B/payload/ZyperOS/pico8/p8fx.so"
