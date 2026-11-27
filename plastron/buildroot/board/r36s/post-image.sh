#!/bin/sh
# Assemble out/r36s-sdcard.img: the Pixel 2's way (board/px2/post-image.sh),
# with the R36S's boot script, device trees and boot partition. Its
# bootloader is the Pixel 2's, made by make bootloader: the same RK3326
# U-Boot ROCKNIX builds for its RK3326 handhelds (the ODROID-GO2 board, with
# the K36 clones'), whose PX30S-only part (its patch 0004) does nothing on a
# plain RK3326. Whether it starts an R36S is the first thing a test card
# tells (docs/devices/r36s.md, "The bootloader").
set -eu
BOARD_DIR=$(dirname "$0")

cp "$BOARD_DIR/../px2/bootloader/u-boot-px2.bin" "$BINARIES_DIR/"
cp "$BOARD_DIR/r36s-boot.txt" "$BINARIES_DIR/"
# The known panels' overlays (overlays/*.dts says where each comes from),
# which r36s-boot.txt's zy_panel= picks from
cp "$BOARD_DIR"/overlays/panel-*.dtbo "$BINARIES_DIR/"
# boot.scr (with the R36S's own tail, board/r36s/boot-slots.cmd),
# bootstate.txt, genimage, and root B as partition 4
sh "$BR2_EXTERNAL_PX2_PATH/board/zyperos/post-image-slots.sh" "$BOARD_DIR" "$BOARD_DIR/boot-slots.cmd"
