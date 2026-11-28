/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_BRICK_INPUT_H
#define ZYPEROS_BRICK_INPUT_H

/* The TrimUI Brick's and the Smart Pro's controls as their pads send them
 * (src/device/devinput.h). brick-pad sends the Pixel 2's codes: A, B, X and
 * Y as SOUTH, EAST, NORTH and WEST, named for their labels, and the d-pad as
 * BTN_DPAD_*; MENU BTN_MODE; L2 and R2 plain keys. The Brick's two front
 * keys, its brightness (F1 and F2), are BTN_THUMBL and BTN_THUMBR, and it
 * has no stick. The Smart Pro's reader (tools/sppad.c, tools/sppad_frame.c)
 * sends the same codes but no front keys, and its two sticks: the left
 * (ABS_X, ABS_Y) the d-pad on the launcher's screens, the right (ABS_RX,
 * ABS_RY) its own four; sppad already takes their dead middle out, so none
 * more here. The profiles and Diatom's Brick port read these tables. */
#include "devinput.h"
#include "evcode.h"

#define BRICK_FAMILY_KEYS \
	{ EVC_BTN_DPAD_UP,   IN_UP },     { EVC_BTN_DPAD_DOWN,  IN_DOWN }, \
	{ EVC_BTN_DPAD_LEFT, IN_LEFT },   { EVC_BTN_DPAD_RIGHT, IN_RIGHT }, \
	{ EVC_BTN_SOUTH,     IN_ACCEPT }, { EVC_BTN_EAST,       IN_BACK }, \
	{ EVC_BTN_NORTH,     IN_X },      { EVC_BTN_WEST,       IN_Y }, \
	{ EVC_BTN_TL,        IN_L1 },     { EVC_BTN_TR,         IN_R1 }, \
	{ EVC_BTN_TL2,       IN_L2 },     { EVC_BTN_TR2,        IN_R2 }, \
	{ EVC_BTN_SELECT,    IN_SELECT }, { EVC_BTN_START,      IN_START }, \
	{ EVC_BTN_MODE,      IN_MENU }, \
	{ EVC_KEY_VOLUMEUP,  IN_VOLUP },  { EVC_KEY_VOLUMEDOWN, IN_VOLDN }, \
	{ EVC_KEY_POWER,     IN_POWER }

static const dev_code BRICK_KEYS[] = {
	BRICK_FAMILY_KEYS,
	{ EVC_BTN_THUMBL, IN_BRIGHTDN }, { EVC_BTN_THUMBR, IN_BRIGHTUP },
};

static const dev_code SMARTPRO_KEYS[] = { BRICK_FAMILY_KEYS };

#define BRICK_INPUT { DEV_KEYS(BRICK_KEYS), { DEV_NO_STICK, DEV_NO_STICK } }
#define SMARTPRO_INPUT { DEV_KEYS(SMARTPRO_KEYS), { \
	{ true, EVC_ABS_X,  EVC_ABS_Y,  IN_LEFT,    0 }, \
	{ true, EVC_ABS_RX, EVC_ABS_RY, IN_RS_LEFT, 0 } } }

#endif
