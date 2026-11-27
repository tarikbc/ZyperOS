/* SPDX-License-Identifier: GPL-3.0-only */
/* The R36S's build and card (src/device/build.h): the Pixel 2's chip, a
 * Cortex-A35 on the aarch64 devices' A53 baseline, its Diatom port the
 * Pixel 2's with its keys (diatom/port/r36s.c), and the Pixel 2's card: MBR,
 * R36SBOOT at 1, root A at 2, games at 3, root B at 4 (plastron's
 * board/r36s/genimage.cfg and board/zyperos/post-image-slots.sh).
 *
 * Its helpers for a first test card: Home (MENU+START, tools/zyhome.c, which
 * knows its FN) and System Update. Not yet PortMaster's ports and their
 * keys (tools/portkeys.c reads the Pixel 2's MENU code alone), NES Player 2
 * or your PICO-8's Shader and Border: each when it is tried on one. */
#include "build.h"

const dev_build device_build_r36s = {
	.id = "r36s",
	.arch = "aarch64",
	.mcpu = "cortex-a53",
	.cpu = "cortex-a35",
	/* Its own platform layer, src/device/r36s.c: the Pixel 2's with the
	 * R36S's keys */
	.backend = "r36s",
	.diatom_port = "r36s",
	.table = "mbr",
	.part_boot = 1,
	.part_root_a = 2,
	.part_root_b = 4,
	.part_games = 3,
	.helpers = DEV_HELPER_ZYHOME | DEV_HELPER_UPDATE,
};
