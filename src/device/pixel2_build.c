/* SPDX-License-Identifier: GPL-3.0-only */
/* The GKD Pixel 2's build and card (src/device/build.h). Its RK3326 is a
 * Cortex-A35; the build's baseline is the A53 every aarch64 device runs,
 * as mk/cross.mk had it for both devices. The card: MBR, PX2BOOT at 1,
 * root A at 2, games at 3, root B at 4 (plastron's board/px2/genimage.cfg
 * and board/zyperos/post-image-slots.sh). */
#include "build.h"

const dev_build device_build_pixel2 = {
	.id = "pixel2",
	.arch = "aarch64",
	.mcpu = "cortex-a53",
	.cpu = "cortex-a35",
	.backend = "pixel2",
	.diatom_port = "pixel2",
	.table = "mbr",
	.part_boot = 1,
	.part_root_a = 2,
	.part_root_b = 4,
	.part_games = 3,
	.helpers = DEV_HELPER_PORTKEYS | DEV_HELPER_ZYHOME | DEV_HELPER_UPDATE | DEV_HELPER_P2D |
	           DEV_HELPER_PORTS,
};
