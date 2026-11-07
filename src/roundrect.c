/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* A rounded rect as a few rects: see roundrect.h. */
#include "roundrect.h"

#include <math.h>

int round_rect_radius(int w, int h, int radius)
{
	if (radius * 2 > w) radius = w / 2;
	if (radius * 2 > h) radius = h / 2;
	return radius < 0 ? 0 : radius;
}

/* The first row of a left corner that column x is in: rows from there to
 * radius - 1 have an inset of x or less, as dx only shrinks toward the
 * middle. `radius` when none is. */
static int first_row(const int *dx, int radius, int x)
{
	int y = radius;

	while (y > 0 && dx[y - 1] <= x) y--;
	return y;
}

int round_rect_rects(int x, int y, int w, int h, int radius, rr_rect *out)
{
	int dx[ROUND_MAX];
	int n = 0, edge, c, k;

	out[n++] = (rr_rect){ x, y + radius, w, h - radius * 2 };
	if (radius <= 0 || radius > ROUND_MAX) return n;
	for (k = 0; k < radius; k++) {
		int dy = radius - k;
		dx[k] = radius - (int)(sqrt((double)(radius * radius - dy * dy)) + 0.5);
	}
	/* The top row's inset is the widest: every corner row reaches past it */
	edge = dx[0];
	if (w - edge * 2 > 0) {
		out[n++] = (rr_rect){ x + edge, y, w - edge * 2, radius };
		out[n++] = (rr_rect){ x + edge, y + h - radius, w - edge * 2, radius };
	}
	/* A run of columns that start on the same row is one rect at each corner:
	 * the right ones mirrored, the bottom ones flipped */
	for (c = 0; c < edge; ) {
		int start = first_row(dx, radius, c), end = c + 1, cw, ch;

		while (end < edge && first_row(dx, radius, end) == start) end++;
		cw = end - c;
		ch = radius - start;
		if (ch > 0) {
			out[n++] = (rr_rect){ x + c, y + start, cw, ch };
			out[n++] = (rr_rect){ x + w - end, y + start, cw, ch };
			out[n++] = (rr_rect){ x + c, y + h - radius, cw, ch };
			out[n++] = (rr_rect){ x + w - end, y + h - radius, cw, ch };
		}
		c = end;
	}
	return n;
}
