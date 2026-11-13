/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See lights.h. */
#include "lights.h"

#include <stdio.h>
#include <stdlib.h>
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
#define MOVE_SLOW    1000000ULL    /* files moving: under this many bytes a second, */
#define MOVE_SLOW_MS 200           /* this long a step; */
#define MOVE_FAST    10000000ULL   /* at this many and more, */
#define MOVE_FAST_MS 60            /* this long; between them, PACE_MS */
#define MOVE_LATE_MS 1000   /* woken this much after a step was due: on from
                             * now, with no catching up */

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

/* The same lights upside down: light i is light 3 - i */
static int upside_down(int bits)
{
	int out = bits & L_RED;

	for (int i = 0; i < 4; i++)
		if (bits & dot(i))
			out |= dot(3 - i);
	return out;
}

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

/* ---- files moving -----------------------------------------------------------
 *
 * My picks in the transfer lights lab (2026-11-13; ZyperOS's DESIGN.md 11.27):
 * a wave that fills the four and empties them ("Fill and empty"), the way the
 * files go, as the Flash Drive screen's arrows point: from the top down while
 * they come in to the card, from the bottom up while they go out of it. Its
 * pace follows the speed ("Follows the speed"); with both ways moving, the
 * one moving more bytes shows ("The bigger one"), chosen again as each run
 * begins, so a run is never cut. Files and Covers in MENU turns it on and off
 * with the climb, one row for both (my pick). */

/* One run, for files coming in: top to bottom */
static const int WAVE_IN[] = {
	L_GREEN << 3,
	L_GREEN << 3 | L_GREEN << 2,
	L_GREEN << 3 | L_GREEN << 2 | L_GREEN << 1,
	L_ALL,
	L_GREEN << 2 | L_GREEN << 1 | L_GREEN,
	L_GREEN << 1 | L_GREEN,
	L_GREEN,
	0,
};
#define WAVE_STEPS ((int)(sizeof WAVE_IN / sizeof WAVE_IN[0]))

static long move_pace(unsigned long long bps)
{
	return bps < MOVE_SLOW ? MOVE_SLOW_MS : bps < MOVE_FAST ? PACE_MS : MOVE_FAST_MS;
}

/* The way that moves more bytes; files coming in when it is even */
static int move_way(const struct lights_state *s) { return s->mv_in >= s->mv_out ? MV_IN : MV_OUT; }

static unsigned long long move_rate(const struct lights_state *s)
{
	return s->mv_way == MV_IN ? s->mv_in : s->mv_out;
}

/* "move IN OUT": two counts of digits, a space between them, nothing after */
static int move_parse(const char *line, unsigned long long *in, unsigned long long *out)
{
	char a[21], b[21], more;
	int end = 0;

	if (sscanf(line, "move %20[0-9]%n %20[0-9] %c", a, &end, b, &more) != 2
	    || (line[end] != ' ' && line[end] != '\t'))
		return 0;
	*in = strtoull(a, NULL, 10);
	*out = strtoull(b, NULL, 10);
	return 1;
}

/* The wave now, stepping it on; -1 when it is over */
static int moving(const struct lights_cfg *c, struct lights_state *s, long now, int *w)
{
	if (!c->done || now - s->mv_last >= MOVE_HOLD_MS) {
		s->mv_on = 0;   /* turned off, or no line for a while */
		return -1;
	}
	if (now - s->mv_next > MOVE_LATE_MS)
		s->mv_next = now;
	while (now >= s->mv_next) {
		if (++s->mv_step >= WAVE_STEPS) {
			s->mv_step = 0;
			s->mv_way = move_way(s);
		}
		s->mv_next += move_pace(move_rate(s));
	}
	soon(w, s->mv_next - now);
	soon(w, s->mv_last + MOVE_HOLD_MS - now);
	return s->mv_way == MV_IN ? WAVE_IN[s->mv_step] : upside_down(WAVE_IN[s->mv_step]);
}

/* ---- previews --------------------------------------------------------------
 *
 * MENU > Side Lights: the row under the cursor shows its part on the lights,
 * in a loop, whether the part is on or off (Side Lights' own page is where
 * you look at them; 2026-11-13). Each loop is the part's own pattern, short:
 *
 *   bar    the bar filling from none to four, STEP_MS a light
 *   low    one red flash, then the two of the second stage
 *   level  up the eight steps, the half light blinking, then the red of
 *          silence
 *   done   the wave down, the wave up, then Done's climb
 *   cheer  the sparkle
 */
