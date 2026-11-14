/* SPDX-License-Identifier: GPL-3.0-only */
/* The marks drawn from strokes (src/ds/glyph.h). Moved whole from src/ui.c,
 * where they were "the play modes' marks" and grew to 25. */
#include "ds/glyph.h"

#include <math.h>
#include <string.h>

/* ---- the play modes' marks ---------------------------------------------- */

/* Each mark is a few strokes and a few arrowheads in a unit square, y down,
 * rendered into a white texture with 4x4 supersampling - which is the whole of
 * the antialiasing - and tinted when drawn. Strokes are segments with round
 * ends; a curve is a few of them. */

typedef struct { float x0, y0, x1, y1; } g_seg;
typedef struct { float ax, ay, bx, by, cx, cy; } g_tri;

#define G_W   0.055f            /* half a stroke */
#define G_MAX DS_GLYPH_STROKES
#define G_TRI DS_GLYPH_HEADS

typedef struct {
	g_seg seg[G_MAX];
	g_tri tri[G_TRI];
	int   nseg, ntri;
	int   want_seg, want_tri;    /* asked for, kept or not: ds_glyph_fits */
	float w[G_MAX];              /* each stroke's half width */
} g_shape;

static void g_line(g_shape *s, float x0, float y0, float x1, float y1, float w)
{
	s->want_seg++;
	if (s->nseg == G_MAX) return;
	s->w[s->nseg] = w;
	s->seg[s->nseg++] = (g_seg){ x0, y0, x1, y1 };
}

/* An arc as six strokes, from angle a0 to a1 (radians, y down). Up to a
 * quarter turn it reads round; a longer one shows its corners, and a whole
 * turn in one call is a hexagon: a circle is four calls (CLOCK, CANCEL). */
static void g_arc(g_shape *s, float cx, float cy, float r, float a0, float a1)
{
	int i;

	for (i = 0; i < 6; i++) {
		float t0 = a0 + (a1 - a0) * (float)i / 6.0f;
		float t1 = a0 + (a1 - a0) * (float)(i + 1) / 6.0f;

		g_line(s, cx + r * cosf(t0), cy + r * sinf(t0),
		       cx + r * cosf(t1), cy + r * sinf(t1), G_W);
	}
}

/* A filled triangle, as many as G_TRI a mark */
static void g_fill(g_shape *s, g_tri t)
{
	s->want_tri++;
	if (s->ntri == G_TRI) return;
	s->tri[s->ntri++] = t;
}

/* An arrowhead with its tip at (tx, ty), pointing along (dx, dy), a unit
 * vector. */
static void g_head(g_shape *s, float tx, float ty, float dx, float dy)
{
	float bx = tx - dx * 0.17f, by = ty - dy * 0.17f;
	float px = -dy * 0.13f, py = dx * 0.13f;

	g_fill(s, (g_tri){ tx, ty, bx + px, by + py, bx - px, by - py });
}

