/* SPDX-License-Identifier: GPL-3.0-only */
/* The R36S's 3D drawing as data (src/device/td_model.h), in millimetres.
 * Its outline, 80 x 119 mm, is the maker's product sheet's; the key places
 * are measured on a straight-on photo of a real one (TechTactician's
 * hands-on review, 2023-12, its unboxing photo deskewed at 8 px a mm, which
 * also gives 1.50 for its height over its width, the sheet's 119 over 80),
 * the sides on the same review's side photos, the shoulders on iFixit's
 * R36S teardown's straight photo of the back (6.8 px a mm) and the depths
 * on the review's photo of the bottom (18.8 mm to the grip, 29.7 in all).
 * docs/devices/r36s.md lists them. Not measured with calipers: a volunteer
 * with an R36S in hand moves a number here and says so beside it.
 *
 * The front: the glass from the top edge down, the d-pad on the left and A
 * B X Y on the right under it (A right, B bottom, X top, Y left: the
 * product sheet that shows them the other way round is a mirrored render),
 * the speaker's grille between them, FN, then SELECT and START, and the two
 * sticks at the bottom, which are no keys of the launcher's (art only). The
 * left side: POWER, a pill, and the reset hole under it (art only); the
 * right side: the volume rocker, VOL+ towards the top. The body is a thin
 * part and, below the middle, a thicker grip behind it, as the Brick's is:
 * the four shoulders stand on that step (the back ledge), L1 and R1 outside.
 * A side key's x is its depth across the face: the thin part's middle,
 * 5.75 mm in front of the body's (the front at -15.0, the thin part's back
 * at 3.5, the grip's at 15.0). Drawn by src/tourdevice.c's R36S art. No
 * SDL. */
#include "td_model.h"

static const td_part r36s_parts[] = {
	/* Its half span, 21.5 across; the arms 7.5 wide */
	{ TD_DPAD,   TD_FACE_FRONT, -25.0f, -13.5f,  10.75f },
	/* The diamond about (25.6, -14.1), each key 8.0 from it, 7.2 across */
	{ TD_A,      TD_FACE_FRONT,  33.6f, -14.1f,  3.6f },
	{ TD_B,      TD_FACE_FRONT,  25.6f, -22.1f,  3.6f },
	{ TD_X,      TD_FACE_FRONT,  25.6f,  -6.1f,  3.6f },
	{ TD_Y,      TD_FACE_FRONT,  17.6f, -14.1f,  3.6f },
	/* FN, the launcher's MENU, in the middle under the grille; SELECT and
	 * START under it either side, all three 7.2 across */
	{ TD_MENU,   TD_FACE_FRONT,   0.0f, -22.9f,  3.6f },
	{ TD_SELECT, TD_FACE_FRONT,  -7.4f, -30.4f,  3.6f },
	{ TD_START,  TD_FACE_FRONT,   7.4f, -30.4f,  3.6f },
	/* The sticks' caps, 15 across in their 22 mm wells, pressed as L3 and
	 * R3 (and the left one moving as the d-pad, src/device/pixel2.c) */
	{ TD_L3,     TD_FACE_FRONT, -26.3f, -38.6f,  7.5f },
	{ TD_R3,     TD_FACE_FRONT,  26.3f, -38.6f,  7.5f },
	/* POWER on the left side, a pill 8.0 long; the volume rocker on the
	 * right, its two halves 6.6 long each, meeting at z 39.7 */
	{ TD_POWER,  TD_FACE_LEFT,   -5.75f, 40.0f,  4.0f },
	{ TD_VOLUP,  TD_FACE_RIGHT,  -5.75f, 43.0f,  3.3f },
	{ TD_VOLDN,  TD_FACE_RIGHT,  -5.75f, 36.4f,  3.3f },
	/* Tall paddles on the back ledge (the grip's top, z 9.5, 50 mm under
	 * the top edge), standing up to 21.0 (38 mm under it): their half
	 * widths, L1 and R1 15.0 wide near the body's edges, L2 and R2 14.4
	 * wide beside them, 12 mm apart in the middle */
	{ TD_L1,     TD_FACE_LEDGE, -30.7f,  21.0f,  7.5f },
	{ TD_L2,     TD_FACE_LEDGE, -13.1f,  21.0f,  7.2f },
	{ TD_R2,     TD_FACE_LEDGE,  13.1f,  21.0f,  7.2f },
	{ TD_R1,     TD_FACE_LEDGE,  30.7f,  21.0f,  7.5f },
};

/* Drawn flat (td_model.h): desk mode's window and the Simulator's. The
 * white R36S of the product sheet: a light body, black keys, A B X Y's
 * letters on their caps, FN printed over its key and SELECT and START
 * under theirs, the two sticks, no side lights. */
