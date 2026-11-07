/* SPDX-License-Identifier: GPL-3.0-only */
/* See blur.h. */
#include "blur.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

void blur_pass(float *p, int n, int stride, int rad, float *line)
{
	float sum = 0.0f, inv = 1.0f / (float)(2 * rad + 1);
	int i;

	for (i = 0; i < n; i++) line[i] = p[(size_t)i * stride];
	for (i = 0; i <= rad && i < n; i++) sum += line[i];
	for (i = 0; i < n; i++) {
		p[(size_t)i * stride] = sum * inv;
		if (i + rad + 1 < n) sum += line[i + rad + 1];
		if (i - rad >= 0) sum -= line[i - rad];
	}
}

void blur_box3_by(float *a, int w, int h, const int rad[3], float *line)
{
	int x, y, pass;

	for (pass = 0; pass < 3; pass++) {
		if (rad[pass] <= 0) continue;
		for (y = 0; y < h; y++) blur_pass(a + (size_t)y * w, w, 1, rad[pass], line);
		for (x = 0; x < w; x++) blur_pass(a + x, h, w, rad[pass], line);
	}
}

void blur_box3(float *a, int w, int h, int rad, float *line)
{
	const int three[3] = { rad, rad, rad };

	blur_box3_by(a, w, h, three, line);
}

/* How far a box `rad` texels each side spreads light, in square units at
 * `unit` units a texel: the variance of 2 * rad + 1 values evenly spaced. */
static double box_spread(int rad, int unit)
{
	double w = 2.0 * rad + 1.0;

	return (double)unit * unit * (w * w - 1.0) / 12.0;
}

blur_shadow blur_shadow_size(int blur, int radius, int unit)
{
	blur_shadow s;
	double want, have;
	int i, full = blur / 2;               /* a box `blur` + 1 units wide */

	if (unit < 1) unit = 1;
	if (full < 0) full = 0;
	s.unit = unit;
	/* Three boxes as wide as the units round up to, then each made a texel
	 * narrower, the last first, while that brings their spread nearer the
	 * three boxes at full size: at a unit a texel, none is. */
	want = 3.0 * box_spread(full, 1);
	for (i = 0; i < 3; i++) s.rad[i] = (full + unit - 1) / unit;
	have = 3.0 * box_spread(s.rad[0], unit);
	for (i = 2; i >= 0 && s.rad[i] > 0; i--) {
		double less = have - box_spread(s.rad[i], unit) + box_spread(s.rad[i] - 1, unit);

		if (fabs(less - want) >= fabs(have - want)) break;
		s.rad[i]--;
		have = less;
	}
	s.m = s.rad[0] + s.rad[1] + s.rad[2];  /* three boxes reach this far */
	s.k = 2 * s.m + (radius + unit - 1) / unit;
	s.n = 2 * s.k + 2;
	return s;
}

void blur_shadow_rect(float *a, const blur_shadow *s, int radius)
{
	const int m = s->m, n = s->n;
	const float R = (float)radius / (float)s->unit;
	int x, y;

	for (y = 0; y < n; y++)
		for (x = 0; x < n; x++) {
			float px = (float)x + 0.5f, py = (float)y + 0.5f;
			float dx = 0.0f, dy = 0.0f;

			a[(size_t)y * n + x] = 0.0f;
			if (y < m || y >= n - m || x < m || x >= n - m) continue;
			if (px < (float)m + R) dx = (float)m + R - px;
			if (px > (float)(n - m) - R) dx = px - ((float)(n - m) - R);
			if (py < (float)m + R) dy = (float)m + R - py;
			if (py > (float)(n - m) - R) dy = py - ((float)(n - m) - R);
			if (dx * dx + dy * dy <= R * R) a[(size_t)y * n + x] = 1.0f;
		}
}

void blur_shadow_fill(float *a, const blur_shadow *s, int radius, float *line)
{
	blur_shadow_rect(a, s, radius);
	blur_box3_by(a, s->n, s->n, s->rad, line);
}

