/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* round_rect_rects against the shape ui_round_rect drew row by row: for every
 * radius up to ROUND_MAX and a spread of sizes around it, every pixel of the
 * row-by-row shape is in exactly one rect, and no pixel outside it is in any.
 * Exactly once is what keeps a translucent fill's blend the same. The rects
 * are about as many as the rows were, but no wider than a corner except the
 * two bands, where the rows were the whole rect wide (roundrect.h). */
#include "../src/roundrect.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

/* The shape as the old ui_round_rect filled it, rows into `want` */
static void rows(int w, int h, int radius, unsigned char *want)
{
	memset(want, 0, (size_t)w * h);
	for (int y = radius; y < h - radius; y++) memset(want + (size_t)y * w, 1, (size_t)w);
	for (int y = 0; y < radius; y++) {
		int dy = radius - y;
		int dx = radius - (int)(sqrt((double)(radius * radius - dy * dy)) + 0.5);
		memset(want + (size_t)y * w + dx, 1, (size_t)(w - dx * 2));
		memset(want + (size_t)(h - 1 - y) * w + dx, 1, (size_t)(w - dx * 2));
	}
}

static void one(int w, int h, int asked, unsigned char *want, unsigned char *got,
                long *sum_rows, long *sum_rects)
{
	rr_rect out[ROUND_RECTS];
	int radius = round_rect_radius(w, h, asked), n;
	const int ox = 7, oy = 3;   /* anywhere on the screen, not only at 0,0 */

	if (radius > ROUND_MAX) return;
	rows(w, h, radius, want);
	memset(got, 0, (size_t)w * h);
	n = round_rect_rects(ox, oy, w, h, radius, out);
	if (n > ROUND_RECTS) { printf("FAIL %dx%d r%d: %d rects\n", w, h, radius, n); failures++; return; }
	for (int i = 0; i < n; i++) {
		const rr_rect *q = &out[i];

		if (q->w < 0 || q->h < 0 || (i && (q->w == 0 || q->h == 0))) {
			printf("FAIL %dx%d r%d: rect %d is %dx%d\n", w, h, radius, i, q->w, q->h);
			failures++;
			return;
		}
		for (int y = q->y - oy; y < q->y - oy + q->h; y++)
			for (int x = q->x - ox; x < q->x - ox + q->w; x++) {
				if (x < 0 || y < 0 || x >= w || y >= h) {
					printf("FAIL %dx%d r%d: rect %d leaves the rect\n", w, h, radius, i);
					failures++;
					return;
				}
				got[(size_t)y * w + x]++;
			}
	}
	for (int k = 0; k < w * h; k++)
		if (got[k] != want[k]) {
			printf("FAIL %dx%d r%d: pixel %d,%d drawn %d times, the shape says %d\n",
			       w, h, radius, k % w, k / w, got[k], want[k]);
			failures++;
			return;
		}
	*sum_rows += radius > 0 ? 1 + 2 * radius : 1;
	*sum_rects += n;
}

int main(void)
{
	/* Room for the largest below: a panel, 900 by 600 */
	const size_t room = 1024 * 1024;
	unsigned char *want = malloc(room), *got = malloc(room);
	long sum_rows = 0, sum_rects = 0, shapes = 0;

	if (!want || !got) return 1;
	/* Every radius, on rects just big enough for it, a little bigger, and a
	 * panel's size; and radii past the sides, which the shape clamps */
	for (int r = 0; r <= ROUND_MAX; r++) {
		const int sides[] = { 2 * r, 2 * r + 1, 2 * r + 7, 2 * r + 40 };

		for (int a = 0; a < 4; a++)
			for (int b = 0; b < 4; b++) {
				if (sides[a] < 1 || sides[b] < 1) continue;
				one(sides[a], sides[b], r, want, got, &sum_rows, &sum_rects);
				shapes++;
			}
		one(r + 1, 3 * r + 1, r, want, got, &sum_rows, &sum_rects);
		shapes++;
	}
	/* The ones the launcher draws: a key, a row, a card, a panel, a glyph */
	{
		const int sizes[][3] = { { 76, 62, 12 }, { 860, 46, 14 }, { 300, 300, 22 },
		                         { 900, 600, 28 }, { 40, 40, 20 }, { 96, 96, 48 } };

		for (int i = 0; i < 6; i++) {
			rr_rect out[ROUND_RECTS];
			int rad = round_rect_radius(sizes[i][0], sizes[i][1], sizes[i][2]);

			one(sizes[i][0], sizes[i][1], sizes[i][2], want, got, &sum_rows, &sum_rects);
			printf("%dx%d radius %d: %d rects, was %d rows\n", sizes[i][0], sizes[i][1],
			       rad, round_rect_rects(0, 0, sizes[i][0], sizes[i][1], rad, out), 1 + 2 * rad);
			shapes++;
		}
	}
	free(want);
	free(got);
	if (failures) {
		printf("roundrect-check: %d failure(s)\n", failures);
		return 1;
	}
	printf("ok: %ld rounded rects drawn as %ld rects where they were %ld rows, every pixel once\n",
	       shapes, sum_rects, sum_rows);
	return 0;
}
