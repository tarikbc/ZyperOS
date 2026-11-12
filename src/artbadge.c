/* SPDX-License-Identifier: GPL-3.0-only */
/* The art badge's shapes and pixels. See src/artbadge.h. */
#include "artbadge.h"

#include "clamp.h"
#include "theme.h"

#include <math.h>
#include <string.h>

int ab_diameter(float k)
{
	int d = (int)lroundf(AB_D * k);

	return d < AB_MIN_D ? 0 : d;
}

void ab_center(float x, float y, float k, float *cx, float *cy)
{
	float in = (AB_INSET + AB_D / 2.0f) * k;

	*cx = x + in;
	*cy = y + in;
}

int ab_buf_side(int d) { return d + AB_PAD * 2; }

ds_rgb ab_disc_color(int theme)
{
	if (theme < 0 || theme >= DS_THEMES) theme = DS_LIGHT;
	return theme_at(theme)->surface;
}

ds_rgb ab_glyph_color(ab_kind kind, int theme)
{
	if (theme < 0 || theme >= DS_THEMES) theme = DS_LIGHT;
	/* Pink is love (DESIGN.md 1), and lime is MP4's, as its song mark in the
	 * top bar is */
	return kind == AB_FAVORITE ? theme_at(theme)->favorite : theme_at(theme)->accent_ink;
}

/* ---- distances, in glyph units, negative inside ------------------------------ */

static float sd_segment(float x, float y, float x0, float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0;
	float len2 = dx * dx + dy * dy;
	float t = len2 > 0 ? clampf(((x - x0) * dx + (y - y0) * dy) / len2, 0, 1) : 0;
	float ex = x - (x0 + dx * t), ey = y - (y0 + dy * t);

	return sqrtf(ex * ex + ey * ey);
}

/* Inside by crossings, so a shape that is not convex (the heart's notch) is
 * still exact. */
