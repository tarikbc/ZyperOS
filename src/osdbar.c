/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The volume and brightness bar's pixels. See src/osdbar.h. */
#include "osdbar.h"

#include <math.h>
#include <string.h>

/* The pill inside the buffer, in units, and what is in it. */
#define PILL_CX  (OSDBAR_W / 2.0f)   /* the pill's middle, at every width */
#define PILL_X0  (PILL_CX - PILL_W / 2)
#define PILL_Y0  12.0f
#define PILL_W   452.0f
#define PILL_H   64.0f
#define PILL_R   (PILL_H / 2)
#define BORDER   ((float)DS_FRAME)  /* edge, the 3-unit line around a surface (DESIGN.md 3.2) */
#define ICON_CX  (PILL_X0 + 40.0f)
#define ICON_S   40.0f             /* the mark's box */
#define STROKE   4.0f              /* its lines */
#define SEG_X0   (PILL_X0 + 84.0f)
#define SEG_X1   (PILL_X0 + PILL_W - 30.0f)
#define SEG_H    16.0f
#define SEG_GAP  4.0f
#define SEG_R    3.0f
#define SHADOW_DY   DS_SHADOW_BAR_Y
#define SHADOW_SOFT DS_SHADOW_BAR_BLUR  /* units over which its edge fades */
#define BODY_A      0.93f          /* the game still reads through, faintly */

/* The colors are the design's (src/design.h), by role, in whichever theme the
 * caller names: the pill is a `surface` with an `edge` rim, a step not yet
 * reached is `track`, and the steps reached are one of two colors fixed per
 * function rather than taken from the system accent, which would make one
 * control change color as you scrolled past it - `light` for brightness,
 * `accent-ink` for volume. Accent-ink and not the accent: in Beard the accent
 * lime is 1.3:1 on the ground, a mark nobody could see, so the darker ink of
 * it is used, and in Salt & Pepper the two are the same lime. The shadow is
 * `ink` at the theme's own opacity, `bar_shadow`: 40% against a dark ground,
 * 18% on a light one, where a shadow is the only lift there is. */
static void rgb3(float out[3], ds_rgb c)
{
	out[0] = c.r;
	out[1] = c.g;
	out[2] = c.b;
}

/* ---- distances --------------------------------------------------------------
 *
 * Every shape is a signed distance: negative inside, in units. A pixel's
 * coverage is how much of a pixel-wide band around the edge it is past, which
 * is all the antialiasing there is, and it is the same at every scale. */