static void g_build(ui_glyph g, g_shape *s)
{
	const float pi = 3.14159265f;

	memset(s, 0, sizeof *s);
	if (g == UI_GLYPH_CHARGE) {
		/* A bolt, outlined: down from the top right, a step across, and
		 * down again to the point, from the USB lab's mark. */
		static const float p[][2] = {
			{ 0.563f, 0.083f }, { 0.208f, 0.583f }, { 0.479f, 0.583f },
			{ 0.417f, 0.917f }, { 0.792f, 0.396f }, { 0.521f, 0.396f },
		};
		int i;

		for (i = 0; i < 6; i++)
			g_line(s, p[i][0], p[i][1], p[(i + 1) % 6][0], p[(i + 1) % 6][1], G_W);
		return;
	}
	if (g == UI_GLYPH_TRANSFER) {
		/* A folder, its tab at the top left, and an arrow up out of it. */
		g_line(s, 0.10f, 0.80f, 0.10f, 0.22f, G_W);
		g_line(s, 0.10f, 0.22f, 0.36f, 0.22f, G_W);
		g_line(s, 0.36f, 0.22f, 0.44f, 0.31f, G_W);
		g_line(s, 0.44f, 0.31f, 0.90f, 0.31f, G_W);
		g_line(s, 0.90f, 0.31f, 0.90f, 0.80f, G_W);
		g_line(s, 0.90f, 0.80f, 0.10f, 0.80f, G_W);
		g_line(s, 0.50f, 0.70f, 0.50f, 0.52f, G_W);
		g_head(s, 0.50f, 0.40f, 0.0f, -1.0f);
		return;
	}
	if (g == UI_GLYPH_TERMINAL) {
		/* A screen, a prompt's chevron and the cursor's line. */
		g_line(s, 0.10f, 0.17f, 0.90f, 0.17f, G_W);
		g_line(s, 0.90f, 0.17f, 0.90f, 0.83f, G_W);
		g_line(s, 0.90f, 0.83f, 0.10f, 0.83f, G_W);
		g_line(s, 0.10f, 0.83f, 0.10f, 0.17f, G_W);
		g_line(s, 0.27f, 0.37f, 0.42f, 0.50f, G_W);
		g_line(s, 0.42f, 0.50f, 0.27f, 0.63f, G_W);
		g_line(s, 0.50f, 0.63f, 0.72f, 0.63f, G_W);
		return;
	}
	if (g == UI_GLYPH_USB) {
		/* The USB trident (2026-11-06): an arrow up the stem, a
		 * disc at its foot, a branch bending out to a disc on the left and
		 * one to a square on the right. Discs are strokes of no length. */
		const float w = 0.045f;

		g_fill(s, (g_tri){ 0.50f, 0.03f, 0.39f, 0.23f, 0.61f, 0.23f });
		g_line(s, 0.50f, 0.20f, 0.50f, 0.85f, w);
		g_line(s, 0.50f, 0.86f, 0.50f, 0.86f, 0.09f);
		g_line(s, 0.27f, 0.41f, 0.27f, 0.41f, 0.085f);
		g_line(s, 0.27f, 0.41f, 0.27f, 0.55f, w);
		g_line(s, 0.27f, 0.55f, 0.30f, 0.61f, w);
		g_line(s, 0.30f, 0.61f, 0.38f, 0.655f, w);
		g_line(s, 0.38f, 0.655f, 0.50f, 0.70f, w);
		g_fill(s, (g_tri){ 0.63f, 0.27f, 0.79f, 0.27f, 0.79f, 0.43f });
		g_fill(s, (g_tri){ 0.63f, 0.27f, 0.79f, 0.43f, 0.63f, 0.43f });
		g_line(s, 0.71f, 0.43f, 0.71f, 0.50f, w);
		g_line(s, 0.71f, 0.50f, 0.68f, 0.55f, w);
		g_line(s, 0.68f, 0.55f, 0.60f, 0.585f, w);
		g_line(s, 0.60f, 0.585f, 0.50f, 0.62f, w);
		return;
	}
	if (g == UI_GLYPH_ARROWS) {
		/* Files both ways: an arrow up, and beside it one down */
		g_line(s, 0.34f, 0.86f, 0.34f, 0.30f, G_W);
		g_head(s, 0.34f, 0.13f, 0.0f, -1.0f);
		g_line(s, 0.66f, 0.14f, 0.66f, 0.70f, G_W);
		g_head(s, 0.66f, 0.87f, 0.0f, 1.0f);
		return;
	}
	if (g == UI_GLYPH_FINISH) {
		/* A page, its corner turned, and a tick on it */
		g_line(s, 0.20f, 0.08f, 0.58f, 0.08f, G_W);
		g_line(s, 0.58f, 0.08f, 0.80f, 0.30f, G_W);
		g_line(s, 0.80f, 0.30f, 0.80f, 0.92f, G_W);
		g_line(s, 0.80f, 0.92f, 0.20f, 0.92f, G_W);
		g_line(s, 0.20f, 0.92f, 0.20f, 0.08f, G_W);
		g_line(s, 0.33f, 0.60f, 0.45f, 0.72f, G_W);
		g_line(s, 0.45f, 0.72f, 0.68f, 0.46f, G_W);
		return;
	}
	if (g == UI_GLYPH_HEADPHONES) {
		/* A band over the head, half a circle, and a cup at each end,
		 * a stroke of no length's width as the USB trident's discs */
		g_arc(s, 0.50f, 0.56f, 0.34f, pi, 1.5f * pi);
		g_arc(s, 0.50f, 0.56f, 0.34f, 1.5f * pi, 2.0f * pi);
		g_line(s, 0.20f, 0.60f, 0.20f, 0.80f, 0.085f);
		g_line(s, 0.80f, 0.60f, 0.80f, 0.80f, 0.085f);
		return;
	}
	if (g == UI_GLYPH_LAPTOP) {
		/* A laptop, open: the screen, and the base wider than it */
		g_line(s, 0.20f, 0.20f, 0.80f, 0.20f, G_W);
		g_line(s, 0.80f, 0.20f, 0.80f, 0.64f, G_W);
		g_line(s, 0.80f, 0.64f, 0.20f, 0.64f, G_W);
		g_line(s, 0.20f, 0.64f, 0.20f, 0.20f, G_W);
		g_line(s, 0.20f, 0.64f, 0.08f, 0.80f, G_W);
		g_line(s, 0.08f, 0.80f, 0.92f, 0.80f, G_W);
		g_line(s, 0.92f, 0.80f, 0.80f, 0.64f, G_W);
		return;
	}
	if (g == UI_GLYPH_PHONE) {
		/* A phone, upright: its body, a little rounded at the corners, and
		 * the short bar at its foot, the laptop's family */
		g_line(s, 0.36f, 0.08f, 0.64f, 0.08f, G_W);
		g_line(s, 0.72f, 0.16f, 0.72f, 0.84f, G_W);
		g_line(s, 0.64f, 0.92f, 0.36f, 0.92f, G_W);
		g_line(s, 0.28f, 0.84f, 0.28f, 0.16f, G_W);
		g_arc(s, 0.36f, 0.16f, 0.08f, pi, 1.5f * pi);
		g_arc(s, 0.64f, 0.16f, 0.08f, 1.5f * pi, 2.0f * pi);
		g_arc(s, 0.64f, 0.84f, 0.08f, 0.0f, 0.5f * pi);
		g_arc(s, 0.36f, 0.84f, 0.08f, 0.5f * pi, pi);
		g_line(s, 0.44f, 0.80f, 0.56f, 0.80f, G_W);
		return;
	}
	if (g == UI_GLYPH_TO_CARD || g == UI_GLYPH_FROM_CARD) {
		/* A tray, and an arrow down into it (to the card) or up out of it
		 * (from the card): System Update's corner mark's family, at a size */
		g_line(s, 0.16f, 0.60f, 0.16f, 0.86f, G_W);
		g_line(s, 0.16f, 0.86f, 0.84f, 0.86f, G_W);
		g_line(s, 0.84f, 0.86f, 0.84f, 0.60f, G_W);
		if (g == UI_GLYPH_TO_CARD) {
			g_line(s, 0.50f, 0.10f, 0.50f, 0.56f, G_W);
			g_head(s, 0.50f, 0.72f, 0.0f, 1.0f);
		} else {
			g_line(s, 0.50f, 0.72f, 0.50f, 0.28f, G_W);
			g_head(s, 0.50f, 0.10f, 0.0f, -1.0f);
		}
		return;
	}
	if (g == UI_GLYPH_COVER) {
		/* A box, upright, its corners cut, and a picture on it: a hill
		 * and the sun */
		g_line(s, 0.30f, 0.08f, 0.70f, 0.08f, G_W);
		g_line(s, 0.70f, 0.08f, 0.76f, 0.14f, G_W);
		g_line(s, 0.76f, 0.14f, 0.76f, 0.86f, G_W);
		g_line(s, 0.76f, 0.86f, 0.70f, 0.92f, G_W);
		g_line(s, 0.70f, 0.92f, 0.30f, 0.92f, G_W);
		g_line(s, 0.30f, 0.92f, 0.24f, 0.86f, G_W);
		g_line(s, 0.24f, 0.86f, 0.24f, 0.14f, G_W);
		g_line(s, 0.24f, 0.14f, 0.30f, 0.08f, G_W);
		g_line(s, 0.33f, 0.72f, 0.44f, 0.54f, G_W);
		g_line(s, 0.44f, 0.54f, 0.53f, 0.66f, G_W);
		g_line(s, 0.53f, 0.66f, 0.59f, 0.58f, G_W);
		g_line(s, 0.59f, 0.58f, 0.67f, 0.72f, G_W);
		g_line(s, 0.58f, 0.32f, 0.58f, 0.32f, 0.07f);
		return;
	}
	if (g == UI_GLYPH_FILM) {
		/* A screen, wide, its corners cut, and a play triangle in it */
		g_line(s, 0.14f, 0.20f, 0.86f, 0.20f, G_W);
		g_line(s, 0.86f, 0.20f, 0.92f, 0.26f, G_W);
		g_line(s, 0.92f, 0.26f, 0.92f, 0.74f, G_W);
		g_line(s, 0.92f, 0.74f, 0.86f, 0.80f, G_W);
		g_line(s, 0.86f, 0.80f, 0.14f, 0.80f, G_W);
		g_line(s, 0.14f, 0.80f, 0.08f, 0.74f, G_W);
		g_line(s, 0.08f, 0.74f, 0.08f, 0.26f, G_W);
		g_line(s, 0.08f, 0.26f, 0.14f, 0.20f, G_W);
		g_fill(s, (g_tri){ 0.42f, 0.36f, 0.42f, 0.64f, 0.65f, 0.50f });
		return;
	}
	if (g == UI_GLYPH_CLOUD) {
		/* A cloud: a flat foot, a small bump each side and a big one on
		 * top - ScreenScraper, the one asked over the internet */
		g_line(s, 0.26f, 0.74f, 0.76f, 0.74f, G_W);
		g_arc(s, 0.26f, 0.60f, 0.14f, 0.5f * pi, 1.5f * pi);
		g_arc(s, 0.50f, 0.48f, 0.21f, 1.05f * pi, 1.95f * pi);
		g_arc(s, 0.76f, 0.60f, 0.14f, 1.5f * pi, 2.5f * pi);
		return;
	}
	if (g == UI_GLYPH_BOOK) {
		/* An open book: two pages from the spine - libretro's catalog */
		g_line(s, 0.50f, 0.30f, 0.12f, 0.22f, G_W);
		g_line(s, 0.12f, 0.22f, 0.12f, 0.78f, G_W);
		g_line(s, 0.12f, 0.78f, 0.50f, 0.86f, G_W);
		g_line(s, 0.50f, 0.30f, 0.88f, 0.22f, G_W);
		g_line(s, 0.88f, 0.22f, 0.88f, 0.78f, G_W);
		g_line(s, 0.88f, 0.78f, 0.50f, 0.86f, G_W);
		g_line(s, 0.50f, 0.30f, 0.50f, 0.86f, G_W);
		return;
	}
	if (g == UI_GLYPH_HEART) {
		/* A heart: two lobes and the point under them - the favorites */
		g_arc(s, 0.33f, 0.36f, 0.17f, 0.75f * pi, 2.0f * pi);
		g_arc(s, 0.67f, 0.36f, 0.17f, pi, 2.25f * pi);
		g_line(s, 0.21f, 0.48f, 0.50f, 0.84f, G_W);
		g_line(s, 0.79f, 0.48f, 0.50f, 0.84f, G_W);
		return;
	}
	if (g == UI_GLYPH_CLOCK) {
		/* A clock face and its two hands - how long a run takes. The face
		 * in four quarters: g_arc's six steps make a whole one a hexagon */
		int q;

		for (q = 0; q < 4; q++) g_arc(s, 0.50f, 0.50f, 0.38f, q * 0.5f * pi, (q + 1) * 0.5f * pi);
		g_line(s, 0.50f, 0.50f, 0.50f, 0.26f, G_W);
		g_line(s, 0.50f, 0.50f, 0.68f, 0.60f, G_W);
		return;
	}
	if (g == UI_GLYPH_GRID) {
		/* Four squares, two by two - SteamGridDB */
		static const float o[4][2] = { { 0.14f, 0.14f }, { 0.56f, 0.14f }, { 0.14f, 0.56f }, { 0.56f, 0.56f } };
		int i;

		for (i = 0; i < 4; i++) {
			float x = o[i][0], y = o[i][1], d = 0.30f;

			g_line(s, x, y, x + d, y, G_W);
			g_line(s, x + d, y, x + d, y + d, G_W);
			g_line(s, x + d, y + d, x, y + d, G_W);
			g_line(s, x, y + d, x, y, G_W);
		}
		return;
	}
	if (g == UI_GLYPH_CHECK) {
		/* A tick, the finish's own, alone and a stroke heavier */
		g_line(s, 0.14f, 0.54f, 0.38f, 0.78f, G_W * 1.4f);
		g_line(s, 0.38f, 0.78f, 0.86f, 0.24f, G_W * 1.4f);
		return;
	}
	if (g == UI_GLYPH_CANCEL) {
		/* A ring, crossed */
		g_arc(s, 0.50f, 0.50f, 0.40f, 0.0f, 0.5f * pi);
		g_arc(s, 0.50f, 0.50f, 0.40f, 0.5f * pi, pi);
		g_arc(s, 0.50f, 0.50f, 0.40f, pi, 1.5f * pi);
		g_arc(s, 0.50f, 0.50f, 0.40f, 1.5f * pi, 2.0f * pi);
		g_line(s, 0.36f, 0.36f, 0.64f, 0.64f, G_W);
		g_line(s, 0.64f, 0.36f, 0.36f, 0.64f, G_W);
		return;
	}
	if (g == UI_GLYPH_PLAY) {
		/* A play triangle, outlined, its point to the right */
		g_line(s, 0.27f, 0.14f, 0.27f, 0.86f, G_W);
		g_line(s, 0.27f, 0.86f, 0.84f, 0.50f, G_W);
		g_line(s, 0.84f, 0.50f, 0.27f, 0.14f, G_W);
		return;
	}
	if (g == UI_GLYPH_WIFI) {
		/* Three arcs over a dot, the widest a quarter turn across */
		g_arc(s, 0.50f, 0.80f, 0.56f, 1.25f * pi, 1.75f * pi);
		g_arc(s, 0.50f, 0.80f, 0.37f, 1.25f * pi, 1.75f * pi);
		g_arc(s, 0.50f, 0.80f, 0.18f, 1.25f * pi, 1.75f * pi);
		g_line(s, 0.50f, 0.80f, 0.50f, 0.80f, 0.075f);
		return;
	}
	if (g == UI_GLYPH_SHUFFLE) {
		/* Two paths that cross, both arriving on the right. */
		g_line(s, 0.10f, 0.32f, 0.30f, 0.32f, G_W);
		g_line(s, 0.30f, 0.32f, 0.60f, 0.68f, G_W);
		g_line(s, 0.60f, 0.68f, 0.72f, 0.68f, G_W);
		g_head(s, 0.89f, 0.68f, 1.0f, 0.0f);
		g_line(s, 0.10f, 0.68f, 0.30f, 0.68f, G_W);
		g_line(s, 0.30f, 0.68f, 0.60f, 0.32f, G_W);
		g_line(s, 0.60f, 0.32f, 0.72f, 0.32f, G_W);
		g_head(s, 0.89f, 0.32f, 1.0f, 0.0f);
		return;
	}
	/* The loop: two arrows chasing each other round a rounded rectangle, one
	 * up the left and along the top, one down the right and along the
	 * bottom. */
	g_line(s, 0.12f, 0.60f, 0.12f, 0.42f, G_W);
	g_arc(s, 0.30f, 0.42f, 0.18f, pi, 1.5f * pi);
	g_line(s, 0.30f, 0.24f, 0.68f, 0.24f, G_W);
	g_head(s, 0.86f, 0.24f, 1.0f, 0.0f);
	g_line(s, 0.88f, 0.40f, 0.88f, 0.58f, G_W);
	g_arc(s, 0.70f, 0.58f, 0.18f, 0.0f, 0.5f * pi);
	g_line(s, 0.70f, 0.76f, 0.32f, 0.76f, G_W);
	g_head(s, 0.14f, 0.76f, -1.0f, 0.0f);
	if (g == UI_GLYPH_REPEAT_ONE) {
		/* A 1 in the middle, lighter than the loop and clear of it. */
		g_line(s, 0.50f, 0.38f, 0.50f, 0.62f, 0.04f);
		g_line(s, 0.50f, 0.38f, 0.445f, 0.43f, 0.04f);
	}
}

