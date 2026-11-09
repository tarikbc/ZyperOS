/* SPDX-License-Identifier: GPL-3.0-only */
/* The status corner's rules and marks. See src/status.h. */
#include "status.h"

#include "clamp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- the rules ------------------------------------------------------------ */

int status_batt_level(int pct, bool charging)
{
	if (charging || pct > STATUS_BATT_LOW) return 0;
	return pct <= STATUS_BATT_CRITICAL ? 2 : 1;
}

int status_batt_fill(int pct)
{
	int w;

	if (pct <= 0) return 0;
	if (pct > 100) pct = 100;
	w = (28 * pct + 50) / 100;
	return w < 4 ? 4 : w;
}

int status_wifi_steps(int dbm)
{
	return dbm >= -60 ? 3 : dbm >= -70 ? 2 : 1;
}

/* "RSSI=-56" at the start of a line, so AVG_RSSI is not it. -9999 is what a
 * driver answers for a level it has not heard. */
bool status_rssi_dbm(const char *text, int *dbm)
{
	const char *p;

	for (p = text; p && *p; p = strchr(p, '\n'), p = p ? p + 1 : NULL) {
		char *e;
		long v;

		if (strncmp(p, "RSSI=", 5)) continue;
		v = strtol(p + 5, &e, 10);
		if (e == p + 5 || v > -1 || v < -110) return false;
		*dbm = (int)v;
		return true;
	}
	return false;
}

/* ---- distances --------------------------------------------------------------
 *
 * Every shape a signed distance, negative inside, in units; a pixel's coverage
 * is how far past the edge it is, over a pixel's width, which is the whole of
 * the antialiasing and the same at every scale. src/osdbar.c's way, with the
 * shapes these marks need: a ring for the battery's outline, a polyline for
 * the rune, a sector and arcs opening upward for the fan, and a polygon for
 * the bolt, which is not convex. */

typedef struct {
	enum { SH_BOX, SH_RING, SH_LINES, SH_SECTOR, SH_ARC, SH_POLY, SH_SWEEP } kind;
	float a, b, c, d, r;   /* box, ring: x0 y0 x1 y1 and the corner radius;
	                          sector, arc: center a b, radius c, half-angle d
	                          about straight up; sweep: center a b, radius c,
	                          from straight up clockwise through angle d */
	float w;               /* half the width of a ring, a line or an arc */
	int   n;               /* SH_LINES, SH_POLY: points in p */
	float p[12];
} shape;

static float sd_box(const shape *s, float x, float y)
{
	float cx = (s->a + s->c) / 2, cy = (s->b + s->d) / 2;
	float hx = (s->c - s->a) / 2 - s->r, hy = (s->d - s->b) / 2 - s->r;
	float qx = fabsf(x - cx) - hx, qy = fabsf(y - cy) - hy;
	float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
	float in = qx > qy ? qx : qy;

	return sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - s->r;
}

static float sd_segment(float x, float y, float x0, float y0, float x1, float y1)
{
	float dx = x1 - x0, dy = y1 - y0;
	float len2 = dx * dx + dy * dy;
	float t = len2 > 0 ? clampf(((x - x0) * dx + (y - y0) * dy) / len2, 0, 1) : 0;
	float ex = x - (x0 + dx * t), ey = y - (y0 + dy * t);

	return sqrtf(ex * ex + ey * ey);
}

