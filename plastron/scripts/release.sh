#!/bin/sh
# Runs inside the build container (make release), once per device. Builds the
# device's ZyperOS image without the developer's SSH key and puts what a
# release carries in /work/release, for the Makefile to copy out:
#
#   ZyperOS-v<version>-<device>.img.xz        the card image, compressed
#   ZyperOS-v<version>-<device>-sources.tar   the licenses and sources of
#                                            everything in it
#   ZyperOS-v<version>-<device>-update.zpk    System Update's pack and its
#   ZyperOS-<device>-update.txt.unsigned     list, for ZyperOS's release.sh
#                                            to sign (the Pixel 2's)
#   ZyperOS-v<version>-<device>-system.zpk    the same for the system (root,
#   ZyperOS-<device>-system.txt.unsigned     kernel, device tree), for a card
#                                            with two system slots
#
# <device> is pixel2 or brick. The Makefile writes SHA256SUMS over both
# devices' files once both are built.
#
# Each build starts from an empty output folder of its own, never the
# everyday build's: Buildroot never takes out a file a package stopped
# installing, so an image built on top of older builds can carry dead files
# (the Brick's carried an old /usr/share/zyperos/brick, 2026-10-24).
set -eu

VERSION=$1
DEVICE=$2
case $DEVICE in
pixel2) FLAVOR=zyperos ;;
brick) FLAVOR=brick ;;
*) echo "release: unknown device $DEVICE (pixel2 or brick)" >&2; exit 1 ;;
esac
NAME=ZyperOS-v$VERSION-$DEVICE
OUTPUT=/work/release-output-$DEVICE
RELEASE=/work/release
SOURCES=$OUTPUT/legal-info/sources

# Its output's lock before emptying it; build.sh finds it held (scripts/lock.sh)
. /src/scripts/lock.sh
plastron_lock "$OUTPUT"
rm -rf "$OUTPUT"
PLASTRON_RELEASE=1 FLAVOR=$FLAVOR PLASTRON_OUTPUT=$OUTPUT sh /src/scripts/build.sh all legal-info

# The package list on the card and in the repository is this build's: one
# that no longer matches would credit what the image doesn't carry, or miss
# what it does
BOARD=$( [ "$DEVICE" = pixel2 ] && echo px2 || echo brick )
python3 /src/scripts/third-party.py "$OUTPUT/legal-info/manifest.csv" "$DEVICE" > /tmp/third-party-$DEVICE.md
if ! cmp -s /tmp/third-party-$DEVICE.md "/src/buildroot/board/$BOARD/third-party-packages.md"; then
	echo "release: buildroot/board/$BOARD/third-party-packages.md doesn't match this build's packages:" >&2
	diff "/src/buildroot/board/$BOARD/third-party-packages.md" /tmp/third-party-$DEVICE.md >&2 || true
	echo "release: run make third-party, check and commit the list, and build again" >&2
	exit 1
fi

# Checked, not trusted: the build has no key of its own to leave behind, but a
# leftover in the output from a development build would ship to everyone
if [ -e "$OUTPUT/target/root/.ssh" ]; then
	echo "release: $OUTPUT/target/root/.ssh is in the image" >&2
	exit 1
fi
if [ -e "$OUTPUT/target/etc/init.d/S50dropbear" ]; then
	echo "release: dropbear's start script is in the image" >&2
	exit 1
fi
# And the program itself is: on the Pixel 2, ZyperOS's USB File Transfer
# starts it for SSH over the cable, with the screen's PIN, while that screen
# is open, and makes its host key with dropbearkey (ZyperOS src/sshlink.h)
if [ "$DEVICE" = pixel2 ]; then
	for f in usr/sbin/dropbear usr/bin/dropbearkey; do
		if [ ! -x "$OUTPUT/target/$f" ]; then
			echo "release: /$f is not in the image; USB File Transfer's SSH needs it" >&2
			exit 1
		fi
	done
fi

