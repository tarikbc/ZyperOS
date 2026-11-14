/* SPDX-License-Identifier: GPL-3.0-only */
/* See remap.h. Diatom's map language and the rules of changing a button, with
 * no SDL and no database. */
#include "remap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The handheld's buttons by the names a map says them (not MENU, which no map
 * moves), and the targets by Diatom's canonical names */
static const char *const FROM[RM_BUTTONS] = {
	"a", "b", "x", "y", "l1", "r1", "l2", "r2", "start", "select"
};
static const char *const TO[RT_COUNT] = {
	"up", "down", "left", "right", "a", "b", "x", "y",
	"l1", "r1", "l2", "r2", "select", "start"
};

/* Diatom's ceiling for a turbo's half-period (TURBO_PERIOD_MAX) */
#define TURBO_MAX 30

int rm_own(int button)
{
	switch (button) {
	case RM_START:  return RT_START;
	case RM_SELECT: return RT_SELECT;
	default:        return RT_A + button;
	}
}

void rm_identity(rm_table *t)
{
	int i;

	memset(t, 0, sizeof *t);
	for (i = 0; i < RM_BUTTONS; i++) t->b[i] = (rm_choice){ (signed char)rm_own(i), 0 };
}

static int index_of(const char *const *names, int count, const char *s, size_t n)
{
	int i;

	for (i = 0; i < count; i++)
		if (strlen(names[i]) == n && !strncmp(names[i], s, n)) return i;
	return -1;
}

/* One `from:to` pair of n bytes, as a choice for a button. False when it is
 * not one Diatom would take. */
static bool read_pair(const char *p, size_t n, int *button, rm_choice *c)
{
	const char *colon = memchr(p, ':', n), *tilde, *to;
	size_t tn;
	int t;

	if (!colon) return false;
	*button = index_of(FROM, RM_BUTTONS, p, (size_t)(colon - p));
	if (*button < 0) return false;
	to = colon + 1;
	tilde = memchr(to, '~', n - (size_t)(to - p));
	tn = tilde ? (size_t)(tilde - to) : n - (size_t)(to - p);
	c->turbo = 0;
	if (tn == 4 && !strncmp(to, "none", 4)) {
		if (tilde) return false;
		c->to = RM_NONE;
		return true;
	}
	t = index_of(TO, RT_COUNT, to, tn);
	if (t < 0) return false;
	if (tilde) {
		char num[8];
		size_t dn = n - (size_t)(tilde + 1 - p);
		char *end;
		long v;

		if (dn == 0 || dn >= sizeof num) return false;
		memcpy(num, tilde + 1, dn);
		num[dn] = '\0';
		v = strtol(num, &end, 10);
		if (*end || v < 1 || v > TURBO_MAX) return false;
		c->turbo = (unsigned char)v;
	}
	c->to = (signed char)t;
	return true;
}

/* A layer pair, `held+from:to`, only checked: held and from are handheld
 * buttons, to a target or none, and no turbo (as Diatom's) */
static bool layer_pair_ok(const char *p, size_t n)
{
	const char *plus = memchr(p, '+', n), *colon = memchr(p, ':', n);
	size_t tn;

	if (!plus || !colon || plus > colon) return false;
	if (index_of(FROM, RM_BUTTONS, p, (size_t)(plus - p)) < 0) return false;
	if (index_of(FROM, RM_BUTTONS, plus + 1, (size_t)(colon - plus - 1)) < 0) return false;
	tn = n - (size_t)(colon + 1 - p);
	if (memchr(colon + 1, '~', tn)) return false;
	return (tn == 4 && !strncmp(colon + 1, "none", 4)) ||
	       index_of(TO, RT_COUNT, colon + 1, tn) >= 0;
}

/* The pairs of a list onto out, which is not touched on a bad one. A layer
 * pair is kept only where layers are allowed (a map, not what is stored). */
static bool read_pairs(const char *spec, rm_table *out, bool layers)
{
	rm_table next = *out;
	const char *p = spec;

	while (*p) {
		const char *end = strchr(p, ',');
		size_t n = end ? (size_t)(end - p) : strlen(p);
		int button;
		rm_choice c;

		if (memchr(p, '+', n)) {
			size_t have = strlen(next.layer);

			if (!layers || !layer_pair_ok(p, n)) return false;
			if (have + (have ? 1 : 0) + n >= sizeof next.layer) return false;
			if (have) next.layer[have++] = ',';
			memcpy(next.layer + have, p, n);
			next.layer[have + n] = '\0';
		} else {
			if (!read_pair(p, n, &button, &c)) return false;
			next.b[button] = c;
		}
		if (!end) break;
		p = end + 1;
	}
	*out = next;
	return true;
}

