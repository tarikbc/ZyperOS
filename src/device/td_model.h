/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TD_MODEL_H
#define ZYPEROS_TD_MODEL_H

/* A handheld's 3D drawing as data (docs/building.md,
 * Devices): where each of its keys is
 * and on which face, the views the tutorial and the Controls tab turn it to,
 * the boxes its kept textures need, and which art draws its body. What the
 * tutorial and the Controls tab work out from this - which way to turn for a
 * lesson, which way to lean for a row, which keys a lesson lights - follows
 * from the keys' places, so a second handheld is its own data and its own
 * art, not a copy of the logic.
 *
 * The art itself stays code (src/tourdevice.c): a handheld's caps, tabs and
 * edges are drawn by hand, and each device's are its own. It reads the key
 * places from here. Millimetres, as tools/build-pixel2-model.py measures the
 * Pixel 2: x right, z up, from the body's middle; on a side face, x is the
 * face's own across (the depth, front negative); on the top edge and on a
 * back ledge, a shoulder's x, its top's z and its half width. No SDL, so the
 * profiles and the checks link it as it is. */

#include <stdbool.h>

#include "../tdkey.h"

/* The faces a key can be on: the front, the sides, the top edge (the Pixel
 * 2's shoulders), and a ledge on the back, the top of a thicker part below
 * the top edge (the Brick's shoulders), which no front view shows */
typedef enum { TD_FACE_FRONT, TD_FACE_LEFT, TD_FACE_RIGHT, TD_FACE_TOP, TD_FACE_LEDGE } td_face;

/* Which art draws the body and its keys (src/tourdevice.c) */
typedef enum { TD_ART_PIXEL2, TD_ART_BRICK, TD_ART_SMARTPRO } td_art;

/* A key: where on which face, and how big - a round key's radius, an
 * elongated one's half length, a shoulder's half width */
typedef struct {
	td_key  key;
	td_face face;
	float   x, z, r;
} td_part;

/* How far turned (degrees, more than 0 shows the right side), how big
 * (units a millimetre), how far tilted back (degrees, more than 0 shows the
 * top) */
typedef struct { float deg, s, pitch; } td_view;

/* A kept texture's box: from the pose's middle, left and up, and its size */
typedef struct { int l, t, w, h; } td_box;

/* ---- the handheld drawn flat ---------------------------------------------
 *
 * Its front face straight on, with what stands out of its edges round it:
 * the shoulders along the top, the side keys out of the sides. Desk mode's
 * window draws it (src/device/deskface.c), and so does the Simulator, from
 * simulator/Sources/SimCore/Handhelds.swift, which `make handhelds` writes
 * from these same files (tools/handhelds-swift.c). The keys' places and
 * sizes are the model's own (td_flat_parts works them out); this adds how
 * the face looks. Millimetres from the body's middle, up positive. */

/* How a key is drawn flat: round or a pill on the front, the d-pad, a key
 * out of a side edge, a shoulder above the top edge */
typedef enum { TD_FLAT_ROUND, TD_FLAT_PILL, TD_FLAT_DPAD, TD_FLAT_SIDE, TD_FLAT_SHOULDER } td_flat_kind;

/* Where a key's keyboard key shows beside it */
typedef enum { TD_HINT_BELOW, TD_HINT_RIGHT, TD_HINT_LEFT, TD_HINT_ABOVE, TD_HINT_ON } td_hint;

/* What the flat drawing adds to one of the model's keys. A key with none
 * is drawn blank, its keyboard key beside it on the body's side. */
typedef struct {
	td_key      key;
	/* Printed on its cap ("" or NULL none); "\xE2\x96\xA0" and
	 * "\xE2\x96\xB6" are drawn as a square and a triangle */
	const char *label;
	/* Printed on the body under it, or NULL */
	const char *under;
	td_hint     hint;
	/* Its keyboard key nudged across, in points: for two keys side by side */
	float       hint_dx;
	/* A front key drawn as a pill this thick; 0 round */
	float       thick;
	/* A side key's middle and length along its edge, where the flat
	 * drawing's are not the model's (a rocker's halves meet; a turned
	 * key's height): len 0 for the model's */
	float       y, len;
} td_flat_key;