static float sd_poly(const float *p, int n, float x, float y)
{
	float d = 1e9f, sign = 1;
	int i, j;

	for (i = 0, j = n - 1; i < n; j = i, i++) {
		float vx = p[i * 2], vy = p[i * 2 + 1];
		float ex = p[j * 2] - vx, ey = p[j * 2 + 1] - vy;
		float wx = x - vx, wy = y - vy;
		float t = clampf((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1);
		float bx = wx - ex * t, by = wy - ey * t;
		int up = y >= vy, down = y < vy + ey, left = ex * wy > ey * wx;

		if (bx * bx + by * by < d) d = bx * bx + by * by;
		if ((up && down && left) || (!up && !down && !left)) sign = -sign;
	}
	return sign * sqrtf(d);
}

static float sd_rbox(float x, float y, float x0, float y0, float x1, float y1, float r)
{
	float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
	float hx = (x1 - x0) / 2 - r, hy = (y1 - y0) / 2 - r;
	float qx = fabsf(x - cx) - hx, qy = fabsf(y - cy) - hy;
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
	float in = qx > qy ? qx : qy;

	return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - r;
}

static float minf(float a, float b) { return a < b ? a : b; }

/* ---- the glyphs, on the 24-unit grid ----------------------------------------- */

/* The heart: the curve draw_heart fans (src/zyperdraw.c, markdef.heart),
 *     x = 16 sin^3 t,  y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t,
 * 21 across and centered on its own bounds, which run from the lobes' tops
 * (11.8 up) to the point (17 down). */
#define HEART_N 64
#define HEART_R 10.5f
static float heart_pts[HEART_N * 2];
static int heart_made;

static void heart_make(void)
{
	const float k = HEART_R / 16.0f, cy = 12.0f - (17.0f - 11.8f) / 2.0f * k;
	int i;

	for (i = 0; i < HEART_N; i++) {
		float t = (float)(2.0 * M_PI * i / HEART_N);
		float s = sinf(t);

		heart_pts[i * 2] = 12.0f + 16.0f * s * s * s * k;
		heart_pts[i * 2 + 1] = cy - (13.0f * cosf(t) - 5.0f * cosf(2 * t) -
		                             2.0f * cosf(3 * t) - cosf(4 * t)) * k;
	}
	heart_made = 1;
}

/* The song mark of the top bar while it plays (status_note_render): two heads
 * low left and higher right, their stems up from their right sides, one beam
 * falling to the left across their tops */
static const float NOTE_BEAM[] = { 7.5f, 4.5f, 21, 1, 21, 5, 7.5f, 8.5f };

/* An open book: two pages rising from the spine to their outer edges, the
 * spine a gap of 3 between them, so it still shows on the Pixel 2's panel
 * at a side card's size */
static const float BOOK_L[] = { 2, 6.5f, 10.5f, 8.5f, 10.5f, 21, 2, 19 };
static const float BOOK_R[] = { 13.5f, 8.5f, 22, 6.5f, 22, 19, 13.5f, 21 };

/* A screen, 20 by 14 with round corners, and a play mark cut out of it */
static const float PLAY[] = { 10, 8.5f, 16, 12, 10, 15.5f };

static float glyph_sd(ab_kind kind, float x, float y)
{
	switch (kind) {
	case AB_FAVORITE:
		return sd_poly(heart_pts, HEART_N, x, y);
	case AB_ALBUM: {
		float d = sqrtf((x - 6.5f) * (x - 6.5f) + (y - 19.5f) * (y - 19.5f)) - 3.5f;

		d = minf(d, sqrtf((x - 17.5f) * (x - 17.5f) + (y - 17) * (y - 17)) - 3.5f);
		d = minf(d, sd_segment(x, y, 8.75f, 19, 8.75f, 6) - 1.25f);
		d = minf(d, sd_segment(x, y, 19.75f, 16.5f, 19.75f, 3) - 1.25f);
		return minf(d, sd_poly(NOTE_BEAM, 4, x, y));
	}
	case AB_BOOK:
		return minf(sd_poly(BOOK_L, 4, x, y), sd_poly(BOOK_R, 4, x, y)) - 0.25f;
	case AB_VIDEO: {
		/* The screen less the play mark grown a little, so the cut reads at
		 * a side card's size */
		float screen = sd_rbox(x, y, 2, 5, 22, 19, 3);
		float play = sd_poly(PLAY, 3, x, y) - 0.25f;

		return screen > -play ? screen : -play;
	}
	default:
		return 1e9f;
	}
}

/* ---- painting ------------------------------------------------------------------- */

static void over(float *o, ds_rgb c, float a)
{
	o[0] = c.r / 255.0f * a + o[0] * (1 - a);
	o[1] = c.g / 255.0f * a + o[1] * (1 - a);
	o[2] = c.b / 255.0f * a + o[2] * (1 - a);
	o[3] = a + o[3] * (1 - a);
}

void ab_render(uint32_t *px, int side, int stride, int d, int theme, ab_kind kind)
{
	const ds_theme *th;
	float c = side / 2.0f, rad = d / 2.0f, g = d / (float)AB_D;
	/* Smaller than at rest, the glyph's strokes are made a little heavier,
	 * as a font's are at small sizes: the notes' stems are 2.5 units, which
	 * on a side card in MP4's row would be under a pixel on the Pixel 2's
	 * panel */
	float bold = g < 1 ? (1 / g - 1) * 0.5f : 0;
	float sh_a;
	ds_rgb glyph, disc;
	int x, y;

	if (side <= 0) return;
	for (y = 0; y < side; y++) memset(px + (size_t)y * stride, 0, (size_t)side * sizeof *px);
	if (d <= 0 || kind < 0 || kind >= AB_KINDS) return;
	if (theme < 0 || theme >= DS_THEMES) theme = DS_LIGHT;
	th = theme_at(theme);
	sh_a = (theme == DS_DARK ? AB_SHADOW_DARK : AB_SHADOW_LIGHT) / 255.0f;
	glyph = ab_glyph_color(kind, theme);
	disc = ab_disc_color(theme);
	if (!heart_made) heart_make();

	for (y = 0; y < side; y++)
		for (x = 0; x < side; x++) {
			float fx = x + 0.5f - c, fy = y + 0.5f - c;
			float o[4] = { 0, 0, 0, 0 };
			float r = sqrtf(fx * fx + fy * fy);
			float sy = fy - AB_SHADOW_Y * g;
			float sd_sh = sqrtf(fx * fx + sy * sy) - rad;
			float cov_disc = clamp01(0.5f - (r - rad));
			float a;
			uint32_t pr, pg, pb, pa;

			/* The shadow: falling off over the blur from just inside the
			 * disc's edge, so it shows as a soft rim below it */
			a = sh_a * clamp01(0.5f - sd_sh / (AB_SHADOW_BLUR * g));
			if (a > 0) over(o, th->ink, a);
			if (cov_disc > 0) {
				/* In glyph units: the 24 grid's middle on the disc's */
				float gx = fx / g + AB_GLYPH / 2.0f, gy = fy / g + AB_GLYPH / 2.0f;
				float cov = clamp01(0.5f - (glyph_sd(kind, gx, gy) - bold) * g);

				over(o, disc, cov_disc);
				if (cov > 0) over(o, glyph, cov * cov_disc);
			}
			if (o[3] <= 0) continue;
			pr = (uint32_t)(clamp01(o[0] / o[3]) * 255 + 0.5f);
			pg = (uint32_t)(clamp01(o[1] / o[3]) * 255 + 0.5f);
			pb = (uint32_t)(clamp01(o[2] / o[3]) * 255 + 0.5f);
			pa = (uint32_t)(clamp01(o[3]) * 255 + 0.5f);
			px[(size_t)y * stride + x] = pa << 24 | pr << 16 | pg << 8 | pb;
		}
}
