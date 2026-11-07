#!/bin/sh
# Assemble images/sdcard.img for QEMU from the kernel and the root filesystem
set -eu
BOARD_DIR=$(dirname "$0")

# boot.scr, bootstate.txt, genimage, and root B as partition 4
sh "$BR2_EXTERNAL_PX2_PATH/board/zyperos/post-image-slots.sh" "$BOARD_DIR"
