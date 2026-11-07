#!/bin/sh
# Runs inside the build container (make qemu): U-Boot's qemu_arm64 from the
# same release the Pixel 2's is built from (scripts/build-bootloader.sh), with
# board/qemu/u-boot.fragment, into /work/qemu-uboot/u-boot.bin. QEMU starts it
# (ZyperOS's tools/qemu-run.sh), and it runs the card's boot.scr, so the VM
# boots its system slots as the Pixel 2 does (ZyperOS's docs/updates.md).
# The container's own gcc is aarch64, as for the Pixel 2's.
set -eu
eval "$(grep -E '^UBOOT_(VERSION|SHA256)=' /src/scripts/build-bootloader.sh)"
WORK=/work/qemu-uboot
SRC=$WORK/u-boot-$UBOOT_VERSION
tarball=/work/dl/u-boot-$UBOOT_VERSION.tar.gz
# One QEMU U-Boot build at a time (scripts/lock.sh)
. /src/scripts/lock.sh
plastron_lock "$WORK"
mkdir -p /work/dl "$WORK"
[ -f "$tarball" ] || wget -q -O "$tarball" "https://github.com/u-boot/u-boot/archive/refs/tags/$UBOOT_VERSION.tar.gz"
echo "$UBOOT_SHA256  $tarball" | sha256sum -c - > /dev/null
rm -rf "$SRC"
mkdir -p "$SRC"
tar -C "$SRC" --strip-components=1 -xzf "$tarball"
cd "$SRC"
make qemu_arm64_defconfig > /dev/null
scripts/kconfig/merge_config.sh -m .config /src/buildroot/board/qemu/u-boot.fragment > /dev/null
make olddefconfig > /dev/null
make -j"$(nproc)" u-boot.bin > /dev/null
cp u-boot.bin "$WORK/u-boot.bin"
ls -l "$WORK/u-boot.bin"
