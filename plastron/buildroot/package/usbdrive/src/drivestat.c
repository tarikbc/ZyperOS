/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Flash Drive page's numbers: see drivestat.h. */
#include "drivestat.h"

#include <stdio.h>
#include <string.h>

/* How far a counter went since the last look: a counter that went back
 * starts again from where it is */
static unsigned long long moved(unsigned long long was, unsigned long long now)
{
	return now >= was ? now - was : 0;
}

static int put_int(unsigned long long v, int *tok, int n, int max)
{
	char d[24];
	int k = snprintf(d, sizeof d, "%llu", v), i;

	for (i = 0; i < k && n < max; i++) tok[n++] = DST_T_0 + (d[i] - '0');
	return n;
}

/* v / unit with `dec` decimals, rounded to the nearest, then the unit */
static int put_scaled(unsigned long long v, unsigned long long unit, int dec, int u, bool per_s,
                      int *tok, int max)
{
	unsigned long long pow10 = dec == 2 ? 100 : dec == 1 ? 10 : 1;
	unsigned long long x = (v * pow10 + unit / 2) / unit;   /* in 1/pow10 units */
	int n = put_int(x / pow10, tok, 0, max);

	if (dec && n < max) {
		unsigned long long f = x % pow10;

		tok[n++] = DST_T_DEC;
		if (dec == 2 && n < max) tok[n++] = DST_T_0 + (int)(f / 10);
		if (n < max) tok[n++] = DST_T_0 + (int)(f % 10);
	}
	if (n < max) tok[n++] = DST_T_SP;
	if (n < max) tok[n++] = u;
	if (per_s && n < max) tok[n++] = DST_T_PS;
	return n;
}

#define KB 1000ull
#define MB 1000000ull
#define GB 1000000000ull

int dst_fmt_total(unsigned long long b, int *tok, int max)
{
	/* Each unit up to where rounding would print 1000 of it */
	if (b < 1000) return put_scaled(b, 1, 0, DST_T_B, false, tok, max);
	if (b < 999500ull) return put_scaled(b, KB, 0, DST_T_KB, false, tok, max);
	if (b < 999500000ull) return put_scaled(b, MB, 0, DST_T_MB, false, tok, max);
	if (b < 999995000000ull) return put_scaled(b, GB, 2, DST_T_GB, false, tok, max);
	return put_scaled(b, GB, 0, DST_T_GB, false, tok, max);
}

int dst_fmt_rate(unsigned long long b, int *tok, int max)
{
	/* Nothing moving reads as the unit the speeds are mostly in */
	if (b == 0) return put_scaled(0, MB, 0, DST_T_MB, true, tok, max);
	if (b < 999500ull) return put_scaled(b, KB, 0, DST_T_KB, true, tok, max);
	if (b < 999950000ull) return put_scaled(b, MB, 1, DST_T_MB, true, tok, max);
	return put_scaled(b, GB, 2, DST_T_GB, true, tok, max);
}

void dst_tokens_text(const int *tok, int n, char dec, char *out, size_t on)
{
	static const char *const UNIT[] = { "B", "KB", "MB", "GB" };
	size_t at = 0;
	int i;

	if (!on) return;
	out[0] = '\0';
	for (i = 0; i < n && at + 4 < on; i++) {
		const char *p = NULL;
		char c[2] = { 0, 0 };

		if (tok[i] >= DST_T_0 && tok[i] <= DST_T_0 + 9) c[0] = (char)('0' + tok[i] - DST_T_0), p = c;
		else if (tok[i] == DST_T_DEC) c[0] = dec, p = c;
		else if (tok[i] == DST_T_SP) p = " ";
		else if (tok[i] >= DST_T_B && tok[i] <= DST_T_GB) p = UNIT[tok[i] - DST_T_B];
		else if (tok[i] == DST_T_PS) p = "/s";
		if (p) at += (size_t)snprintf(out + at, on - at, "%s", p);
	}
}

/* One way's look: `d` bytes moved over `dt` ms */
static void way_step(dst_way *w, unsigned long long d, unsigned dt, unsigned now)
{
	w->total += d;
	if (d) {
		w->moved_ms = now;
		w->active = true;
		if (dt) w->rate = d * 1000 / dt;
	} else if (w->active && now - w->moved_ms >= DST_IDLE_MS) {
		w->active = false;
	}
	/* A look with nothing new inside the idle wait keeps the last speed,
	 * so a pause between two files does not blink */
	if (!w->active) w->rate = 0;
}

static void way_text(const dst_way *w, char *out, size_t on)
{
	char a[32], b[32];
	int tok[DST_FMT_MAX], n;

	n = dst_fmt_rate(w->rate, tok, DST_FMT_MAX);
	dst_tokens_text(tok, n, '.', a, sizeof a);
	n = dst_fmt_total(w->total, tok, DST_FMT_MAX);
	dst_tokens_text(tok, n, '.', b, sizeof b);
	snprintf(out, on, "%d|%s|%s", (int)w->active, a, b);
}

bool dst_step(dst_state *s, unsigned long long rd, unsigned long long wr, unsigned now)
{
	char shown[sizeof s->shown], a[60], b[60];

	if (!s->started) {
		memset(s, 0, sizeof *s);
		s->started = true;
		s->rd0 = rd;
		s->wr0 = wr;
		s->last_ms = now;
	} else {
		unsigned dt = now - s->last_ms;

		way_step(&s->in, moved(s->wr0, wr) * 512, dt, now);
		way_step(&s->out, moved(s->rd0, rd) * 512, dt, now);
		s->rd0 = rd;
		s->wr0 = wr;
		s->last_ms = now;
	}
	way_text(&s->in, a, sizeof a);
	way_text(&s->out, b, sizeof b);
	snprintf(shown, sizeof shown, "%s/%s", a, b);
	if (!strcmp(shown, s->shown)) return false;
	snprintf(s->shown, sizeof s->shown, "%s", shown);
	return true;
}
