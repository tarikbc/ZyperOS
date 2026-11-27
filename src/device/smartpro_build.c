/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Smart Pro's build and card (src/device/build.h). Its A133P is
 * the Brick's chip, a Cortex-A53, and it runs the Brick's platform layer
 * (src/device/brick.c) and Diatom port with its own profile: the Brick's
 * plastron board with its own boot loader (its memory is LPDDR4), device
 * tree and panel. Its card is the Brick's layout (plastron's
 * board/smartpro/genimage.cfg): GPT, SMARTBOOT at 1, root A at 2, plastron's
 * state at 3, root B at 4, the games partition at 5. Every button and both
 * sticks come from two serial halves, which sppad (tools/sppad.c) turns into
 * one gamepad from the system's start script. */
#include "build.h"

const dev_build device_build_smartpro = {
	.id = "smartpro",
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
	.helpers = DEV_HELPER_UPDATE | DEV_HELPER_BTPLAYER | DEV_HELPER_BTALSA | DEV_HELPER_SPPAD,
};
