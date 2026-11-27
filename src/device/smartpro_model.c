/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Smart Pro's 3D drawing as data (src/device/td_model.h), in the
 * millimetres of its published outline (188 x 80 x 17) and the key places
 * measured on the line drawing in TrimUI's user manual, at 7.8 of its
 * pixels a millimetre (the numbers are the ones in this file): the d-pad,
 * A B X Y, MENU, SELECT and START on the front; and on the top edge L and
 * R toward the front, L2 and R2 behind them, POWER and the volume keys,
 * minus left of plus. The two sticks are drawn, but are no keys of the
 * launcher's (the left one is the d-pad's, src/stickpad.h). Drawn by
 * src/tourdevice.c's Smart Pro art. No SDL. */
#include "td_model.h"

#include <stddef.h>

static const td_part smartpro_parts[] = {
	/* Its half span; the arms 7.0 wide */
	{ TD_DPAD,   TD_FACE_FRONT, -76.7f,  13.65f, 9.6f },
	/* The diamond about (75.8, 13.6), each key 7.4 from it, 7.5 across */
	{ TD_A,      TD_FACE_FRONT,  83.2f,  13.6f,  3.75f },
	{ TD_B,      TD_FACE_FRONT,  75.8f,   6.2f,  3.75f },
	{ TD_X,      TD_FACE_FRONT,  75.8f,  21.0f,  3.75f },
	{ TD_Y,      TD_FACE_FRONT,  68.4f,  13.6f,  3.75f },
	/* The round keys under the sticks, 6.6 across */
	{ TD_MENU,   TD_FACE_FRONT, -67.6f, -28.1f,  3.3f },
	{ TD_SELECT, TD_FACE_FRONT,  66.9f, -28.2f,  3.3f },
	{ TD_START,  TD_FACE_FRONT,  76.5f, -28.2f,  3.3f },
	/* On the top edge (z 40), up to their tops: their half widths */
	{ TD_L1,     TD_FACE_TOP,   -76.3f,  41.0f, 13.6f },
	{ TD_R1,     TD_FACE_TOP,    76.3f,  41.0f, 13.6f },
	{ TD_L2,     TD_FACE_TOP,   -74.7f,  43.0f, 11.2f },
	{ TD_R2,     TD_FACE_TOP,    74.7f,  43.0f, 11.2f },
	{ TD_POWER,  TD_FACE_TOP,   -35.4f,  40.8f,  5.0f },
	{ TD_VOLDN,  TD_FACE_TOP,    25.2f,  40.8f,  5.6f },
	{ TD_VOLUP,  TD_FACE_TOP,    36.5f,  40.8f,  5.6f },
};

const td_model td_model_smartpro = {
	.name = "TrimUI Smart Pro",
	.art = TD_ART_SMARTPRO,
	.body_w = 188.0f,
	.body_h = 80.0f,
	/* Every key is far out at one side or the other: none in the middle */
	.lean_centre = 20.0f,
	.parts = smartpro_parts,
	.n_parts = (int)(sizeof smartpro_parts / sizeof smartpro_parts[0]),
	/* Wide and low: in the tutorial's column, which a 16:9 canvas widens
	 * (src/tourdraw.c), turned a little so its depth shows */
	.front = { 8.0f, 2.85f, 0.0f },
	/* No keys on its sides: a side's view is its front, but for how far it
	 * may turn (facing_of reads the right's) */
	.right = { 52.0f, 2.85f, 0.0f },
	.left = { -52.0f, 2.85f, 0.0f },
	.both = { 8.0f, 2.85f, 0.0f },
	/* Tilted back, so the top edge and its keys show */
	.top = { 0.0f, 2.85f, TD_PITCH_DEG },
	/* No back ledge: the front */
	.back = { 8.0f, 2.85f, 0.0f },
	/* The Controls tab's front pose, as wide as its column (280) */
	.tab = { 8.0f, 1.3f, 0.0f },
	/* Every vertex of every pose and turn, a halo's faint edge too, as
	 * --handheld-box measures it under --device smartpro (td_box_holds),
	 * with two units to spare: the tutorial's reaches x 2.5 to 559.2 and y
	 * 2.4 to 273.9 of its box (from the column's corner, the 4:3 canvas's
	 * 0 at x; src/tourdevice.c adds a wider canvas's half difference), the
	 * Controls tab's x 2.9 to 281.1 and y 2.1 to 183.5 of its */
	.keep_tour = { -143, 109, 562, 276 },
	.keep_tab = { 142, 101, 284, 186 },
	.flat = NULL,
};
