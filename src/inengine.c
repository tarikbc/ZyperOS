/* SPDX-License-Identifier: GPL-3.0-only */
/* The one engine from a handheld's codes to the launcher's buttons:
 * src/inengine.h. */
#include "inengine.h"

#include <string.h>

#include "inmodel.h"

void ie_init(inengine *e, const device_profile *p)
{
	bool (*take)(in_button) = e->take;
	void (*battery)(void) = e->battery;

	memset(e, 0, sizeof *e);
	e->p = p;
	e->take = take;
	e->battery = battery;
	e->vol_as[0] = e->vol_as[1] = IN_NONE;
	e->due = IN_NONE;
}

in_button ie_code(const device_profile *p, int code)
{
	int i;

	for (i = 0; p && i < p->input.n_keys; i++)
		if (p->input.keys[i].code == code) return p->input.keys[i].b;
	return IN_NONE;
}

/* MENU went down or up: held, and pressed as it is let go when no chord was
 * made under it */
static void menu_edge(inengine *e, const ie_sink *s, bool down)
{
	if (down && !e->menu) {
		e->menu = true;
		e->menu_chorded = false;
		s->hold(s->ctx, IN_MENU, true);
	} else if (!down && e->menu) {
		e->menu = false;
		s->hold(s->ctx, IN_MENU, false);
		if (!e->menu_chorded) s->press(s->ctx, IN_MENU);
	}
}

/* A volume key: brightness under MENU where the handheld has no keys of its
 * own for it; let go as whatever it went down as, so MENU let go first leaves
 * no brightness key stuck down */
static void volume_edge(inengine *e, const ie_sink *s, bool up, bool down)
{
	const int k = up ? 0 : 1;

	if (down) {
		if (e->p && e->p->chords.bright == DEV_BRIGHT_MENU_VOL && e->menu) {
			e->vol_as[k] = up ? IN_BRIGHTUP : IN_BRIGHTDN;
			e->menu_chorded = true;
		} else {
			e->vol_as[k] = up ? IN_VOLUP : IN_VOLDN;
		}
		s->set(s->ctx, e->vol_as[k], true);
	} else if (e->vol_as[k] != IN_NONE) {
		s->set(s->ctx, e->vol_as[k], false);
		e->vol_as[k] = IN_NONE;
	}
}

/* MENU's chord: true when it takes `b`'s edge, which nothing else sees */
static bool chord_edge(inengine *e, in_button b, bool down)
{
	if (!down) {
		if (!e->taken[b]) return false;
		e->taken[b] = false;
		return true;
	}
	if (!e->menu || !e->take || !e->take(b)) return false;
	e->taken[b] = true;
	e->menu_chorded = true;
	e->due = b;
	return true;
}

/* L2 or R2 as a tap let go, where both together are the battery's lights:
 * pressed as it is let go, and only when the other was not pressed with it
 * since both were last up */
static void tap_edge(inengine *e, const ie_sink *s, in_button b, bool down)
{
	const int i = b == IN_R2;
	const bool both = e->lr2[0] && e->lr2[1];

	e->lr2[i] = down;
	if (chord_edge(e, b, down)) {
		if (!e->lr2[0] && !e->lr2[1]) e->lr2_chorded = false;
		return;
	}
	if (!both && e->lr2[0] && e->lr2[1]) {
		if (e->p->chords.battery_chord && e->battery) e->battery();
		e->lr2_chorded = true;
	}
	if (!down && !e->lr2_chorded) {
		s->set(s->ctx, b, true);
		e->tap[b] = true;
	}
	if (!e->lr2[0] && !e->lr2[1]) e->lr2_chorded = false;
}

/* What `b` does as the hand takes it or lets it go */
static void edge(inengine *e, const ie_sink *s, in_button b, bool down)
{
	const in_info *in = in_info_of(b);

	switch (in->kind) {
	case IK_POWER:
		s->set(s->ctx, b, down);
		return;
	case IK_LEVEL:
		if (b == IN_VOLUP || b == IN_VOLDN) {
			volume_edge(e, s, b == IN_VOLUP, down);
			return;
		}
		break;   /* F1 and F2, the Brick's own: plain keys */
	case IK_KEY:
		if (b == IN_MENU) {
			menu_edge(e, s, down);
			return;
		}
		break;
	case IK_SHOULDER:
		if ((b == IN_L2 || b == IN_R2) && e->p->chords.lr2_taps) {
			tap_edge(e, s, b, down);
			return;
		}
		break;
	case IK_DPAD:
	case IK_FACE:
	case IK_CLICK:
	case IK_STICK:
		break;
	}
	if (chord_edge(e, b, down)) return;
	s->set(s->ctx, b, down);
}

/* `b` held by source `src` or not: what the hand holds changes when the
 * first source takes it or the last lets it go */
static void source(inengine *e, const ie_sink *s, in_button b, int src, bool down)
{
	bool was, now;

	if (!in_info_of(b) || !e->p) return;
	was = e->src[b] != 0;
	if (down) e->src[b] |= (uint8_t)(1u << src);
	else e->src[b] &= (uint8_t)~(1u << src);
	now = e->src[b] != 0;
	if (was == now) return;
	s->hand(s->ctx, b, now);
	edge(e, s, b, now);
}

