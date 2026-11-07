/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_INBUTTON_H
#define ZYPEROS_INBUTTON_H

/* The launcher's buttons, as every device reports them (src/platform.h's
 * in_state). A header of its own, with no SDL, so a file that the checks
 * link without SDL can name them: src/tour.c, whose key table and hand
 * mapping tools/tour-check.c holds. */
typedef enum {
	IN_LEFT, IN_RIGHT, IN_UP, IN_DOWN,
	IN_ACCEPT, IN_BACK, IN_X, IN_Y,
	IN_L1, IN_R1, IN_START, IN_SELECT, IN_MENU,
	IN_VOLUP, IN_VOLDN, IN_BRIGHTUP, IN_BRIGHTDN,
	IN_POWER,
	/* The lower shoulders: the menu music's track before and the next on
	 * the home and the games shelf (2026-11-06). Last, so the
	 * numbers of the others stand. On the Pixel 2 each is pressed when it
	 * is let go without the other, L2+R2 being the battery level there
	 * (src/device/pixel2.c). */
	IN_L2, IN_R2,
	IN_COUNT,
	IN_NONE = -1,
} in_button;

#endif
