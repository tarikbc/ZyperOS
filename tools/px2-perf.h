/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-perf's arithmetic and parsing, apart from the device so the check can
 * hold it on a computer (tools/px2-perf-check.c): the frame-time spread, a
 * process's CPU time from /proc/PID/stat, a key from Diatom's now file
 * (diatom/src/now.h) and the speed against the clock from two of them.
 * Header only, static: px2-perf.c is built alone, as px2-shot is. */
#ifndef PX2_PERF_H
#define PX2_PERF_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The p-th percentile (0 to 100) of n sorted values, interpolated between
 * the two nearest ranks (what a spreadsheet's PERCENTILE says): the 50th of
 * an even count is the mean of the middle two, the 0th the least, the 100th
 * the most. 0 for no values. */
static double perf_percentile(const double *sorted, int n, double p)
{
	double h;
	int lo;

	if (n <= 0) return 0.0;
	if (p <= 0.0) return sorted[0];
	if (p >= 100.0) return sorted[n - 1];
	h = (n - 1) * p / 100.0;
	lo = (int)floor(h);
	if (lo >= n - 1) return sorted[n - 1];
	return sorted[lo] + (h - lo) * (sorted[lo + 1] - sorted[lo]);
}

static int perf_cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;

	return (x > y) - (x < y);
}

/* The panel's frames, from the times (microseconds) a new framebuffer was
 * first seen at a vertical blank */
struct perf_frames {
	int    frames;                     /* new framebuffers seen */
	double fps;                        /* frames over the seconds watched */
	int    times;                      /* frame times: frames - 1 */
	double min_ms, median_ms, p95_ms, max_ms;
};

/* `t` holds n times in order, `seconds` how long the panel was watched.
 * The frame times are the gaps between them: the first frame's own time is
 * not known, it began before the watch did. */
static void perf_frame_stats(const uint64_t *t, int n, double seconds, struct perf_frames *out)
{
	double *d;
	int i;

	memset(out, 0, sizeof *out);
	out->frames = n;
	out->fps = seconds > 0.0 ? n / seconds : 0.0;
	if (n < 2 || !(d = malloc(sizeof *d * (size_t)(n - 1)))) return;
	for (i = 1; i < n; i++)
		d[i - 1] = (double)(t[i] - t[i - 1]) / 1000.0;
	qsort(d, (size_t)(n - 1), sizeof *d, perf_cmp_double);
	out->times = n - 1;
	out->min_ms = d[0];
	out->median_ms = perf_percentile(d, n - 1, 50.0);
	out->p95_ms = perf_percentile(d, n - 1, 95.0);
	out->max_ms = d[n - 2];
	free(d);
}

/* utime + stime, in clock ticks, from a /proc/PID/stat line. The name in
 * parentheses can hold spaces and parentheses itself, so the fields are
 * counted from the last ')': state is the first after it, utime the 12th and
 * stime the 13th. False when the line does not read. */
static int perf_stat_ticks(const char *line, unsigned long long *ticks)
{
	const char *p = strrchr(line, ')');
	unsigned long long ut, st;
	int field = 0;

	if (!p) return 0;
	p++;
	while (*p && field < 12) {
		while (*p == ' ') p++;
		if (!*p) return 0;
		if (++field == 12) break;
		while (*p && *p != ' ') p++;
	}
	if (field != 12 || sscanf(p, "%llu %llu", &ut, &st) != 2) return 0;
	*ticks = ut + st;
	return 1;
}

/* The value of `key` in lines of key=value, into out; false when absent */
static int perf_kv(const char *text, const char *key, char *out, size_t n)
{
	size_t k = strlen(key);
	const char *p = text;

	while (p && *p) {
		const char *end = strchr(p, '\n');
		size_t len = end ? (size_t)(end - p) : strlen(p);

		if (len > k && !strncmp(p, key, k) && p[k] == '=') {
			len -= k + 1;
			if (len >= n) len = n - 1;
			memcpy(out, p + k + 1, len);
			out[len] = '\0';
			return 1;
		}
		p = end ? end + 1 : NULL;
	}
	return 0;
}

static double perf_kv_num(const char *text, const char *key)
{
	char v[64];

	return perf_kv(text, key, v, sizeof v) ? strtod(v, NULL) : 0.0;
}

/* The speed against the clock between two of Diatom's now files, a then b:
 * the game's frames over the time it ran, as a share of its own rate. 0 when
 * the two do not say (the same writing, another game, no time between). */
static double perf_speed(const char *a, const char *b, double *dropped)
{
	double fps = perf_kv_num(b, "fps");
	double df = perf_kv_num(b, "frames") - perf_kv_num(a, "frames");
	double dr = perf_kv_num(b, "run_us") - perf_kv_num(a, "run_us");
	char ra[1024], rb[1024];

	if (dropped) *dropped = perf_kv_num(b, "dropped") - perf_kv_num(a, "dropped");
	if (!perf_kv(a, "rom", ra, sizeof ra) || !perf_kv(b, "rom", rb, sizeof rb) || strcmp(ra, rb) ||
	    perf_kv_num(a, "pid") != perf_kv_num(b, "pid"))
		return 0.0;
	if (fps <= 0.0 || dr <= 0.0 || df < 0.0) return 0.0;
	return df * 1000000.0 / fps / dr;
}

#endif
