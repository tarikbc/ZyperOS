/* SPDX-License-Identifier: GPL-3.0-only */
/* The R36S: the GKD Pixel 2's device file (src/device/pixel2.c), whose RK3326,
 * RK817, Panfrost and DRM handover it shares, with what tells the two apart
 * (docs/devices/r36s.md, "The hardware"):
 *
 * - its folder on the card, .userdata/r36s;
 * - its keys as ROCKNIX's R36S device tree sends them: the A cap BTN_EAST and
 *   the B cap BTN_SOUTH (the Pixel 2's the other way round), FN BTN_MODE;
 * - L2 and R2 plain keys: it has no battery lights for them to ask for;
 * - its sticks (RK_STICKS): the left one moves as the d-pad, and their
 *   clicks are L3 and R3.
 *
 * Its panel scans 640x480 as it is seen, so pixel2.c's turn, which follows
 * the panel's mode, is none here. Nothing in this file was run on an R36S
 * yet: the key codes are the device tree's, not measured (evtest on a real
 * one says; docs/devices/r36s.md, "For a tester"). */
#include <linux/input.h>

#define RK_ID            "r36s"
#define RK_CODE_A        BTN_EAST
#define RK_CODE_B        BTN_SOUTH
#define RK_CODE_FUNCTION BTN_MODE
#define RK_L2R2_TAPS     0
#define RK_STICKS        1

#include "pixel2.c"
