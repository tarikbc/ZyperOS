#!/bin/sh
# Assemble out/sdcard.img from the kernel, device tree, boot script, root
# filesystem and the bootloader (Rockchip's pieces with our U-Boot; make
# bootloader).
set -eu
BOARD_DIR=$(dirname "$0")

cp "$BOARD_DIR/bootloader/u-boot-px2.bin" "$BINARIES_DIR/"
# boot.scr, bootstate.txt, genimage, and root B as partition 4
sh "$BR2_EXTERNAL_PX2_PATH/board/zyperos/post-image-slots.sh" "$BOARD_DIR"