if [ "$DEVICE" = pixel2 ]; then
	# U-Boot is built outside Buildroot (make bootloader) and committed, so
	# legal-info has no source for it. Its source is the tarball it is built
	# from plus our patches and config, which go in beside the rest.
	UBOOT_VERSION=$(sed -n 's/^UBOOT_VERSION=//p' /src/scripts/build-bootloader.sh)
	UBOOT=$SOURCES/u-boot-$UBOOT_VERSION
	mkdir -p "$UBOOT"
	cp "/work/dl/u-boot-$UBOOT_VERSION.tar.gz" "$UBOOT/"
	cp -R /src/buildroot/board/px2/u-boot/. "$UBOOT/plastron/"
	# Its BL31 is Rockchip's, a binary like the DDR init: no source to add
else
	# The Brick's kernel and the modules it loads are built outside Buildroot
	# (make brick-kernel), so legal-info has no source for them: the tree as
	# Creality published it (with the GPU's kernel driver, dual MIT/GPL), our
	# patches (the defconfig is in them), the device tree, and how it's built
	KERNEL=$SOURCES/linux-brick
	mkdir -p "$KERNEL/plastron"
	cp /work/dl/linux-4.9.191-a133.tar.xz "$KERNEL/"
	cp -R /src/buildroot/board/brick/linux /src/buildroot/board/brick/dts "$KERNEL/plastron/"
	cp /src/scripts/build-brick-kernel.sh /src/docker/brick-kernel/Dockerfile "$KERNEL/plastron/"
	# Its boot loader too (make brick-bootloader, committed): U-Boot and TF-A
	# at their commits, with our patches
	UBOOT_COMMIT=$(sed -n 's/^UBOOT_COMMIT=//p' /src/scripts/build-brick-bootloader.sh)
	TFA_COMMIT=$(sed -n 's/^TFA_COMMIT=//p' /src/scripts/build-brick-bootloader.sh)
	BOOT=$SOURCES/u-boot-brick
	mkdir -p "$BOOT/plastron"
	cp "/work/dl/u-boot-$UBOOT_COMMIT.tar.gz" "/work/dl/tf-a-$TFA_COMMIT.tar.gz" "$BOOT/"
	cp -R /src/buildroot/board/brick/u-boot/. "$BOOT/plastron/"
	cp /src/scripts/build-brick-bootloader.sh /src/docker/brick-boot/Dockerfile "$BOOT/plastron/"
	# The closed parts are in the image and nowhere else: their packages are
	# marked not to be redistributed, so legal-info leaves their files out
	cat > "$OUTPUT/legal-info/PROPRIETARY.txt" <<-EOF
	The TrimUI Brick's image carries two closed parts that are not ours, are
	not in these sources, and are not distributed on their own:

	- Imagination Technologies' PowerVR DDK 1.11@5516664 userspace libraries
	  and firmware for the GE8300 GPU (OpenGL ES, EGL, the null window
	  system, rgx.fw.22.102.54.38), as Allwinner ships them for the A133,
	  under Imagination's own terms. Package pvr-ge8300.
	- XRadio's firmware for the XR829 Wi-Fi and Bluetooth chip
	  (boot_xr829.bin, fw_xr829.bin, sdd_xr829.bin, fw_xr829_bt.bin), with no
	  license stated. Package xr829-firmware.

	Both as Creality's Sonic Pad SDK has them (github.com/CrealityTech/
	sonic_pad_os at 7f37a7f).
	EOF
fi
# Copied from a working tree on a Mac, so Finder's files come too
find "$SOURCES" -name .DS_Store -delete

# Buildroot itself is GPL-2.0+, and its makefiles and patches built every
# package in the image, but legal-info never saves it (it says so on every
# run). The release tarball the build started from goes in.
BUILDROOT_VERSION=$(sed -n 's/^BUILDROOT_VERSION=//p' /src/scripts/build.sh)
mkdir -p "$OUTPUT/legal-info/buildroot-source"
cp "/work/dl/buildroot-$BUILDROOT_VERSION.tar.xz" "$OUTPUT/legal-info/buildroot-source/"