typedef struct {
	enum { SH_RBOX, SH_CIRCLE, SH_SEGMENT, SH_ARC, SH_QUAD, SH_RING } kind;
	float a, b, c, d, r;           /* box x0 y0 x1 y1 r; circle cx cy r;
	                                  segment x0 y0 x1 y1 half-width;
	                                  arc cx cy radius half-angle half-width;
	                                  quad r rounds it; ring = box x0 y0 x1 y1 r,
	                                  the band of q[0] units inside its edge */
	float q[8];                    /* quad, four corners, clockwise on screen */
} shape;

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static float sd_rbox(const shape *s, float x, float y)
{
	/* Beside the straight edges, which are most of the pill, the nearest
	 * edge is a straight one: no square root. The bar repaints every frame
	 * while the boost moves, on the thread Diatom presents from. */
	if (x >= s->a + s->r && x <= s->c - s->r) return fmaxf(s->b - y, y - s->d);
	if (y >= s->b + s->r && y <= s->d - s->r) return fmaxf(s->a - x, x - s->c);
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

static float sd(const shape *s, float x, float y)
{
	switch (s->kind) {
	case SH_RBOX:
		return sd_rbox(s, x, y);
	case SH_RING:
		return fabsf(sd_rbox(s, x, y) + s->q[0] / 2) - s->q[0] / 2;
	case SH_CIRCLE:
		return sqrtf((x - s->a) * (x - s->a) + (y - s->b) * (y - s->b)) - s->c;
	case SH_SEGMENT:
		return sd_segment(x, y, s->a, s->b, s->c, s->d) - s->r;
	case SH_ARC: {
		/* An arc of a circle opening to the right, round-capped: the ring's
		 * distance within the angle, the caps' outside it. Turned by q[0]
		 * radians, clockwise on screen, when that is set (the power mark's
		 * ring, centered on its bottom). */
		float px = x - s->a, py = y - s->b;
		if (s->q[0] != 0) {
			float c = cosf(s->q[0]), sn = sinf(s->q[0]), tx = px;
			px = tx * c + py * sn;
			py = -tx * sn + py * c;
		}
		float ang = atan2f(py, px);
		if (fabsf(ang) <= s->d)
			return fabsf(sqrtf(px * px + py * py) - s->c) - s->r;
		float ex = s->c * cosf(s->d), ey = s->c * sinf(s->d);
		float d0 = sqrtf((px - ex) * (px - ex) + (py + ey) * (py + ey));
		float d1 = sqrtf((px - ex) * (px - ex) + (py - ey) * (py - ey));
		return (d0 < d1 ? d0 : d1) - s->r;
	}
	case SH_QUAD: {
		/* Convex: the farthest of the four edges' outward distances. Exact
		 * inside and near enough just outside, which is all the edge needs. */
		float m = -1e9f;
		int i;
		for (i = 0; i < 4; i++) {
			float x0 = s->q[i * 2], y0 = s->q[i * 2 + 1];
			float x1 = s->q[(i + 1) % 4 * 2], y1 = s->q[(i + 1) % 4 * 2 + 1];
			float nx = y1 - y0, ny = x0 - x1;
			float l = sqrtf(nx * nx + ny * ny);
			float e = l > 0 ? ((x - x0) * nx + (y - y0) * ny) / l : 0;
			if (e > m) m = e;
		}
		return m - s->r;
	}
	}
	return 1e9f;
}

/* ---- painting ---------------------------------------------------------------
 *
 * Into a float buffer of premultiplied RGBA, one shape at a time over its own
 * bounds, with "over". The bar is small - 373 x 58 pixels on the panel - and
 * painted only when what it shows changes: a fade, which Diatom and portkeys
 * make by asking for the same bar at a new opacity every frame, only turns
 * the painted buffer into pixels again (osdbar_render). */

typedef struct {
	float *f;                      /* w * h * 4 */
	int w, h;
	float scale;
} canvas;

static void bounds(const shape *s, float *x0, float *y0, float *x1, float *y1)
{
	switch (s->kind) {
	case SH_RBOX:
	case SH_RING:    *x0 = s->a; *y0 = s->b; *x1 = s->c; *y1 = s->d; break;
	case SH_CIRCLE:  *x0 = s->a - s->c; *y0 = s->b - s->c;
	                 *x1 = s->a + s->c; *y1 = s->b + s->c; break;
	case SH_SEGMENT: *x0 = fminf(s->a, s->c) - s->r; *y0 = fminf(s->b, s->d) - s->r;
	                 *x1 = fmaxf(s->a, s->c) + s->r; *y1 = fmaxf(s->b, s->d) + s->r; break;
	case SH_ARC:     *x0 = s->a - s->c - s->r; *y0 = s->b - s->c - s->r;
	                 *x1 = s->a + s->c + s->r; *y1 = s->b + s->c + s->r; break;
	case SH_QUAD: {
		int i;
		*x0 = *x1 = s->q[0]; *y0 = *y1 = s->q[1];
		for (i = 1; i < 4; i++) {
			*x0 = fminf(*x0, s->q[i * 2]); *x1 = fmaxf(*x1, s->q[i * 2]);
			*y0 = fminf(*y0, s->q[i * 2 + 1]); *y1 = fmaxf(*y1, s->q[i * 2 + 1]);
		}
		*x0 -= s->r; *y0 -= s->r; *x1 += s->r; *y1 += s->r;
		break;
	}
	}
}

/* `soft` is the width of the edge in units: one pixel for a crisp shape,
 * more for the shadow. `inner`, when given, is a second color for the part
 * more than `inner_at` units inside the edge - the body's border. */
static void paint(canvas *cv, const shape *s, const float rgb[3], float a,
                  float soft, const float *inner, float inner_at)
{
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0, pad = soft + 1.0f / cv->scale;
	int px0, py0, px1, py1, x, y;

	bounds(s, &x0, &y0, &x1, &y1);
	px0 = (int)floorf((x0 - pad) * cv->scale);
	py0 = (int)floorf((y0 - pad) * cv->scale);
	px1 = (int)ceilf((x1 + pad) * cv->scale);
	py1 = (int)ceilf((y1 + pad) * cv->scale);
	if (px0 < 0) px0 = 0;
	if (py0 < 0) py0 = 0;
	if (px1 > cv->w) px1 = cv->w;
	if (py1 > cv->h) py1 = cv->h;
	for (y = py0; y < py1; y++)
		for (x = px0; x < px1; x++) {
			float ux = (x + 0.5f) / cv->scale, uy = (y + 0.5f) / cv->scale;
			float dist = sd(s, ux, uy);
			float cov = clampf(0.5f - dist / soft, 0, 1) * a;
			float c[3], *o;
			int k;

			if (cov <= 0) continue;
			for (k = 0; k < 3; k++) c[k] = rgb[k];
			if (inner) {
				float t = clampf(0.5f - (dist + inner_at) / soft, 0, 1);
				for (k = 0; k < 3; k++) c[k] = rgb[k] + (inner[k] - rgb[k]) * t;
			}
			o = cv->f + ((size_t)y * cv->w + x) * 4;
			for (k = 0; k < 3; k++) o[k] = c[k] / 255.0f * cov + o[k] * (1 - cov);
			o[3] = cov + o[3] * (1 - cov);
		}
}

static shape rbox(float x0, float y0, float x1, float y1, float r)
{
	return (shape){ .kind = SH_RBOX, .a = x0, .b = y0, .c = x1, .d = y1, .r = r };
}

static shape line(float x0, float y0, float x1, float y1)
{
	return (shape){ .kind = SH_SEGMENT, .a = x0, .b = y0, .c = x1, .d = y1, .r = STROKE / 2 };
}

/* The marks, redrawn for the boost (DESIGN.md 11.6): each in a 52-unit box
 * centered MARK_CX in from the pill's left end, 13 units clear of the first
 * cell at its largest. They follow the level as it moves (`f`, 0..1, which a
 * host animates in quick): the speaker's waves grow arc by arc, each over its
 * third of the scale, and the sun's disc and rays grow. The boost adds its
 * own parts in the boost color (`bf`, the share of the boost filled), so the
 * mark keeps its color: a fourth wave, and eight short rays between the
 * sun's. */
#define MARK_CX ((float)DS_LEVEL_MARK)
#define DEG     ((float)M_PI / 180)

static void wave(canvas *cv, float cx, float cy, float r, float half_deg, const float *col, float px)
{
	shape w = { .kind = SH_ARC, .a = cx, .b = cy, .c = r, .d = half_deg * DEG, .r = STROKE / 2 };

	paint(cv, &w, col, 1, px, NULL, 0);
}

static void speaker(canvas *cv, float cx, float cy, float f, float cut, float bf,
                    const float *col, const float *boost, float px)
{
	const float mx = cx - 2;   /* the cone's mouth, where the waves are centered */
	shape body = rbox(cx - 20.25f, cy - 8.25f, cx - 10.75f, cy + 8.25f, 1.25f);
	shape cone = { .kind = SH_QUAD, .r = 1.25f, .q = {
		cx - 12, cy - 7,  cx - 2, cy - 17,  cx - 2, cy + 17,  cx - 12, cy + 7 } };
	int i;

	paint(cv, &body, col, 1, px, NULL, 0);
	paint(cv, &cone, col, 1, px, NULL, 0);
	for (i = 0; i < 3; i++) {
		float loc = clampf(f * 3 - i, 0, 1);

		if (loc > 0.002f) wave(cv, mx, cy, 8.0f + 7.0f * i, 5 + 45 * loc, col, px);
	}
	if (bf > 0.002f) wave(cv, mx, cy, 29, 5 + 41 * clampf(bf, 0, 1), boost, px);
	if (cut > 0.01f) {
		shape a = line(cx + 4, cy - 6, cx + 16, cy + 6);
		shape b = line(cx + 4, cy + 6, cx + 16, cy - 6);

		paint(cv, &a, col, clampf(cut, 0, 1), px, NULL, 0);
		paint(cv, &b, col, clampf(cut, 0, 1), px, NULL, 0);
	}
}

static void sun(canvas *cv, float cx, float cy, float f, float bf, const float *col,
                const float *boost, float px)
{
	const float d = 6.5f + 1.5f * f, r0 = d + 4.5f;
	const float len = 1 + 6 * f + 3 * clampf(bf, 0, 1);
	shape disc = { .kind = SH_CIRCLE, .a = cx, .b = cy, .c = d };
	int i;

	paint(cv, &disc, col, 1, px, NULL, 0);
	for (i = 0; i < 8; i++) {
		float ang = i * (float)M_PI / 4, c = cosf(ang), s = sinf(ang);
		shape r = line(cx + c * r0, cy + s * r0, cx + c * (r0 + len), cy + s * (r0 + len));

		paint(cv, &r, col, 1, px, NULL, 0);
	}
	if (bf > 0.002f)
		for (i = 0; i < 8; i++) {
			float ang = (i + 0.5f) * (float)M_PI / 4, c = cosf(ang), s = sinf(ang);
			float l = 4 * clampf(bf, 0, 1);
			shape r = { .kind = SH_SEGMENT, .a = cx + c * (r0 + 1), .b = cy + s * (r0 + 1),
			            .c = cx + c * (r0 + 1 + l), .d = cy + s * (r0 + 1 + l), .r = 1.75f };

			paint(cv, &r, boost, 1, px, NULL, 0);
		}
}

/* The power mark: a ring open at the top, and the line down into the gap,
 * as every power button has it (IEC 60417-5009, a public symbol). */
static void power_mark(canvas *cv, float cx, float cy, const float *col, float px)
{
	const float s = ICON_S;
	const float r = 0.40f * s;
	shape ring = { .kind = SH_ARC, .a = cx, .b = cy + 0.05f * s, .c = r,
	               .d = (float)M_PI - 0.70f, .r = STROKE / 2,
	               .q = { (float)M_PI / 2 } };
	shape stem = line(cx, cy - 0.50f * s, cx, cy - 0.04f * s);

	paint(cv, &ring, col, 1, px, NULL, 0);
	paint(cv, &stem, col, 1, px, NULL, 0);
}

void osdbar_size(float scale, int *w, int *h)
{
	*w = (int)ceilf(OSDBAR_W * scale);
	*h = (int)ceilf(OSDBAR_H * scale);
}

/* The look's colors for each theme, when it has any (osdbar_colors), and a
 * count that moves with them so a painted bar is painted again */
static ds_theme colors[DS_THEMES];
static bool     colors_on[DS_THEMES];
static unsigned colors_gen;

void osdbar_colors(int theme, const ds_theme *t)
{
	if (theme < 0 || theme >= DS_THEMES) return;
	if (t) colors[theme] = *t;
	colors_on[theme] = t != NULL;
	colors_gen++;
}

void osdbar_look_plain(osdbar_look *l, int kind, int level, int max)
{
	memset(l, 0, sizeof *l);
	if (max < 1) max = 1;
	if (level < 0) level = 0;
	if (level > max) level = max;
	l->kind = kind;
	l->level = level;
	l->max = max;
	l->mark = (float)level / max;
	l->cut = level == 0 ? 1 : 0;
}

/* The boost's geometry (DESIGN.md 11.6), in units: the gap at the top with
 * its mark, the big charging cell, and how far the end pushes out for it in
 * the first 30% of the charge, on out, so a press shows at once and a hold
 * reads as a fill. */
#define TOP_GAP  ((float)DS_BOOST_GAP)
#define CHARGE_W ((float)DS_BOOST_CHARGE_W)
#define CHARGE_H ((float)DS_BOOST_CHARGE_H)
static float stretch(float charge) { return ds_out(clampf(charge / 0.3f, 0, 1)); }

static void mixc(float out[3], const float a[3], const float b[3], float t)
{
	int k;

	for (k = 0; k < 3; k++) out[k] = a[k] + (b[k] - a[k]) * t;
}

/* How much wider than 452 the pill is for this look, the push included */
static float look_ext(const osdbar_look *l, float sw)
{
	float g = 0, ext = 0;

	if (l->ncells > 0 && l->kind != OSDBAR_POWER) {
		g = TOP_GAP + l->ncells * sw + (l->ncells - 1) * SEG_GAP;
		ext = fmaxf(clampf(l->grow, 0, 1) * g, stretch(l->charge) * (TOP_GAP + CHARGE_W));
	}
	return ext + clampf(l->push, 0, 1) * 6;
}

/* Muted, the room before the steps (the mark's, SEG_X0 in from the end),
 * the word, and the end's padding after it (SEG_X1's from the other end);
 * with no word, the mark's room alone, 84 wide, the mark nearly centered
 * in it. Changing between this and the bar of steps while it is up, the
 * switch flipped under a press, it is drawn again whole the next frame:
 * the width is what it holds, not a level that moves. */
float osdbar_pill_w(const osdbar_look *l)
{
	float w;

	if (!l->muted || l->kind != OSDBAR_VOLUME) return PILL_W;
	w = clampf(l->word_w, 0, OSDBAR_WORD_MAX);
	return (SEG_X0 - PILL_X0) + (w > 0 ? w + (PILL_X0 + PILL_W - SEG_X1) : 0);
}

float osdbar_word_x(const osdbar_look *l)
{
	return PILL_CX - osdbar_pill_w(l) / 2 + (SEG_X0 - PILL_X0);
}

void osdbar_render_look(uint32_t *px, int w, int h, int stride, float scale,
                        int theme, const osdbar_look *look, float alpha, bool premul)
{
	/* The largest the bar is ever drawn, at scale 1. Kept from one call to
	 * the next with what it was painted for, so a call that asks for the
	 * same bar - the same theme, look, scale and size - at another opacity,
	 * or premultiplied where it was straight, is only the conversion at the
	 * end: about twenty a show, each a whole paint before, on the thread
	 * Diatom presents from. While the boost moves, every frame is a paint. */
	static float f[OSDBAR_W * OSDBAR_H * 4];
	static struct {
		bool valid;
		int theme, w, h;
		float scale;
		unsigned gen;
		osdbar_look look;
	} painted;
	osdbar_look L = *look;
	int x, y;

	if ((size_t)w * h > sizeof f / sizeof f[0] / 4) return;
	if (theme < 0 || theme >= DS_THEMES) theme = DS_LIGHT;
	if (L.max < 1) L.max = 1;
	if (L.level < 0) L.level = 0;
	if (L.level > L.max) L.level = L.max;
	if (L.ncells < 0) L.ncells = 0;
	if (L.ncells > OSDBAR_BOOST_MAX) L.ncells = OSDBAR_BOOST_MAX;
	/* Muted, the volume's bar shows neither the level nor the boost: what
	 * they would play, nothing plays */
	if (L.kind != OSDBAR_VOLUME) L.muted = false;
	if (L.muted) {
		osdbar_look m;

		memset(&m, 0, sizeof m);
		m.kind = OSDBAR_VOLUME;
		m.max = L.max;
		m.cut = 1;
		m.muted = true;
		m.word_w = clampf(L.word_w, 0, OSDBAR_WORD_MAX);
		L = m;
	}
	if (!painted.valid || painted.theme != theme || painted.w != w || painted.h != h ||
	    painted.scale != scale || painted.gen != colors_gen || memcmp(&painted.look, &L, sizeof L)) {
		canvas cv = { f, w, h, scale };
		const ds_theme *th = colors_on[theme] ? &colors[theme] : &DS_THEME[theme];
		float col[3], track[3], surface[3], edge[3], ink[3], warn[3], dim[3], btrack[3], soft[3];
		const float pix = 1.0f / scale;            /* a pixel, in units */
		const float cy = PILL_Y0 + PILL_H / 2;
		const float sw = (SEG_X1 - SEG_X0 - SEG_GAP * (L.max - 1)) / L.max;
		const float ext = look_ext(&L, sw);
		const float pw = osdbar_pill_w(&L);
		const float x0 = L.muted ? PILL_CX - pw / 2 : PILL_X0 - ext / 2;
		const float x1 = L.muted ? PILL_CX + pw / 2 : PILL_X0 + PILL_W + ext / 2;
		const float seg0 = SEG_X0 - ext / 2;
		shape sh = rbox(x0, PILL_Y0 + SHADOW_DY, x1, PILL_Y0 + PILL_H + SHADOW_DY, PILL_R);
		shape body = rbox(x0, PILL_Y0, x1, PILL_Y0 + PILL_H, PILL_R);
		int i;

		rgb3(col, L.kind == OSDBAR_BRIGHT ? th->light : th->accent_ink);
		rgb3(track, th->track);
		rgb3(surface, th->surface);
		rgb3(edge, th->edge);
		rgb3(ink, th->ink);
		rgb3(warn, th->warning);
		rgb3(dim, th->text_dim);
		rgb3(soft, th->text_soft);
		mixc(btrack, warn, track, 0.72f);
		memset(f, 0, (size_t)w * h * 4 * sizeof f[0]);

		paint(&cv, &sh, ink, th->bar_shadow / 255.0f, SHADOW_SOFT, NULL, 0);
		paint(&cv, &body, edge, BODY_A, pix, surface, BORDER);
		if (L.flash > 0.004f) {
			shape rim = { .kind = SH_RING, .a = x0, .b = PILL_Y0, .c = x1, .d = PILL_Y0 + PILL_H,
			              .r = PILL_R, .q = { BORDER } };
			paint(&cv, &rim, warn, clampf(L.flash, 0, 1) * BODY_A, pix, NULL, 0);
		}
		{
			float bf = 0;

			for (i = 0; i < L.ncells; i++) bf += clampf(L.fill[i], 0, 1);
			bf = L.ncells ? bf / L.ncells : 0;
			if (L.kind == OSDBAR_BRIGHT)
				sun(&cv, x0 + MARK_CX, cy, clampf(L.mark, 0, 1), bf, col, warn, pix);
			else if (L.kind == OSDBAR_POWER)
				power_mark(&cv, x0 + 40, cy, col, pix);
			else
				speaker(&cv, x0 + MARK_CX, cy, clampf(L.mark, 0, 1), L.cut, bf,
				        L.muted ? soft : col, warn, pix);
		}

		/* One segment per step, widths shared out evenly across the track:
		 * none muted, where the word goes */
		for (i = 0; i < L.max && !L.muted; i++) {
			float sx = seg0 + i * (sw + SEG_GAP);
			shape seg = rbox(sx, cy - SEG_H / 2, sx + sw, cy + SEG_H / 2, SEG_R);
			paint(&cv, &seg, i < L.level ? col : track, 1, pix, NULL, 0);
		}

		/* Past the top: its mark, then each boost cell coming out of the
		 * end as the pill grows (its share of the growth past its place),
		 * the first the big charging cell until it settles */
		if (L.ncells > 0 && L.kind != OSDBAR_POWER) {
			const float xb = seg0 + L.max * (sw + SEG_GAP) - SEG_GAP + TOP_GAP;
			const float tick = fmaxf(clampf(L.tick, 0, 1), stretch(L.charge) * 0.6f) * 0.7f;
			const float big = 1 - clampf(L.morph, 0, 1);
			float off = 0;

			if (tick > 0.004f) {
				shape t = rbox(xb - TOP_GAP / 2 - 1, cy - 15, xb - TOP_GAP / 2 + 1, cy + 15, 1);
				paint(&cv, &t, dim, tick, pix, NULL, 0);
			}
			for (i = 0; i < L.ncells; i++) {
				float cw = sw, ch = SEG_H + 12.0f * (i + 1) / L.ncells, r = SEG_R;
				float rev, fv;

				if (i == 0) {
					cw = sw + (CHARGE_W - sw) * big;
					ch += (CHARGE_H - ch) * big;
					r = SEG_R + 3 * big;
				}
				rev = clampf((ext - clampf(L.push, 0, 1) * 6 - (TOP_GAP + off)) / cw, 0, 1);
				fv = clampf(L.fill[i], 0, 1);
				if (i == 0) fv = fmaxf(fv, clampf(L.charge, 0, 1));
				if (rev > 0.004f) {
					float rw = cw * rev;
					shape tr = rbox(xb + off, cy - ch / 2, xb + off + rw, cy + ch / 2, fminf(r, rw / 2));
					paint(&cv, &tr, btrack, 1, pix, NULL, 0);
					if (fv > 0.004f) {
						float fw = cw * fminf(rev, fv);
						shape fl = rbox(xb + off, cy - ch / 2, xb + off + fw, cy + ch / 2, fminf(r, fw / 2));
						paint(&cv, &fl, warn, 1, pix, NULL, 0);
					}
				}
				off += cw + SEG_GAP;
			}
		}
		painted.valid = true;
		painted.theme = theme;
		painted.w = w;
		painted.h = h;
		painted.scale = scale;
		painted.gen = colors_gen;
		painted.look = L;
	}

	if (alpha < 0) alpha = 0;
	if (alpha > 1) alpha = 1;
	for (y = 0; y < h; y++)
		for (x = 0; x < w; x++) {
			const float *o = f + ((size_t)y * w + x) * 4;
			float a = o[3] * alpha, k = premul ? alpha : (o[3] > 0 ? 1 / o[3] : 0);
			uint32_t r = (uint32_t)(clampf(o[0] * k, 0, 1) * 255 + 0.5f);
			uint32_t g = (uint32_t)(clampf(o[1] * k, 0, 1) * 255 + 0.5f);
			uint32_t b = (uint32_t)(clampf(o[2] * k, 0, 1) * 255 + 0.5f);
			uint32_t A = (uint32_t)(clampf(a, 0, 1) * 255 + 0.5f);
			px[(size_t)y * stride + x] = A << 24 | r << 16 | g << 8 | b;
		}
}

void osdbar_render(uint32_t *px, int w, int h, int stride, float scale,
                   int theme, int kind, int level, int max, float alpha, bool premul)
{
	osdbar_look l;

	osdbar_look_plain(&l, kind, level, max);
	osdbar_render_look(px, w, h, stride, scale, theme, &l, alpha, premul);
}

/* The way in is the design's `out` curve, the quintic ease-out every arrival
 * uses (DESIGN.md 8): 87% of the way there a third into the time, so a press
 * shows at once and then settles. The way out is its `fade`, linear, the
 * opacity of something going away. */
bool osdbar_envelope(uint32_t since_shown, uint32_t since_change,
                     float *alpha, float *rise)
{
	float in = ds_out((float)since_shown / OSDBAR_IN_MS);
	float out = 1;

	if (since_change >= OSDBAR_HOLD_MS + OSDBAR_OUT_MS) return false;
	if (since_change > OSDBAR_HOLD_MS)
		out = 1 - (float)(since_change - OSDBAR_HOLD_MS) / OSDBAR_OUT_MS;
	*alpha = (in < out ? in : out);
	*rise = (1 - in) * OSDBAR_RISE;
	return true;
}

uint32_t osdbar_next_change(uint32_t since_shown, uint32_t since_change)
{
	if (since_shown < OSDBAR_IN_MS || since_change >= OSDBAR_HOLD_MS) return 0;
	return OSDBAR_HOLD_MS - since_change;
}