/* Inside by crossings, so a polygon that is not convex is still exact. */
static float sd_poly(const shape *s, float x, float y)
{
	float d = 1e9f, sign = 1;
	int i, j;

	for (i = 0, j = s->n - 1; i < s->n; j = i, i++) {
		float vx = s->p[i * 2], vy = s->p[i * 2 + 1];
		float ex = s->p[j * 2] - vx, ey = s->p[j * 2 + 1] - vy;
		float wx = x - vx, wy = y - vy;
		float t = clampf((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1);
		float bx = wx - ex * t, by = wy - ey * t;
		bool up = y >= vy, down = y < vy + ey, left = ex * wy > ey * wx;

		if (bx * bx + by * by < d) d = bx * bx + by * by;
		if ((up && down && left) || (!up && !down && !left)) sign = -sign;
	}
	return sign * sqrtf(d);
}

static float sd(const shape *s, float x, float y)
{
	switch (s->kind) {
	case SH_BOX:
		return sd_box(s, x, y);
	case SH_RING:
		return fabsf(sd_box(s, x, y)) - s->w;
	case SH_LINES: {
		float m = 1e9f;
		int i;

		/* Round where the lines meet, as a stroke's round join is. */
		for (i = 0; i + 1 < s->n; i++) {
			float e = sd_segment(x, y, s->p[i * 2], s->p[i * 2 + 1],
			                     s->p[i * 2 + 2], s->p[i * 2 + 3]);
			if (e < m) m = e;
		}
		return m - s->w;
	}
	case SH_SECTOR: {
		float h = x - s->a, u = s->b - y;   /* across, and up */
		float edge = fabsf(h) * cosf(s->d) - u * sinf(s->d);
		float circle = sqrtf(h * h + u * u) - s->c;

		return edge > circle ? edge : circle;
	}
	case SH_ARC: {
		float h = x - s->a, u = s->b - y;
		float ex = s->c * sinf(s->d), ey = s->c * cosf(s->d);
		float d0, d1;

		if (fabsf(atan2f(h, u)) <= s->d)
			return fabsf(sqrtf(h * h + u * u) - s->c) - s->w;
		/* round caps past the ends */
		d0 = sqrtf((h - ex) * (h - ex) + (u - ey) * (u - ey));
		d1 = sqrtf((h + ex) * (h + ex) + (u - ey) * (u - ey));
		return (d0 < d1 ? d0 : d1) - s->w;
	}
	case SH_POLY:
		return sd_poly(s, x, y);
	case SH_SWEEP: {
		float h = x - s->a, u = s->b - y;
		float th = atan2f(h, u), ex, ey, d0, d1;

		if (th < 0) th += 2 * (float)M_PI;
		if (th <= s->d) return fabsf(sqrtf(h * h + u * u) - s->c) - s->w;
		/* round caps at its two ends: straight up, and at d */
		ex = s->c * sinf(s->d);
		ey = s->c * cosf(s->d);
		d0 = sqrtf(h * h + (u - s->c) * (u - s->c));
		d1 = sqrtf((h - ex) * (h - ex) + (u - ey) * (u - ey));
		return (d0 < d1 ? d0 : d1) - s->w;
	}
	}
	return 1e9f;
}

static shape box(float x0, float y0, float x1, float y1, float r)
{
	float m = (x1 - x0 < y1 - y0 ? x1 - x0 : y1 - y0) / 2;

	return (shape){ .kind = SH_BOX, .a = x0, .b = y0, .c = x1, .d = y1,
	                .r = r < m ? r : m };
}

/* ---- painting -----------------------------------------------------------------
 *
 * Into floats, premultiplied RGBA, one shape at a time over the mark it is in,
 * with "over". A cut is the other operation: what is under a shape grown by
 * `grow` units is taken away, so a slash or a bolt stands apart from what it
 * crosses on whatever ground the corner is on, not on a color painted to look
 * like the ground. */

typedef struct {
	float *f;            /* w * h * 4 */
	int w, h;
	float scale;
	float ox;            /* the mark's left in the row, in units */
	float mw;            /* and its width */
} canvas;

static void apply(canvas *cv, const shape *s, const ds_rgb *col, float grow)
{
	float soft = 1.0f / cv->scale;   /* a pixel, in units */
	int x0 = (int)floorf((cv->ox - 1 + STATUS_PAD) * cv->scale);
	int x1 = (int)ceilf((cv->ox + cv->mw + 1 + STATUS_PAD) * cv->scale);
	int x, y;

	if (x0 < 0) x0 = 0;
	if (x1 > cv->w) x1 = cv->w;
	for (y = 0; y < cv->h; y++)
		for (x = x0; x < x1; x++) {
			float ux = (x + 0.5f) / cv->scale - STATUS_PAD - cv->ox;
			float uy = (y + 0.5f) / cv->scale - STATUS_PAD;
			float cov = clampf(0.5f - (sd(s, ux, uy) - grow) / soft, 0, 1);
			float *o = cv->f + ((size_t)y * cv->w + x) * 4;
			int k;

			if (cov <= 0) continue;
			if (!col) {
				for (k = 0; k < 4; k++) o[k] *= 1 - cov;
				continue;
			}
			o[0] = col->r / 255.0f * cov + o[0] * (1 - cov);
			o[1] = col->g / 255.0f * cov + o[1] * (1 - cov);
			o[2] = col->b / 255.0f * cov + o[2] * (1 - cov);
			o[3] = cov + o[3] * (1 - cov);
		}
}

static void paint(canvas *cv, const shape *s, ds_rgb col) { apply(cv, s, &col, 0); }
static void cut(canvas *cv, const shape *s, float grow) { apply(cv, s, NULL, grow); }

/* The fan: a wedge and two arcs about (18, 24), the bottom middle of its 36. */
static void wifi_mark(canvas *cv, int steps, const ds_theme *th)
{
	const float q = (float)M_PI / 4;
	bool idle = steps == STATUS_WIFI_IDLE;
	shape part[3] = {
		{ .kind = SH_SECTOR, .a = 18, .b = 24, .c = 8,  .d = q },
		{ .kind = SH_ARC,    .a = 18, .b = 24, .c = 14, .d = q, .w = 2 },
		{ .kind = SH_ARC,    .a = 18, .b = 24, .c = 22, .d = q, .w = 2 },
	};
	int i;

	for (i = 0; i < 3; i++)
		paint(cv, &part[i], idle ? th->text_dim : i < steps ? th->text_soft : th->track);
	if (idle) {
		/* The slash, 4 wide, cut 3 units out of the fan on either side */
		shape slash = { .kind = SH_LINES, .n = 2, .p = { 6, 1.5f, 30, 22.5f }, .w = 2 };

		cut(cv, &slash, 3);
		paint(cv, &slash, th->text_dim);
	}
}

/* The rune, one 3-unit stroke, in its 16. */
static void bt_mark(canvas *cv, const ds_theme *th)
{
	shape rune = { .kind = SH_LINES, .n = 6,
	               .p = { 3, 7, 13, 17, 8, 22, 8, 2, 13, 7, 3, 17 }, .w = 1.5f };

	paint(cv, &rune, th->text_soft);
}

/* The battery in its 44: the body's outline on a 40 x 24, the tip, the fill,
 * and the bolt while it charges, or a plug while the charge limit holds it
 * (My pick in the charge limit lab, 2026-11-06): on the cable, not
 * charging. */
static void batt_mark(canvas *cv, const status_marks *m, const ds_theme *th)
{
	ds_rgb col = status_batt_level(m->batt, m->charging) ? th->warning : th->text_soft;
	shape body = { .kind = SH_RING, .a = 1.5f, .b = 1.5f, .c = 38.5f, .d = 22.5f,
	               .r = 6.5f, .w = 1.5f };
	shape tip = box(41, 8, 44, 16, 1.5f);
	int fw = status_batt_fill(m->batt);

	paint(cv, &body, col);
	paint(cv, &tip, col);
	if (fw && !m->blink_off) {
		shape fill = box(6, 6, 6 + (float)fw, 18, 3);

		/* lime is success, DESIGN.md 1.3: charging */
		paint(cv, &fill, m->charging ? th->accent_ink : col);
	}
	if (m->charging && m->held) {
		/* Two prongs, the body, the cord, in the outline's ink */
		shape part[4] = {
			{ .kind = SH_LINES, .n = 2, .p = { 16.5f, 2.5f, 16.5f, 9 }, .w = 1.25f },
			{ .kind = SH_LINES, .n = 2, .p = { 22.5f, 2.5f, 22.5f, 9 }, .w = 1.25f },
			box(13, 8, 26, 15.5f, 2.5f),
			{ .kind = SH_LINES, .n = 2, .p = { 19.5f, 15, 19.5f, 21.5f }, .w = 1.25f },
		};
		int i;

		for (i = 0; i < 4; i++) cut(cv, &part[i], 1.5f);
		for (i = 0; i < 4; i++) paint(cv, &part[i], th->text);
	} else if (m->charging) {
		shape bolt = { .kind = SH_POLY, .n = 6,
		               .p = { 23, 0.5f, 12, 14, 19, 14, 16, 23.5f, 28.5f, 9.5f, 21.5f, 9.5f } };

		cut(cv, &bolt, 1.5f);
		paint(cv, &bolt, th->accent_ink);
	}
}

/* File Transfer behind the shelf in its 24: an arrow up and one down, a
 * 2.5-unit stroke, in **text-dim** with nothing moving and **text-soft**
 * while files do, and then a 2-unit ring round them of radius 11 in
 * **track**, filled in **accent-ink** clockwise from the top. */
static void xfer_mark(canvas *cv, const status_marks *m, const ds_theme *th)
{
	ds_rgb ink = m->xfer_moving ? th->text_soft : th->text_dim;
	shape up  = { .kind = SH_LINES, .n = 2, .p = { 9, 18, 9, 9 }, .w = 1.25f };
	shape uph = { .kind = SH_POLY,  .n = 3, .p = { 9, 5, 5, 10, 13, 10 } };
	shape dn  = { .kind = SH_LINES, .n = 2, .p = { 15, 6, 15, 15 }, .w = 1.25f };
	shape dnh = { .kind = SH_POLY,  .n = 3, .p = { 15, 19, 11, 14, 19, 14 } };

	if (m->xfer_moving) {
		shape track = { .kind = SH_ARC, .a = 12, .b = 12, .c = 11, .d = (float)M_PI, .w = 1 };
		int pct = m->xfer_pct < 0 ? 0 : m->xfer_pct > 100 ? 100 : m->xfer_pct;

		paint(cv, &track, th->track);
		if (pct > 0) {
			shape sweep = { .kind = SH_SWEEP, .a = 12, .b = 12, .c = 11,
			                .d = 2 * (float)M_PI * (float)pct / 100.0f, .w = 1 };

			paint(cv, &sweep, th->accent_ink);
		}
	}
	paint(cv, &up, ink);
	paint(cv, &uph, ink);
	paint(cv, &dn, ink);
	paint(cv, &dnh, ink);
}

/* Headphones in their 24: a headband over the top, a 3-unit stroke as the
 * rune's, its ends down inside two cups 6 by 10 with the battery's tip's
 * corners. */
static void phones_mark(canvas *cv, const ds_theme *th)
{
	shape band = { .kind = SH_ARC, .a = 12, .b = 14, .c = 9, .d = 1.75f, .w = 1.5f };
	shape left = box(1.5f, 13, 7.5f, 23, 2.5f), right = box(16.5f, 13, 22.5f, 23, 2.5f);

	paint(cv, &band, th->text_soft);
	paint(cv, &left, th->text_soft);
	paint(cv, &right, th->text_soft);
}

/* The calculating mark, three dots in the number's place (Time Left, before
 * it has a time): each 6 across, 10.5 apart, on the row's middle, filling in
 * turn, `step` of them in **text-soft** and the rest in **track**, as the
 * fan's lit and unlit parts. They end at the room's right edge, where the
 * number's digits would. */
static void calc_mark(canvas *cv, int step, const ds_theme *th)
{
	float right = cv->mw, y = STATUS_ROW / 2.0f;
	int i;

	for (i = 0; i < 3; i++) {
		float x = right - 3.0f - 10.5f * (float)(2 - i);
		shape dot = { .kind = SH_LINES, .n = 2, .p = { x, y, x, y }, .w = 3.0f };

		paint(cv, &dot, i < step ? th->text_soft : th->track);
	}
}

/* ---- the row ------------------------------------------------------------------- */

#define W_XFER   24
#define W_BT     16
#define W_PHONES 24
#define W_WIFI 36
#define W_BATT STATUS_BATT_W

static int wifi_of(const status_marks *m)
{
	if (m->wifi < STATUS_WIFI_IDLE) return STATUS_WIFI_NONE;
	return m->wifi > STATUS_WIFI_FULL ? STATUS_WIFI_FULL : m->wifi;
}

int status_marks_width(const status_marks *m)
{
	int w = 0, n = 0;

	if (m->xfer) { w += W_XFER; n++; }
	if (m->bt) { w += W_BT; n++; }
	if (m->phones) { w += W_PHONES; n++; }
	if (wifi_of(m) != STATUS_WIFI_NONE) { w += W_WIFI; n++; }
	if (m->batt >= 0) { w += W_BATT; n++; }
	if (m->batt >= 0 && m->pct_w > 0) w += m->pct_w + STATUS_PCT_GAP;
	return n ? w + (n - 1) * STATUS_GAP : 0;
}

void status_marks_size(const status_marks *m, float scale, int *w, int *h)
{
	*w = (int)ceilf((status_marks_width(m) + STATUS_PAD * 2) * scale);
	*h = (int)ceilf((STATUS_ROW + STATUS_PAD * 2) * scale);
}

void status_marks_render(uint32_t *px, int w, int h, int stride, float scale,
                         int theme, const status_marks *m)
{
	canvas cv = { NULL, w, h, scale, 0, 0 };
	const ds_theme *th;
	int x, y, wifi = wifi_of(m);

	if (w <= 0 || h <= 0) return;
	for (y = 0; y < h; y++) memset(px + (size_t)y * stride, 0, (size_t)w * sizeof *px);
	if (!(cv.f = calloc((size_t)w * h * 4, sizeof *cv.f))) return;
	if (theme < 0 || theme >= DS_THEMES) theme = DS_LIGHT;
	th = &DS_THEME[theme];

	/* Left to right: File Transfer, the rune, headphones, the fan, the
	 * battery, at the row's right end */
	if (m->xfer) {
		cv.mw = W_XFER;
		xfer_mark(&cv, m, th);
		cv.ox += W_XFER + STATUS_GAP;
	}
	if (m->bt) {
		cv.mw = W_BT;
		bt_mark(&cv, th);
		cv.ox += W_BT + STATUS_GAP;
	}
	if (m->phones) {
		cv.mw = W_PHONES;
		phones_mark(&cv, th);
		cv.ox += W_PHONES + STATUS_GAP;
	}
	if (wifi != STATUS_WIFI_NONE) {
		cv.mw = W_WIFI;
		wifi_mark(&cv, wifi, th);
		cv.ox += W_WIFI + STATUS_GAP;
	}
	if (m->batt >= 0) {
		if (m->calc && m->pct_w > 0) {
			cv.mw = m->pct_w;
			calc_mark(&cv, m->calc_step, th);
		}
		if (m->pct_w > 0) cv.ox += m->pct_w + STATUS_PCT_GAP;   /* the number's room */
		cv.mw = W_BATT;
		batt_mark(&cv, m, th);
	}

	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			const float *o = cv.f + ((size_t)y * w + x) * 4;
			float k = o[3] > 0 ? 1 / o[3] : 0;
			uint32_t r = (uint32_t)(clampf(o[0] * k, 0, 1) * 255 + 0.5f);
			uint32_t g = (uint32_t)(clampf(o[1] * k, 0, 1) * 255 + 0.5f);
			uint32_t b = (uint32_t)(clampf(o[2] * k, 0, 1) * 255 + 0.5f);
			uint32_t a = (uint32_t)(clampf(o[3], 0, 1) * 255 + 0.5f);

			px[(size_t)y * stride + x] = a << 24 | r << 16 | g << 8 | b;
		}
	free(cv.f);
}

