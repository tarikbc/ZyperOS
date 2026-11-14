/* SPDX-License-Identifier: GPL-3.0-only */
/* The handheld beside each lesson: DESIGN.md 11.10, "The handheld". See
 * src/tourdevice.h.
 *
 * The GKD Pixel 2 in millimetres, as tools/build-pixel2-model.py builds the
 * landing page's model from its dimensioned drawing: x right, z up, y deep
 * (the front at -8.5, the back at 8.5), the body's middle at 0. A pose turns
 * it by `phi` about its upright axis - more than 0 shows the right side -
 * tilts it back by its pitch - more than 0 shows the top, as seen from a
 * little above, its upright edges staying upright - and draws it
 * orthographic, `s` units a millimetre, so a key on a face is that face's
 * flat shape squashed, and every shape stays a fan of triangles from
 * SDL_RenderGeometry.
 *
 * Every shape is filled and edged as a band that does not overlap its fill,
 * so the first lesson's fade-in shows no seam. */
#include <math.h>
#include <stddef.h>
#include <string.h>

#include "clamp.h"
#include "design.h"
#include "platform.h"   /* PLAT_TARGET_FORMAT */
#include "theme.h"
#include "tourdevice.h"
#include "ui.h"

/* ---- the model ----------------------------------------------------------- */

#define BODY_W     63.0f
#define BODY_H     81.0f
#define BODY_R     3.9f
#define BODY_FRONT -8.5f
#define BODY_BACK  8.5f
/* The side face's plane, a hair proud of the body so its keys sit on it */
#define SIDE_X     (BODY_W / 2.0f + 0.1f)

/* Where the front's round keys are, (x, z), and how big */
static const struct { float x, z, r; } FACE[] = {
	[TD_A] = { 23.6f, -21.5f, 3.05f }, [TD_B] = { 16.9f, -28.2f, 3.05f },
	[TD_X] = { 16.9f, -14.8f, 3.05f }, [TD_Y] = { 10.2f, -21.5f, 3.05f },
	[TD_SELECT] = { -3.8f, -29.6f, 2.85f }, [TD_START] = { 4.0f, -29.6f, 2.85f },
};
#define DPAD_X -16.9f
#define DPAD_Z -21.0f

/* The right face: MENU, round with three dimples in a column; POWER, a
 * capsule at 45 degrees; four light lenses below it. The left face: the
 * volume rocker, VOL+ above VOL-, and the card slot. (y, z) on the face. */
#define MENU_Y  -1.54f
#define MENU_Z  17.13f
#define MENU_R  2.65f
/* Its three dots, drawn bigger and further apart than the model's 0.3 mm
 * dimples 1.05 apart, so they are three dots and not a line on the panel:
 * at 3.95 units a millimetre and 0.625 a unit, 2.5 pixels each, 1.2 apart */
#define MENU_DOT_R   0.5f
#define MENU_DOT_GAP 1.45f
#define POWER_Y -1.74f
#define POWER_Z 6.65f
static const float LED_Z[4] = { 1.41f, -4.03f, -9.67f, -15.31f };
#define VOL_Y   -1.82f
#define ROCKER_Z 11.69f
#define VOLUP_Z 16.52f
#define VOLDN_Z 7.05f

/* The top edge: the shoulders, L1/R1 outside and L2/R2 inside, (x, width),
 * on a plane behind the body's middle, from under the edge to SH_TOP, so
 * that only their tops show over it */
static const struct { int k; float x, w; } SHOULDER[4] = {
	{ TD_L1, -23.9f, 15.1f }, { TD_R1, 23.9f, 15.1f },
	{ TD_L2, -12.6f, 7.4f }, { TD_R2, 12.6f, 7.4f } };
#define BODY_TOP   (BODY_H / 2.0f)
#define SH_Y       2.5f
#define SH_BASE    36.0f
#define SH_TOP     41.7f
/* With the top showing they are blocks standing on it, from the front of
 * their notch (the model's caps, -0.95) to the back the body's outline has
 * (BODY_BACK less its 0.8 mm chamfer), their whole depth from SH_DEEP_DEG of
 * pitch up; below it they are as deep as that much of it, so at 0 they are
 * the plane above and the tilt starts without a jump */
#define SH_FRONT    -0.95f
#define SH_BACK     (BODY_BACK - 0.8f)
#define SH_DEEP_DEG 12.0f

/* ---- the poses ------------------------------------------------------------- */

typedef struct { float deg, s; } pose;

static const pose FRONT = { 10.0f, 3.55f }, RIGHT = { 52.0f, 3.95f },
                  LEFT = { -52.0f, 3.95f }, BOTH = { 0.0f, 3.45f };
/* How far a side key stands out of its edge as a tab in BOTH, mm; a lit
 * shoulder stands as far out of the top edge */
#define TAB_OUT     0.9f
#define TAB_OUT_LIT 2.8f
/* ...but a lit volume key further, so its + or - fits on it pressed too */
#define TAB_OUT_VOL 4.6f
/* The + and the - on the volume keys: this long and this thick, mm */
#define SIGN_L  2.4f
#define SIGN_TH 0.65f
/* The lit key's ring: this far out of the key, this wide, in units */
#define RING_GAP 9.0f
#define RING_W   3.0f
/* Its halo: from the key's edge out, at this much of accent, more pressed */
#define HALO_A       0.35f
#define HALO_A_PRESS 0.40f
#define HALO_IN      10.0f
#define HALO_OUT     22.0f

static pose pose_of(tour_step s)
{
	switch (s) {
	case TOUR_MENU: case TOUR_POWER: return RIGHT;
	case TOUR_VOL:                   return LEFT;
	case TOUR_BRIGHT:                return BOTH;
	default:                         return FRONT;
	}
}

static unsigned lit_of(tour_step s)
{
	switch (s) {
	case TOUR_MOVE:   return 1u << TD_DPAD | 1u << TD_L1 | 1u << TD_R1;
	case TOUR_A:      return 1u << TD_A;
	case TOUR_B:      return 1u << TD_B;
	case TOUR_X:      return 1u << TD_X;
	case TOUR_Y:      return 1u << TD_Y;
	case TOUR_MENU:   return 1u << TD_MENU;
	case TOUR_SELECT: return 1u << TD_SELECT | 1u << TD_L2 | 1u << TD_R2;
	case TOUR_VOL:    return 1u << TD_VOLUP | 1u << TD_VOLDN;
	case TOUR_BRIGHT: return 1u << TD_MENU | 1u << TD_VOLUP | 1u << TD_VOLDN;
	case TOUR_POWER:  return 1u << TD_POWER;
	default:          return 0;
	}
}

bool tourdev_shown(const tour *t)
{
	return t && t->fn_volume && t->step >= TOUR_FIRST_LESSON &&
	       t->step < TOUR_FIRST_LESSON + TOUR_LESSONS;
}

/* ---- the frame's colors and view ------------------------------------------------ */

/* Everything one frame of the handheld is drawn from, and nothing else: the
 * pose (td_pose) and the theme's colors. `r` is set only while it is drawn. */
