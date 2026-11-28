/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_DEVINPUT_H
#define ZYPEROS_DEVINPUT_H

/* What a handheld reports, as its profile states it (src/device/profile.h,
 * the `input` of device_profile): each key code it sends and the logical
 * button that code is (src/inmodel.h), and each stick it has, by its two
 * axes, with the buttons its four directions are and how much of its middle
 * is dead. ONE table a device, in src/device/<id>_input.h, which its
 * profile and Diatom's port of it both read, so the launcher and a game
 * take the same key for the same button. src/inengine.h turns what the
 * device reports into presses through it; no other file reads a code.
 *
 * A new handheld adds itself with one such table and its profile's line
 * `.input = <ID>_INPUT`; tools/inmodel-check.c then walks every button,
 * stick and chord of it through the tutorial and the hotkeys' capture.
 *
 * Plain data with no SDL and no Linux header (src/device/evcode.h has the
 * codes), so the checks link it on any computer. */

#include <stdbool.h>

#include "../inbutton.h"

/* An EV_KEY code and the button it is */
typedef struct {
	unsigned short code;
	in_button      b;
} dev_code;

/* The sticks: the left one, and the right one */
enum { DEV_STICK_L, DEV_STICK_R, DEV_STICKS };

typedef struct {
	bool           present;
	unsigned short axis_x, axis_y;   /* EV_ABS codes: + is right and down */
	/* The button its leftward push is, the next three right, up and down
	 * (src/inmodel.h's order): IN_LEFT where the stick is the d-pad on the
	 * launcher's screens, IN_RS_LEFT for the right stick's own */
	in_button      first;
	/* evdev units taken out of the middle before the push is measured, at
	 * least the axis's own flat: a stick resting off its middle by less is
	 * still (src/stickpad.h) */
	int            dead;
} dev_stick;

typedef struct {
	const dev_code *keys;
	int            n_keys;
	dev_stick      stick[DEV_STICKS];
} dev_input;

/* A table's rows and their count, for dev_input's first two */
#define DEV_KEYS(t) (t), (int)(sizeof (t) / sizeof (t)[0])
/* No stick */
#define DEV_NO_STICK { false, 0, 0, IN_NONE, 0 }

/* The button `code` is in `in`, IN_NONE for a code it does not send; and
 * the code of button `b`, -1 for none */
static inline in_button dev_button_of_code(const dev_input *in, int code)
{
	int i;

	for (i = 0; in && i < in->n_keys; i++)
		if (in->keys[i].code == code) return in->keys[i].b;
	return IN_NONE;
}

static inline int dev_code_of(const dev_input *in, in_button b)
{
	int i;

	for (i = 0; in && i < in->n_keys; i++)
		if (in->keys[i].b == b) return in->keys[i].code;
	return -1;
}

#endif