/* ---- the song's mark ------------------------------------------------------------ */

void status_note_size(float scale, int *w, int *h)
{
	*w = (int)ceilf((STATUS_NOTE + STATUS_PAD * 2) * scale);
	*h = (int)ceilf((STATUS_ROW + STATUS_PAD * 2) * scale);
}

void status_note_render(uint32_t *px, int w, int h, int stride, float scale,
                        int theme, bool paused)
{
	canvas cv = { NULL, w, h, scale, 0, STATUS_NOTE };
	const ds_theme *th;
	int x, y;

	if (w <= 0 || h <= 0) return;
	for (y = 0; y < h; y++) memset(px + (size_t)y * stride, 0, (size_t)w * sizeof *px);
	if (!(cv.f = calloc((size_t)w * h * 4, sizeof *cv.f))) return;
	if (theme < 0 || theme >= DS_THEMES) theme = DS_LIGHT;
	th = &DS_THEME[theme];
	if (paused) {
		shape a = box(6, 3, 10.5f, 21, 1.5f), b = box(13.5f, 3, 18, 21, 1.5f);

		paint(&cv, &a, th->accent_ink);
		paint(&cv, &b, th->accent_ink);
	} else {
		/* Two heads low left and higher right, their stems up from their
		 * right sides, and one beam across the tops, falling to the left */
		shape h1 = { .kind = SH_LINES, .n = 2, .p = { 6.5f, 19.5f, 6.5f, 19.5f }, .w = 3.5f };
		shape h2 = { .kind = SH_LINES, .n = 2, .p = { 17.5f, 17, 17.5f, 17 }, .w = 3.5f };
		shape s1 = { .kind = SH_LINES, .n = 2, .p = { 8.75f, 19, 8.75f, 6 }, .w = 1.25f };
		shape s2 = { .kind = SH_LINES, .n = 2, .p = { 19.75f, 16.5f, 19.75f, 3 }, .w = 1.25f };
		shape beam = { .kind = SH_POLY, .n = 4,
		               .p = { 7.5f, 4.5f, 21, 1, 21, 5, 7.5f, 8.5f } };

		paint(&cv, &s1, th->accent_ink);
		paint(&cv, &s2, th->accent_ink);
		paint(&cv, &beam, th->accent_ink);
		paint(&cv, &h1, th->accent_ink);
		paint(&cv, &h2, th->accent_ink);
	}
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			const float *o = cv.f + ((size_t)y * w + x) * 4;
			float k = o[3] > 0 ? 1 / o[3] : 0;
			uint32_t r = (uint32_t)(clampf(o[0] * k, 0, 1) * 255 + 0.5f);
			uint32_t g = (uint32_t)(clampf(o[1] * k, 0, 1) * 255 + 0.5f);
			uint32_t b = (uint32_t)(clampf(o[2] * k, 0, 1) * 255 + 0.5f);
			uint32_t a = (uint32_t)(clampf(o[3], 0, 1) * 255 + 0.5f);

			px[(size_t)y * stride + x] = a << 24 | r << 16 | g << 8 | b;
		}
	free(cv.f);
}