/* One axis of the nine: the near corner, the stretch, the far corner. A
 * corner is k texels drawn over k * unit units. */
static void slice_axis(const blur_shadow *s, int o, int len,
                       int src_at[3], int src_len[3], int dst_at[3], int dst_len[3])
{
	const int u = s->unit;
	int k = 2 * s->k * u > len ? len / (2 * u) : s->k;

	src_at[0] = 0;        src_len[0] = k; dst_at[0] = o;               dst_len[0] = k * u;
	src_at[1] = s->k;     src_len[1] = 2; dst_at[1] = o + k * u;       dst_len[1] = len - 2 * k * u;
	src_at[2] = s->n - k; src_len[2] = k; dst_at[2] = o + len - k * u; dst_len[2] = k * u;
}

void blur_slice(const blur_shadow *s, int ox, int oy, int ow, int oh,
                blur_rect src[9], blur_rect dst[9])
{
	int sx[3], sw[3], dx[3], dw[3], sy[3], sh[3], dy[3], dh[3];
	int i, j;

	slice_axis(s, ox, ow, sx, sw, dx, dw);
	slice_axis(s, oy, oh, sy, sh, dy, dh);
	for (j = 0; j < 3; j++)
		for (i = 0; i < 3; i++) {
			src[j * 3 + i] = (blur_rect){ sx[i], sy[j], sw[i], sh[j] };
			dst[j * 3 + i] = (blur_rect){ dx[i], dy[j], dw[i], dh[j] };
		}
}

/* Sample s of a row is at (s + 0.5) / 4 of a texel: four to a texel each
 * way, none on a texel's edge. */
#define SAMPLES 4

bool blur_fill_tris(float *a, int w, int h, const float *xy, int n)
{
	/* A bit a sample, so a texel is sixteen bits and the union is an or */
	unsigned short *m;
	const int sw = w * SAMPLES, sh = h * SAMPLES;
	int t, i;

	if (w <= 0 || h <= 0) return true;
	memset(a, 0, (size_t)w * h * sizeof *a);
	if (!(m = calloc((size_t)w * h, sizeof *m))) return false;
	for (t = 0; t + 2 < n; t += 3) {
		const float *p = xy + 2 * t;
		const float x0 = p[0], y0 = p[1], x1 = p[2], y1 = p[3], x2 = p[4], y2 = p[5];
		const float lx = fminf(x0, fminf(x1, x2)), hx = fmaxf(x0, fmaxf(x1, x2));
		const float ly = fminf(y0, fminf(y1, y2)), hy = fmaxf(y0, fmaxf(y1, y2));
		int sx0 = (int)floorf(lx * SAMPLES - 0.5f), sx1 = (int)ceilf(hx * SAMPLES - 0.5f);
		int sy0 = (int)floorf(ly * SAMPLES - 0.5f), sy1 = (int)ceilf(hy * SAMPLES - 0.5f);
		int sx, sy;

		if (sx0 < 0) sx0 = 0;
		if (sy0 < 0) sy0 = 0;
		if (sx1 > sw - 1) sx1 = sw - 1;
		if (sy1 > sh - 1) sy1 = sh - 1;
		for (sy = sy0; sy <= sy1; sy++)
			for (sx = sx0; sx <= sx1; sx++) {
				const float px = ((float)sx + 0.5f) / SAMPLES;
				const float py = ((float)sy + 0.5f) / SAMPLES;
				const float d1 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
				const float d2 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
				const float d3 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);

				/* On an edge is in, so an edge two triangles share is
				 * covered, by one or both, and the or counts it once */
				if ((d1 >= 0 && d2 >= 0 && d3 >= 0) || (d1 <= 0 && d2 <= 0 && d3 <= 0))
					m[(size_t)(sy / SAMPLES) * w + sx / SAMPLES] |=
						(unsigned short)(1u << (sy % SAMPLES * SAMPLES + sx % SAMPLES));
			}
	}
	for (i = 0; i < w * h; i++)
		a[i] = (float)__builtin_popcount(m[i]) / (SAMPLES * SAMPLES);
	free(m);
	return true;
}