bool ie_key(inengine *e, const ie_sink *s, int code, int value)
{
	const in_button b = ie_code(e->p, code);

	if (b == IN_NONE) return false;
	if (value != 2) source(e, s, b, IE_SRC_PAD, value != 0);
	return true;
}

void ie_button(inengine *e, const ie_sink *s, in_button b, bool down)
{
	source(e, s, b, IE_SRC_KEYS, down);
}

/* The stick and the axis (0 x, 1 y) `code` is, false for none */
static bool axis_of(const device_profile *p, int code, int *stick, int *xy)
{
	int i;

	for (i = 0; p && i < DEV_STICKS; i++) {
		const dev_stick *d = &p->input.stick[i];

		if (!d->present) continue;
		if (d->axis_x == code || d->axis_y == code) {
			*stick = i;
			*xy = d->axis_y == code;
			return true;
		}
	}
	return false;
}

bool ie_wants_axis(const device_profile *p, int code)
{
	int st, xy;

	return axis_of(p, code, &st, &xy);
}

void ie_axis(inengine *e, int code, int value, int min, int max, int flat)
{
	int st, xy;

	if (!axis_of(e->p, code, &st, &xy)) return;
	e->axis[st][xy].value = value;
	e->axis[st][xy].min = min;
	e->axis[st][xy].max = max;
	e->axis[st][xy].flat = flat;
	e->axis[st][xy].known = max > min;
	e->moved = true;
}

void ie_abs(inengine *e, int code, int value)
{
	int st, xy;

	if (!axis_of(e->p, code, &st, &xy)) return;
	e->axis[st][xy].value = value;
	e->moved = true;
}

/* Stick `i` where its axes are: each direction it holds or lets go */
static void stick_apply(inengine *e, const ie_sink *s, int i)
{
	const dev_stick *d = &e->p->input.stick[i];
	int rx, ry, x, y, k, dead[2];
	bool was[STICK_DIRS];

	if (!d->present || !e->axis[i][0].known || !e->axis[i][1].known) return;
	for (k = 0; k < 2; k++)
		dead[k] = e->axis[i][k].flat > d->dead ? e->axis[i][k].flat : d->dead;
	memcpy(was, e->stick[i].held, sizeof was);
	x = stickpad_centred(e->axis[i][0].value, e->axis[i][0].min, e->axis[i][0].max, dead[0], &rx);
	y = stickpad_centred(e->axis[i][1].value, e->axis[i][1].min, e->axis[i][1].max, dead[1], &ry);
	/* The two axes on one scale, the smaller range's */
	if (rx != ry && rx > 0 && ry > 0) {
		if (rx > ry) x = (int)((long)x * ry / rx);
		else y = (int)((long)y * rx / ry);
	}
	stickpad_update(&e->stick[i], x, y, rx < ry ? rx : ry);
	for (k = 0; k < STICK_DIRS; k++)
		if (e->stick[i].held[k] != was[k])
			source(e, s, (in_button)(d->first + k), IE_SRC_STICK_L + i, e->stick[i].held[k]);
}

void ie_sync(inengine *e, const ie_sink *s)
{
	int i;

	if (!e->moved || !e->p) return;
	e->moved = false;
	for (i = 0; i < DEV_STICKS; i++) stick_apply(e, s, i);
}

int ie_made_of(const device_profile *p, in_button b, in_button out[2])
{
	if ((b == IN_BRIGHTUP || b == IN_BRIGHTDN) && p && p->chords.bright == DEV_BRIGHT_MENU_VOL) {
		out[0] = IN_MENU;
		out[1] = b == IN_BRIGHTUP ? IN_VOLUP : IN_VOLDN;
		return 2;
	}
	out[0] = b;
	return 1;
}

void ie_poll_begin(inengine *e, const ie_sink *s)
{
	int b;

	for (b = 0; b < IN_COUNT; b++)
		if (e->tap[b]) {
			e->tap[b] = false;
			s->set(s->ctx, (in_button)b, false);
		}
}

in_button ie_due(inengine *e)
{
	const in_button b = e->due;

	e->due = IN_NONE;
	return b;
}

void ie_forget(inengine *e)
{
	int i, k;

	memset(e->src, 0, sizeof e->src);
	memset(e->taken, 0, sizeof e->taken);
	memset(e->tap, 0, sizeof e->tap);
	e->menu = e->menu_chorded = false;
	e->vol_as[0] = e->vol_as[1] = IN_NONE;
	e->due = IN_NONE;
	e->lr2[0] = e->lr2[1] = e->lr2_chorded = false;
	/* The sticks held nothing; where they are, they are, and the next
	 * report says what they hold */
	for (i = 0; i < DEV_STICKS; i++) {
		for (k = 0; k < STICK_DIRS; k++) e->stick[i].held[k] = false;
	}
	e->moved = true;
}