typedef struct {
	SDL_Renderer *r;
	float cx, cy, s, cp, sp, op;
	float cq, sq;                 /* the pitch's cosine and sine */
	float deep;                   /* how much of their depth the shoulders show, 0 to 1 */
	unsigned body, line, side, glass, screen, key, key_line, pocket, mark;
	unsigned lit, lit_line, dot, ink, on_accent;
	float on[TD_KEYS], press[TD_KEYS];
	float arm[4];                 /* the d-pad's arms pressed: up, down, left, right */
	float facing, tab_ring;
	bool dark, tabs;
} view;

/* The words of a look: the theme's colors and whether it is dark. A pose
 * drawn under another look is another picture, so a kept texture is found
 * by them too (look_of). */
#define LOOK_N 15

static SDL_Color col(const view *v, unsigned c, float a)
{
	return (SDL_Color){ (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c,
	                    (Uint8)(255.0f * clampf(a * v->op, 0.0f, 1.0f) + 0.5f) };
}

/* A model point on the screen: turned about its upright axis, then seen
 * from the pitch above, where a point further back is higher. At pitch 0
 * the height is z exactly (z * 1 + 0), so the level poses are as they were. */
static SDL_FPoint at(const view *v, float x, float y, float z)
{
	const float up = z * v->cq + (y * v->cp - x * v->sp) * v->sq;

	return (SDL_FPoint){ v->cx + v->s * (x * v->cp + y * v->sp), v->cy - v->s * up };
}

/* ---- shapes ----------------------------------------------------------------------- */

#define MAXP 96

/* The frame's triangles, kept and drawn in one SDL_RenderGeometry at its
 * end. The Pixel 2 asks SDL for its renderer by name, which turns SDL's own
 * batching off, so each shape would be a draw and an upload of its own,
 * some 40 a frame.
 * Triangles in one draw blend in their order, so the frame is the same.
 * About 1,500 points a frame; full, it draws what it has and goes on. */
#define BATCH_V 2048
#define BATCH_I 6144
static SDL_Vertex g_bv[BATCH_V];
static int g_bi[BATCH_I];
static int g_nv, g_ni;
/* Where the screen's (0, 0) is in what it is drawn into: the texture's
 * corner, while it is drawn there */
static float g_dx, g_dy;

static void flush(SDL_Renderer *r)
{
	if (g_ni > 0) {
		SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(r, NULL, g_bv, g_nv, g_bi, g_ni);
	}
	g_nv = g_ni = 0;
}

static void geometry(SDL_Renderer *r, const SDL_Vertex *v, int nv, const int *idx, int ni)
{
	int i;

	if (nv > BATCH_V || ni > BATCH_I) return;
	if (g_nv + nv > BATCH_V || g_ni + ni > BATCH_I) flush(r);
	for (i = 0; i < nv; i++) {
		g_bv[g_nv + i] = v[i];
		g_bv[g_nv + i].position.x += g_dx;
		g_bv[g_nv + i].position.y += g_dy;
	}
	for (i = 0; i < ni; i++) g_bi[g_ni + i] = g_nv + idx[i];
	g_nv += nv;
	g_ni += ni;
}

/* The points a step apart at least, so no edge is too short to have a side */
static int dedup(SDL_FPoint *p, int n)
{
	int i, m = 0;

	for (i = 0; i < n; i++) {
		const SDL_FPoint *q = m ? &p[m - 1] : NULL;

		if (q && fabsf(q->x - p[i].x) + fabsf(q->y - p[i].y) < 0.01f) continue;
		p[m++] = p[i];
	}
	while (m > 2 && fabsf(p[m - 1].x - p[0].x) + fabsf(p[m - 1].y - p[0].y) < 0.01f) m--;
	return m;
}

/* `p` moved `d` units out (in, below 0) of its outline, each corner along
 * the middle of its two sides: the shapes here are convex, or the d-pad's
 * cross, whose inside corners take it the same way. */
static void offset(const SDL_FPoint *p, int n, float d, SDL_FPoint *o)
{
	float area = 0.0f, sign;
	int i;

	for (i = 0; i < n; i++) {
		const SDL_FPoint *a = &p[i], *b = &p[(i + 1) % n];

		area += a->x * b->y - b->x * a->y;
	}
	sign = area > 0.0f ? 1.0f : -1.0f;
	for (i = 0; i < n; i++) {
		const SDL_FPoint *a = &p[(i + n - 1) % n], *b = &p[i], *c = &p[(i + 1) % n];
		float e1x = b->x - a->x, e1y = b->y - a->y, e2x = c->x - b->x, e2y = c->y - b->y;
		float l1 = sqrtf(e1x * e1x + e1y * e1y), l2 = sqrtf(e2x * e2x + e2y * e2y);
		float n1x = sign * e1y / l1, n1y = -sign * e1x / l1;
		float n2x = sign * e2y / l2, n2y = -sign * e2x / l2;
		float mx = n1x + n2x, my = n1y + n2y, ml = sqrtf(mx * mx + my * my), k;

		if (ml < 1e-4f) { mx = n1x; my = n1y; ml = 1.0f; }
		mx /= ml;
		my /= ml;
		k = mx * n1x + my * n1y;
		k = d / (k < 0.35f ? 0.35f : k);
		o[i] = (SDL_FPoint){ b->x + mx * k, b->y + my * k };
	}
}

/* A fan from the middle: a convex shape, or the cross from its center */
static void fill(SDL_Renderer *r, const SDL_FPoint *p, int n, SDL_Color c)
{
	SDL_Vertex v[MAXP + 1];
	int idx[MAXP * 3], i;
	float mx = 0.0f, my = 0.0f;

	if (n < 3 || n > MAXP || c.a == 0) return;
	for (i = 0; i < n; i++) { mx += p[i].x; my += p[i].y; }
	v[0] = (SDL_Vertex){ { mx / n, my / n }, c, { 0, 0 } };
	for (i = 0; i < n; i++) {
		v[1 + i] = (SDL_Vertex){ p[i], c, { 0, 0 } };
		idx[i * 3] = 0;
		idx[i * 3 + 1] = 1 + i;
		idx[i * 3 + 2] = 1 + (i + 1) % n;
	}
	geometry(r, v, n + 1, idx, n * 3);
}

/* The band between two outlines of the same points, `ca` along `a` and `cb`
 * along `b`: an edge, a ring, or a halo's fall. */
static void band(SDL_Renderer *r, const SDL_FPoint *a, const SDL_FPoint *b, int n,
                 SDL_Color ca, SDL_Color cb)
{
	SDL_Vertex v[2 * MAXP];
	int idx[MAXP * 6], i;

	if (n < 3 || n > MAXP || (ca.a == 0 && cb.a == 0)) return;
	for (i = 0; i < n; i++) {
		int *q = &idx[i * 6], j = (i + 1) % n;

		v[2 * i] = (SDL_Vertex){ a[i], ca, { 0, 0 } };
		v[2 * i + 1] = (SDL_Vertex){ b[i], cb, { 0, 0 } };
		q[0] = 2 * i; q[1] = 2 * i + 1; q[2] = 2 * j;
		q[3] = 2 * i + 1; q[4] = 2 * j + 1; q[5] = 2 * j;
	}
	geometry(r, v, 2 * n, idx, n * 6);
}

/* A shape filled and edged: the edge `w` wide astride its outline, the
 * fill inside the edge. */
static void shape(const view *v, SDL_FPoint *p, int n, SDL_Color f, SDL_Color e, float w)
{
	SDL_FPoint in[MAXP], out[MAXP];

	n = dedup(p, n);
	if (n < 3) return;
	if (w <= 0.0f || e.a == 0) {
		fill(v->r, p, n, f);
		return;
	}
	offset(p, n, -w / 2.0f, in);
	offset(p, n, w / 2.0f, out);
	fill(v->r, in, n, f);
	band(v->r, out, in, n, e, e);
}

/* A rounded rect `w` x `h` with corners `rad`, about (0, 0), turned `ang`,
 * as points of a plane: (a, b) pairs, `steps` points a corner. */
static int rrect(float w, float h, float rad, float ang, int steps, float *ab)
{
	const float ca = cosf(ang), sa = sinf(ang);
	int q, i, n = 0;

	rad = fminf(rad, fminf(w, h) / 2.0f);
	for (q = 0; q < 4; q++) {
		const float x = (q == 0 || q == 3) ? w / 2.0f - rad : -w / 2.0f + rad;
		const float y = q < 2 ? h / 2.0f - rad : -h / 2.0f + rad;

		for (i = 0; i <= steps; i++) {
			const float t = (float)M_PI / 2.0f * ((float)q + (float)i / steps);
			const float a = x + cosf(t) * rad, b = y + sinf(t) * rad;

			ab[n * 2] = a * ca - b * sa;
			ab[n * 2 + 1] = a * sa + b * ca;
			n++;
		}
	}
	return n;
}

/* A rounded rect on the front (or any plane facing it), at depth y */
static int front_rr(const view *v, float w, float h, float rad, float x, float y, float z,
                    SDL_FPoint *p)
{
	float ab[MAXP * 2];
	int n = rrect(w, h, rad, 0.0f, 5, ab), i;

	for (i = 0; i < n; i++) p[i] = at(v, x + ab[i * 2], y, z + ab[i * 2 + 1]);
	return n;
}

/* A rounded rect on a side face (`side` -1 left, 1 right), at (y, z) on it */
static int side_rr(const view *v, int side, float w, float h, float rad, float ang,
                   float y, float z, SDL_FPoint *p)
{
	float ab[MAXP * 2];
	int n = rrect(w, h, rad, ang, 4, ab), i;

	for (i = 0; i < n; i++) p[i] = at(v, side * SIDE_X, y + ab[i * 2], z + ab[i * 2 + 1]);
	return n;
}

/* An ellipse's points */
static int oval(float cx, float cy, float rx, float ry, SDL_FPoint *p)
{
	enum { N = 32 };
	int i;

	for (i = 0; i < N; i++) {
		const float t = 2.0f * (float)M_PI * i / N;

		p[i] = (SDL_FPoint){ cx + cosf(t) * rx, cy + sinf(t) * ry };
	}
	return N;
}

/* A circle of radius `r` about a model point, on the front's plane (`side`
 * 0) or a side face's (`side` -1 or 1): its points on the screen. Level, the
 * callers keep their oval, the ellipse it is then; tilted, it is slanted. */
static int disc(const view *v, int side, float x, float y, float z, float r, SDL_FPoint *p)
{
	enum { N = 32 };
	int i;

	for (i = 0; i < N; i++) {
		const float t = 2.0f * (float)M_PI * i / N, a = cosf(t) * r, b = sinf(t) * r;

		p[i] = side ? at(v, x, y + a, z + b) : at(v, x + a, y, z + b);
	}
	return N;
}

/* ---- the lit key --------------------------------------------------------------- */

/* A key's colors, `on` of the way lit */
static SDL_Color key_fill(const view *v, int k)
{
	return col(v, ui_mix(v->key, v->lit, v->on[k]), 1.0f);
}

static SDL_Color key_edge(const view *v, int k)
{
	return col(v, ui_mix(v->key_line, v->lit_line, v->on[k]), 1.0f);
}

/* The halo round a lit key whose bounds are the ellipse (cx, cy, rx, ry):
 * from its edge out, falling to nothing, stronger while it is pressed. */
static void halo(const view *v, float on, float press, float cx, float cy, float rx, float ry)
{
	SDL_FPoint a[MAXP], b[MAXP], c[MAXP];
	const float k = on * (HALO_A + HALO_A_PRESS * press);
	int n;

	if (k <= 0.004f) return;
	n = oval(cx, cy, rx, ry, a);
	oval(cx, cy, rx + HALO_IN, ry + HALO_IN, b);
	oval(cx, cy, rx + HALO_OUT, ry + HALO_OUT, c);
	band(v->r, a, b, n, col(v, v->lit, k), col(v, v->lit, k * 0.45f));
	band(v->r, b, c, n, col(v, v->lit, k * 0.45f), col(v, v->lit, 0.0f));
}

/* The ring that says "here", RING_GAP out of the key's bounds */
static void ring(const view *v, float on, float cx, float cy, float rx, float ry)
{
	SDL_FPoint a[MAXP], b[MAXP];
	const SDL_Color c = col(v, v->lit_line, on);
	int n;

	if (on <= 0.004f) return;
	rx = fmaxf(rx, 6.0f) + RING_GAP;
	ry += RING_GAP;
	n = oval(cx, cy, rx - RING_W / 2.0f, ry - RING_W / 2.0f, a);
	oval(cx, cy, rx + RING_W / 2.0f, ry + RING_W / 2.0f, b);
	band(v->r, a, b, n, c, c);
}

/* ---- the handheld ------------------------------------------------------------------ */

/* One key's halo and ring, kept to be drawn under and over the keys; `under`
 * when its halo is drawn already, under the body (a shoulder's) */
typedef struct { float on, press, cx, cy, rx, ry; bool ring, under; } spot;

static void face_key(const view *v, int k, float y)
{
	SDL_FPoint p[MAXP];
	const float q = 1.0f - (1.0f - 0.92f) * v->press[k];
	const SDL_FPoint c = at(v, FACE[k].x, y, FACE[k].z);
	const int n = v->sq == 0.0f ? oval(c.x, c.y, FACE[k].r * v->s * v->cp * q, FACE[k].r * v->s * q, p)
	                            : disc(v, 0, FACE[k].x, y, FACE[k].z, FACE[k].r * q, p);

	shape(v, p, n, key_fill(v, k), key_edge(v, k), DS_FRAME);
}

static void dpad(const view *v)
{
	static const float X[12][2] = {
		{ -3.2f, 9 }, { 3.2f, 9 }, { 3.2f, 3.2f }, { 9, 3.2f }, { 9, -3.2f }, { 3.2f, -3.2f },
		{ 3.2f, -9 }, { -3.2f, -9 }, { -3.2f, -3.2f }, { -9, -3.2f }, { -9, 3.2f }, { -3.2f, 3.2f } };
	/* Each arm's far end, as a quad from the center square's side out: up,
	 * down, left, right, as v->arm */
	static const float ARM[4][4][2] = {
		{ { -3.2f, 3.2f }, { 3.2f, 3.2f }, { 3.2f, 9 }, { -3.2f, 9 } },
		{ { -3.2f, -3.2f }, { 3.2f, -3.2f }, { 3.2f, -9 }, { -3.2f, -9 } },
		{ { -3.2f, 3.2f }, { -9, 3.2f }, { -9, -3.2f }, { -3.2f, -3.2f } },
		{ { 3.2f, 3.2f }, { 9, 3.2f }, { 9, -3.2f }, { 3.2f, -3.2f } } };
	SDL_FPoint p[12];
	int i, j;

	for (i = 0; i < 12; i++) p[i] = at(v, DPAD_X + X[i][0], BODY_FRONT, DPAD_Z + X[i][1]);
	shape(v, p, 12, key_fill(v, TD_DPAD), key_edge(v, TD_DPAD), DS_FRAME);
	/* A pressed arm of the lit d-pad goes a step darker, so which way shows */
	if (v->on[TD_DPAD] <= 0.0f) return;
	for (i = 0; i < 4; i++) {
		const float a = v->arm[i] * v->on[TD_DPAD];
		SDL_FPoint q[4], in[4];

		if (a <= 0.004f) continue;
		for (j = 0; j < 4; j++) q[j] = at(v, DPAD_X + ARM[i][j][0], BODY_FRONT, DPAD_Z + ARM[i][j][1]);
		offset(q, 4, -DS_FRAME / 2.0f, in);
		fill(v->r, in, 4, col(v, v->dark ? ui_mix(v->lit, v->ink, 0.45f) : v->lit_line, a));
	}
}

/* A side key standing out of its edge as a tab (the front pose's way of
 * showing MENU and VOL), from z0 to z1, out `out` mm */
static void tab(const view *v, int k, int side, float z0, float z1, float out, spot *sp)
{
	SDL_FPoint p[MAXP];
	const float rad = fminf(1.2f, (z1 - z0) / 2.0f), x0 = side * (BODY_W / 2.0f - 2.0f);
	const float x1 = side * (BODY_W / 2.0f + out);
	int n = 0, i;

	if (out <= 0.05f) return;
	p[n++] = at(v, x0, -2.0f, z0);
	for (i = 0; i <= 4; i++) {
		const float a = -(float)M_PI / 2.0f + (float)M_PI / 2.0f * i / 4.0f;

		p[n++] = at(v, x1 - side * rad + side * cosf(a) * rad, -2.0f, z0 + rad + sinf(a) * rad);
	}
	for (i = 0; i <= 4; i++) {
		const float a = (float)M_PI / 2.0f * i / 4.0f;

		p[n++] = at(v, x1 - side * rad + side * cosf(a) * rad, -2.0f, z1 - rad + sinf(a) * rad);
	}
	p[n++] = at(v, x0, -2.0f, z1);
	shape(v, p, n, key_fill(v, k), key_edge(v, k), DS_FRAME);
	if (sp) {
		const SDL_FPoint c = at(v, side * (BODY_W / 2.0f + 1.4f), -2.0f, (z0 + z1) / 2.0f);

		*sp = (spot){ v->on[k], v->press[k], c.x, c.y, 7.0f, v->s * (z1 - z0) / 2.0f, true, false };
	}
}

/* The + on VOL+ and the - on VOL-, `l` mm long and `th` thick, centered on
 * (a, z): on the left side face, `a` its y, or (`face` false) on the plane
 * the tabs stand in, `a` its x. In **on-accent** on the lit key, as MENU's
 * dots are, and in their color at rest. */
static void vol_sign(const view *v, int k, bool face, float a, float z, float l, float th)
{
	const SDL_Color c = col(v, ui_mix(v->dot, v->on_accent, v->on[k]), 1.0f);
	SDL_FPoint p[MAXP];
	float ab[MAXP * 2];
	int bar, n, i;

	if (l < 0.6f) return;
	for (bar = 0; bar < (k == TD_VOLUP ? 2 : 1); bar++) {
		n = rrect(bar ? th : l, bar ? l : th, th / 2.0f, 0.0f, 2, ab);
		for (i = 0; i < n; i++)
			p[i] = face ? at(v, -SIDE_X, a + ab[i * 2], z + ab[i * 2 + 1])
			            : at(v, a + ab[i * 2], -2.0f, z + ab[i * 2 + 1]);
		fill(v->r, p, n, c);
	}
}

/* A shoulder's face toward the front, `k`, on the plane at depth `y`, from
 * `base` up to `top`: square at rest; lit, its top corners rounded as a
 * tab's. Its points, 12. */
static int shoulder_face(const view *v, int k, float x, float w, float y, float base, float top,
                         SDL_FPoint *p)
{
	const float rad = 1.2f * v->on[k];
	int n = 0, i;

	p[n++] = at(v, x - w / 2.0f, y, base);
	p[n++] = at(v, x + w / 2.0f, y, base);
	for (i = 0; i <= 4; i++) {
		const float a = (float)M_PI / 2.0f * i / 4.0f;

		p[n++] = at(v, x + w / 2.0f - rad + cosf(a) * rad, y, top - rad + sinf(a) * rad);
	}
	for (i = 0; i <= 4; i++) {
		const float a = (float)M_PI / 2.0f * (1.0f + i / 4.0f);

		p[n++] = at(v, x - w / 2.0f + rad + cosf(a) * rad, y, top - rad + sinf(a) * rad);
	}
	return n;
}

/* A shoulder, `k`, from under the top edge up to `top`, level: at rest the
 * square top the model's shows over the edge; lit, standing up out of it as
 * a tab does */
static void shoulder(const view *v, int k, float x, float w, float top)
{
	SDL_FPoint p[MAXP];
	const int n = shoulder_face(v, k, x, w, SH_Y, SH_BASE, top, p);

	shape(v, p, n, key_fill(v, k), key_edge(v, k), DS_FRAME);
}

/* The convex hull of `p`'s `n` points (sorted in place) into `out`, MAXP at
 * most: a monotone chain. Its count; `out` holds one more, the first again. */
static int hull(SDL_FPoint *p, int n, SDL_FPoint *out)
{
	int a = 0, b, i, lo;

	for (i = 1; i < n; i++) {
		SDL_FPoint x = p[i];

		for (b = i - 1; b >= 0 && (p[b].x > x.x || (p[b].x == x.x && p[b].y > x.y)); b--)
			p[b + 1] = p[b];
		p[b + 1] = x;
	}
#define CROSS(o, a_, b_) (((a_).x - (o).x) * ((b_).y - (o).y) - ((a_).y - (o).y) * ((b_).x - (o).x))
	for (i = 0; i < n; i++) {
		while (a >= 2 && CROSS(out[a - 2], out[a - 1], p[i]) <= 0.0f) a--;
		if (a < MAXP) out[a++] = p[i];
	}
	for (i = n - 2, lo = a + 1; i >= 0; i--) {
		while (a >= lo && CROSS(out[a - 2], out[a - 1], p[i]) <= 0.0f) a--;
		if (a < MAXP) out[a++] = p[i];
	}
#undef CROSS
	return a - 1;
}

/* The shoulders with the top showing (a pitch above 0), up to `top`: each a
 * block standing on the top face, `deep` of its depth, its face toward the
 * front over it so the edge between the two shows. The nearer side's last,
 * so a lit one's side stands over its neighbor. A lit one's halo first, over
 * the top face, and its ring kept in `spots` for over everything. */
static void shoulders_tilted(const view *v, const float top[4], spot *spots, int *nspots)
{
	static const int RIGHTWARD[4] = { 0, 2, 3, 1 };   /* L1, L2, R2, R1 */
	const float y0 = SH_Y + (SH_FRONT - SH_Y) * v->deep, y1 = SH_Y + (SH_BACK - SH_Y) * v->deep;
	SDL_FPoint face[4][16], both[32], out[MAXP];
	int nface[4], i, j, n;

	for (i = 0; i < 4; i++) {
		const int k = SHOULDER[i].k;
		float x0 = 1e9f, x1 = -1e9f, ya = 1e9f, yb = -1e9f;

		nface[i] = shoulder_face(v, k, SHOULDER[i].x, SHOULDER[i].w, y0, BODY_TOP, top[i], face[i]);
		if (v->on[k] <= 0.0f) continue;
		n = shoulder_face(v, k, SHOULDER[i].x, SHOULDER[i].w, y1, BODY_TOP, top[i], both);
		for (j = 0; j < n; j++) both[n + j] = face[i][j];
		for (j = 0; j < 2 * n; j++) {
			x0 = fminf(x0, both[j].x); x1 = fmaxf(x1, both[j].x);
			ya = fminf(ya, both[j].y); yb = fmaxf(yb, both[j].y);
		}
		spots[*nspots] = (spot){ v->on[k], v->press[k], (x0 + x1) / 2.0f, (ya + yb) / 2.0f,
		                         (x1 - x0) / 2.0f, (yb - ya) / 2.0f, true, true };
		halo(v, v->on[k], v->press[k], spots[*nspots].cx, spots[*nspots].cy,
		     spots[*nspots].rx, spots[*nspots].ry);
		(*nspots)++;
	}
	for (j = 0; j < 4; j++) {
		const int s = RIGHTWARD[v->sp < 0.0f ? 3 - j : j], k = SHOULDER[s].k;

		n = shoulder_face(v, k, SHOULDER[s].x, SHOULDER[s].w, y1, BODY_TOP, top[s], both);
		for (i = 0; i < nface[s]; i++) both[n + i] = face[s][i];
		n = hull(both, n + nface[s], out);
		shape(v, out, n, key_fill(v, k), key_edge(v, k), DS_FRAME);
		shape(v, face[s], nface[s], key_fill(v, k), key_edge(v, k), DS_FRAME);
	}
}

/* The colors of the theme in force, into `v` */
static void td_colors(view *v)
{
	const ds_theme *th = theme();
	const bool dark = theme_index() == DS_DARK;

	/* DESIGN.md 11.10's roles: Beard's keys a step darker so the shape of
	 * the face reads on its light body */
	v->body = ui_rgb(th->surface);
	v->line = ui_rgb(th->edge);
	v->side = dark ? ui_mix(ui_rgb(th->surface), ui_rgb(th->raised), 0.6f) : ui_rgb(th->raised);
	v->glass = ui_rgb(th->ink);
	v->screen = dark ? ui_mix(ui_rgb(th->ink), ui_rgb(th->surface), 0.5f) : ui_rgb(th->ground);
	v->key = dark ? ui_rgb(th->raised) : ui_mix(ui_rgb(th->raised), ui_rgb(th->text_dim), 0.45f);
	v->key_line = dark ? ui_rgb(th->edge) : ui_mix(ui_rgb(th->edge), ui_rgb(th->text_dim), 0.3f);
	v->pocket = dark ? ui_mix(ui_rgb(th->surface), ui_rgb(th->ink), 0.45f)
	                : ui_mix(ui_rgb(th->surface), ui_rgb(th->raised), 0.55f);
	v->mark = dark ? ui_rgb(th->edge) : ui_mix(ui_rgb(th->edge), ui_rgb(th->text_dim), 0.2f);
	v->lit = ui_rgb(th->accent);
	v->lit_line = dark ? ui_rgb(th->accent) : ui_rgb(th->accent_ink);
	v->dot = dark ? ui_rgb(th->text_dim) : ui_rgb(th->text_soft);
	v->ink = ui_rgb(th->ink);
	v->on_accent = ui_rgb(th->on_accent);
	v->dark = dark;
}

/* The tabs stand out only as it faces front: none at 52 degrees */
static float facing_of(float phi)
{
	return clampf(1.0f - fabsf(sinf(phi)) / sinf(RIGHT.deg * (float)M_PI / 180.0f), 0.0f, 1.0f);
}

/* The view of `p` in the theme in force */
static void view_of(view *v, const td_pose *p)
{
	const float phi = p->deg * (float)M_PI / 180.0f;

	/* Cleared whole, padding too */
	memset(v, 0, sizeof *v);
	v->cx = p->cx;
	v->cy = p->cy;
	v->s = p->scale;
	v->cp = cosf(phi);
	v->sp = sinf(phi);
	v->cq = cosf(p->pitch * (float)M_PI / 180.0f);
	v->sq = sinf(p->pitch * (float)M_PI / 180.0f);
	v->deep = clampf(p->pitch / SH_DEEP_DEG, 0.0f, 1.0f);
	v->op = p->op;
	td_colors(v);
	memcpy(v->on, p->on, sizeof v->on);
	memcpy(v->press, p->press, sizeof v->press);
	memcpy(v->arm, p->arm, sizeof v->arm);
	v->facing = facing_of(phi);
	v->tab_ring = p->tab_ring;
	v->tabs = p->tabs;
}

/* The colors of `v` as words, to tell one look from another */
static void look_of(const view *v, unsigned look[LOOK_N])
{
	_Static_assert(offsetof(view, on_accent) == offsetof(view, body) + 13 * sizeof(unsigned),
	               "the colors lie together");
	memcpy(look, &v->body, 14 * sizeof(unsigned));
	look[14] = v->dark;
}

/* Whether there is a handheld to draw for `t` at `op`: tourdev_shown, and
 * `op` above nothing */
static bool tour_has(const tour *t, float op)
{
	return tourdev_shown(t) && op > 0.004f;
}

/* The tutorial's pose for `t` at `now`, at `op` */
static void tourdev_pose(const tour *t, uint32_t now, float op, td_pose *out)
{
	const tour_step prev = t->step > TOUR_FIRST_LESSON ? (tour_step)(t->step - 1) : t->step;
	const pose p0 = pose_of(prev), p1 = pose_of(t->step);
	/* It turns from the last lesson's pose as the step comes on */
	const float k = prev == t->step ? 1.0f : ds_turn(clampf((float)(int32_t)(now - t->step_at)
	                                                       / DS_MS_TURN, 0.0f, 1.0f));
	const float deg = p0.deg + (p1.deg - p0.deg) * k, phi = deg * (float)M_PI / 180.0f;
	/* The new key lights in quick, the last one's goes out in quick */
	const float in = prev == t->step ? 1.0f : ds_out(clampf((float)(int32_t)(now - t->step_at)
	                                                       / DS_MS_QUICK, 0.0f, 1.0f));
	const unsigned lit0 = prev == t->step ? 0 : lit_of(prev), lit1 = lit_of(t->step);
	const float facing = facing_of(phi);
	const bool tabs = t->step == TOUR_BRIGHT || prev == TOUR_BRIGHT;
	/* A tab's ring comes in late in the turn, once the tab stands out of the
	 * edge; a side key's ring goes as it does, so there is one at a time */
	const float tab_ring = tabs ? ds_turn(clampf((facing - 0.4f) / 0.6f, 0.0f, 1.0f)) : 0.0f;
	static const tour_part PART[TD_KEYS] = {
		[TD_DPAD] = TP_PARTS, [TD_A] = TP_A, [TD_B] = TP_B, [TD_X] = TP_X, [TD_Y] = TP_Y,
		[TD_SELECT] = TP_SELECT, [TD_START] = TP_START, [TD_MENU] = TP_MENU,
		[TD_VOLUP] = TP_VOLUP, [TD_VOLDN] = TP_VOLDN, [TD_POWER] = TP_POWER,
		[TD_L1] = TP_L1, [TD_R1] = TP_R1, [TD_L2] = TP_L2, [TD_R2] = TP_R2 };
	static const tour_part ARM_PART[4] = { TP_UP, TP_DOWN, TP_LEFT, TP_RIGHT };
	int i;

	/* Cleared whole, padding too, so two poses compare as bytes */
	memset(out, 0, sizeof *out);
	out->cx = TOURDEV_X;
	out->cy = TOURDEV_Y;
	out->scale = p0.s + (p1.s - p0.s) * k;
	out->deg = deg;
	out->op = op;
	for (i = 0; i < TD_KEYS; i++) {
		const bool now_lit = lit1 >> i & 1u, was_lit = lit0 >> i & 1u;

		out->on[i] = now_lit && was_lit ? 1.0f : now_lit ? in : was_lit ? 1.0f - in : 0.0f;
		out->press[i] = PART[i] < TP_PARTS ? tour_part_press(t, PART[i], now) : 0.0f;
	}
	/* An arm's press shows only on the lit d-pad (dpad) */
	for (i = 0; i < 4 && out->on[TD_DPAD] > 0.0f; i++)
		out->arm[i] = tour_part_press(t, ARM_PART[i], now);
	out->tab_ring = tab_ring;
	out->tabs = tabs;
}

/* The handheld as `v` has it, into what `r` draws to */
static void draw_view(SDL_Renderer *r, view *vp)
{
	view v = *vp;
	const float facing = v.facing, tab_ring = v.tab_ring;
	const bool tabs = v.tabs;
	spot spots[TD_KEYS + 2];
	int nspots = 0, i, n;
	SDL_FPoint pt[MAXP], p2[2 * MAXP];
	float top[4];

	v.r = r;

	/* The shoulders, level, behind the body: only their tops show over it. A
	 * lit one stands TAB_OUT_LIT out of the top edge, as MENU and VOL stand
	 * out of theirs for Brightness, and goes back toward it as it presses.
	 * Its halo first, so the body covers the halo's lower half and it glows
	 * from behind the edge; its ring goes over everything with the others.
	 * Tilted, they stand on the top face, over the body (shoulders_tilted). */
	{
		const bool level = v.sq == 0.0f;

		for (i = 0; i < 4; i++) {
			const int k = SHOULDER[i].k;

			top[i] = SH_TOP + (BODY_TOP + TAB_OUT_LIT - 1.2f * v.press[k] - SH_TOP) * v.on[k];
			if (level && v.on[k] > 0.0f) {
				const SDL_FPoint c = at(&v, SHOULDER[i].x, SH_Y, (BODY_TOP + top[i]) / 2.0f);
				const spot s = { v.on[k], v.press[k], c.x, c.y,
				                 SHOULDER[i].w / 2.0f * v.s * v.cp,
				                 (top[i] - BODY_TOP) / 2.0f * v.s, true, true };

				halo(&v, s.on, s.press, s.cx, s.cy, s.rx, s.ry);
				spots[nspots++] = s;
			}
		}
		for (i = 0; i < 4 && level; i++) shoulder(&v, SHOULDER[i].k, SHOULDER[i].x, SHOULDER[i].w, top[i]);
	}
	/* MENU and VOL as tabs out of the edges, behind the body, as it faces
	 * front for Brightness */
	if (tabs && facing > 0.0f) {
		const float up = (TAB_OUT + (TAB_OUT_VOL - 1.2f * v.press[TD_VOLUP] - TAB_OUT) * v.on[TD_VOLUP]) * facing;
		const float dn = (TAB_OUT + (TAB_OUT_VOL - 1.2f * v.press[TD_VOLDN] - TAB_OUT) * v.on[TD_VOLDN]) * facing;
		const float mn = (TAB_OUT + (TAB_OUT_LIT - 1.2f * v.press[TD_MENU] - TAB_OUT) * v.on[TD_MENU]) * facing;
		const float qu = 1.0f - 0.08f * v.press[TD_VOLUP], qd = 1.0f - 0.08f * v.press[TD_VOLDN];
		/* The volume tabs' ring is a shoulder's, as much longer as they are */
		const float more = (TAB_OUT_VOL - TAB_OUT_LIT) / 2.0f;
		spot s = { 0 };

		tab(&v, TD_VOLUP, -1, 12.2f, 19.6f, up, NULL);
		tab(&v, TD_VOLDN, -1, 3.8f, 11.2f, dn, NULL);
		/* Each sign in the middle of what shows of its tab, as long as
		 * leaves room inside the tab's edge and the body's */
		vol_sign(&v, TD_VOLUP, false, -(BODY_W / 2.0f + up / 2.0f), 15.9f,
		         fminf(SIGN_L * qu, up - 1.4f), SIGN_TH * qu);
		vol_sign(&v, TD_VOLDN, false, -(BODY_W / 2.0f + dn / 2.0f), 7.5f,
		         fminf(SIGN_L * qd, dn - 1.4f), SIGN_TH * qd);
		if (v.on[TD_VOLUP] > 0.0f && facing > 0.0f) {
			const SDL_FPoint c = at(&v, -(BODY_W / 2.0f + 1.4f + more), -2.0f, ROCKER_Z);

			spots[nspots++] = (spot){ v.on[TD_VOLUP] * tab_ring, 0.0f, c.x, c.y, 7.0f + more * v.s,
			                          v.s * 7.9f, true, false };
		}
		tab(&v, TD_MENU, 1, MENU_Z - MENU_R, MENU_Z + MENU_R, mn, &s);
		if (mn > 0.05f && s.on > 0.0f) { s.on *= tab_ring; spots[nspots++] = s; }
	}

	/* The body: everything between its front and back outlines, its turned
	 * side (and, tilted, its top) showing, the shoulders standing on the top,
	 * then its front over them */
	{
		float ab[MAXP * 2];
		const int m = rrect(BODY_W, BODY_H, BODY_R, 0.0f, 8, ab);

		for (i = 0; i < m; i++) {
			p2[2 * i] = at(&v, ab[i * 2], BODY_FRONT + 0.8f, ab[i * 2 + 1]);
			p2[2 * i + 1] = at(&v, ab[i * 2], BODY_BACK - 0.8f, ab[i * 2 + 1]);
		}
		/* Its outline on the screen: the hull of both */
		n = hull(p2, 2 * m, pt);
		shape(&v, pt, n, col(&v, v.side, 1.0f), col(&v, v.line, 1.0f), DS_FRAME);
		if (v.sq != 0.0f) shoulders_tilted(&v, top, spots, &nspots);
		for (i = 0; i < m; i++) pt[i] = at(&v, ab[i * 2] * 0.985f, BODY_FRONT, ab[i * 2 + 1] * 0.988f);
		shape(&v, pt, m, col(&v, v.body, 1.0f), col(&v, v.line, 1.0f), DS_FRAME);
	}

	/* The glass and the screen in it; SELECT and START's pocket */
	n = front_rr(&v, 55.1f, 43.8f, 0.8f, 0.0f, BODY_FRONT, 14.2f, pt);
	shape(&v, pt, n, col(&v, v.glass, 1.0f), col(&v, 0, 0.0f), 0.0f);
	n = front_rr(&v, 48.8f, 36.6f, 0.3f, 0.0f, BODY_FRONT, 15.55f, pt);
	shape(&v, pt, n, col(&v, v.screen, 1.0f), col(&v, 0, 0.0f), 0.0f);
	n = front_rr(&v, 16.4f, 7.6f, 3.8f, 0.1f, BODY_FRONT, -29.6f, pt);
	shape(&v, pt, n, col(&v, v.pocket, 1.0f), col(&v, 0, 0.0f), 0.0f);

	/* Where the lit keys' halos and rings go */
	if (v.on[TD_DPAD] > 0.0f) {
		const SDL_FPoint c = at(&v, DPAD_X, BODY_FRONT, DPAD_Z);

		spots[nspots++] = (spot){ v.on[TD_DPAD], 0.0f, c.x, c.y, 9.0f * v.s * v.cp, 9.0f * v.s * v.cq, true, false };
	}
	for (i = TD_A; i <= TD_START; i++)
		if (v.on[i] > 0.0f) {
			const SDL_FPoint c = at(&v, FACE[i].x, BODY_FRONT, FACE[i].z);

			spots[nspots++] = (spot){ v.on[i], v.press[i], c.x, c.y, FACE[i].r * v.s * v.cp,
			                          FACE[i].r * v.s * v.cq, true, false };
		}
	{
		const float ss = fabsf(v.sp);
		const int side = v.sp > 0.0f ? 1 : -1;

		if (ss > 0.05f && side > 0) {
			const SDL_FPoint m = at(&v, SIDE_X, MENU_Y, MENU_Z), pw = at(&v, SIDE_X, POWER_Y, POWER_Z);
			/* The capsule at 45 degrees, its bounds on the face */
			const float pr = (9.8f / 2.0f) * 0.7071f + 1.15f * 0.7071f;

			if (v.on[TD_MENU] > 0.0f)
				spots[nspots++] = (spot){ v.on[TD_MENU] * (1.0f - tab_ring), v.press[TD_MENU], m.x, m.y,
				                          MENU_R * v.s * ss, MENU_R * v.s, true, false };
			if (v.on[TD_POWER] > 0.0f)
				spots[nspots++] = (spot){ v.on[TD_POWER], v.press[TD_POWER], pw.x, pw.y,
				                          pr * v.s * ss, pr * v.s, true, false };
		} else if (ss > 0.05f && (v.on[TD_VOLUP] > 0.0f || v.on[TD_VOLDN] > 0.0f)) {
			const SDL_FPoint c = at(&v, -SIDE_X, VOL_Y, ROCKER_Z);
			const float on = fmaxf(v.on[TD_VOLUP], v.on[TD_VOLDN]);

			spots[nspots++] = (spot){ on * (1.0f - tab_ring), fmaxf(v.press[TD_VOLUP], v.press[TD_VOLDN]), c.x, c.y,
			                          2.2f * v.s * ss, 8.1f * v.s, true, false };
		}
	}
	for (i = 0; i < nspots; i++)
		if (!spots[i].under)
			halo(&v, spots[i].on, spots[i].press, spots[i].cx, spots[i].cy, spots[i].rx, spots[i].ry);

	/* The front's keys */
	dpad(&v);
	for (i = TD_A; i <= TD_START; i++) face_key(&v, i, BODY_FRONT);

	/* The side that shows, its keys on its own plane */
	if (fabsf(v.sp) > 0.05f) {
		const float ss = fabsf(v.sp);

		if (v.sp < 0.0f) {
			const float qu = 1.0f - 0.08f * v.press[TD_VOLUP], qd = 1.0f - 0.08f * v.press[TD_VOLDN];

			/* The recess, the bridge joining the two keys, the keys on it */
			n = side_rr(&v, -1, 4.4f, 16.2f, 2.1f, 0.0f, VOL_Y, ROCKER_Z, pt);
			shape(&v, pt, n, col(&v, v.glass, 1.0f), col(&v, 0, 0.0f), 0.0f);
			n = side_rr(&v, -1, 3.5f, 15.7f, 1.7f, 0.0f, VOL_Y, ROCKER_Z, pt);
			shape(&v, pt, n, key_fill(&v, TD_VOLUP), key_edge(&v, TD_VOLUP), DS_FRAME);
			n = side_rr(&v, -1, 3.6f * qu, 5.9f * qu, 1.75f, 0.0f, VOL_Y, VOLUP_Z, pt);
			shape(&v, pt, n, key_fill(&v, TD_VOLUP), key_edge(&v, TD_VOLUP), DS_FRAME);
			n = side_rr(&v, -1, 3.6f * qd, 5.9f * qd, 1.75f, 0.0f, VOL_Y, VOLDN_Z, pt);
			shape(&v, pt, n, key_fill(&v, TD_VOLDN), key_edge(&v, TD_VOLDN), DS_FRAME);
			vol_sign(&v, TD_VOLUP, true, VOL_Y, VOLUP_Z, SIGN_L * qu, SIGN_TH * qu);
			vol_sign(&v, TD_VOLDN, true, VOL_Y, VOLDN_Z, SIGN_L * qd, SIGN_TH * qd);
			n = side_rr(&v, -1, 1.2f, 11.7f, 0.3f, 0.0f, -1.89f, -23.4f, pt);
			shape(&v, pt, n, col(&v, v.glass, 1.0f), col(&v, 0, 0.0f), 0.0f);
		} else {
			const float q = 1.0f - 0.08f * v.press[TD_MENU], qp = 1.0f - 0.08f * v.press[TD_POWER];
			const SDL_FPoint m = at(&v, SIDE_X, MENU_Y, MENU_Z);
			/* The dots in on-accent on the lit key, as text on accent is, in
			 * both themes; at rest a soft text on the key (4:1 and more) */
			const SDL_Color dot = col(&v, ui_mix(v.dot, v.on_accent, v.on[TD_MENU]), 1.0f);
			int d;

			for (i = 0; i < 4; i++) {
				n = side_rr(&v, 1, 10.0f, 2.15f, 1.05f, -(float)M_PI / 4.0f, POWER_Y, LED_Z[i], pt);
				shape(&v, pt, n, col(&v, v.mark, 1.0f), col(&v, 0, 0.0f), 0.0f);
			}
			n = side_rr(&v, 1, 9.8f * qp, 2.3f * qp, 1.1f, -(float)M_PI / 4.0f, POWER_Y, POWER_Z, pt);
			shape(&v, pt, n, key_fill(&v, TD_POWER), key_edge(&v, TD_POWER), DS_FRAME);
			n = v.sq == 0.0f ? oval(m.x, m.y, MENU_R * q * v.s * ss, MENU_R * q * v.s, pt)
			                 : disc(&v, 1, SIDE_X, MENU_Y, MENU_Z, MENU_R * q, pt);
			shape(&v, pt, n, key_fill(&v, TD_MENU), key_edge(&v, TD_MENU), DS_FRAME);
			for (d = -1; d <= 1; d++) {
				const SDL_FPoint c = at(&v, SIDE_X, MENU_Y, MENU_Z + d * MENU_DOT_GAP * q);

				n = v.sq == 0.0f ? oval(c.x, c.y, MENU_DOT_R * q * v.s * ss, MENU_DOT_R * q * v.s, pt)
				                 : disc(&v, 1, SIDE_X, MENU_Y, MENU_Z + d * MENU_DOT_GAP * q, MENU_DOT_R * q, pt);
				fill(r, pt, n, dot);
			}
		}
	}

	/* The rings, over everything */
	for (i = 0; i < nspots; i++)
		if (spots[i].ring) ring(&v, spots[i].on, spots[i].cx, spots[i].cy, spots[i].rx, spots[i].ry);
	flush(r);
}

/* ---- the frame kept ------------------------------------------------------------ */

/* The handheld changes only while it turns, a key lights or goes out, a key
 * presses, and in the first lesson's fade-in. Drawn every frame, its ~1,500
 * points cost the Pixel 2 ~15 ms a frame (a third of the lesson's frame
 * rate, though they cover ~200,000 pixels), so it is drawn into a texture
 * when its pose changes and copied the rest of the time. The tutorial's is
 * 304 x 368, the column's box (what shows of the figure is x 5 to 272, y 86
 * to 424 in every pose and every frame of every turn, the top a lit
 * shoulder's halo), 447 KB. What a texture holds is premultiplied - drawn
 * with the blend into a clear texture - so it is copied with the
 * premultiplied blend. A renderer without that blend (the host's software
 * one, which --shot uses) draws the handheld every frame, as before.
 *
 * Each caller keeps its own (td_keep): the box it covers on the screen is
 * the texture's size, and the texture's corner is the box's. */

static bool keep_ready(SDL_Renderer *r, td_keep *k)
{
	if (k->can > 0 && k->tex) {
		int w, h;

		SDL_QueryTexture(k->tex, NULL, NULL, &w, &h);
		if (w == k->box.w && h == k->box.h) return true;
		td_keep_free(k);
	}
	if (k->can == 0) {
		const SDL_BlendMode pm = SDL_ComposeCustomBlendMode(
			SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
			SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);

		k->tex = SDL_CreateTexture(r, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
		                           k->box.w, k->box.h);
		k->can = k->tex && SDL_SetTextureBlendMode(k->tex, pm) == 0 ? 1 : -1;
		if (k->can > 0) {
			SDL_SetTextureScaleMode(k->tex, SDL_ScaleModeNearest);
		} else if (k->tex) {
			SDL_DestroyTexture(k->tex);
			k->tex = NULL;
		}
		k->ok = false;
	}
	return k->can > 0;
}

/* Whether `k` holds `p` as `v` has it */
static bool keep_has(const td_keep *k, const td_pose *p, const view *v)
{
	unsigned look[LOOK_N];

	look_of(v, look);
	return k->ok && memcmp(p, &k->kept, sizeof *p) == 0 && memcmp(look, k->look, sizeof look) == 0;
}

/* Draws `p` as `v` has it into `k`'s texture, if that holds something
 * else, and gives the screen's target, color and blend back after. False
 * if the renderer cannot keep, or the texture could not be drawn into. */
static bool keep_make(SDL_Renderer *r, td_keep *k, const td_pose *p, const view *v)
{
	SDL_Texture *was;
	SDL_BlendMode bm;
	Uint8 cr, cg, cb, ca;

	if (!keep_ready(r, k)) return false;
	if (keep_has(k, p, v)) return true;
	was = SDL_GetRenderTarget(r);
	SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
	SDL_GetRenderDrawBlendMode(r, &bm);
	k->ok = false;
	if (SDL_SetRenderTarget(r, k->tex) == 0) {
		view vv = *v;

		SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
		SDL_RenderClear(r);
		g_dx = (float)-k->box.x;
		g_dy = (float)-k->box.y;
		draw_view(r, &vv);
		g_dx = g_dy = 0.0f;
		k->kept = *p;
		look_of(v, k->look);
		k->ok = true;
	}
	SDL_SetRenderTarget(r, was);
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	SDL_SetRenderDrawBlendMode(r, bm);
	return k->ok;
}

void td_draw(SDL_Renderer *r, td_keep *k, const td_pose *p)
{
	view v;

	view_of(&v, p);
	if (keep_make(r, k, p, &v))
		SDL_RenderCopy(r, k->tex, NULL, &k->box);
	else
		draw_view(r, &v);
}

void td_make(SDL_Renderer *r, td_keep *k, const td_pose *p)
{
	view v;

	view_of(&v, p);
	keep_make(r, k, p, &v);
}

void td_keep_free(td_keep *k)
{
	if (k->tex) SDL_DestroyTexture(k->tex);
	k->tex = NULL;
	k->can = 0;
	k->ok = false;
}

/* ---- the tutorial's ------------------------------------------------------------- */

static td_keep g_tour_keep = { .box = { 0, 80, 304, 368 } };

void tourdev_make(SDL_Renderer *r, const tour *t, uint32_t now, float op)
{
	td_pose p;
	view v;

	if (!tour_has(t, op)) return;
	tourdev_pose(t, now, op, &p);
	view_of(&v, &p);
	keep_make(r, &g_tour_keep, &p, &v);
}

void tourdev_draw(SDL_Renderer *r, const tour *t, uint32_t now, float op)
{
	td_pose p;

	if (!tour_has(t, op)) return;
	tourdev_pose(t, now, op, &p);
	td_draw(r, &g_tour_keep, &p);
}

void tourdev_free(void)
{
	td_keep_free(&g_tour_keep);
}
