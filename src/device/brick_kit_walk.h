/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_BRICK_KIT_WALK_H
#define ZYPEROS_BRICK_KIT_WALK_H

/* The Hardware Check's lights walk (src/device/brick_kit.c), with no file in
 * it: which LED is which among /sys/class/leds' names, and the walk as steps
 * the page's loop takes one at a time, each when it comes due, so the page
 * never waits (make check-kitwalk). Static, as only the kit's file and its
 * check include it.
 *
 * The walk: the LEDs' 5 V on, every channel the kernel holds at a value
 * other than 0 back to 0, then each LED white alone for KW_LIT_MS in the
 * kernel's order, by number (sunxi_led0, 1, 2 ... 22, not 0, 1, 10, 11),
 * then the 5 V off. A stop, from the tester or from leaving the page, turns
 * the lit channels off and the 5 V with them, and nothing else.
 *
 * One channel a write, KW_GAP_MS apart: the kernel's LED class queues each
 * write as work (led-core.c's schedule_work), and its sunxi driver sleeps in
 * each one until the controller says the frame went out. Two writes close
 * together run at once, the second reprogramming the controller in the
 * middle of the first's frame. */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KW_NAMES    96     /* LED devices read, at most */
#define KW_LEDS     64     /* LEDs, at most */
#define KW_NAME     48
/* The 5 V's on to the first write: a WS2812's start */
#define KW_POWER_MS 50
#define KW_GAP_MS   40
#define KW_LIT_MS   1000
/* The level each channel is lit at: TrimUI's own ceiling for these LEDs
 * (its kernel's max_scale help: "tg5040 limit brightness 60"), so no LED is
 * driven past what TrimUI's system drives it to */
#define KW_LEVEL    60
#define KW_NEVER    0xffffffffu

/* One LED: its name less the channel's letter, the kernel's number in it
 * (-1 for none), and its channels' places in the names, red, green, blue,
 * or one channel for a name with no letter */
typedef struct {
	char stem[KW_NAME];
	int  num;
	int  ch[3];
	int  nch;
} kw_led;

static int kw_led_cmp(const void *a, const void *b)
{
	const kw_led *x = a, *y = b;
	size_t lx = strlen(x->stem), ly = strlen(y->stem);
	int c;

	/* The stems less their numbers first, then the numbers as numbers */
	while (lx && x->stem[lx - 1] >= '0' && x->stem[lx - 1] <= '9') lx--;
	while (ly && y->stem[ly - 1] >= '0' && y->stem[ly - 1] <= '9') ly--;
	c = strncmp(x->stem, y->stem, lx < ly ? lx : ly);
	if (c) return c;
	if (lx != ly) return lx < ly ? -1 : 1;
	if (x->num != y->num) return x->num < y->num ? -1 : 1;
	return strcmp(x->stem, y->stem);
}

/* The LEDs among `n` LED devices' names: a name ending in r, g or b after a
 * digit is that LED's channel (sunxi_led7r, sunxi_led7g, sunxi_led7b), any
 * other a one-channel LED. In the kernel's order. The number found. */
static int kw_group(const char names[][KW_NAME], int n, kw_led *leds, int max)
{
	int nl = 0, i, j;

	for (i = 0; i < n; i++) {
		char stem[KW_NAME];
		size_t l = strlen(names[i]);
		int c = -1;

		snprintf(stem, sizeof stem, "%s", names[i]);
		if (l > 1 && strchr("rgb", stem[l - 1]) && stem[l - 2] >= '0' && stem[l - 2] <= '9') {
			c = stem[l - 1] == 'r' ? 0 : stem[l - 1] == 'g' ? 1 : 2;
			stem[l - 1] = 0;
		}
		for (j = 0; j < nl && strcmp(leds[j].stem, stem); j++) {}
		if (j == nl) {
			size_t sl = strlen(stem), d = sl;

			if (nl == max) continue;
			memset(&leds[nl], 0, sizeof leds[nl]);
			snprintf(leds[nl].stem, sizeof leds[nl].stem, "%s", stem);
			while (d && stem[d - 1] >= '0' && stem[d - 1] <= '9') d--;
			leds[nl].num = d < sl ? atoi(stem + d) : -1;
			leds[nl].ch[0] = leds[nl].ch[1] = leds[nl].ch[2] = -1;
			nl++;
		}
		if (c < 0) {
			/* A name of its own: one channel, the first place */
			if (leds[j].ch[0] < 0) leds[j].ch[0] = i;
		} else if (leds[j].ch[c] < 0) {
			leds[j].ch[c] = i;
		}
	}
	/* The channels each LED has, red, green, blue, packed in that order */
	for (j = 0; j < nl; j++) {
		int k, m = 0;

		for (k = 0; k < 3; k++)
			if (leds[j].ch[k] >= 0) leds[j].ch[m++] = leds[j].ch[k];
		for (k = m; k < 3; k++) leds[j].ch[k] = -1;
		leds[j].nch = m;
	}
	qsort(leds, (size_t)nl, sizeof leds[0], kw_led_cmp);
	return nl;
}

