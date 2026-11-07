#!/bin/sh
# Builds U-Boot the way ROCKNIX does for RK3326 (v2025.10, their patch, their
# config) plus board/px2/u-boot/px2.config, fetches Rockchip's prebuilt DDR
# init, miniloader and BL31 from rkbin and makes the card's first stage. Runs
# in the build container, whose native gcc is aarch64, so no cross-compiler is
# needed.
# Output in /work/bootloader: u-boot-dtb.bin, bl31.elf, idbloader.img and
# rkbin/, put together afterwards by pack-bootloader.sh.
set -eu

UBOOT_VERSION=v2025.10
UBOOT_SHA256=5414ee86562abc5ed524c6dc5e092511ad281441020a88e7027de6e83fd16116

# What each says about itself, fixed, so the same sources always build the
# same bytes
UBOOT_EPOCH=1792107000
BOARD=/src/buildroot/board/px2/u-boot
WORK=/work/bootloader
SRC=$WORK/u-boot-$UBOOT_VERSION

mkdir -p /work/dl
# One bootloader build at a time (scripts/lock.sh)
. /src/scripts/lock.sh
plastron_lock "$WORK"
mkdir -p "$WORK"
tarball=/work/dl/u-boot-$UBOOT_VERSION.tar.gz
[ -f "$tarball" ] || wget -q -O "$tarball" "https://github.com/u-boot/u-boot/archive/refs/tags/$UBOOT_VERSION.tar.gz"
echo "$UBOOT_SHA256  $tarball" | sha256sum -c -

rm -rf "$SRC"
mkdir -p "$SRC"
tar -C "$SRC" --strip-components=1 -xzf "$tarball"
cd "$SRC"
for p in "$BOARD"/patches/*.patch; do patch -p1 -F0 -s --batch < "$p"; done
cp -R "$BOARD"/sources/. .

make rk3326-handheld_defconfig
scripts/kconfig/merge_config.sh -m .config "$BOARD/px2.config"
make olddefconfig
SOURCE_DATE_EPOCH=$UBOOT_EPOCH make -j"$(nproc)" u-boot-dtb.bin
cp u-boot-dtb.bin "$WORK/"

# BL31: Rockchip's, not TF-A built from source. The DDR clock only moves when
# the firmware moves it: U-Boot has it train the RK3326S's DDR frequencies
# (FSP_INIT, patches/0004) and the kernel's DMC driver asks for one of them
# (ROCKCHIP_SIP_DRAM_FREQ, board/px2/patches/linux). Mainline TF-A for the
# PX30 has neither call, and the RAM stayed at the 333 MHz the DDR init sets,
# where the device's own firmware runs it at 1056: Stardew Valley's farm ran
# at 2-6 fps on that memory, 15 at 1056. Rockchip's v1.37 is what ROCKNIX
# ships with the same driver, beside the DDR init v2.12 below.

# Rockchip's prebuilt pieces, from the rkbin commit ROCKNIX pins: the DDR init
# and miniloader (the first stage), and the x86 tools that pack U-Boot and
# BL31 into the images the miniloader reads
RKBIN=74213af1e952c4683d2e35952507133b61394862
# The firmware pair ROCKNIX's RK3326 build uses with its DMC driver, from the
# newer rkbin commit its projects tree pins
RKBIN_FW=3e288fe814e059dd06833495f845cab04ac20a5c
mkdir -p "$WORK/rkbin"
rkbin() {
	f="$WORK/rkbin/$(basename "$1")"
	[ -f "$f" ] || wget -q -O "$f" "https://raw.githubusercontent.com/rockchip-linux/rkbin/${3:-$RKBIN}/$1"
	echo "$2  $f" | sha256sum -c - >/dev/null
}
rkbin bin/rk33/rk3326_ddr_333MHz_v2.12.bin 285a87e760a3586953f637b6de0fe80745b0ecf455dfb052bc3ecfc8b756cb7e $RKBIN_FW
rkbin bin/rk33/rk3326_bl31_v1.37.elf 1174a89ad30b7e4fb21d3f3794f23946f659c45de8e0414c9dbf717199f43b7b $RKBIN_FW
cp "$WORK/rkbin/rk3326_bl31_v1.37.elf" "$WORK/bl31.elf"
rkbin bin/rk33/rk3326_miniloader_v1.40.bin 473aacd5378f356757a71efeb54f4339925db154fbce288df7e3c1177f45bc28
rkbin tools/loaderimage e26a311d449c6254d0d89ecaf8cd1870438205e07a66b27265d68830b8977439
rkbin tools/trust_merger 059b622fc7597da572d8fcf726da3897e498985b3e922015f1413fb45f89963a
chmod +x "$WORK/rkbin/loaderimage" "$WORK/rkbin/trust_merger"

# The first stage the way ROCKNIX makes it: the DDR init through mkimage, then
# the miniloader appended after it
./tools/mkimage -n px30 -T rksd -d "$WORK/rkbin/rk3326_ddr_333MHz_v2.12.bin" "$WORK/idbloader.img" >/dev/null
cat "$WORK/rkbin/rk3326_miniloader_v1.40.bin" >> "$WORK/idbloader.img"
grep -E "^CONFIG_BOOTDELAY=" .config
