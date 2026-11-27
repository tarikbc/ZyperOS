/* R36S port: the GKD Pixel 2's (port/pixel2.c), whose RK3326, Panfrost, KMS
 * flip thread, RK817 sound and levels it shares, with the R36S's keys as
 * ROCKNIX's R36S device tree sends them (the A cap BTN_EAST, the B cap
 * BTN_SOUTH, FN BTN_MODE) and its folder on the card. Its panel scans
 * 640x480 as it is seen, so pixel2.c's turn, which follows the panel's mode,
 * is none here.
 *
 * Its two sticks (RK_STICKS): the joypad reports ABS_X, ABS_Y, ABS_RX and
 * ABS_RY, read as the RetroPad's analog sticks, and L3 and R3 as BTN_THUMBL
 * and BTN_THUMBR. */
#include <linux/input.h>

#define RK_PORT          "r36s"
#define RK_CODE_A        BTN_EAST
#define RK_CODE_B        BTN_SOUTH
#define RK_CODE_FUNCTION BTN_MODE
#define RK_STICKS        1

#include "pixel2.c"
