/* SPDX-License-Identifier: GPL-3.0-only */
/* A handheld model's parts (src/device/td_model.h). No SDL. */
#include "td_model.h"
#include "../design.h"

#include <stddef.h>

const td_part *td_part_of(const td_model *m, td_key k)
{
	int i;

	for (i = 0; m && i < m->n_parts; i++)
		if (m->parts[i].key == k) return &m->parts[i];
	return NULL;
}

bool td_spins(const td_model *m, unsigned lit)
{
	bool ledge = false, off = false;
	int i;

	for (i = 0; m && i < m->n_parts; i++) {
		if (!(lit >> m->parts[i].key & 1u)) continue;
		if (m->parts[i].face == TD_FACE_LEDGE) ledge = true;
		else off = true;
	}
	return ledge && off;
}

float td_swing(int ms)
{
	const int leg = DS_MS_HOLD_SPIN + DS_MS_SPIN;
	int at;

	if (ms <= 0) return 0.0f;
	at = ms % (2 * leg);
	/* Held on the front, turning round, held on the back, turning back */
	if (at < DS_MS_HOLD_SPIN) return 0.0f;
	if (at < leg) return ds_turn((float)(at - DS_MS_HOLD_SPIN) / DS_MS_SPIN);
	if (at < leg + DS_MS_HOLD_SPIN) return 1.0f;
	return 1.0f - ds_turn((float)(at - leg - DS_MS_HOLD_SPIN) / DS_MS_SPIN);
}

td_view td_view_mix(td_view a, td_view b, float k)
{
	return (td_view){ a.deg + (b.deg - a.deg) * k, a.s + (b.s - a.s) * k,
	                  a.pitch + (b.pitch - a.pitch) * k };
}

int td_flat_parts(const td_model *m, td_flat_part *out, int max)
{
	/* A key the flat drawing says nothing of: blank, its keyboard key
	 * towards the body, or on it for a shoulder */
	static const td_flat_key blank_front = { .hint = TD_HINT_BELOW }, blank_left = { .hint = TD_HINT_RIGHT },
	                         blank_right = { .hint = TD_HINT_LEFT }, blank_top = { .hint = TD_HINT_ON };
	const td_flat *f = m ? m->flat : NULL;
	int i, j, n = 0;

	if (!f) return 0;
	for (i = 0; i < m->n_parts && n < max; i++) {
		const td_part *p = &m->parts[i];
		const td_flat_key *look = p->face == TD_FACE_FRONT ? &blank_front : p->face == TD_FACE_LEFT ? &blank_left
		                        : p->face == TD_FACE_RIGHT ? &blank_right : &blank_top;
		td_flat_part *o;

		/* The sticks' caps are the flat drawing's sticks (td_flat_stick), not
		 * keys of it */
		if (p->key == TD_L3 || p->key == TD_R3) continue;
		o = &out[n++];

		for (j = 0; j < f->n_keys; j++)
			if (f->keys[j].key == p->key) look = &f->keys[j];
		o->key = p->key;
		o->look = look;
		switch (p->face) {
		case TD_FACE_FRONT:
			o->kind = p->key == TD_DPAD ? TD_FLAT_DPAD : look->thick > 0 ? TD_FLAT_PILL : TD_FLAT_ROUND;
			o->x = p->x;
			o->y = p->z;
			o->w = 2 * p->r;
			o->h = look->thick > 0 ? look->thick : 2 * p->r;
			break;
		case TD_FACE_LEFT:
		case TD_FACE_RIGHT:
			o->kind = TD_FLAT_SIDE;
			o->x = (p->face == TD_FACE_LEFT ? -1 : 1) * (m->body_w / 2 + f->side_out);
			o->y = look->len > 0 ? look->y : p->z;
			o->w = f->side_w;
			o->h = look->len > 0 ? look->len : 2 * p->r;
			break;
		default:
			/* The top edge and a back ledge: above the top edge either way,
			 * as a front view sees them stand */
			o->kind = TD_FLAT_SHOULDER;
			o->x = p->x;
			o->y = m->body_h / 2 + f->shoulder_rise;
			o->w = 2 * p->r;
			o->h = f->shoulder_h;
			break;
		}
	}
	return n;
}