static const char *const PREVIEW_WORD[] = { NULL, "bar", "low", "level", "done", "cheer" };
#define PV_LOW_MS   2600
#define PV_LEVEL_MS 500                  /* a step of the level's */
#define PV_DONE_MS  3000
#define PV_CHEER_MS 1500

/* The preview at t ms into it, and in *w the ms to its next change */
static int preview_bits(int part, long t, int *w)
{
	switch (part) {
	case PV_BAR:
		soon(w, STEP_MS - t % STEP_MS);
		return greens((int)(t / STEP_MS % 5));
	case PV_LOW: {
		static const long edge[] = { LOW_ON_MS, 1000, 1000 + VLOW_ON_MS, 1000 + 2 * VLOW_ON_MS,
		                             1000 + 3 * VLOW_ON_MS, PV_LOW_MS };
		long u = t % PV_LOW_MS;

		for (int i = 0; i < 6; i++)
			if (u < edge[i]) {
				soon(w, edge[i] - u);
				return i == 0 || i == 2 || i == 4 ? L_RED : 0;
			}
		return 0;
	}
	case PV_LEVEL: {
		long u = t % (9 * PV_LEVEL_MS);
		int n = (int)(u / PV_LEVEL_MS) + 1;   /* eighths, 9 the silence */

		soon(w, HALF_MS - t % HALF_MS);
		if (n > 8)
			return L_RED;
		return greens(n / 2) | (n % 2 && u % PV_LEVEL_MS < HALF_MS ? dot(n / 2) : 0);
	}
	case PV_DONE: {
		long u = t % PV_DONE_MS;

		if (u >= 24 * PACE_MS) {
			soon(w, PV_DONE_MS - u);
			return 0;
		}
		soon(w, PACE_MS - u % PACE_MS);
		if (u < 8 * PACE_MS)
			return WAVE_IN[u / PACE_MS];
		if (u < 16 * PACE_MS)
			return upside_down(WAVE_IN[u / PACE_MS - 8]);
		return dot((int)(u / PACE_MS % 4));
	}
	case PV_CHEER: {
		long u = t % PV_CHEER_MS, step = PACE_MS * 3 / 2;

		if (u >= 6 * step) {
			soon(w, PV_CHEER_MS - u);
			return 0;
		}
		soon(w, step - u % step);
		return u / step % 2 ? dot(1) | dot(3) : dot(0) | dot(2);
	}
	}
	return 0;
}

/* "preview WORD": the part's number, PV_NONE for "off", -1 for anything else */
static int preview_parse(const char *line)
{
	const char *w = line + 8;

	if (!strcmp(w, "off"))
		return PV_NONE;
	for (int i = PV_BAR; i <= PV_CHEER; i++)
		if (!strcmp(w, PREVIEW_WORD[i]))
			return i;
	return -1;
}

void lights_line(struct lights_state *s, const struct lights_cfg *c, const char *line, long now)
{
	char kind[16];
	int a, b;
	unsigned long long in, out;

	if (!strncmp(line, "preview ", 8)) {
		int part = preview_parse(line);

		if (part < 0)
			return;
		if (part != s->pv_part)
			s->pv_at = now;
		s->pv_part = part;
		s->pv_last = now;
		return;
	}

	if (!strncmp(line, "move ", 5)) {
		if (!c->done || !move_parse(line, &in, &out))
			return;
		if (!in && !out) {
			s->mv_on = 0;
			return;
		}
		s->mv_in = in;
		s->mv_out = out;
		s->mv_last = now;
		if (!s->mv_on) {
			s->mv_on = 1;
			s->mv_step = 0;
			s->mv_way = move_way(s);
			s->mv_next = now + move_pace(move_rate(s));
		}
		return;
	}

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
	int bits = 0, w = IDLE_MS, previewing = 0;
	if (s->known && s->plugged && c->bar)
		bits = bar(s, now, &w);

	/* Files moving, over the bar, for as long as they move */
	if (s->mv_on) {
		int b = moving(c, s, now, &w);

		if (b >= 0)
			bits = b;
	}

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

	/* MENU > Side Lights' preview over all of that, the low red included:
	 * it is what you are looking at */
	if (s->pv_part != PV_NONE) {
		if (now - s->pv_last >= PREVIEW_HOLD_MS) {
			s->pv_part = PV_NONE;
		} else {
			long t = now - s->pv_at;

			bits = preview_bits(s->pv_part, t < 0 ? 0 : t, &w);
			soon(&w, s->pv_last + PREVIEW_HOLD_MS - now);
			previewing = 1;
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
	if (low_red(c, s, now, &w) && !previewing)
		bits |= L_RED;

	*wait = w;
	return bits;
}