static const td_flat_key r36s_flat_keys[] = {
	{ .key = TD_X, .label = "X", .hint = TD_HINT_ABOVE },
	{ .key = TD_Y, .label = "Y", .hint = TD_HINT_LEFT },
	{ .key = TD_A, .label = "A", .hint = TD_HINT_RIGHT },
	{ .key = TD_B, .label = "B", .hint = TD_HINT_BELOW },
	{ .key = TD_MENU, .hint = TD_HINT_RIGHT },
	{ .key = TD_SELECT, .under = "SELECT", .hint = TD_HINT_LEFT },
	{ .key = TD_START, .under = "START", .hint = TD_HINT_RIGHT },
	{ .key = TD_L1, .label = "L1", .hint = TD_HINT_ON },
	{ .key = TD_L2, .label = "L2", .hint = TD_HINT_ON },
	{ .key = TD_R2, .label = "R2", .hint = TD_HINT_ON },
	{ .key = TD_R1, .label = "R1", .hint = TD_HINT_ON },
	{ .key = TD_VOLUP, .label = "+", .hint = TD_HINT_LEFT },
	{ .key = TD_VOLDN, .label = "-", .hint = TD_HINT_LEFT },
	{ .key = TD_POWER, .hint = TD_HINT_RIGHT },
};
static const td_flat_label r36s_flat_labels[] = {
	{ "FN", 0.0f, -17.3f, false },
	{ "POWER", -43.6f, 40.0f, true }, { "VOL", 43.6f, 39.7f, true },
};
/* The sticks, at the bottom either side: their wells 22 across, their caps
 * 15 (no keys of the launcher's: drawn, never pressed) */
static const td_flat_stick r36s_flat_sticks[] = {
	{ -26.3f, -38.6f, 11.0f, 7.5f }, { 26.3f, -38.6f, 11.0f, 7.5f },
};
static const td_flat r36s_flat = {
	.corner = 4.5f,
	.margin_x = 6.0f, .margin_top = 4.0f, .margin_bottom = 2.5f,
	.glass = { 0, 31.2f, 77.0f, 55.6f }, .glass_r = 2.0f,
	/* 3.5 in at 4:3 */
	.screen = { 0, 31.2f, 70.0f, 52.5f },
	.dpad_arm = 3.75f,
	.side_out = 0.8f, .side_w = 3.0f, .shoulder_rise = 0.65f, .shoulder_h = 3.6f,
	.body_hi = 0xF4F4F1, .body = 0xE5E5E1, .body_lo = 0xD0D0CB, .recess_c = 0xC6C6C1,
	.cap = 0x2A2B2E, .cap_down = 0x141517, .marking = 0x6A6D72, .glass_c = 0x0A0B0D, .light_off = 0xC6C6C1,
	.keys = r36s_flat_keys, .n_keys = (int)(sizeof r36s_flat_keys / sizeof r36s_flat_keys[0]),
	.labels = r36s_flat_labels, .n_labels = (int)(sizeof r36s_flat_labels / sizeof r36s_flat_labels[0]),
	.sticks = r36s_flat_sticks, .n_sticks = (int)(sizeof r36s_flat_sticks / sizeof r36s_flat_sticks[0]),
};

const td_model td_model_r36s = {
	.name = "R36S",
	.art = TD_ART_R36S,
	.body_w = 80.0f,
	.body_h = 119.0f,
	/* FN, SELECT and START are the middle; Y, at 17.6, is the right's */
	.lean_centre = 11.0f,
	.parts = r36s_parts,
	.n_parts = (int)(sizeof r36s_parts / sizeof r36s_parts[0]),
	/* The Brick's views, a little smaller for its 119 mm against the
	 * Brick's 109.9, so it is as tall in the tutorial's column */
	.front = { 10.0f, 2.4f, 0.0f },
	.right = { 52.0f, 2.65f, 0.0f },
	.left = { -52.0f, 2.65f, 0.0f },
	.both = { 10.0f, 2.4f, 0.0f },
	.top = { 10.0f, 2.4f, 0.0f },
	/* Round to the back the right side's way and tilted, as the Brick */
	.back = { 160.0f, 2.25f, 20.0f },
	.tab = { 10.0f, 2.1f, 0.0f },
	/* Every vertex of every pose and turn, a halo's faint edge too, as
	 * --handheld-box measures it under --device r36s (src/tourdevice.c,
	 * td_box_holds), with a unit or more to spare: the tutorial's reaches
	 * x 11.2 to 255.3 and y 100.0 to 422.9 on the screen, the Controls
	 * tab's x -107.2 to 110.7 and y -144.2 to 143.0 about its middle */
	.keep_tour = { 10, 95, 256, 334 },
	.keep_tab = { 111, 148, 224, 294 },
	.flat = &r36s_flat,
};
