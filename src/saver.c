/* SPDX-License-Identifier: GPL-3.0-only */
/* See saver.h. */
#include "saver.h"

static const int LADDER[] = { 0, 30, 60, 120, 300, 600 };
#define LADDER_N ((int)(sizeof LADDER / sizeof LADDER[0]))

int saver_step(int seconds, int dir)
{
	int k, at = 2;               /* an unknown value starts from the default's rung */

	for (k = 0; k < LADDER_N; k++)
		if (LADDER[k] == seconds) { at = k; break; }
	at += dir;
	if (at < 0) at = 0;
	if (at >= LADDER_N) at = LADDER_N - 1;
	return LADDER[at];
}

bool saver_low_battery(int pct, bool charging)
{
	return !charging && pct >= 0 && pct < SAVER_MIN_BATTERY;
}

bool saver_due(saver_start *s, unsigned now_ms, int delay_s, bool input, bool held)
{
	bool due;

	s->clock.seconds = delay_s;
	/* A button ends "spent": the screensaver ran, ended on its own, and a
	 * press is somebody back */
	if (input) s->spent = false;
	due = idle_check(&s->clock, now_ms, input, held);
	return due && !s->spent;
}

void saver_spend(saver_start *s)
{
	s->spent = true;
}

saver_end saver_must_end(unsigned now_ms, unsigned started_ms, bool held, int pct,
                         bool charging)
{
	if (held) return SAVER_END_HELD;
	if (saver_low_battery(pct, charging)) return SAVER_END_BATTERY;
	if (!charging && now_ms - started_ms >= SAVER_BATTERY_MAX_MS) return SAVER_END_CAP;
	return SAVER_GO_ON;
}

/* ---- what is shown --------------------------------------------------------- */

void saver_show_begin(saver_show *s, unsigned now_ms, bool still)
{
	s->phase = SHOW_LOAD;
	s->t = now_ms;
	s->on_ms = still ? SAVER_STILL_MS : SAVER_VIDEO_MS;
}

bool saver_show_step(saver_show *s, unsigned now_ms, bool ready, float *alpha)
{
	unsigned el = now_ms - s->t;
	float k;

	switch (s->phase) {
	case SHOW_LOAD:
		*alpha = 0;
		if (ready) {
			s->phase = SHOW_IN;
			s->t = now_ms;
			return false;
		}
		/* A clip that never opens is skipped, not waited for */
		return el >= SAVER_LOAD_MS;
	case SHOW_IN:
		k = el >= SAVER_IN_MS ? 1.0f : (float)el / SAVER_IN_MS;
		/* DESIGN.md 8.1's out: 1 - (1-t)^5 */
		{
			float u = 1.0f - k, u2 = u * u;

			*alpha = 1.0f - u2 * u2 * u;
		}
		if (el >= SAVER_IN_MS) {
			s->phase = SHOW_ON;
			s->t = now_ms;
		}
		return false;
	case SHOW_ON:
		*alpha = 1;
		if (el >= s->on_ms) {
			s->phase = SHOW_OUT;
			s->t = now_ms;
		}
		return false;
	case SHOW_OUT:
		/* fade is linear (8.1): the opacity of something going away */
		k = el >= SAVER_OUT_MS ? 1.0f : (float)el / SAVER_OUT_MS;
		*alpha = 1.0f - k;
		return el >= SAVER_OUT_MS;
	}
	*alpha = 0;
	return false;
}

bool saver_show_moving(const saver_show *s)
{
	return s->phase == SHOW_IN || s->phase == SHOW_OUT;
}

/* ---- which one ------------------------------------------------------------- */

void saver_recent_clear(saver_recent *r)
{
	r->n = 0;
}

void saver_recent_add(saver_recent *r, int idx)
{
	int i;

	for (i = SAVER_RECENT - 1; i > 0; i--)
		r->ring[i] = r->ring[i - 1];
	r->ring[0] = idx;
	if (r->n < SAVER_RECENT) r->n++;
}

static bool is_recent(const saver_recent *r, int idx, int keep)
{
	int i;

	for (i = 0; i < r->n && i < keep; i++)
		if (r->ring[i] == idx) return true;
	return false;
}

int saver_pick(int n, uint32_t r, const saver_recent *recent)
{
	int i, start, keep;

	if (n <= 0) return -1;
	/* Avoid as many of the last shown as leaves at least one to give */
	keep = recent ? recent->n : 0;
	if (keep > SAVER_RECENT) keep = SAVER_RECENT;
	if (keep > n - 1) keep = n - 1;
	start = (int)(r % (uint32_t)n);
	for (i = 0; i < n; i++) {
		int c = (start + i) % n;

		if (!recent || !is_recent(recent, c, keep)) return c;
	}
	return start;
}