bool rm_parse(const char *spec, rm_table *out)
{
	rm_table next;

	rm_identity(&next);
	if (spec && *spec && strcmp(spec, "identity") && !read_pairs(spec, &next, true))
		return false;
	*out = next;
	return true;
}

static bool same(rm_choice a, rm_choice b)
{
	return a.to == b.to && a.turbo == b.turbo;
}

/* One pair, "x:a~3", at out + at; the new at */
static size_t put_pair(char *out, size_t n, size_t at, int button, rm_choice c)
{
	int w;

	if (at >= n) return at;
	if (c.to == RM_NONE)
		w = snprintf(out + at, n - at, "%s%s:none", at ? "," : "", FROM[button]);
	else if (c.turbo)
		w = snprintf(out + at, n - at, "%s%s:%s~%d", at ? "," : "", FROM[button], TO[c.to],
		             c.turbo);
	else
		w = snprintf(out + at, n - at, "%s%s:%s", at ? "," : "", FROM[button], TO[c.to]);
	return w < 0 ? at : at + (size_t)w;
}

void rm_format(const rm_table *t, char *out, size_t n)
{
	size_t at = 0;
	int i;

	if (!n) return;
	out[0] = '\0';
	for (i = 0; i < RM_BUTTONS; i++)
		if (!same(t->b[i], (rm_choice){ (signed char)rm_own(i), 0 }))
			at = put_pair(out, n, at, i, t->b[i]);
	if (t->layer[0] && at < n)
		snprintf(out + at, n - at, "%s%s", at ? "," : "", t->layer);
	if (!out[0]) snprintf(out, n, "identity");
}

void rm_store(const rm_table *def, const rm_table *t, char *out, size_t n)
{
	size_t at = 0;
	int i;
	char pairs[256] = "";

	if (!n) return;
	for (i = 0; i < RM_BUTTONS; i++)
		if (!same(def->b[i], t->b[i])) at = put_pair(pairs, sizeof pairs, at, i, t->b[i]);
	if (!at) out[0] = '\0';
	else snprintf(out, n, "1;%s", pairs);
}

bool rm_load(const rm_table *def, const char *stored, rm_table *out)
{
	rm_table next = *def;

	*out = *def;
	if (!stored || !*stored) return true;
	if (strncmp(stored, "1;", 2)) return false;
	if (stored[2] && !read_pairs(stored + 2, &next, false)) return false;
	*out = next;
	return true;
}

bool rm_equal(const rm_table *a, const rm_table *b)
{
	int i;

	for (i = 0; i < RM_BUTTONS; i++)
		if (!same(a->b[i], b->b[i])) return false;
	return !strcmp(a->layer, b->layer);
}

bool rm_changed(const rm_table *def, const rm_table *t, int button)
{
	return !same(def->b[button], t->b[button]);
}

void rm_set(rm_table *t, const rm_table *base, int button, rm_choice c)
{
	rm_table next = *base;
	int q;

	next.b[button] = c;
	if (!c.turbo && c.to != RM_NONE) {
		for (q = 0; q < RM_BUTTONS; q++) {
			if (q == button || base->b[q].turbo || base->b[q].to != c.to) continue;
			next.b[q] = base->b[button];
			break;
		}
	}
	*t = next;
}

int rm_unreached(const rm_table *def, const rm_table *t, int *targets, int max)
{
	bool in_def[RT_COUNT] = { false }, in_t[RT_COUNT] = { false };
	int i, n = 0;

	for (i = 0; i < RM_BUTTONS; i++) {
		if (def->b[i].to != RM_NONE) in_def[def->b[i].to] = true;
		if (t->b[i].to != RM_NONE) in_t[t->b[i].to] = true;
	}
	for (i = 0; i < RT_COUNT && n < max; i++)
		if (in_def[i] && !in_t[i]) targets[n++] = i;
	return n;
}

void rm_faces(rm_table *t, const rm_table *layout)
{
	int i;

	for (i = RM_A; i <= RM_Y; i++) t->b[i] = layout->b[i];
}

int rm_faces_of(const rm_table *t, const rm_table *letters)
{
	bool by_letter = true, by_place = true;
	int i;

	for (i = RM_A; i <= RM_Y; i++) {
		if (!same(t->b[i], letters->b[i])) by_letter = false;
		if (!same(t->b[i], (rm_choice){ (signed char)rm_own(i), 0 })) by_place = false;
	}
	return by_letter ? 0 : by_place ? 1 : 2;
}
