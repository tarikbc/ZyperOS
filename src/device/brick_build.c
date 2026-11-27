/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Brick's build and card (src/device/build.h). Its A133P is a
 * Cortex-A53. The card as plastron lays it out (board/brick/genimage.cfg):
 * GPT, BRICKBOOT at 1, root A at 2, plastron's state at 3, root B at 4 (the
 * Pixel 2's numbers for the roots, so both boot through one bootstate
 * script, board/zyperos/bootstate.cmd), and the games partition datapart
 * adds after it at the first start, at 5. Its payload carries System
 * Update's program. */
#include "build.h"

const dev_build device_build_brick = {
	.id = "brick",
	.arch = "aarch64",
	.mcpu = "cortex-a53",
	.cpu = "cortex-a53",
	.backend = "brick",
	.diatom_port = "brick",
	.table = "gpt",
	.part_boot = 1,
	.part_root_a = 2,
	.part_root_b = 4,
	.part_games = 5,
	.helpers = DEV_HELPER_UPDATE | DEV_HELPER_BTPLAYER | DEV_HELPER_BTALSA,
};