# The libretro cores ZyperOS fetches are not Buildroot packages, so
# legal-info has no source for them: their sources are in ZyperOS's cores
# release, each copyleft one at the commit of the binary it ships
# (ZyperOS's mk/release-cores.sh). Said here, where the rest of the sources
# are.
CORES_TAG=$(sed -n 's/^CORES_TAG=//p' /zyperos/mk/fetch-vendor.sh 2>/dev/null || true)
if [ -n "$CORES_TAG" ]; then
	cat > "$OUTPUT/legal-info/CORES-SOURCE.txt" <<-EOF
	The libretro cores in ZyperOS/cores that mk/fetch-vendor.sh fetches are
	not built here, so their sources are not in this archive. They are in
	ZyperOS's cores release, $CORES_TAG:

	  https://github.com/tarikbc/ZyperOS/releases/tag/$CORES_TAG

	with the exact binaries, and for each core under a GPL or an LGPL an
	archive of its source at the commit of that binary,
	<core>-<commit>-source.tar.gz, all in its SHA256SUMS. Each core, its
	license and its source are listed in THIRD-PARTY-LICENSES.md in
	ZyperOS's repository and in the card's Licenses folder.
	EOF
fi

mkdir -p "$RELEASE"
rm -f "$RELEASE/$NAME.img.xz" "$RELEASE/$NAME-sources.tar"
xz -T0 -9 -c "$OUTPUT/images/sdcard.img" > "$RELEASE/$NAME.img.xz"
tar -C "$OUTPUT" -cf "$RELEASE/$NAME-sources.tar" legal-info
ls -l "$RELEASE/$NAME.img.xz" "$RELEASE/$NAME-sources.tar"

# System Update's pack and list (ZyperOS's tools/update-pack.py and
# docs/updates.md): this image's own ZyperOS folder, so an update over the
# air installs exactly what a freshly written card gets. The list comes out
# unsigned: ZyperOS's tools/release.sh signs it on the Mac that holds the
# key, which this container never sees. A ZyperOS from before System Update
# has no packer, and its release has no update.
CARDDIR=$OUTPUT/target/usr/share/zyperos/card
rm -f "$RELEASE/$NAME-update.zpk" "$RELEASE/ZyperOS-$DEVICE-update.txt.unsigned"
if [ -f /zyperos/tools/update-pack.py ] && [ -f "$CARDDIR/ZyperOS/zyperos-update" ]; then
	# What's New on the device: the release notes' first points, signed
	# with the list
	python3 /zyperos/tools/update-pack.py --payload "$CARDDIR/ZyperOS" --card-extra "$CARDDIR" \
		--version "$VERSION" --device "$DEVICE" --out "$RELEASE" \
		--notes "/zyperos/docs/releases/v$VERSION.md"
	ls -l "$RELEASE/$NAME-update.zpk" "$RELEASE/ZyperOS-$DEVICE-update.txt.unsigned"
	# And the system itself, for a card with two system slots: the root, the
	# kernel and the device tree, which zyperos-update writes into the slot
	# that is not running (ZyperOS's docs/updates.md, layer 2). The same
	# key signs it.
	SYS=/tmp/release-system-$DEVICE
	rm -rf "$SYS"
	mkdir -p "$SYS/boot"
	cp "$OUTPUT/images/rootfs.squashfs" "$SYS/root.squashfs"
	cp "$OUTPUT/images/Image" "$SYS/boot/"
	for dtb in "$OUTPUT/images/"*.dtb; do cp "$dtb" "$SYS/boot/"; done
	rm -f "$RELEASE/$NAME-system.zpk" "$RELEASE/ZyperOS-$DEVICE-system.txt.unsigned"
	python3 /zyperos/tools/update-pack.py --kind system --payload "$SYS" \
		--version "$VERSION" --device "$DEVICE" --out "$RELEASE" \
		--notes "/zyperos/docs/releases/v$VERSION.md"
	ls -l "$RELEASE/$NAME-system.zpk" "$RELEASE/ZyperOS-$DEVICE-system.txt.unsigned"
fi