static int g_inside(const g_shape *s, float x, float y)
{
	int i;

	for (i = 0; i < s->nseg; i++) {
		const g_seg *g = &s->seg[i];
		float vx = g->x1 - g->x0, vy = g->y1 - g->y0;
		float len2 = vx * vx + vy * vy;
		float t = len2 > 0 ? ((x - g->x0) * vx + (y - g->y0) * vy) / len2 : 0;
		float dx, dy;

		if (t < 0) t = 0;
		if (t > 1) t = 1;
		dx = x - (g->x0 + t * vx);
		dy = y - (g->y0 + t * vy);
		if (dx * dx + dy * dy <= s->w[i] * s->w[i]) return 1;
	}
	for (i = 0; i < s->ntri; i++) {
		const g_tri *t = &s->tri[i];
		float d1 = (x - t->bx) * (t->ay - t->by) - (t->ax - t->bx) * (y - t->by);
		float d2 = (x - t->cx) * (t->by - t->cy) - (t->bx - t->cx) * (y - t->cy);
		float d3 = (x - t->ax) * (t->cy - t->ay) - (t->cx - t->ax) * (y - t->ay);
		int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;

		if (!(neg && pos)) return 1;
	}
	return 0;
}

static SDL_Texture *g_render(SDL_Renderer *r, ui_glyph g, int size)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32,
	                                                SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t;
	g_shape shape;
	int x, y, i, j;

	if (!s) return NULL;
	g_build(g, &shape);
	for (y = 0; y < size; y++) {
		Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);

		for (x = 0; x < size; x++) {
			int hit = 0;

			for (j = 0; j < 4; j++)
				for (i = 0; i < 4; i++)
					hit += g_inside(&shape, ((float)x + ((float)i + 0.5f) / 4.0f) / (float)size,
					                ((float)y + ((float)j + 0.5f) / 4.0f) / (float)size);
			row[x] = (Uint32)(hit * 255 / 16) << 24 | 0x00FFFFFFu;
		}
	}
	t = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	if (t) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	return t;
}

