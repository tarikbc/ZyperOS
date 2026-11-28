/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PIXEL2_INPUT_H
#define ZYPEROS_PIXEL2_INPUT_H

/* The GKD Pixel 2's controls as its kernel sends them (src/device/
 * devinput.h): ROCKNIX's rk3326s-gkd-pixel2.dts, measured 2026-10-01 pressing
 * each in a known order. The A, B, X and Y caps send SOUTH, EAST, NORTH and
 * WEST, named for their labels, not for where they sit (A is on the right
 * and B at the bottom). FUNCTION (BTN_TRIGGER_HAPPY1) is MENU. The volume
 * keys and POWER come on nodes of their own. No stick. Its profile and
 * Diatom's Pixel 2 port read this table. */
#include "devinput.h"
#include "evcode.h"

static const dev_code PIXEL2_KEYS[] = {
	{ EVC_BTN_DPAD_UP,        IN_UP },     { EVC_BTN_DPAD_DOWN,  IN_DOWN },
	{ EVC_BTN_DPAD_LEFT,      IN_LEFT },   { EVC_BTN_DPAD_RIGHT, IN_RIGHT },
	{ EVC_BTN_SOUTH,          IN_ACCEPT }, { EVC_BTN_EAST,       IN_BACK },
	{ EVC_BTN_NORTH,          IN_X },      { EVC_BTN_WEST,       IN_Y },
	{ EVC_BTN_TL,             IN_L1 },     { EVC_BTN_TR,         IN_R1 },
	{ EVC_BTN_TL2,            IN_L2 },     { EVC_BTN_TR2,        IN_R2 },
	{ EVC_BTN_SELECT,         IN_SELECT }, { EVC_BTN_START,      IN_START },
	{ EVC_BTN_TRIGGER_HAPPY1, IN_MENU },
	{ EVC_KEY_VOLUMEUP,       IN_VOLUP },  { EVC_KEY_VOLUMEDOWN, IN_VOLDN },
	{ EVC_KEY_POWER,          IN_POWER },
};

#define PIXEL2_INPUT { DEV_KEYS(PIXEL2_KEYS), { DEV_NO_STICK, DEV_NO_STICK } }

#endif
