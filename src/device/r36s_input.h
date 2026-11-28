/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_R36S_INPUT_H
#define ZYPEROS_R36S_INPUT_H

/* The R36S's controls as our R36S device tree sends them (src/device/
 * devinput.h; plastron/buildroot/board/r36s/dts, "r36s_Gamepad" on
 * rocknix-singleadc-joypad): the A cap BTN_EAST and the B cap BTN_SOUTH, the
 * Pixel 2's the other way round; FN, its MENU, BTN_MODE; L2 and R2 plain
 * keys, BTN_TL2 and BTN_TR2; the sticks' clicks BTN_THUMBL and BTN_THUMBR,
 * L3 and R3. (The stock dArkOSen 2604 tree sends other codes for SELECT,
 * START, FN and the clicks, its F1 to F5; ours is ROCKNIX's.) Its two
 * sticks, four ADC axes of -1800..1800 with a flat of 32: the left one
 * (ABS_X, ABS_Y) the d-pad on the launcher's screens, the right one (ABS_RX,
 * ABS_RY) its own four (IN_RS_*), which the tutorial answers and no screen
 * moves with. The tree inverts the left stick's axes itself (invert-absx,
 * invert-absy) and not the right's, so both report + right and down as they
 * come. Their dead middle is the stock tree's ADC deadzone, 384, where ours
 * gives the driver 64: a resting stick that drifts under it is still. Its
 * profile and Diatom's R36S port read this table. */
#include "devinput.h"
#include "evcode.h"

static const dev_code R36S_KEYS[] = {
	{ EVC_BTN_DPAD_UP,    IN_UP },     { EVC_BTN_DPAD_DOWN,  IN_DOWN },
	{ EVC_BTN_DPAD_LEFT,  IN_LEFT },   { EVC_BTN_DPAD_RIGHT, IN_RIGHT },
	{ EVC_BTN_EAST,       IN_ACCEPT }, { EVC_BTN_SOUTH,      IN_BACK },
	{ EVC_BTN_NORTH,      IN_X },      { EVC_BTN_WEST,       IN_Y },
	{ EVC_BTN_TL,         IN_L1 },     { EVC_BTN_TR,         IN_R1 },
	{ EVC_BTN_TL2,        IN_L2 },     { EVC_BTN_TR2,        IN_R2 },
	{ EVC_BTN_THUMBL,     IN_L3 },     { EVC_BTN_THUMBR,     IN_R3 },
	{ EVC_BTN_SELECT,     IN_SELECT }, { EVC_BTN_START,      IN_START },
	{ EVC_BTN_MODE,       IN_MENU },
	{ EVC_KEY_VOLUMEUP,   IN_VOLUP },  { EVC_KEY_VOLUMEDOWN, IN_VOLDN },
	{ EVC_KEY_POWER,      IN_POWER },
};

#define R36S_STICK_DEAD 384
#define R36S_INPUT { DEV_KEYS(R36S_KEYS), { \
	{ true, EVC_ABS_X,  EVC_ABS_Y,  IN_LEFT,    R36S_STICK_DEAD }, \
	{ true, EVC_ABS_RX, EVC_ABS_RY, IN_RS_LEFT, R36S_STICK_DEAD } } }

#endif