bool ds_glyph_fits(ui_glyph g, int *strokes, int *heads)
{
	g_shape s;

	g_build(g, &s);
	if (strokes) *strokes = s.want_seg;
	if (heads) *heads = s.want_tri;
	return s.want_seg <= G_MAX && s.want_tri <= G_TRI;
}

/* A handful of (mark, size) pairs is all a screen asks for: MENU > Scraper's
 * page draws about 20 at once. Full, the one drawn longest ago makes room,
 * so a screen's own marks stay while another's come and go (it was all 32
 * thrown away at once). */
#define G_CACHE 48
static struct { ui_glyph g; int size; SDL_Texture *tex; unsigned used; } g_cache[G_CACHE];
static unsigned g_clock;

void ds_glyphs_free(void)
{
	int i;

	for (i = 0; i < G_CACHE; i++)
		if (g_cache[i].tex) { SDL_DestroyTexture(g_cache[i].tex); g_cache[i].tex = NULL; }
}

void ui_glyph_draw(SDL_Renderer *r, ui_glyph g, int cx, int cy, int size,
                   SDL_Color col)
{
	SDL_Texture *t = NULL;
	int i, slot = -1;

	if (g < 0 || g >= UI_GLYPH_COUNT || size <= 0) return;
	for (i = 0; i < G_CACHE; i++) {
		if (g_cache[i].tex && g_cache[i].g == g && g_cache[i].size == size) {
			t = g_cache[i].tex;
			g_cache[i].used = ++g_clock;
			break;
		}
		/* an empty slot, else the one drawn longest ago */
		if (slot < 0 || (g_cache[slot].tex &&
		                 (!g_cache[i].tex || g_cache[i].used < g_cache[slot].used)))
			slot = i;
	}
	if (!t) {
		if (!(t = g_render(r, g, size))) return;
		if (g_cache[slot].tex) SDL_DestroyTexture(g_cache[slot].tex);
		g_cache[slot].g = g;
		g_cache[slot].size = size;
		g_cache[slot].tex = t;
		g_cache[slot].used = ++g_clock;
	}
	SDL_SetTextureColorMod(t, col.r, col.g, col.b);
	SDL_SetTextureAlphaMod(t, col.a);
	SDL_RenderCopy(r, t, NULL, &(SDL_Rect){ cx - size / 2, cy - size / 2, size, size });
}
