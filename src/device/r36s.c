/* SPDX-License-Identifier: GPL-3.0-only */
/* The R36S: the GKD Pixel 2's device file (src/device/pixel2.c), whose RK3326,
 * RK817, Panfrost and DRM handover it shares, with what tells the two apart
 * (docs/devices/r36s.md, "The hardware"):
 *
 * - its folder on the card, .userdata/r36s;
 * - its keys and its two sticks, its profile's table (src/device/
 *   r36s_input.h): the A cap BTN_EAST and the B cap BTN_SOUTH (the Pixel 2's
 *   the other way round), FN BTN_MODE, L2 and R2 plain keys (it has no
 *   battery lights for them to ask for), the left stick the d-pad, the right
 *   one its own four, their clicks L3 and R3;
 * - no headphone jack read (RK_JACK 0): on board R36S-V12 the device tree's
 *   jack line read "plugged" with nothing in it and with headphones
 *   (Sylver, 2026-11-26), so the volume keeps the speaker's window and the
 *   status bar shows no headphones. Neither ROCKNIX's R36S nor ArkOS nor
 *   dArkOSRE reads the line either;
 * - its charging also from the battery's own "Charging" (RK_DC_CHARGING):
 *   its DC port is watched by a GPIO of its own in its stock tree, which
 *   mainline's charger does not read.
 *
 * Its panel scans 640x480 as it is seen, so pixel2.c's turn, which follows
 * the panel's mode, is none here. Nothing in this file was run on an R36S
 * yet: the key codes are the device tree's, not measured (evtest on a real
 * one says; docs/devices/r36s.md, "For a tester"). */
#define RK_ID            "r36s"
#define RK_JACK          0
#define RK_DC_CHARGING   1

#include "pixel2.c"