typedef enum { KW_IDLE, KW_RUN, KW_DONE, KW_STOPPED } kw_state;

/* What the page does now: the 5 V on or off, or one channel to a level */
typedef enum { KW_ACT_POWER_ON, KW_ACT_SET, KW_ACT_POWER_OFF } kw_kind;

typedef struct {
	kw_kind kind;
	int     led, ch;   /* KW_ACT_SET: the LED's place and its channel's */
	int     value;
} kw_act;

typedef enum {
	KW_PH_POWER,   /* the 5 V on */
	KW_PH_CLEAR,   /* what the kernel holds lit back to 0 */
	KW_PH_ON,      /* the LED's channels to KW_LEVEL */
	KW_PH_OFF,     /* and back to 0 */
	KW_PH_END      /* the 5 V off */
} kw_phase;

typedef struct {
	kw_state state;
	kw_phase phase;
	const kw_led *leds;
	int      n;
	int      at;          /* the LED a step is on, by place */
	int      ch;          /* the channel next written */
	int      shown;       /* the LED lit last, or being lit, by place */
	bool     stopping;
	bool     powered;
	unsigned due;         /* when the next step is due (ms) */
	unsigned last;        /* when the last step was taken */
	/* What each channel is at as far as the walk knows: the kernel's
	 * stored values at the start, then each write */
	unsigned char level[KW_LEDS][3];
} kw_walk;

/* The walk over `n` LEDs from `now`, `held` the levels the kernel holds for
 * each LED's channels (its brightness files), NULL for all 0 */
static void kw_start(kw_walk *w, const kw_led *leds, int n, const unsigned char (*held)[3], unsigned now)
{
	memset(w, 0, sizeof *w);
	w->leds = leds;
	w->n = n < KW_LEDS ? n : KW_LEDS;
	if (held) memcpy(w->level, held, sizeof w->level[0] * (size_t)w->n);
	w->state = w->n ? KW_RUN : KW_DONE;
	w->phase = KW_PH_POWER;
	w->due = now;
}

static bool kw_running(const kw_walk *w)
{
	return w->state == KW_RUN;
}

/* When the next step is due, KW_NEVER when none is */
static unsigned kw_due(const kw_walk *w)
{
	return w->state == KW_RUN ? w->due : KW_NEVER;
}

/* Stop: the lit channels off, then the 5 V, each a step as before, the
 * first no sooner than KW_GAP_MS after the last write */
static void kw_stop(kw_walk *w, unsigned now)
{
	if (w->state != KW_RUN || w->stopping) return;
	w->stopping = true;
	w->due = w->phase == KW_PH_POWER || (int)(now - (w->last + KW_GAP_MS)) >= 0 ? now : w->last + KW_GAP_MS;
	if (w->phase == KW_PH_POWER) {
		/* Nothing on yet: nothing to turn off */
		w->state = KW_STOPPED;
		return;
	}
	if (w->phase != KW_PH_END) {
		w->phase = KW_PH_OFF;
		w->ch = 0;
	}
}

/* The next lit channel from LED `at`'s channel `ch` on, into them; false
 * for none. `any`: on past LED `at` to the last one, as the clearing and a
 * stop look (a channel a stopped clearing left lit would stay lit), not
 * only in it, as the walk's own off steps do */
static bool kw_next_lit(const kw_walk *w, int *at, int *ch, bool any)
{
	for (; *at < w->n; (*at)++, *ch = 0) {
		for (; *ch < w->leds[*at].nch; (*ch)++)
			if (w->level[*at][*ch]) return true;
		if (!any) return false;
	}
	return false;
}