/* A word on the body: along an edge, turned, or flat */
typedef struct { const char *text; float x, y; bool turned; } td_flat_label;

/* A side light: its middle down the edge, and the lights (the LED class's
 * battery<N>) that light it green and red, -1 none; both lit, amber */
typedef struct { float y; signed char green, red; } td_flat_light;

/* A box: its middle and its size */
typedef struct { float x, y, w, h; } td_rect;

typedef struct td_flat {
	float   corner;                                /* the body's corners */
	/* Room round the body for what stands out of it */
	float   margin_x, margin_top, margin_bottom;
	td_rect glass;
	float   glass_r;
	td_rect screen;                                /* the lit picture */
	/* The word printed under the picture, or NULL */
	const char *word;
	float   word_y;
	/* SELECT and START's well, w 0 none */
	td_rect recess;
	float   recess_r;
	float   dpad_arm;                              /* half an arm's width */
	/* The side keys: their middles out of the body's edge, how wide they
	 * stand; the shoulders: their middles over the top edge, how tall */
	float   side_out, side_w, shoulder_rise, shoulder_h;
	/* The side lights, top to bottom: across, wide, tall */
	float   light_x, light_w, light_h;
	const td_flat_light *lights;
	int     n_lights;
	/* 0xRRGGBB: the body lit from above to below, the well, a key's cap up
	 * and pressed, what is printed, the glass, a light off */
	unsigned body_hi, body, body_lo, recess_c, cap, cap_down, marking, glass_c, light_off;
	const td_flat_key *keys;
	int     n_keys;
	const td_flat_label *labels;
	int     n_labels;
} td_flat;

/* One of the model's keys as the flat drawing has it: where, how big, how
 * drawn, and its look (never NULL: a blank one where the flat has none) */
typedef struct {
	td_key       key;
	td_flat_kind kind;
	float        x, y, w, h;
	const td_flat_key *look;
} td_flat_part;

typedef struct td_model {
	const char *name;
	td_art      art;
	float       body_w, body_h;
	/* Keys within this of the middle, across, lean the Controls tab no way:
	 * SELECT and START between the two halves */
	float       lean_centre;
	const td_part *parts;
	int         n_parts;
	/* The tutorial's views: facing front, turned to show the right side or
	 * the left, front with both sides' keys out of the edges, tilted back to
	 * show the top, and turned round to the back, tilted, to show a back
	 * ledge's keys (a model with no key on a face has its front there) */
	td_view     front, right, left, both, top, back;
	/* The Controls tab's front pose: its turn and its size (its pitch 0);
	 * its leans are off it (ct_lean) */
	td_view     tab;
	/* The kept textures' boxes: the tutorial's (from the screen's corner,
	 * as it stands still in its column) and the Controls tab's (from the
	 * pose's middle) */
	td_box      keep_tour, keep_tab;
	/* The handheld drawn flat (above), NULL where it has none yet: desk
	 * mode then shows the screen alone, and the Simulator cannot draw it */
	const td_flat *flat;
} td_model;

/* The flat drawing's keys of `m`, in the model's order, into `out` (room
 * for `max`): how many, 0 where `m` has no flat drawing */
int td_flat_parts(const td_model *m, td_flat_part *out, int max);

/* `k` on `m`, or NULL where `m` has no such key */
const td_part *td_part_of(const td_model *m, td_key k);

/* Whether the keys `lit` (1u << td_key) of `m` are on its back ledge and
 * off it: no one view shows both, so the handheld turns between the view
 * of the others and its back, front first (td_swing) */
bool td_spins(const td_model *m, unsigned lit);

/* How far round from the front view to the back, 0 to 1, `ms` after the
 * handheld came to rest on its front (0 before): held there for
 * **hold-spin**, turned in **spin** on the turn curve, held on its back,
 * turned back, and again, for as long as the keys stay lit */
float td_swing(int ms);

/* `a` to `b`, `k` of the way: the turn, the size and the tilt together */
td_view td_view_mix(td_view a, td_view b, float k);

extern const td_model td_model_pixel2;
extern const td_model td_model_brick;
extern const td_model td_model_smartpro;

#endif
