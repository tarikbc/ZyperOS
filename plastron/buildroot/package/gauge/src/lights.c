/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See lights.h. */
#include "lights.h"

#include <stdio.h>
#include <string.h>

#define STEP_MS      400    /* the charging bar's pace */
#define LOW_EVERY_MS 5000   /* under LOW: one flash this often */
#define LOW_ON_MS    200
#define VLOW_EVERY_MS 2000  /* under VERY_LOW: two flashes this often */
#define VLOW_ON_MS   150    /* each, with as long between them */
#define ASK_MS       2000   /* L2+R2: four flashes in this long */
#define ASK_STEP_MS  250
#define HOLD_MS      1500   /* the level stays this long after its last change,
                             * as the screen's bar does (DESIGN.md hold-bar) */
#define PACE_MS      100    /* a step of the climb and the sparkle */
#define HALF_MS      250    /* a half step of the level: on this long, off as long */

/* Parts the setting file does not name are on after "auto" */
#define PARTS_AFTER_AUTO 1

/* n green lights from the bottom, n from 0 to 4 */
static int greens(int n)
{
	int bits = 0;

	for (int i = 0; i < n && i < 4; i++)
		bits |= L_GREEN << i;
	return bits;
}

/* One green light, 0 the bottom one */
static int dot(int i) { return L_GREEN << i; }

/* *w is the soonest the lights could change; ms from now is another */
static void soon(int *w, long ms)
{
	if (ms < 1)
		ms = 1;
	if (ms < *w)
		*w = (int)ms;
}

int lights_level_bits(int pct)
{
	if (pct >= 75) return greens(4);
	if (pct >= 50) return greens(3);
	if (pct >= 25) return greens(2);
	if (pct >= LOW) return greens(1);
	return L_RED;
}

void lights_parse(struct lights_cfg *c, const char *text)
{
	char buf[256], *p, *save = NULL;
	int first = 1, more;

	snprintf(buf, sizeof buf, "%s", text ? text : "");
	c->bar = c->low = 1;
	more = PARTS_AFTER_AUTO;
	for (p = strtok_r(buf, " \t\r\n", &save); p; p = strtok_r(NULL, " \t\r\n", &save)) {
		int on = *p != '-';
		const char *w = on ? p : p + 1;

		if (first) {
			first = 0;
			if (!strcmp(p, "auto")) continue;
			if (!strcmp(p, "charging")) { c->low = 0; more = 0; continue; }
			if (!strcmp(p, "low"))      { c->bar = 0; more = 0; continue; }
			if (!strcmp(p, "off"))      { c->bar = c->low = 0; more = 0; continue; }
			/* not one of the four: auto, and the word is a part */
		}
		if (first == 0 && more >= 0) {
			c->level = c->done = c->cheer = more;
			more = -1;   /* the defaults are set; only words from here */
		}
		if (!strcmp(w, "bar"))   c->bar = on;
		else if (!strcmp(w, "low"))   c->low = on;
		else if (!strcmp(w, "level")) c->level = on;
		else if (!strcmp(w, "done"))  c->done = on;
		else if (!strcmp(w, "cheer")) c->cheer = on;
	}
	if (more >= 0)
		c->level = c->done = c->cheer = more;
}

void lights_init(struct lights_state *s)
{
	memset(s, 0, sizeof *s);
	s->asked_at = -1;
}

void lights_battery(struct lights_state *s, int pct, int plugged, int held, long now)
{
	plugged = plugged != 0;
	held = plugged && held;
	if (!s->known || plugged != s->plugged || held != s->held)
		s->bar_at = now;
	if (!s->known || plugged != s->plugged
	    || (s->pct >= LOW && pct < LOW) || (s->pct >= VERY_LOW && pct < VERY_LOW))
		s->low_at = now;
	s->pct = pct;
	s->plugged = plugged;
	s->held = held;
	s->known = 1;
}

void lights_line(struct lights_state *s, const struct lights_cfg *c, const char *line, long now)
{
	char kind[16];
	int a, b;

	if (sscanf(line, "level %15s %d %d", kind, &a, &b) == 3) {
		int vol = !strcmp(kind, "volume");

		if (!c->level || b <= 0 || (!vol && strcmp(kind, "brightness")))
			return;
		if (s->ev != LE_LEVEL || s->ev_vol != vol) {
			s->ev = LE_LEVEL;
			s->ev_at = now;
			s->ev_vol = vol;
		}
		s->ev_last = now;
		s->ev_val = a < 0 ? 0 : a > b ? b : a;
		s->ev_max = b;
	} else if (!strcmp(line, "done")) {
		if (c->done) { s->ev = LE_DONE; s->ev_at = now; }
	} else if (!strcmp(line, "cheer")) {
		if (c->cheer) { s->ev = LE_CHEER; s->ev_at = now; }
	}
}