/* The step due at `now` into `a`, and the walk past it; false when none is
 * due. Each step's wait is counted from when it is taken, so a page that
 * was held up (the motor's 300 ms) never takes two steps in one pass. */
static bool kw_next(kw_walk *w, unsigned now, kw_act *a)
{
	if (w->state != KW_RUN || (int)(now - w->due) < 0) return false;
	w->last = now;
	for (;;) {
		switch (w->phase) {
		case KW_PH_POWER:
			*a = (kw_act){ KW_ACT_POWER_ON, -1, -1, 0 };
			w->powered = true;
			w->phase = KW_PH_CLEAR;
			w->at = w->ch = 0;
			w->due = now + KW_POWER_MS;
			return true;
		case KW_PH_CLEAR:
			if (kw_next_lit(w, &w->at, &w->ch, true)) {
				*a = (kw_act){ KW_ACT_SET, w->at, w->ch, 0 };
				w->level[w->at][w->ch] = 0;
				w->ch++;
				w->due = now + KW_GAP_MS;
				return true;
			}
			w->phase = KW_PH_ON;
			w->at = w->ch = 0;
			continue;
		case KW_PH_ON:
			if (w->ch < w->leds[w->at].nch) {
				*a = (kw_act){ KW_ACT_SET, w->at, w->ch, KW_LEVEL };
				w->shown = w->at;
				w->level[w->at][w->ch] = KW_LEVEL;
				w->ch++;
				w->due = now + (w->ch < w->leds[w->at].nch ? KW_GAP_MS : KW_LIT_MS);
				return true;
			}
			w->phase = KW_PH_OFF;
			w->ch = 0;
			continue;
		case KW_PH_OFF: {
			/* In a stop, every lit channel; in the walk, this LED's */
			int at = w->stopping ? 0 : w->at, ch = w->stopping ? 0 : w->ch;

			if (kw_next_lit(w, &at, &ch, w->stopping)) {
				*a = (kw_act){ KW_ACT_SET, at, ch, 0 };
				w->level[at][ch] = 0;
				if (!w->stopping) w->ch = ch + 1;
				w->due = now + KW_GAP_MS;
				return true;
			}
			if (!w->stopping && w->at + 1 < w->n) {
				w->at++;
				w->ch = 0;
				w->phase = KW_PH_ON;
				continue;
			}
			w->phase = KW_PH_END;
			continue;
		}
		case KW_PH_END:
			*a = (kw_act){ KW_ACT_POWER_OFF, -1, -1, 0 };
			w->powered = false;
			w->state = w->stopping ? KW_STOPPED : KW_DONE;
			w->due = KW_NEVER;
			return true;
		}
	}
}

/* What the walk's end writes to PD11 (GPIO 107), the LEDs' 5 V, given its
 * sysfs state before the walk ("out, 1", "in, 0", "not exported", "" for
 * never asked) and whether this is the Smart Pro: "high", "low", "in", or
 * NULL to write nothing; `unexport` says to give the pin back to the kernel.
 * On the Smart Pro the pin is also the two controllers' 5 V (plastron's
 * init.d/brick drives it high before sppad), so there it is never low,
 * whatever was read: low left the Smart Pro with no buttons. Elsewhere, back
 * as it was found. */
static const char *kw_power_end(const char *was, bool smartpro, bool *unexport)
{
	*unexport = false;
	if (!was || !was[0]) return smartpro ? "high" : NULL;
	if (smartpro || !strncmp(was, "out, 1", 6)) return "high";
	if (!strncmp(was, "in", 2)) return "in";
	*unexport = !strcmp(was, "not exported");
	return "low";
}

/* The Lights row's value for the walk as it is: idle, as it goes, ended */
static void kw_text(const kw_walk *w, char *out, size_t n)
{
	switch (w->state) {
	case KW_IDLE:    snprintf(out, n, "Press A (one at a time)"); break;
	case KW_RUN:     snprintf(out, n, "Light %d of %d", w->shown + 1, w->n); break;
	case KW_DONE:    snprintf(out, n, "%d lit one at a time", w->n); break;
	case KW_STOPPED: snprintf(out, n, "Stopped at %d of %d", w->shown + 1, w->n); break;
	}
}

#endif