void lights_ask(struct lights_state *s, long now) { s->asked_at = now; }

/* The charging bar: the lights held for each full quarter, then one more each
 * STEP_MS up to all four, and again, in games as on the shelf. Still when
 * full; still at the level while the charger holds the battery at its limit. */
static int bar(const struct lights_state *s, long now, int *w)
{
	int held = s->pct >= 100 ? 4 : s->pct / 25;
	long t = now - s->bar_at;
	int steps;

	if (held >= 4)
		return greens(4);
	if (s->held)
		return greens(held > 1 ? held : 1);
	steps = 4 - held + 1;
	soon(w, STEP_MS - t % STEP_MS);
	return greens(held + (int)(t / STEP_MS % steps));
}

/* The pattern now showing, or -1 when it is over */
static int pattern(const struct lights_state *s, long now, int *w)
{
	long t = now - s->ev_at;

	switch (s->ev) {
	case LE_LEVEL: {
		long left = s->ev_last + HOLD_MS - now;
		int n;

		if (left <= 0)
			return -1;
		soon(w, left);
		if (s->ev_vol && s->ev_val <= 0)
			return L_RED;   /* silence */
		/* Eight steps: each light two, the one above the lit ones
		 * blinking for a half (my pick, 2026-11-06) */
		n = (8 * s->ev_val + s->ev_max - 1) / s->ev_max;
		if (n < 1)
			n = 1;
		if (n % 2 == 0)
			return greens(n / 2);
		soon(w, HALF_MS - t % HALF_MS);
		return greens(n / 2) | (t / HALF_MS % 2 ? 0 : dot(n / 2));
	}
	case LE_DONE:   /* one light runs up the four, twice */
		if (t >= 8 * PACE_MS)
			return -1;
		soon(w, PACE_MS - t % PACE_MS);
		return dot((int)(t / PACE_MS % 4));
	case LE_CHEER: {   /* two pairs take turns, three times each */
		long step = PACE_MS * 3 / 2;

		if (t >= 6 * step)
			return -1;
		soon(w, step - t % step);
		return t / step % 2 ? dot(1) | dot(3) : dot(0) | dot(2);
	}
	}
	return -1;
}

/* The red flash, off the cable: under LOW one every LOW_EVERY_MS, under
 * VERY_LOW two every VLOW_EVERY_MS. Its phase starts when it began, so the
 * first flash comes at once. */
static int low_red(const struct lights_cfg *c, const struct lights_state *s, long now, int *w)
{
	long t, u;

	if (!c->low || !s->known || s->plugged || s->pct >= LOW)
		return 0;
	t = now - s->low_at;
	if (t < 0)
		t = 0;
	if (s->pct < VERY_LOW) {
		static const long edge[] = { VLOW_ON_MS, 2 * VLOW_ON_MS, 3 * VLOW_ON_MS, VLOW_EVERY_MS };

		u = t % VLOW_EVERY_MS;
		for (int i = 0; i < 4; i++)
			if (u < edge[i]) {
				soon(w, edge[i] - u);
				return i == 0 || i == 2;
			}
		return 0;
	}
	u = t % LOW_EVERY_MS;
	soon(w, u < LOW_ON_MS ? LOW_ON_MS - u : LOW_EVERY_MS - u);
	return u < LOW_ON_MS;
}

int lights_frame(const struct lights_cfg *c, struct lights_state *s, long now, int *wait)
{
	int bits = 0, w = IDLE_MS;
	if (s->known && s->plugged && c->bar)
		bits = bar(s, now, &w);

	/* The newest pattern over the bar, for its time */
	if (s->ev != LE_NONE) {
		int pw = IDLE_MS, b = pattern(s, now, &pw);

		if (b < 0) {
			s->ev = LE_NONE;
		} else {
			bits = b;
			soon(&w, pw);
		}
	}

	/* Asked: four flashes of the level over the rest */
	if (s->asked_at >= 0) {
		long t = now - s->asked_at;

		if (t < ASK_MS && t >= 0) {
			bits = t / ASK_STEP_MS % 2 ? 0 : lights_level_bits(s->pct);
			soon(&w, ASK_STEP_MS - t % ASK_STEP_MS);
		} else {
			s->asked_at = -1;
		}
	}

	/* The red flash shows through everything: it is the bottom light's red,
	 * which no pattern uses but silence */
	if (low_red(c, s, now, &w))
		bits |= L_RED;

	*wait = w;
	return bits;
}
