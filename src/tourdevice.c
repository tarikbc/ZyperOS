/* SPDX-License-Identifier: GPL-3.0-only */
/* The handheld beside each lesson: DESIGN.md 11.10, "The handheld". See
 * src/tourdevice.h. Two arts: the GKD Pixel 2's, here, and the slab
 * handhelds', the TrimUI Brick's and the R36S's (slab_draw, below).
 *
 * The GKD Pixel 2's art, in millimetres as tools/build-pixel2-model.py builds
 * the landing page's model from its dimensioned drawing, its keys where its
 * td_model has them (src/device/pixel2_model.c): x right, z up, y deep
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
#include <stdio.h>
#include <string.h>

#include "clamp.h"
#include "design.h"
#include "platform.h"   /* PLAT_TARGET_FORMAT */
#include "theme.h"
#include "tourdevice.h"
#include "ui.h"

/* ---- the model ----------------------------------------------------------- */

/* Where each key is, and how big, is the model's (src/device/td_model.h,
 * the Pixel 2's in src/device/pixel2_model.c), read as the view's part[k]:
 * on the front, (x, z) and a round key's radius; on a side face, (y, z) and
 * its radius, or POWER's half length; on the top edge, a shoulder's x, its
 * top's z and its half width. The rest here is the art's own: the body, the
 * screen, MENU's dots, the lights under POWER, the volume keys' rocker and
 * the shoulders' plane. */

/* The body: the model's body_w x body_h */
#define BODY_W     63.0f
#define BODY_H     81.0f
#define BODY_R     3.9f
#define BODY_FRONT -8.5f
#define BODY_BACK  8.5f
/* The side face's plane, a hair proud of the body so its keys sit on it */
#define SIDE_X     (BODY_W / 2.0f + 0.1f)

/* The right face: MENU, round with three dimples in a column; POWER, a
 * capsule at 45 degrees; four light lenses below it. The left face: the
 * volume rocker, VOL+ above VOL-, and the card slot. */
/* MENU's three dots, drawn bigger and further apart than the model's 0.3 mm
 * dimples 1.05 apart, so they are three dots and not a line on the panel:
 * at 3.95 units a millimetre and 0.625 a unit, 2.5 pixels each, 1.2 apart */
#define MENU_DOT_R   0.5f
#define MENU_DOT_GAP 1.45f
/* The lights, a column under POWER as deep as it is */
static const float LED_Z[4] = { 1.41f, -4.03f, -9.67f, -15.31f };
/* The rocker the volume keys stand on, as deep as they are */
#define ROCKER_Z 11.69f

/* The top edge: the shoulders, L1/R1 outside and L2/R2 inside, on a plane
 * behind the body's middle, from under the edge to their tops, so that only
 * their tops show over it */
static const td_key SHOULDER[4] = { TD_L1, TD_R1, TD_L2, TD_R2 };
#define BODY_TOP   (BODY_H / 2.0f)
#define SH_Y       2.5f
#define SH_BASE    36.0f
/* With the top showing they are blocks standing on it, from the front of
 * their notch (the model's caps, -0.95) to the back the body's outline has
 * (BODY_BACK less its 0.8 mm chamfer), their whole depth from SH_DEEP_DEG of
 * pitch up; below it they are as deep as that much of it, so at 0 they are
 * the plane above and the tilt starts without a jump */
#define SH_FRONT    -0.95f
#define SH_BACK     (BODY_BACK - 0.8f)
#define SH_DEEP_DEG 12.0f
/* How far down the body's round top corner an outside cap's base follows
 * it, tilted (shoulder_face) */
#define SH_HUG      1.2f

/* ---- the poses ------------------------------------------------------------- */

/* How far a side key stands out of its edge as a tab in the both-sides
 * view, mm; a lit
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

/* The keys lesson `s` lights with `t`'s chords: Brightness's are MENU+VOL or
 * the device's own keys (F1 and F2) */
static unsigned lit_of(const tour *t, tour_step s)
{
	switch (s) {
	/* The left stick too, which moves as the d-pad does, where the
	 * handheld has one (the R36S): a key its model has not stays unlit */
	case TOUR_MOVE:   return 1u << TD_DPAD | 1u << TD_L1 | 1u << TD_R1 | 1u << TD_L3;
	case TOUR_A:      return 1u << TD_A;
	case TOUR_B:      return 1u << TD_B;
	case TOUR_X:      return 1u << TD_X;
	case TOUR_Y:      return 1u << TD_Y;
	case TOUR_MENU:   return 1u << TD_MENU;
	case TOUR_SELECT: return 1u << TD_SELECT | 1u << TD_L2 | 1u << TD_R2;
	case TOUR_VOL:    return 1u << TD_VOLUP | 1u << TD_VOLDN;
	case TOUR_BRIGHT: return t->bright == DEV_BRIGHT_MENU_VOL ? 1u << TD_MENU | 1u << TD_VOLUP | 1u << TD_VOLDN
	                                                          : 1u << TD_F1 | 1u << TD_F2;
	case TOUR_POWER:  return 1u << TD_POWER;
	default:          return 0;
	}
}

/* The faces of `m` the keys `lit` are on, 1u << TD_FACE_* */
static unsigned faces_of(const td_model *m, unsigned lit)
{
	unsigned f = 0;
	int i;

	for (i = 0; i < m->n_parts; i++)
		if (lit >> m->parts[i].key & 1u) f |= 1u << m->parts[i].face;
	return f;
}

#define BOTH_SIDES (1u << TD_FACE_LEFT | 1u << TD_FACE_RIGHT)

/* Whether keys on `faces` show as tabs out of both side edges, as the
 * handheld faces front: keys on both sides and none on the top */
static bool tabs_on(unsigned faces)
{
	return (faces & BOTH_SIDES) == BOTH_SIDES && !(faces >> TD_FACE_TOP & 1u);
}

/* The view of `m` a lesson lighting keys on `faces` turns to: round to the
 * back for a back ledge's (the Brick's shoulders, which no front view
 * shows), tilted back for the top's (the Pixel 2's shoulders, both sides'
 * in each lesson, so it stays turned the front's way), front with both
 * sides' out of the edges, the one side they are on, or the front */
static td_view view_for(const td_model *m, unsigned faces)
{
	if (faces >> TD_FACE_LEDGE & 1u) return m->back;
	if (faces >> TD_FACE_TOP & 1u) return m->top;
	if (tabs_on(faces)) return m->both;
	if (faces >> TD_FACE_RIGHT & 1u) return m->right;
	if (faces >> TD_FACE_LEFT & 1u) return m->left;
	return m->front;
}

/* The view of `m` for a lesson lighting `lit`, `ms` after its turn into
 * the lesson ended: view_for's, or for keys on the back ledge and off it,
 * from the others' view round to the back and back again (td_swing) */
static td_view lesson_view(const td_model *m, unsigned lit, int ms)
{
	const unsigned f = faces_of(m, lit);

	if (!td_spins(m, lit)) return view_for(m, f);
	return td_view_mix(view_for(m, f & ~(1u << TD_FACE_LEDGE)), m->back, td_swing(ms));
}

bool tourdev_shown(const tour *t)
{
	return t && t->model && t->step >= TOUR_FIRST_LESSON &&
	       t->step < TOUR_FIRST_LESSON + TOUR_LESSONS;
}

/* ---- the frame's colors and view ------------------------------------------------ */

/* Everything one frame of the handheld is drawn from, and nothing else: the
 * pose (td_pose), its model's keys and the theme's colors. `r` is set only
 * while it is drawn. */
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
	const td_model *m;
	td_part part[TD_KEYS];        /* the model's keys by key, zero where it has none */
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

/* While td_bounds measures: what the vertices reach, and nothing drawn */
static SDL_FRect *g_measure;

static void flush(SDL_Renderer *r)
{
	if (g_ni > 0 && !g_measure) {
		SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
		SDL_RenderGeometry(r, NULL, g_bv, g_nv, g_bi, g_ni);
	}
	g_nv = g_ni = 0;
}

static void geometry(SDL_Renderer *r, const SDL_Vertex *v, int nv, const int *idx, int ni)
{
	int i;

	if (g_measure) {
		for (i = 0; i < nv; i++) {
			const float x = v[i].position.x, y = v[i].position.y;
			SDL_FRect *m = g_measure;

			if (m->w < 0.0f) {
				*m = (SDL_FRect){ x, y, 0.0f, 0.0f };
				continue;
			}
			if (x < m->x) { m->w += m->x - x; m->x = x; }
			if (y < m->y) { m->h += m->y - y; m->y = y; }
			m->w = fmaxf(m->w, x - m->x);
			m->h = fmaxf(m->h, y - m->y);
		}
		return;
	}
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
	const td_part *pa = &v->part[k];
	const SDL_FPoint c = at(v, pa->x, y, pa->z);
	const int n = v->sq == 0.0f ? oval(c.x, c.y, pa->r * v->s * v->cp * q, pa->r * v->s * q, p)
	                            : disc(v, 0, pa->x, y, pa->z, pa->r * q, p);

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
	const td_part *pa = &v->part[TD_DPAD];
	SDL_FPoint p[12];
	int i, j;

	for (i = 0; i < 12; i++) p[i] = at(v, pa->x + X[i][0], BODY_FRONT, pa->z + X[i][1]);
	shape(v, p, 12, key_fill(v, TD_DPAD), key_edge(v, TD_DPAD), DS_FRAME);
	/* A pressed arm of the lit d-pad goes a step darker, so which way shows */
	if (v->on[TD_DPAD] <= 0.0f) return;
	for (i = 0; i < 4; i++) {
		const float a = v->arm[i] * v->on[TD_DPAD];
		SDL_FPoint q[4], in[4];

		if (a <= 0.004f) continue;
		for (j = 0; j < 4; j++) q[j] = at(v, pa->x + ARM[i][j][0], BODY_FRONT, pa->z + ARM[i][j][1]);
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

/* The body's outline at `x`, its top: flat, then down its round corner */
static float body_top_at(float x)
{
	const float d = fabsf(x) - (BODY_W / 2.0f - BODY_R);

	return d <= 0.0f ? BODY_TOP : BODY_TOP - BODY_R + sqrtf(fmaxf(0.0f, BODY_R * BODY_R - d * d));
}

/* The x where the body's outline has come SH_HUG down its round corner */
#define SH_HUG_X (BODY_W / 2.0f - BODY_R + sqrtf(BODY_R * BODY_R - (BODY_R - SH_HUG) * (BODY_R - SH_HUG)))

/* A shoulder's face toward the front, `k`, on the plane at depth `y`, from
 * `base` up to `top`; or (`base` below 0) from the body's outline up, down
 * its round corner as far as SH_HUG under the top, so an outside cap sits on
 * the corner with nothing under it, and square from there to its end rather
 * than in a thin point down the side. Square at rest; lit, its top corners
 * rounded as a tab's. Its points, 17 at most; how many of them are its base,
 * left to right, in *nbase. */
static int shoulder_face(const view *v, int k, float x, float w, float y, float base, float top,
                         SDL_FPoint *p, int *nbase)
{
	enum { ARC = 4 };
	const float rad = 1.2f * v->on[k], x0 = x - w / 2.0f, x1 = x + w / 2.0f;
	const float flat = BODY_W / 2.0f - BODY_R, low = BODY_TOP - SH_HUG;
	int n = 0, i;

	if (base >= 0.0f) {
		p[n++] = at(v, x0, y, base);
		p[n++] = at(v, x1, y, base);
	} else {
		/* Left to right along the outline: flat, the corner's arc where
		 * the face is over it, and level at SH_HUG down past it */
		if (x0 < -SH_HUG_X) p[n++] = at(v, x0, y, low);
		if (x0 < -flat)
			for (i = 0; i <= ARC; i++) {
				const float xa = fmaxf(x0, -SH_HUG_X) + (fminf(-flat, x1) - fmaxf(x0, -SH_HUG_X)) * i / ARC;

				p[n++] = at(v, xa, y, body_top_at(xa));
			}
		else
			p[n++] = at(v, x0, y, BODY_TOP);
		if (x1 > flat)
			for (i = 0; i <= ARC; i++) {
				const float xa = fmaxf(flat, x0) + (fminf(x1, SH_HUG_X) - fmaxf(flat, x0)) * i / ARC;

				p[n++] = at(v, xa, y, body_top_at(xa));
			}
		else
			p[n++] = at(v, x1, y, BODY_TOP);
		if (x1 > SH_HUG_X) p[n++] = at(v, x1, y, low);
	}
	if (nbase) *nbase = n;
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
	const int n = shoulder_face(v, k, x, w, SH_Y, SH_BASE, top, p, NULL);

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

/* Twice the signed area of `p` */
static float area2(const SDL_FPoint *p, int n)
{
	float a = 0.0f;
	int i;

	for (i = 0; i < n; i++) a += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
	return a;
}

/* A simple polygon filled, concave or not: cut into triangles ear by ear
 * (fill's fan from the middle is for convex shapes and the cross) */
static void fill_any(SDL_Renderer *r, const SDL_FPoint *p, int n, SDL_Color c)
{
	SDL_Vertex vx[MAXP];
	int idx[MAXP * 3], rest[MAXP], m = n, ni = 0, i, j;
	float sign;

	if (n < 3 || n > MAXP || c.a == 0) return;
	sign = area2(p, n) > 0.0f ? 1.0f : -1.0f;
	for (i = 0; i < n; i++) {
		vx[i] = (SDL_Vertex){ p[i], c, { 0, 0 } };
		rest[i] = i;
	}
#define TURN(a_, b_, c_) (((b_).x - (a_).x) * ((c_).y - (b_).y) - ((b_).y - (a_).y) * ((c_).x - (b_).x))
	while (m > 3) {
		bool cut = false;

		for (i = 0; i < m && !cut; i++) {
			const int a = rest[(i + m - 1) % m], b = rest[i], d = rest[(i + 1) % m];

			if (sign * TURN(p[a], p[b], p[d]) < 0.0f) continue;
			for (j = 0; j < m; j++) {
				const int q = rest[j];

				if (q == a || q == b || q == d) continue;
				if (sign * TURN(p[a], p[b], p[q]) > 0.0f && sign * TURN(p[b], p[d], p[q]) > 0.0f &&
				    sign * TURN(p[d], p[a], p[q]) > 0.0f)
					break;
			}
			if (j < m) continue;
			idx[ni++] = a; idx[ni++] = b; idx[ni++] = d;
			for (j = i; j < m - 1; j++) rest[j] = rest[j + 1];
			m--;
			cut = true;
		}
		if (!cut) break;   /* no ear (not a simple polygon): the rest as a fan */
	}
#undef TURN
	for (i = 1; i + 1 < m; i++) {
		idx[ni++] = rest[0]; idx[ni++] = rest[i]; idx[ni++] = rest[i + 1];
	}
	geometry(r, vx, n, idx, ni);
}

/* shape for a simple polygon that may be concave: the edge `w` wide astride
 * its outline, the fill inside it cut ear by ear */
static void shape_any(const view *v, SDL_FPoint *p, int n, SDL_Color f, SDL_Color e, float w)
{
	SDL_FPoint in[MAXP], out[MAXP];

	n = dedup(p, n);
	if (n < 3) return;
	offset(p, n, -w / 2.0f, in);
	offset(p, n, w / 2.0f, out);
	fill_any(v->r, in, n, f);
	band(v->r, out, in, n, e, e);
}

/* What face `f` covers swept back to face `b` (each point of it moved the
 * same way), less its base's sweep, which is on the body: `f`'s base, then
 * round its upper side, taking each edge from the face it shows on (`b`'s
 * where it faces the way of the sweep), with a step between the two where
 * that changes. The upper side is convex, so there are two such steps at
 * most. Into `u`; its count. */
static int swept(const SDL_FPoint *f, const SDL_FPoint *b, int n, int nbase, SDL_FPoint *u)
{
	const float sign = area2(f, n) > 0.0f ? 1.0f : -1.0f;
	const float dx = b[0].x - f[0].x, dy = b[0].y - f[0].y;
	bool back = false;
	int m = 0, j;

	for (j = 0; j < nbase; j++) u[m++] = f[j];
	/* The upper side: from the base's last point round to its first */
	for (j = nbase - 1; j < n; j++) {
		const int a = j, c = (j + 1) % n;
		const float ex = f[c].x - f[a].x, ey = f[c].y - f[a].y;
		const bool use_b = sign * (ey * dx - ex * dy) > 0.0f;

		if (use_b != back) {
			u[m++] = use_b ? b[a] : f[a];
			back = use_b;
		}
		u[m++] = back ? b[c] : f[c];
	}
	if (back) u[m++] = f[0];
	return m;
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
	SDL_FPoint face[4][20], back[20], both[40], out[MAXP];
	int nface[4], nbase[4], i, j, n;

	for (i = 0; i < 4; i++) {
		const int k = SHOULDER[i];
		const float x = v->part[k].x, w = 2.0f * v->part[k].r;
		float x0 = 1e9f, x1 = -1e9f, ya = 1e9f, yb = -1e9f;

		nface[i] = shoulder_face(v, k, x, w, y0, -1.0f, top[i], face[i], &nbase[i]);
		if (v->on[k] <= 0.0f) continue;
		/* Its ring and halo about what stands over the top, as level */
		n = shoulder_face(v, k, x, w, y1, BODY_TOP, top[i], both, NULL);
		n += shoulder_face(v, k, x, w, y0, BODY_TOP, top[i], both + n, NULL);
		for (j = 0; j < n; j++) {
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
		const int s = RIGHTWARD[v->sp < 0.0f ? 3 - j : j], k = SHOULDER[s];

		shoulder_face(v, k, v->part[k].x, 2.0f * v->part[k].r, y1, -1.0f, top[s], back, NULL);
		n = swept(face[s], back, nface[s], nbase[s], out);
		shape_any(v, out, n, key_fill(v, k), key_edge(v, k), DS_FRAME);
		shape_any(v, face[s], nface[s], key_fill(v, k), key_edge(v, k), DS_FRAME);
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

/* The tabs stand out only as it faces front: none as far turned as `m`'s
 * side view (52 degrees, the Pixel 2's) */
static float facing_of(const td_model *m, float phi)
{
	return clampf(1.0f - fabsf(sinf(phi)) / sinf(m->right.deg * (float)M_PI / 180.0f), 0.0f, 1.0f);
}

/* The view of `p` in the theme in force */
static void view_of(view *v, const td_pose *p)
{
	const float phi = p->deg * (float)M_PI / 180.0f;
	int i;

	/* Cleared whole, padding too */
	memset(v, 0, sizeof *v);
	v->m = p->model;
	for (i = 0; v->m && i < v->m->n_parts; i++) v->part[v->m->parts[i].key] = v->m->parts[i];
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
	v->facing = v->m ? facing_of(v->m, phi) : 0.0f;
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

/* The tutorial's pose for `t` at `now`, at `op`: `t`'s model turned to the
 * view its lesson's keys ask for (lesson_view) */
static void tourdev_pose(const tour *t, uint32_t now, float op, td_pose *out)
{
	const td_model *m = t->model;
	const tour_step prev = t->step > TOUR_FIRST_LESSON ? (tour_step)(t->step - 1) : t->step;
	const unsigned f0 = faces_of(m, lit_of(t, prev)), f1 = faces_of(m, lit_of(t, t->step));
	/* From where the last lesson's was as this one came on, a spinning one
	 * anywhere in its turns, to this one's, which turns once it is there */
	const int ago = t->prev_at ? (int)(int32_t)(t->step_at - t->prev_at) : 0;
	const td_view p0 = lesson_view(m, lit_of(t, prev), ago - DS_MS_TURN);
	const td_view p1 = lesson_view(m, lit_of(t, t->step), (int)(int32_t)(now - t->step_at) - DS_MS_TURN);
	/* It turns from the last lesson's pose as the step comes on */
	const float k = prev == t->step ? 1.0f : ds_turn(clampf((float)(int32_t)(now - t->step_at)
	                                                       / DS_MS_TURN, 0.0f, 1.0f));
	const float deg = p0.deg + (p1.deg - p0.deg) * k, phi = deg * (float)M_PI / 180.0f;
	/* The new key lights in quick, the last one's goes out in quick */
	const float in = prev == t->step ? 1.0f : ds_out(clampf((float)(int32_t)(now - t->step_at)
	                                                       / DS_MS_QUICK, 0.0f, 1.0f));
	const unsigned lit0 = prev == t->step ? 0 : lit_of(t, prev), lit1 = lit_of(t, t->step);
	const float facing = facing_of(m, phi);
	const bool tabs = tabs_on(f0) || tabs_on(f1);
	/* A tab's ring comes in late in the turn, once the tab stands out of the
	 * edge; a side key's ring goes as it does, so there is one at a time */
	const float tab_ring = tabs ? ds_turn(clampf((facing - 0.4f) / 0.6f, 0.0f, 1.0f)) : 0.0f;
	static const tour_part PART[TD_KEYS] = {
		[TD_DPAD] = TP_PARTS, [TD_A] = TP_A, [TD_B] = TP_B, [TD_X] = TP_X, [TD_Y] = TP_Y,
		[TD_SELECT] = TP_SELECT, [TD_START] = TP_START, [TD_MENU] = TP_MENU,
		[TD_VOLUP] = TP_VOLUP, [TD_VOLDN] = TP_VOLDN, [TD_POWER] = TP_POWER,
		[TD_L1] = TP_L1, [TD_R1] = TP_R1, [TD_L2] = TP_L2, [TD_R2] = TP_R2,
		[TD_F1] = TP_F1, [TD_F2] = TP_F2,
		/* The sticks' caps have no part of the lesson's: they never show a press */
		[TD_L3] = TP_PARTS, [TD_R3] = TP_PARTS };
	static const tour_part ARM_PART[4] = { TP_UP, TP_DOWN, TP_LEFT, TP_RIGHT };
	int i;

	/* Cleared whole, padding too, so two poses compare as bytes */
	memset(out, 0, sizeof *out);
	out->model = m;
	/* in the column, which a wider canvas widens by its difference */
	out->cx = TOURDEV_X + (ZYPEROS_SCREEN_W - 1024) / 2;
	out->cy = TOURDEV_Y;
	out->scale = p0.s + (p1.s - p0.s) * k;
	out->deg = deg;
	/* ...and tilts back to show its top, or level again, in the same turn
	 * (0 exactly between two level lessons) */
	out->pitch = p0.pitch + (p1.pitch - p0.pitch) * k;
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

/* ---- the TrimUI Brick and the R36S ----------------------------------------------- */

/* The slab handhelds' art (TD_ART_BRICK, TD_ART_R36S): a body of two
 * blocks, the thin part, the whole outline from the front, and behind it a
 * thicker part below a ledge across the back, which carries the shoulders.
 * Each one's measures and marks are its own `slab`, below; its keys are
 * where its model has them (src/device/brick_model.c, r36s_model.c), in its
 * millimetres: x right, z up, y deep, from the body's middle. The back view
 * goes all the way round, so each block draws the faces of it that face
 * the viewer, the farther block first, and each key shows only from where
 * its face does. Original art from the published outlines and the measured
 * key places; no maker's mark.
 *
 * The TrimUI Brick: the thin part 11.8 deep from the front, the thick part
 * to 19.9 below the ledge (z 12.05), the USB-C housing on the ledge between
 * the shoulders, F1 and F2, SELECT's square and START's triangle, two dot
 * grilles, the FN slider over POWER, the light bar along the top edge.
 *
 * The R36S: the thin part 18.5 deep, the grip behind it to 30.0 below the
 * ledge (z 9.5), the shoulders tall paddles on it with nothing between them;
 * the glass from the top edge; the speaker's slots between the d-pad and
 * A B X Y; the two sticks, which are no keys of the launcher's and never
 * lit; POWER a pill and the reset hole under it. */
typedef struct {
	float w, top, rt, rb;              /* the body, its half height, its corners */
	float front, thin, back, ledge;    /* depths from the middle; the ledge's z */
	/* The glass, and the lit screen centred in it */
	float glass_w, glass_h, glass_x, glass_z, glass_r, screen_w, screen_h;
	float arm;                         /* the d-pad's arms, half as wide as they are */
	float pill_h;                      /* F1 and F2, pills this tall */
	/* SELECT's square and START's triangle, this big (0 none); in MENU's
	 * dots' colors (on-accent lit, a soft text at rest) */
	float icon;
	/* Dot grilles: two rows of dots either side of the bottom keys (n 0 none) */
	float grille_x[2], grille_z, dot_r;
	int   grille_n;
	/* A slot grille: each slot's middle and length, all slot_h tall
	 * (n_slots 0 none) */
	const float (*slots)[3];
	int   n_slots;
	float slot_h;
	/* A microphone's hole at (0, mic_z), its radius (0 none) */
	float mic_z, mic_r;
	/* A seam across the face at seam_z, where its mouldings meet (0 none) */
	float seam_z;
	/* The sticks, at (+-stick_x, stick_z): the well and the cap (0 none) */
	float stick_x, stick_z, stick_well, stick_cap;
	/* The well a light ring standing out of the face, this wide, round the
	 * cap's dark base (0: a plain dark well) */
	float stick_ring;
	float vol_w;                       /* the volume keys, pills this wide across the side */
	bool  power_pill;                  /* POWER a pill as the volume keys, or round */
	/* On POWER's side: the FN slider (its slot, its knob at the top; l 0
	 * none), and the reset hole (r 0 none), drawn and never lit */
	float fn_z, fn_l, fn_w, reset_z, reset_r;
	float bar_w;                       /* the light bar along the top edge (0 none) */
	/* The shoulders: blocks from just behind the thin part to the back,
	 * from under the ledge (where the back shows their faces) up to their
	 * tops; a press takes a top this far down */
	float key_low, key_press;
	/* How far the shoulders stand out past the back (0: flush) */
	float key_out;
	/* On the back: a panel on the thin part (w, h about z; w 0 none), and
	 * ribs across the thick part's back from z rib_z down, rib_n of them
	 * rib_gap apart, rib_w wide */
	float panel_w, panel_h, panel_z, rib_z, rib_gap, rib_w;
	int   rib_n;
	/* The USB-C housing between L2 and R2, a little higher than the ledge,
	 * and the port in its back (w 0 none) */
	float usb_w, usb_rise, port_w, port_h;
} slab;

static const slab SLAB_BRICK = {
	.w = 73.2f, .top = 109.9f / 2.0f, .rt = 0.5f, .rb = 4.1f,
	.front = -9.95f, .thin = -9.95f + 11.8f, .back = -9.95f + 19.9f, .ledge = 12.05f,
	/* The glass x 0.9 to 72.2, y 0.8 to 57.2 from the front's top left */
	.glass_w = 71.3f, .glass_h = 56.4f, .glass_x = -0.05f, .glass_z = 25.95f, .glass_r = 0.6f,
	.screen_w = 65.0f, .screen_h = 48.8f,
	.arm = 3.5f, .pill_h = 2.6f, .icon = 1.6f,
	/* 7.4 wide, either side of the bottom keys */
	.grille_x = { -25.5f, 25.3f }, .grille_z = -44.65f, .dot_r = 0.4f, .grille_n = 6,
	.vol_w = 2.8f,
	.fn_z = 31.65f, .fn_l = 10.3f, .fn_w = 3.0f,
	.bar_w = 37.9f,
	.key_low = 7.95f, .key_press = 1.2f,
	.usb_w = 13.0f, .usb_rise = 0.6f, .port_w = 7.0f, .port_h = 2.2f,
};

/* The R36S's grille, as the maker's straight-on photo has it: three rows
 * 3.85 apart, the top one level with X, three slots a row in columns 5.63
 * apart, long (5.4) and short (2.25): long, short, long; short, long,
 * short; long, short, long */
static const float R36S_SLOTS[9][3] = {
	{ -5.63f, -5.6f, 5.4f },   { 0.0f, -5.6f, 2.25f },   { 5.63f, -5.6f, 5.4f },
	{ -5.63f, -9.45f, 2.25f }, { 0.0f, -9.45f, 5.4f },   { 5.63f, -9.45f, 2.25f },
	{ -5.63f, -13.3f, 5.4f },  { 0.0f, -13.3f, 2.25f },  { 5.63f, -13.3f, 5.4f },
};

/* Measured on photos of a real one (src/device/r36s_model.c) */
static const slab SLAB_R36S = {
	.w = 80.0f, .top = 119.0f / 2.0f, .rt = 3.0f, .rb = 6.0f,
	.front = -15.0f, .thin = -15.0f + 18.5f, .back = 15.0f, .ledge = 9.5f,
	.glass_w = 77.0f, .glass_h = 55.6f, .glass_x = 0.0f, .glass_z = 31.2f, .glass_r = 2.0f,
	.screen_w = 70.0f, .screen_h = 52.5f,
	.arm = 3.75f,
	.slots = R36S_SLOTS, .n_slots = 9, .slot_h = 1.5f,
	.mic_z = -30.4f, .mic_r = .45f,
	/* Just under the glass (31.2 less half its 55.6) */
	.seam_z = 1.8f,
	.stick_x = 26.3f, .stick_z = -38.6f, .stick_well = 11.0f, .stick_cap = 7.5f, .stick_ring = 2.4f,
	.vol_w = 3.0f, .power_pill = true,
	.reset_z = 29.5f, .reset_r = 1.2f,
	.key_low = 5.4f, .key_press = 1.2f, .key_out = 3.0f,
	.panel_w = 60.0f, .panel_h = 28.0f, .panel_z = 41.0f,
	.rib_z = -6.0f, .rib_gap = 3.0f, .rib_w = 64.0f, .rib_n = 15,
};

/* The slab being drawn (slab_draw) */
static const slab *g_slab = &SLAB_BRICK;

#define BK_W        (g_slab->w)
#define BK_TOP      (g_slab->top)
#define BK_RT       (g_slab->rt)
#define BK_RB       (g_slab->rb)
#define BK_FRONT    (g_slab->front)
#define BK_THIN     (g_slab->thin)
#define BK_BACK     (g_slab->back)
#define BK_LEDGE    (g_slab->ledge)
/* A side face's plane, a hair proud of the body so its keys sit on it */
#define BK_SIDE     (BK_W / 2.0f + 0.1f)
#define BK_KEY_FRONT (BK_THIN + 0.5f)
#define BK_KEY_LOW  (g_slab->key_low)
#define BK_KEY_PRESS (g_slab->key_press)
#define BK_USB_TOP  (BK_LEDGE + g_slab->usb_rise)

/* How thin `p` is on the screen, about: twice its area over its perimeter
 * (a long shape's width, a round one's radius) */
static float thinness(const SDL_FPoint *p, int n)
{
	float per = 0.0f;
	int i;

	for (i = 0; i < n; i++) {
		const float dx = p[(i + 1) % n].x - p[i].x, dy = p[(i + 1) % n].y - p[i].y;

		per += sqrtf(dx * dx + dy * dy);
	}
	return per > 0.0f ? fabsf(area2(p, n)) / per : 0.0f;
}

/* shape, for a face that turns edge-on: thinner than its edge, it is all
 * edge, a line, rather than an edge turned inside out */
static void face_shape(const view *v, SDL_FPoint *p, int n, SDL_Color f, SDL_Color e, float w)
{
	n = dedup(p, n);
	if (n < 3) return;
	if (w > 0.0f && e.a > 0 && thinness(p, n) < w) {
		fill(v->r, p, n, e);
		return;
	}
	shape(v, p, n, f, e, w);
}

/* The front's outline in (x, z), counterclockwise from the bottom right: the
 * bottom's corners BK_RB, up to `zt` with corners `rt` (0 square). Into
 * `xz`, its count. */
static int bk_outline(float zt, float rt, float *xz)
{
	int n = 0, q, i;

	for (q = 0; q < 4; q++) {
		const bool top = q == 1 || q == 2;
		const float r = top ? rt : BK_RB;
		const float cx = q < 2 ? BK_W / 2.0f - r : -BK_W / 2.0f + r;
		const float cz = top ? zt - r : -BK_TOP + r;
		const int steps = r > 1.0f ? 6 : r > 0.0f ? 2 : 0;

		for (i = 0; i <= steps; i++) {
			const float a = (float)M_PI / 2.0f * ((float)(q - 1) + (steps ? (float)i / steps : 0.0f));

			xz[n * 2] = cx + cosf(a) * r;
			xz[n * 2 + 1] = cz + sinf(a) * r;
			n++;
		}
	}
	return n;
}

/* A flat quad of four model points, drawn as a face */
static void face_quad(const view *v, const float q[4][3], SDL_Color f, SDL_Color e)
{
	SDL_FPoint p[4];
	int i;

	for (i = 0; i < 4; i++) p[i] = at(v, q[i][0], q[i][1], q[i][2]);
	face_shape(v, p, 4, f, e, DS_FRAME);
}

/* The color of a top face, between the sides' and the ends' */
static unsigned top_col(const view *v)
{
	return ui_mix(v->side, v->body, 0.5f);
}

/* A block of the body: outline `xz` (`n` points) from depth `ya` to `yb`.
 * Its outline on the screen in **side**, edged; its flat top (at `zt`, x
 * within `half`) where the tilt shows it; and the end of it facing the
 * viewer in **body**, the front where `front` says it can show, the back
 * where `back` does. */
static void block(const view *v, const float *xz, int n, float ya, float yb,
                     float zt, float half, bool front, bool back)
{
	SDL_FPoint p[2 * MAXP], pt[MAXP];
	int i, m;

	for (i = 0; i < n; i++) {
		p[2 * i] = at(v, xz[2 * i], ya, xz[2 * i + 1]);
		p[2 * i + 1] = at(v, xz[2 * i], yb, xz[2 * i + 1]);
	}
	m = hull(p, 2 * n, pt);
	shape(v, pt, m, col(v, v->side, 1.0f), col(v, v->line, 1.0f), DS_FRAME);
	if (v->sq > 0.0f) {
		const float q[4][3] = { { -half, ya, zt }, { half, ya, zt }, { half, yb, zt }, { -half, yb, zt } };

		face_quad(v, q, col(v, top_col(v), 1.0f), col(v, v->line, 1.0f));
	}
	if ((v->cp > 0.0f && front) || (v->cp < 0.0f && back)) {
		const float y = v->cp > 0.0f ? ya : yb;

		for (i = 0; i < n; i++) pt[i] = at(v, xz[2 * i], y, xz[2 * i + 1]);
		face_shape(v, pt, n, col(v, v->body, 1.0f), col(v, v->line, 1.0f), DS_FRAME);
	}
}

/* How much of a lit key's halo and ring show, by how far its face turns to
 * the viewer (`facing`, the cosine): all of it from a quarter on */
static float shows(float facing)
{
	return clampf(facing / 0.25f, 0.0f, 1.0f);
}

/* A lit key's spot about the screen points `p` reach, `vis` of it showing */
static spot spot_of(const view *v, int k, const SDL_FPoint *p, int n, float vis)
{
	float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
	int i;

	for (i = 0; i < n; i++) {
		x0 = fminf(x0, p[i].x); x1 = fmaxf(x1, p[i].x);
		y0 = fminf(y0, p[i].y); y1 = fmaxf(y1, p[i].y);
	}
	return (spot){ v->on[k] * vis, v->press[k], (x0 + x1) / 2.0f, (y0 + y1) / 2.0f,
	               (x1 - x0) / 2.0f, (y1 - y0) / 2.0f, true, false };
}

/* A round key on a front face at depth `y`, at its place, `press` of the
 * way to 92% */
static int round_pts(const view *v, int k, float press, float y, SDL_FPoint *p)
{
	const td_part *pa = &v->part[k];

	return disc(v, 0, pa->x, y, pa->z, pa->r * (1.0f - 0.08f * press), p);
}

/* F1 or F2, a pill on the front, `press` of the way to 92% */
static int bk_pill_pts(const view *v, int k, float press, SDL_FPoint *p)
{
	const td_part *pa = &v->part[k];
	const float q = 1.0f - 0.08f * press;
	float ab[MAXP * 2];
	const float h = g_slab->pill_h;
	const int n = rrect(2.0f * pa->r * q, h * q, h * q / 2.0f, 0.0f, 4, ab);
	int i;

	for (i = 0; i < n; i++) p[i] = at(v, pa->x + ab[i * 2], BK_FRONT, pa->z + ab[i * 2 + 1]);
	return n;
}

/* The d-pad's cross on a front face at depth `y`, its arms `a` half wide
 * out to its model's reach, as Pixel 2's dpad draws its own: a pressed arm
 * of the lit d-pad a step darker */
static void cross(const view *v, float y, float a)
{
	const td_part *pa = &v->part[TD_DPAD];
	const float s = pa->r;
	const float X[12][2] = { { -a, s }, { a, s }, { a, a }, { s, a }, { s, -a }, { a, -a },
	                         { a, -s }, { -a, -s }, { -a, -a }, { -s, -a }, { -s, a }, { -a, a } };
	const float ARM[4][4][2] = {
		{ { -a, a }, { a, a }, { a, s }, { -a, s } },
		{ { -a, -a }, { a, -a }, { a, -s }, { -a, -s } },
		{ { -a, a }, { -s, a }, { -s, -a }, { -a, -a } },
		{ { a, a }, { s, a }, { s, -a }, { a, -a } } };
	SDL_FPoint p[12];
	int i, j;

	for (i = 0; i < 12; i++) p[i] = at(v, pa->x + X[i][0], y, pa->z + X[i][1]);
	face_shape(v, p, 12, key_fill(v, TD_DPAD), key_edge(v, TD_DPAD), DS_FRAME);
	if (v->on[TD_DPAD] <= 0.0f) return;
	for (i = 0; i < 4; i++) {
		const float k = v->arm[i] * v->on[TD_DPAD];
		SDL_FPoint q[4], in[4];

		if (k <= 0.004f) continue;
		for (j = 0; j < 4; j++) q[j] = at(v, pa->x + ARM[i][j][0], y, pa->z + ARM[i][j][1]);
		offset(q, 4, -DS_FRAME / 2.0f, in);
		fill(v->r, in, 4, col(v, v->dark ? ui_mix(v->lit, v->ink, 0.45f) : v->lit_line, k));
	}
}

/* The mark on SELECT (a square) or START (a play triangle) on a front face
 * at depth `y`, `size` across, pressed with its key */
static void icon(const view *v, int k, float y, float size)
{
	const td_part *pa = &v->part[k];
	const float q = (1.0f - 0.08f * v->press[k]) * size / 2.0f;
	const SDL_Color c = col(v, ui_mix(v->dot, v->on_accent, v->on[k]), 1.0f);
	SDL_FPoint p[4];

	if (k == TD_SELECT) {
		p[0] = at(v, pa->x - q, y, pa->z - q);
		p[1] = at(v, pa->x + q, y, pa->z - q);
		p[2] = at(v, pa->x + q, y, pa->z + q);
		p[3] = at(v, pa->x - q, y, pa->z + q);
		fill(v->r, p, 4, c);
	} else {
		/* About its middle, a little right of its box's, so it looks centred */
		p[0] = at(v, pa->x - 0.8f * q, y, pa->z + 1.1f * q);
		p[1] = at(v, pa->x - 0.8f * q, y, pa->z - 1.1f * q);
		p[2] = at(v, pa->x + 1.2f * q, y, pa->z);
		fill(v->r, p, 3, c);
	}
}

/* The front's glass and the screen in it, and the speaker grilles */
static void bk_front_face(const view *v)
{
	SDL_FPoint pt[MAXP];
	int i, j, n;

	n = front_rr(v, g_slab->glass_w, g_slab->glass_h, g_slab->glass_r, g_slab->glass_x, BK_FRONT,
	             g_slab->glass_z, pt);
	face_shape(v, pt, n, col(v, v->glass, 1.0f), col(v, 0, 0.0f), 0.0f);
	n = front_rr(v, g_slab->screen_w, g_slab->screen_h, 0.3f, g_slab->glass_x, BK_FRONT, g_slab->glass_z, pt);
	face_shape(v, pt, n, col(v, v->screen, 1.0f), col(v, 0, 0.0f), 0.0f);
	for (i = 0; g_slab->grille_n > 1 && i < 2; i++)
		for (j = 0; j < 2 * g_slab->grille_n; j++) {
			const int gn = g_slab->grille_n;
			const float x = g_slab->grille_x[i] - 3.2f + 6.4f * (float)(j % gn) / (float)(gn - 1);
			const float z = g_slab->grille_z + (j < gn ? 0.65f : -0.65f);

			n = disc(v, 0, x, BK_FRONT, z, g_slab->dot_r, pt);
			fill(v->r, pt, n, col(v, v->mark, 1.0f));
		}
	for (i = 0; i < g_slab->n_slots; i++) {
		float ab[MAXP * 2];
		const float x = g_slab->slots[i][0], z = g_slab->slots[i][1];
		const float h = g_slab->slot_h;

		n = rrect(g_slab->slots[i][2], h, h / 2.0f, 0.0f, 4, ab);
		for (j = 0; j < n; j++) pt[j] = at(v, x + ab[j * 2], BK_FRONT, z + ab[j * 2 + 1]);
		face_shape(v, pt, n, col(v, v->mark, 1.0f), col(v, 0, 0.0f), 0.0f);
	}
	if (g_slab->mic_r > 0.0f) {
		n = disc(v, 0, 0.0f, BK_FRONT, g_slab->mic_z, g_slab->mic_r, pt);
		fill(v->r, pt, n, col(v, v->mark, 1.0f));
	}
	if (g_slab->seam_z != 0.0f) {
		const float x = BK_W / 2.0f, z = g_slab->seam_z, h = .25f;
		const float q[4][3] = { { -x, BK_FRONT, z - h }, { x, BK_FRONT, z - h }, { x, BK_FRONT, z + h },
		                        { -x, BK_FRONT, z + h } };

		for (j = 0; j < 4; j++) pt[j] = at(v, q[j][0], q[j][1], q[j][2]);
		if (thinness(pt, 4) >= .4f) fill(v->r, pt, 4, col(v, v->mark, 1.0f));
	}
	/* The sticks: a well (a light ring standing out of the face, where the
	 * slab has one, round the cap's dark base). Their caps are keys where
	 * the model has them (L3, R3: bk_front_keys), else drawn here, never
	 * lit. */
	for (i = 0; g_slab->stick_well > 0.0f && i < 2; i++) {
		const float x = (i ? 1.0f : -1.0f) * g_slab->stick_x, z = g_slab->stick_z;

		n = disc(v, 0, x, BK_FRONT, z, g_slab->stick_well, pt);
		if (g_slab->stick_ring > 0.0f) {
			face_shape(v, pt, n, col(v, v->body, 1.0f), col(v, v->line, 1.0f), DS_FRAME);
			n = disc(v, 0, x, BK_FRONT, z, g_slab->stick_well - g_slab->stick_ring, pt);
		}
		face_shape(v, pt, n, col(v, v->pocket, 1.0f), col(v, v->line, 1.0f), DS_FRAME);
		if (v->part[i ? TD_R3 : TD_L3].r > 0.0f) continue;
		n = disc(v, 0, x, BK_FRONT, z, g_slab->stick_cap, pt);
		face_shape(v, pt, n, col(v, v->key, 1.0f), col(v, v->key_line, 1.0f), DS_FRAME);
	}
}

/* The round keys on the front: the sticks' caps too, where the model has
 * them (a key it has not is passed over, its radius 0) */
static const td_key BK_ROUND[] = { TD_A, TD_B, TD_X, TD_Y, TD_MENU, TD_SELECT, TD_START, TD_L3, TD_R3 };
#define BK_NROUND ((int)(sizeof BK_ROUND / sizeof BK_ROUND[0]))

/* Where the front's lit keys' halos and rings go, each about its key at
 * rest so its ring holds still as it presses */
static void bk_front_spots(const view *v, spot *spots, int *ns)
{
	const float vis = shows(v->cp);
	SDL_FPoint pt[2 * MAXP];
	int i, n;

	if (v->on[TD_DPAD] > 0.0f) {
		const td_part *pa = &v->part[TD_DPAD];

		n = disc(v, 0, pa->x, BK_FRONT, pa->z, pa->r, pt);
		spots[*ns] = spot_of(v, TD_DPAD, pt, n, vis);
		spots[(*ns)++].press = 0.0f;
	}
	for (i = 0; i < BK_NROUND; i++)
		if (v->on[BK_ROUND[i]] > 0.0f && v->part[BK_ROUND[i]].r > 0.0f) {
			n = round_pts(v, BK_ROUND[i], 0.0f, BK_FRONT, pt);
			spots[(*ns)++] = spot_of(v, BK_ROUND[i], pt, n, vis);
		}
	/* F1 and F2 lit together (the Brightness lesson): one ring round both,
	 * as VOL's goes round both volume keys */
	if (v->on[TD_F1] > 0.0f && v->on[TD_F2] > 0.0f) {
		n = bk_pill_pts(v, TD_F1, 0.0f, pt);
		n += bk_pill_pts(v, TD_F2, 0.0f, pt + n);
		spots[*ns] = spot_of(v, TD_F1, pt, n, vis);
		spots[*ns].on = fmaxf(v->on[TD_F1], v->on[TD_F2]) * vis;
		spots[(*ns)++].press = fmaxf(v->press[TD_F1], v->press[TD_F2]);
		return;
	}
	for (i = TD_F1; i <= TD_F2; i++)
		if (v->on[i] > 0.0f) {
			n = bk_pill_pts(v, i, 0.0f, pt);
			spots[(*ns)++] = spot_of(v, i, pt, n, vis);
		}
}

/* The front's keys */
static void bk_front_keys(const view *v)
{
	SDL_FPoint pt[MAXP];
	int i, n;

	cross(v, BK_FRONT, g_slab->arm);
	for (i = 0; i < BK_NROUND; i++) {
		if (v->part[BK_ROUND[i]].r <= 0.0f) continue;
		n = round_pts(v, BK_ROUND[i], v->press[BK_ROUND[i]], BK_FRONT, pt);
		face_shape(v, pt, n, key_fill(v, BK_ROUND[i]), key_edge(v, BK_ROUND[i]), DS_FRAME);
	}
	if (g_slab->icon > 0.0f) {
		icon(v, TD_SELECT, BK_FRONT, g_slab->icon);
		icon(v, TD_START, BK_FRONT, g_slab->icon);
	}
	for (i = TD_F1; i <= TD_F2; i++) {
		if (v->part[i].r <= 0.0f) continue;
		n = bk_pill_pts(v, i, v->press[i], pt);
		face_shape(v, pt, n, key_fill(v, i), key_edge(v, i), DS_FRAME);
	}
}

/* The halos of spots `from` to `to`, under the keys they are about */
static void halos(const view *v, const spot *spots, int from, int to)
{
	int i;

	for (i = from; i < to; i++)
		halo(v, spots[i].on, spots[i].press, spots[i].cx, spots[i].cy, spots[i].rx, spots[i].ry);
}

/* A rounded rect on a side face (`side` -1 left, 1 right), at (y, z) on it,
 * `w` across and `h` along */
static int bk_side_rr(const view *v, int side, float w, float h, float y, float z, SDL_FPoint *p)
{
	float ab[MAXP * 2];
	const int n = rrect(w, h, fminf(w, h) / 2.0f, 0.0f, 4, ab);
	int i;

	for (i = 0; i < n; i++) p[i] = at(v, side * BK_SIDE, y + ab[i * 2], z + ab[i * 2 + 1]);
	return n;
}

/* Which side face a key is on: -1 the left, 1 the right, 0 neither */
static int bk_side_of(const view *v, int k)
{
	return v->part[k].face == TD_FACE_LEFT ? -1 : v->part[k].face == TD_FACE_RIGHT ? 1 : 0;
}

/* A volume key's + or -, on its side face, in MENU's dots' colors */
static void bk_vol_sign(const view *v, int k, float q)
{
	const td_part *pa = &v->part[k];
	const SDL_Color c = col(v, ui_mix(v->dot, v->on_accent, v->on[k]), 1.0f);
	const float x = (float)bk_side_of(v, k) * BK_SIDE;
	SDL_FPoint p[MAXP];
	float ab[MAXP * 2];
	int bar, n, i;

	for (bar = 0; bar < (k == TD_VOLUP ? 2 : 1); bar++) {
		n = rrect(bar ? SIGN_TH * q : SIGN_L * q, bar ? SIGN_L * q : SIGN_TH * q, SIGN_TH * q / 2.0f, 0.0f, 2, ab);
		for (i = 0; i < n; i++) p[i] = at(v, x, pa->x + ab[i * 2], pa->z + ab[i * 2 + 1]);
		fill(v->r, p, n, c);
	}
}

/* Whether a side shows its keys: turned far enough that they are more than
 * a line */
static bool bk_side_shows(const view *v)
{
	return fabsf(v->sp) > 0.05f;
}

/* POWER at `q` of its size, on its side: round (the Brick's) or a pill as
 * the volume keys are (the R36S's) */
static int bk_power_pts(const view *v, float q, SDL_FPoint *p)
{
	const td_part *pw = &v->part[TD_POWER];
	const int side = bk_side_of(v, TD_POWER);

	if (g_slab->power_pill) return bk_side_rr(v, side, g_slab->vol_w * q, 2.0f * pw->r * q, pw->x, pw->z, p);
	return disc(v, side, (float)side * BK_SIDE, pw->x, pw->z, pw->r * q, p);
}

/* Where the lit keys of the side that shows have their halos and rings: the
 * volume keys, one ring round both when both are lit (the Volume lesson),
 * and POWER, on whichever sides the model has them (the Brick's volume on
 * the left, the R36S's on the right) */
static void bk_side_spots(const view *v, spot *spots, int *ns)
{
	const float vis = shows(fabsf(v->sp));
	const int side = v->sp < 0.0f ? -1 : 1;
	SDL_FPoint pt[MAXP];
	int n, i;

	if (!bk_side_shows(v)) return;
	if (bk_side_of(v, TD_VOLUP) == side) {
		const td_part *up = &v->part[TD_VOLUP], *dn = &v->part[TD_VOLDN];

		if (v->on[TD_VOLUP] > 0.0f && v->on[TD_VOLDN] > 0.0f) {
			const float z0 = dn->z - dn->r, z1 = up->z + up->r;

			n = bk_side_rr(v, side, g_slab->vol_w, z1 - z0, up->x, (z0 + z1) / 2.0f, pt);
			spots[*ns] = spot_of(v, TD_VOLUP, pt, n, vis);
			spots[*ns].on = fmaxf(v->on[TD_VOLUP], v->on[TD_VOLDN]) * vis;
			spots[(*ns)++].press = fmaxf(v->press[TD_VOLUP], v->press[TD_VOLDN]);
		} else {
			for (i = TD_VOLUP; i <= TD_VOLDN; i++)
				if (v->on[i] > 0.0f) {
					const td_part *pa = &v->part[i];

					n = bk_side_rr(v, side, g_slab->vol_w, 2.0f * pa->r, pa->x, pa->z, pt);
					spots[(*ns)++] = spot_of(v, i, pt, n, vis);
				}
		}
	}
	if (bk_side_of(v, TD_POWER) == side && v->on[TD_POWER] > 0.0f) {
		n = bk_power_pts(v, 1.0f, pt);
		spots[(*ns)++] = spot_of(v, TD_POWER, pt, n, vis);
	}
}

/* The side that shows, its keys on its own plane: the volume keys with
 * their + and -; POWER, and what is drawn by it and never lit, the Brick's
 * FN slider above it, the R36S's reset hole under it */
static void bk_side_keys(const view *v)
{
	const int side = v->sp < 0.0f ? -1 : 1;
	SDL_FPoint pt[MAXP];
	int n, i;

	if (!bk_side_shows(v)) return;
	if (bk_side_of(v, TD_VOLUP) == side)
		for (i = TD_VOLUP; i <= TD_VOLDN; i++) {
			const td_part *pa = &v->part[i];
			const float q = 1.0f - 0.08f * v->press[i];

			n = bk_side_rr(v, side, g_slab->vol_w * q, 2.0f * pa->r * q, pa->x, pa->z, pt);
			face_shape(v, pt, n, key_fill(v, i), key_edge(v, i), DS_FRAME);
			if (thinness(pt, n) >= DS_FRAME) bk_vol_sign(v, i, q);
		}
	if (bk_side_of(v, TD_POWER) == side) {
		const td_part *pw = &v->part[TD_POWER];
		const float l = g_slab->fn_l, w = g_slab->fn_w;

		if (l > 0.0f) {
			/* The FN slider: its slot, and its knob at the top of it */
			n = bk_side_rr(v, side, w, l, pw->x, g_slab->fn_z, pt);
			face_shape(v, pt, n, col(v, v->glass, 1.0f), col(v, 0, 0.0f), 0.0f);
			n = bk_side_rr(v, side, w - 0.8f, l / 2.0f, pw->x, g_slab->fn_z + l / 4.0f - 0.4f, pt);
			face_shape(v, pt, n, col(v, v->key, 1.0f), col(v, v->key_line, 1.0f), DS_FRAME);
		}
		if (g_slab->reset_r > 0.0f) {
			n = disc(v, side, (float)side * BK_SIDE, pw->x, g_slab->reset_z, g_slab->reset_r, pt);
			face_shape(v, pt, n, col(v, v->glass, 1.0f), col(v, 0, 0.0f), 0.0f);
		}
		n = bk_power_pts(v, 1.0f - 0.08f * v->press[TD_POWER], pt);
		face_shape(v, pt, n, key_fill(v, TD_POWER), key_edge(v, TD_POWER), DS_FRAME);
	}
}

/* Screws in a back, at (x, z) and (-x, z) for each of `n` heights `zs`,
 * marks as the light bar is, so the back reads as a back */
static void bk_screws(const view *v, float y, float x, const float *zs, int n)
{
	SDL_FPoint p[MAXP];
	int i, side, m;

	for (i = 0; i < n; i++)
		for (side = -1; side <= 1; side += 2) {
			m = disc(v, 0, side * x, y, zs[i], 0.9f, p);
			if (thinness(p, m) >= 0.8f) fill(v->r, p, m, col(v, v->mark, 1.0f));
		}
}

/* The back's own marks, where the slab has them: the panel on the thin
 * part's back (`thin`), or the ribs across the thick part's */
static void bk_back_marks(const view *v, bool thin)
{
	SDL_FPoint p[MAXP];
	int i, j, n;

	if (thin && g_slab->panel_w > 0.0f) {
		float ab[MAXP * 2];

		n = rrect(g_slab->panel_w, g_slab->panel_h, 2.0f, 0.0f, 4, ab);
		for (j = 0; j < n; j++) p[j] = at(v, ab[j * 2], BK_THIN, g_slab->panel_z + ab[j * 2 + 1]);
		face_shape(v, p, n, col(v, v->body, 1.0f), col(v, v->line, 1.0f), DS_FRAME);
	}
	for (i = 0; !thin && i < g_slab->rib_n; i++) {
		const float x = g_slab->rib_w / 2.0f, z = g_slab->rib_z - g_slab->rib_gap * (float)i, h = .45f;
		const float q[4][3] = { { -x, BK_BACK, z - h }, { x, BK_BACK, z - h }, { x, BK_BACK, z + h },
		                        { -x, BK_BACK, z + h } };

		for (j = 0; j < 4; j++) p[j] = at(v, q[j][0], q[j][1], q[j][2]);
		if (thinness(p, 4) >= .5f) fill(v->r, p, 4, col(v, v->mark, 1.0f));
	}
}

/* The light bar along the top edge at the back, a mark as the Pixel 2's
 * light lenses are: on the top where the tilt shows it, and over the back's
 * top edge where the back shows */
static void bk_bar(const view *v)
{
	const float x = g_slab->bar_w / 2.0f, y0 = BK_THIN - 3.4f, y1 = BK_THIN - 0.4f, z = BK_TOP - 1.0f;
	const float top[4][3] = { { -x, y0, BK_TOP }, { x, y0, BK_TOP }, { x, y1, BK_TOP }, { -x, y1, BK_TOP } };
	const float back[4][3] = { { -x, BK_THIN, z }, { x, BK_THIN, z }, { x, BK_THIN, BK_TOP }, { -x, BK_THIN, BK_TOP } };
	SDL_FPoint p[4];
	int i;

	if (x <= 0.0f) return;
	if (v->sq > 0.0f) {
		for (i = 0; i < 4; i++) p[i] = at(v, top[i][0], top[i][1], top[i][2]);
		if (thinness(p, 4) >= 1.0f) fill(v->r, p, 4, col(v, v->mark, 1.0f));
	}
	if (v->cp < 0.0f) {
		for (i = 0; i < 4; i++) p[i] = at(v, back[i][0], back[i][1], back[i][2]);
		if (thinness(p, 4) >= 1.0f) fill(v->r, p, 4, col(v, v->mark, 1.0f));
	}
}

/* A block on the ledge from x0 to x1 up to `top`: the faces of it that face
 * the viewer, each edged, in `f` and `e` (`tf` its top), where `draw` says
 * to draw them. Its front shows only over the ledge, and its ends below it
 * only at the body's sides: under the ledge it is in the thick part. Their
 * points into `pts` (4 a face, 16 at most), their count. */
static int bk_ledge_block(const view *v, float x0, float x1, float top, SDL_Color f, SDL_Color tf,
                          SDL_Color e, SDL_FPoint *pts, bool draw)
{
	const float y0 = BK_KEY_FRONT, y1 = BK_BACK + g_slab->key_out;
	const float lo0 = x0 <= -BK_W / 2.0f + 0.01f ? BK_KEY_LOW : BK_LEDGE;
	const float lo1 = x1 >= BK_W / 2.0f - 0.01f ? BK_KEY_LOW : BK_LEDGE;
	int n = 0, i;

	if (v->cp < 0.0f) {
		const float q[4][3] = { { x0, y1, BK_KEY_LOW }, { x1, y1, BK_KEY_LOW }, { x1, y1, top }, { x0, y1, top } };

		if (draw) face_quad(v, q, f, e);
		for (i = 0; i < 4; i++) pts[n++] = at(v, q[i][0], q[i][1], q[i][2]);
	} else if (v->cp > 0.0f && top > BK_LEDGE) {
		const float q[4][3] = { { x0, y0, BK_LEDGE }, { x1, y0, BK_LEDGE }, { x1, y0, top }, { x0, y0, top } };

		if (draw) face_quad(v, q, f, e);
		for (i = 0; i < 4; i++) pts[n++] = at(v, q[i][0], q[i][1], q[i][2]);
	}
	if (v->sp != 0.0f) {
		const float x = v->sp > 0.0f ? x1 : x0, lo = v->sp > 0.0f ? lo1 : lo0;

		if (top > lo) {
			const float q[4][3] = { { x, y0, lo }, { x, y1, lo }, { x, y1, top }, { x, y0, top } };

			if (draw) face_quad(v, q, f, e);
			for (i = 0; i < 4; i++) pts[n++] = at(v, q[i][0], q[i][1], q[i][2]);
		}
	}
	if (v->sq > 0.0f) {
		const float q[4][3] = { { x0, y0, top }, { x1, y0, top }, { x1, y1, top }, { x0, y1, top } };

		if (draw) face_quad(v, q, tf, e);
		for (i = 0; i < 4; i++) pts[n++] = at(v, q[i][0], q[i][1], q[i][2]);
	}
	return n;
}

/* A shoulder's ends, from its place in the model, inside the body's sides */
static void bk_shoulder_x(const view *v, int k, float *x0, float *x1)
{
	const td_part *pa = &v->part[k];

	*x0 = fmaxf(pa->x - pa->r, -BK_W / 2.0f);
	*x1 = fminf(pa->x + pa->r, BK_W / 2.0f);
}

/* The ledge's keys and the USB-C housing between them, the farther first
 * (the viewer's side last); the lit keys' halos first, over the ledge */
static void bk_ledge(const view *v, spot *spots, int *ns)
{
	/* Left to right: L1, L2, the housing (TD_KEYS), R2, R1 */
	static const int ORDER[5] = { TD_L1, TD_L2, TD_KEYS, TD_R2, TD_R1 };
	const float vis = shows(-v->cp);
	SDL_FPoint pt[24];
	int i, n, first = *ns;

	for (i = 0; i < 5; i++) {
		const int k = ORDER[i];
		float x0, x1;

		if (k == TD_KEYS || v->on[k] <= 0.0f) continue;
		bk_shoulder_x(v, k, &x0, &x1);
		/* About the key at rest, so the ring holds still as it presses */
		n = bk_ledge_block(v, x0, x1, v->part[k].z, key_fill(v, k), key_fill(v, k), key_edge(v, k), pt, false);
		if (n) spots[(*ns)++] = spot_of(v, k, pt, n, vis);
	}
	halos(v, spots, first, *ns);
	for (i = 0; i < 5; i++) {
		const int k = ORDER[v->sp < 0.0f ? 4 - i : i];

		if (k == TD_KEYS) {
			const SDL_Color line = col(v, v->line, 1.0f);
			const float uw = g_slab->usb_w;

			if (uw <= 0.0f) continue;
			bk_ledge_block(v, -uw / 2.0f, uw / 2.0f, BK_USB_TOP, col(v, v->body, 1.0f),
			               col(v, top_col(v), 1.0f), line, pt, true);
			if (v->cp < 0.0f) {
				float ab[MAXP * 2];
				SDL_FPoint p[MAXP];
				const int m = rrect(g_slab->port_w, g_slab->port_h, g_slab->port_h / 2.0f, 0.0f, 4, ab);
				int j;

				for (j = 0; j < m; j++)
					p[j] = at(v, ab[j * 2], BK_BACK, (BK_KEY_LOW + BK_LEDGE) / 2.0f + ab[j * 2 + 1]);
				face_shape(v, p, m, col(v, v->glass, 1.0f), col(v, 0, 0.0f), 0.0f);
			}
		} else {
			float x0, x1;

			bk_shoulder_x(v, k, &x0, &x1);
			bk_ledge_block(v, x0, x1, v->part[k].z - BK_KEY_PRESS * v->press[k],
			               key_fill(v, k), key_fill(v, k), key_edge(v, k), pt, true);
		}
	}
}

/* A slab handheld as `v` has it, in the measures of `sl`, into v->r: the
 * farther block first, each one's keys after it, the rings over everything */
static void slab_draw(view *v, const slab *sl)
{
	float full[MAXP], low[MAXP];
	spot spots[TD_KEYS + 2];
	int nf, nl, ns = 0, first, i;

	g_slab = sl;
	nf = bk_outline(BK_TOP, BK_RT, full);
	nl = bk_outline(BK_LEDGE, 0.0f, low);

	if (v->cp >= 0.0f) {
		/* From the front: the thick part's back and the ledge behind the
		 * thin part, which covers them but at the sides; then the thin
		 * part, its front's and its side's lit keys' halos, their keys */
		block(v, low, nl, BK_THIN, BK_BACK, BK_LEDGE, BK_W / 2.0f, false, true);
		bk_ledge(v, spots, &ns);
		first = ns;
		block(v, full, nf, BK_FRONT, BK_THIN, BK_TOP, BK_W / 2.0f - BK_RT, true, true);
		bk_bar(v);
		if (v->cp > 0.0f) {
			bk_front_face(v);
			bk_front_spots(v, spots, &ns);
		}
		bk_side_spots(v, spots, &ns);
		halos(v, spots, first, ns);
		if (v->cp > 0.0f) bk_front_keys(v);
		bk_side_keys(v);
	} else {
		/* From the back: the thin part, its side's keys, then the thick
		 * part in front of it, its ledge and the shoulders on it */
		const float THIN_Z[1] = { BK_TOP - 4.5f }, THICK_Z[2] = { 3.0f, -BK_TOP + 4.5f };

		block(v, full, nf, BK_FRONT, BK_THIN, BK_TOP, BK_W / 2.0f - BK_RT, true, true);
		bk_bar(v);
		bk_screws(v, BK_THIN, BK_W / 2.0f - 4.5f, THIN_Z, 1);
		bk_back_marks(v, true);
		bk_side_spots(v, spots, &ns);
		halos(v, spots, 0, ns);
		bk_side_keys(v);
		block(v, low, nl, BK_THIN, BK_BACK, BK_LEDGE, BK_W / 2.0f, false, true);
		bk_screws(v, BK_BACK, BK_W / 2.0f - 4.5f, THICK_Z, 2);
		bk_back_marks(v, false);
		bk_ledge(v, spots, &ns);
	}
	for (i = 0; i < ns; i++)
		if (spots[i].ring) ring(v, spots[i].on, spots[i].cx, spots[i].cy, spots[i].rx, spots[i].ry);
}

/* ---- the TrimUI Smart Pro -------------------------------------------------------- */

/* The TrimUI Smart Pro's art (TD_ART_SMARTPRO), its keys where its model has
 * them (src/device/smartpro_model.c), in its millimetres: x right, z up, y
 * deep, from the body's middle. One block, 188 x 80 and 17 deep, its ends
 * rounded: on its front the glass and the picture, the d-pad, A B X Y, the
 * two sticks in their light rings, MENU, SELECT and START, the speaker
 * grilles; on its top edge L and R toward the front with L2 and R2 behind
 * them, POWER, the USB-C port and the volume keys. The sticks are no keys
 * of the launcher's: drawn, never lit. Original art from the published
 * outline (188 x 80 x 17) and the places in TrimUI's manual drawing; no
 * maker's mark (MENU and the d-pad's middle are plain). */
#define SP_W        188.0f
#define SP_TOP      40.0f
#define SP_R        27.0f               /* the ends' corners */
#define SP_FRONT    -8.5f
#define SP_BACK     8.5f
/* The glass and the lit picture in it */
#define SP_GLASS_W  122.1f
#define SP_GLASS_H  69.1f
#define SP_GLASS_Z  0.8f
#define SP_SCREEN_W 113.3f
#define SP_SCREEN_H 63.7f
/* The d-pad's arms, half as wide as they are (7.0); SELECT's and START's
 * marks */
#define SP_ARM      3.5f
#define SP_ICON     1.7f
/* The sticks: a light ring (the mood light), the well in it, the cap */
static const float SP_STICK_X[2] = { -73.0f, 72.9f };
#define SP_STICK_Z  -12.2f
#define SP_RING_OUT 9.6f
#define SP_RING_IN  8.0f
#define SP_CAP      4.8f
/* The speaker grilles, two rows of six dots, and the light between them */
static const float SP_GRILLE_X[2] = { -56.2f, 56.4f };
#define SP_GRILLE_Z -35.8f
#define SP_GRILLE_N 6
#define SP_DOT_R    0.35f
/* The keys on the top edge, as bands across its depth: L and R toward the
 * front, L2 and R2 behind them, POWER, the port and the volume keys in the
 * middle; a press takes a top this far down */
#define SP_ROW_Y0   -5.4f
#define SP_ROW_Y1   0.1f
#define SP_REAR_Y0  2.7f
#define SP_REAR_Y1  SP_BACK
#define SP_MID_Y0   -3.8f
#define SP_MID_Y1   1.0f
#define SP_KEY_PRESS 0.6f
#define SP_PORT_W   8.8f
#define SP_PORT_H   2.6f

/* The front's outline in (x, z), counterclockwise from the bottom right, all
 * four corners SP_R. Into `xz`, its count. */
static int sp_outline(float *xz)
{
	int n = 0, q, i;

	for (q = 0; q < 4; q++) {
		const bool top = q == 1 || q == 2;
		const float cx = q < 2 ? SP_W / 2.0f - SP_R : -SP_W / 2.0f + SP_R;
		const float cz = top ? SP_TOP - SP_R : -SP_TOP + SP_R;

		for (i = 0; i <= 8; i++) {
			const float a = (float)M_PI / 2.0f * ((float)(q - 1) + (float)i / 8.0f);

			xz[n * 2] = cx + cosf(a) * SP_R;
			xz[n * 2 + 1] = cz + sinf(a) * SP_R;
			n++;
		}
	}
	return n;
}

/* The glass, the picture, the speaker grilles and the light between them */
static void sp_front_face(const view *v)
{
	SDL_FPoint pt[MAXP];
	int i, j, n;

	n = front_rr(v, SP_GLASS_W, SP_GLASS_H, 1.0f, 0.0f, SP_FRONT, SP_GLASS_Z, pt);
	face_shape(v, pt, n, col(v, v->glass, 1.0f), col(v, 0, 0.0f), 0.0f);
	n = front_rr(v, SP_SCREEN_W, SP_SCREEN_H, 0.4f, 0.0f, SP_FRONT, SP_GLASS_Z, pt);
	face_shape(v, pt, n, col(v, v->screen, 1.0f), col(v, 0, 0.0f), 0.0f);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2 * SP_GRILLE_N; j++) {
			const float x = SP_GRILLE_X[i] - 3.6f + 7.2f * (float)(j % SP_GRILLE_N) / (SP_GRILLE_N - 1);
			const float z = SP_GRILLE_Z + (j < SP_GRILLE_N ? 0.6f : -0.6f);

			n = disc(v, 0, x, SP_FRONT, z, SP_DOT_R, pt);
			fill(v->r, pt, n, col(v, v->mark, 1.0f));
		}
	n = disc(v, 0, 0.0f, SP_FRONT, SP_GRILLE_Z, 0.8f, pt);
	fill(v->r, pt, n, col(v, v->mark, 1.0f));
}

/* A stick: its light ring, the well, the cap; or, with `cap_only`, the cap
 * alone, over the keys */
static void sp_stick(const view *v, float x, bool cap_only)
{
	SDL_FPoint pt[MAXP];
	int n;

	if (!cap_only) {
		n = disc(v, 0, x, SP_FRONT, SP_STICK_Z, SP_RING_OUT, pt);
		face_shape(v, pt, n, col(v, v->mark, 1.0f), col(v, v->line, 1.0f), DS_FRAME);
		n = disc(v, 0, x, SP_FRONT, SP_STICK_Z, SP_RING_IN, pt);
		face_shape(v, pt, n, col(v, v->pocket, 1.0f), col(v, v->line, 1.0f), DS_FRAME);
		return;
	}
	n = disc(v, 0, x, SP_FRONT - 1.0f, SP_STICK_Z, SP_CAP, pt);
	face_shape(v, pt, n, col(v, v->key, 1.0f), col(v, v->key_line, 1.0f), DS_FRAME);
}

/* The round keys on the front */
static const td_key SP_ROUND[] = { TD_A, TD_B, TD_X, TD_Y, TD_MENU, TD_SELECT, TD_START };
#define SP_NROUND ((int)(sizeof SP_ROUND / sizeof SP_ROUND[0]))

/* Where the front's lit keys' halos and rings go, about each key at rest */
static void sp_front_spots(const view *v, spot *spots, int *ns)
{
	const float vis = shows(v->cp);
	SDL_FPoint pt[MAXP];
	int i, n;

	if (v->on[TD_DPAD] > 0.0f) {
		const td_part *pa = &v->part[TD_DPAD];

		n = disc(v, 0, pa->x, SP_FRONT, pa->z, pa->r, pt);
		spots[*ns] = spot_of(v, TD_DPAD, pt, n, vis);
		spots[(*ns)++].press = 0.0f;
	}
	for (i = 0; i < SP_NROUND; i++)
		if (v->on[SP_ROUND[i]] > 0.0f) {
			n = round_pts(v, SP_ROUND[i], 0.0f, SP_FRONT, pt);
			spots[(*ns)++] = spot_of(v, SP_ROUND[i], pt, n, vis);
		}
}

/* The front's keys, and the sticks' caps */
static void sp_front_keys(const view *v)
{
	SDL_FPoint pt[MAXP];
	int i, n;

	cross(v, SP_FRONT, SP_ARM);
	for (i = 0; i < SP_NROUND; i++) {
		n = round_pts(v, SP_ROUND[i], v->press[SP_ROUND[i]], SP_FRONT, pt);
		face_shape(v, pt, n, key_fill(v, SP_ROUND[i]), key_edge(v, SP_ROUND[i]), DS_FRAME);
	}
	icon(v, TD_SELECT, SP_FRONT, SP_ICON);
	icon(v, TD_START, SP_FRONT, SP_ICON);
	for (i = 0; i < 2; i++) sp_stick(v, SP_STICK_X[i], true);
}

/* A key's band across the top edge's depth (TD_KEYS: the USB-C port's) */
static void sp_band(int k, float *y0, float *y1)
{
	const bool rear = k == TD_L2 || k == TD_R2, row = k == TD_L1 || k == TD_R1;

	*y0 = rear ? SP_REAR_Y0 : row ? SP_ROW_Y0 : SP_MID_Y0;
	*y1 = rear ? SP_REAR_Y1 : row ? SP_ROW_Y1 : SP_MID_Y1;
}

/* The outline's top from x0 to x1, left to right: the flat top, and where
 * it goes past it the rounded corner down the side, as (x, z) with the way
 * out of the body at each (nx, nz). A shoulder wraps round its corner so.
 * Into the arrays, the count (SP_PATH at most). */
#define SP_PATH 24
static int sp_path(float x0, float x1, float *x, float *z, float *nx, float *nz)
{
	const float flat = SP_W / 2.0f - SP_R, cz = SP_TOP - SP_R;
	int n = 0;

	/* The points along it: the ends, and the corners' every 6 degrees, an
	 * angle on the left corner from 90 (its top) to 180 (the side), on the
	 * right from 90 to 0 */
	if (x0 < -flat) {
		const float a0 = (float)M_PI - acosf(clampf((-x0 - flat) / SP_R, 0.0f, 1.0f));
		float a;

		for (a = a0; a > (float)M_PI / 2.0f + 0.01f && n < SP_PATH - 4; a -= 6.0f * (float)M_PI / 180.0f) {
			x[n] = -flat + cosf(a) * SP_R;
			z[n] = cz + sinf(a) * SP_R;
			nx[n] = cosf(a);
			nz[n] = sinf(a);
			n++;
		}
	}
	x[n] = fmaxf(x0, -flat);
	z[n] = SP_TOP;
	nx[n] = 0.0f;
	nz[n] = 1.0f;
	n++;
	x[n] = fminf(x1, flat);
	z[n] = SP_TOP;
	nx[n] = 0.0f;
	nz[n] = 1.0f;
	n++;
	if (x1 > flat) {
		const float a1 = acosf(clampf((x1 - flat) / SP_R, 0.0f, 1.0f));
		float a;

		for (a = (float)M_PI / 2.0f - 6.0f * (float)M_PI / 180.0f; a > a1 && n < SP_PATH - 1;
		     a -= 6.0f * (float)M_PI / 180.0f) {
			x[n] = flat + cosf(a) * SP_R;
			z[n] = cz + sinf(a) * SP_R;
			nx[n] = cosf(a);
			nz[n] = sinf(a);
			n++;
		}
		x[n] = flat + cosf(a1) * SP_R;
		z[n] = cz + sinf(a1) * SP_R;
		nx[n] = cosf(a1);
		nz[n] = sinf(a1);
		n++;
	}
	return n;
}

/* A key on the top edge from x0 to x1, across y0 to y1, standing `rise`
 * out of the outline and round its corner: its outer face a quad a step of
 * the path, each drawn where it faces the viewer (on the screen, its corners
 * go round the right way), its two ends the same, and its front where the
 * viewer is in front. In `f` and `e`, where `draw` says to draw them; their
 * points into `pts`, their count. */
static int sp_top_key(const view *v, float x0, float x1, float y0, float y1, float rise,
                      SDL_Color f, SDL_Color e, SDL_FPoint *pts, int room, bool draw)
{
	float x[SP_PATH], z[SP_PATH], nx[SP_PATH], nz[SP_PATH];
	const int n = sp_path(x0, x1, x, z, nx, nz);
	SDL_FPoint q[4], face[2 * SP_PATH];
	int i, j, np = 0;

	if (rise <= 0.0f || n < 2) return 0;
	/* The outer face's steps that face the viewer, a run of them on a
	 * convex path: drawn as one shape, its near edge and its far edge */
	{
		int lo = -1, hi = -1, m = 0;

		for (i = 0; i + 1 < n; i++) {
			q[0] = at(v, x[i] + nx[i] * rise, y0, z[i] + nz[i] * rise);
			q[1] = at(v, x[i + 1] + nx[i + 1] * rise, y0, z[i + 1] + nz[i + 1] * rise);
			q[2] = at(v, x[i + 1] + nx[i + 1] * rise, y1, z[i + 1] + nz[i + 1] * rise);
			q[3] = at(v, x[i] + nx[i] * rise, y1, z[i] + nz[i] * rise);
			if (area2(q, 4) >= 0.0f) continue;
			if (lo < 0) lo = i;
			hi = i + 1;
		}
		if (lo >= 0) {
			for (i = lo; i <= hi; i++) face[m++] = at(v, x[i] + nx[i] * rise, y0, z[i] + nz[i] * rise);
			for (i = hi; i >= lo; i--) face[m++] = at(v, x[i] + nx[i] * rise, y1, z[i] + nz[i] * rise);
			if (draw && thinness(face, m) < DS_FRAME) fill_any(v->r, face, m, e);
			else if (draw) shape_any(v, face, m, f, e, DS_FRAME);
			for (j = 0; j < m && np < room; j++) pts[np++] = face[j];
		}
	}
	/* Its ends, from the outline out: the left end faces left, the right
	 * right */
	for (i = 0; i < 2; i++) {
		const int at_i = i ? n - 1 : 0;
		const float ox = x[at_i] + nx[at_i] * rise, oz = z[at_i] + nz[at_i] * rise;

		q[0] = at(v, x[at_i], y0, z[at_i]);
		q[1] = at(v, ox, y0, oz);
		q[2] = at(v, ox, y1, oz);
		q[3] = at(v, x[at_i], y1, z[at_i]);
		if ((area2(q, 4) < 0.0f) != (i == 0)) continue;
		if (draw) face_shape(v, q, 4, f, e, DS_FRAME);
		for (j = 0; j < 4 && np < room; j++) pts[np++] = q[j];
	}
	if (v->cp != 0.0f) {
		const float y = v->cp > 0.0f ? y0 : y1;

		for (i = 0; i < n; i++) face[i] = at(v, x[i] + nx[i] * rise, y, z[i] + nz[i] * rise);
		for (i = 0; i < n; i++) face[n + i] = at(v, x[n - 1 - i], y, z[n - 1 - i]);
		/* A band round the corner, concave: thinner than its edge, all edge */
		if (draw && thinness(face, 2 * n) < DS_FRAME) fill_any(v->r, face, 2 * n, e);
		else if (draw) shape_any(v, face, 2 * n, f, e, DS_FRAME);
		for (j = 0; j < 2 * n && np < room; j++) pts[np++] = face[j];
	}
	return np;
}

/* The top edge's keys, the rear row first, then the middle and the front
 * row; the lit ones' halos over the body first. And the USB-C port, where
 * the tilt shows the top. */
static void sp_top(const view *v, spot *spots, int *ns)
{
	static const int ORDER[9] = { TD_L2, TD_R2, TD_POWER, TD_VOLDN, TD_VOLUP, TD_KEYS, TD_L1, TD_R1, -1 };
	const float vis = v->sq > 0.0f ? 1.0f : 0.6f;
	SDL_FPoint pt[4 * SP_PATH + 8];
	const int room = (int)(sizeof pt / sizeof pt[0]);
	int i, n, first = *ns;

	for (i = 0; ORDER[i] >= 0; i++) {
		const int k = ORDER[i];
		const td_part *pa = &v->part[k];
		float y0, y1;

		if (k == TD_KEYS || v->on[k] <= 0.0f || pa->r <= 0.0f) continue;
		sp_band(k, &y0, &y1);
		/* About the key at rest, so the ring holds still as it presses */
		n = sp_top_key(v, pa->x - pa->r, pa->x + pa->r, y0, y1, pa->z - SP_TOP, key_fill(v, k),
		               key_edge(v, k), pt, room, false);
		if (n) spots[(*ns)++] = spot_of(v, k, pt, n, vis);
	}
	halos(v, spots, first, *ns);
	if (v->sq > 0.0f) {
		float ab[MAXP * 2];
		SDL_FPoint p[MAXP];
		const int m = rrect(SP_PORT_W, SP_PORT_H, SP_PORT_H / 2.0f, 0.0f, 4, ab);
		int j;

		for (j = 0; j < m; j++)
			p[j] = at(v, ab[j * 2], (SP_MID_Y0 + SP_MID_Y1) / 2.0f + ab[j * 2 + 1], SP_TOP);
		face_shape(v, p, m, col(v, v->glass, 1.0f), col(v, 0, 0.0f), 0.0f);
	}
	for (i = 0; ORDER[i] >= 0; i++) {
		const int k = ORDER[i];
		const td_part *pa = &v->part[k];
		float y0, y1;

		if (k == TD_KEYS || pa->r <= 0.0f) continue;
		sp_band(k, &y0, &y1);
		sp_top_key(v, pa->x - pa->r, pa->x + pa->r, y0, y1, pa->z - SP_TOP - SP_KEY_PRESS * v->press[k],
		           key_fill(v, k), key_edge(v, k), pt, room, true);
	}
}

/* The Smart Pro as `v` has it, into v->r: the body, its front, the lit
 * keys' halos, the keys, the top edge's keys, the rings over everything */
static void smartpro_draw(view *v)
{
	float out[MAXP];
	const int n = sp_outline(out);
	spot spots[TD_KEYS + 2];
	int ns = 0, i;

	block(v, out, n, SP_FRONT, SP_BACK, SP_TOP, SP_W / 2.0f - SP_R, true, true);
	if (v->cp > 0.0f) {
		sp_front_face(v);
		for (i = 0; i < 2; i++) sp_stick(v, SP_STICK_X[i], false);
		sp_front_spots(v, spots, &ns);
		halos(v, spots, 0, ns);
		sp_front_keys(v);
	}
	sp_top(v, spots, &ns);
	for (i = 0; i < ns; i++)
		if (spots[i].ring) ring(v, spots[i].on, spots[i].cx, spots[i].cy, spots[i].rx, spots[i].ry);
}

/* The handheld as `v` has it, into what `r` draws to: its model's art (the
 * Pixel 2's here, the slab handhelds' (the Brick and the R36S) in slab_draw,
 * the Smart Pro's in smartpro_draw), its keys where its model has them
 * (none without a model) */
static void draw_view(SDL_Renderer *r, view *vp)
{
	view v = *vp;
	const float facing = v.facing, tab_ring = v.tab_ring;
	const bool tabs = v.tabs;
	const td_part *menu = &v.part[TD_MENU], *power = &v.part[TD_POWER];
	const td_part *volup = &v.part[TD_VOLUP], *voldn = &v.part[TD_VOLDN];
	spot spots[TD_KEYS + 2];
	int nspots = 0, i, n;
	SDL_FPoint pt[MAXP], p2[2 * MAXP];
	float top[4];

	if (!v.m) return;
	v.r = r;
	if (v.m->art == TD_ART_BRICK || v.m->art == TD_ART_R36S) {
		slab_draw(&v, v.m->art == TD_ART_R36S ? &SLAB_R36S : &SLAB_BRICK);
		flush(r);
		return;
	}
	if (v.m->art == TD_ART_SMARTPRO) {
		smartpro_draw(&v);
		flush(r);
		return;
	}
	if (v.m->art != TD_ART_PIXEL2) return;

	/* The shoulders, level, behind the body: only their tops show over it. A
	 * lit one stands TAB_OUT_LIT out of the top edge, as MENU and VOL stand
	 * out of theirs for Brightness, and goes back toward it as it presses.
	 * Its halo first, so the body covers the halo's lower half and it glows
	 * from behind the edge; its ring goes over everything with the others.
	 * Tilted, they stand on the top face, over the body (shoulders_tilted). */
	{
		const bool level = v.sq == 0.0f;

		for (i = 0; i < 4; i++) {
			const int k = SHOULDER[i];
			const td_part *pa = &v.part[k];

			top[i] = pa->z + (BODY_TOP + TAB_OUT_LIT - 1.2f * v.press[k] - pa->z) * v.on[k];
			if (level && v.on[k] > 0.0f) {
				const SDL_FPoint c = at(&v, pa->x, SH_Y, (BODY_TOP + top[i]) / 2.0f);
				const spot s = { v.on[k], v.press[k], c.x, c.y,
				                 pa->r * v.s * v.cp,
				                 (top[i] - BODY_TOP) / 2.0f * v.s, true, true };

				halo(&v, s.on, s.press, s.cx, s.cy, s.rx, s.ry);
				spots[nspots++] = s;
			}
		}
		for (i = 0; i < 4 && level; i++)
			shoulder(&v, SHOULDER[i], v.part[SHOULDER[i]].x, 2.0f * v.part[SHOULDER[i]].r, top[i]);
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
		tab(&v, TD_MENU, 1, menu->z - menu->r, menu->z + menu->r, mn, &s);
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
		const td_part *pa = &v.part[TD_DPAD];
		const SDL_FPoint c = at(&v, pa->x, BODY_FRONT, pa->z);

		spots[nspots++] = (spot){ v.on[TD_DPAD], 0.0f, c.x, c.y, pa->r * v.s * v.cp, pa->r * v.s * v.cq, true, false };
	}
	for (i = TD_A; i <= TD_START; i++)
		if (v.on[i] > 0.0f) {
			const td_part *pa = &v.part[i];
			const SDL_FPoint c = at(&v, pa->x, BODY_FRONT, pa->z);

			spots[nspots++] = (spot){ v.on[i], v.press[i], c.x, c.y, pa->r * v.s * v.cp,
			                          pa->r * v.s * v.cq, true, false };
		}
	{
		const float ss = fabsf(v.sp);
		const int side = v.sp > 0.0f ? 1 : -1;

		if (ss > 0.05f && side > 0) {
			const SDL_FPoint m = at(&v, SIDE_X, menu->x, menu->z), pw = at(&v, SIDE_X, power->x, power->z);
			/* The capsule at 45 degrees, its bounds on the face */
			const float pr = power->r * 0.7071f + 1.15f * 0.7071f;

			if (v.on[TD_MENU] > 0.0f)
				spots[nspots++] = (spot){ v.on[TD_MENU] * (1.0f - tab_ring), v.press[TD_MENU], m.x, m.y,
				                          menu->r * v.s * ss, menu->r * v.s, true, false };
			if (v.on[TD_POWER] > 0.0f)
				spots[nspots++] = (spot){ v.on[TD_POWER], v.press[TD_POWER], pw.x, pw.y,
				                          pr * v.s * ss, pr * v.s, true, false };
		} else if (ss > 0.05f && (v.on[TD_VOLUP] > 0.0f || v.on[TD_VOLDN] > 0.0f)) {
			const SDL_FPoint c = at(&v, -SIDE_X, volup->x, ROCKER_Z);
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
			n = side_rr(&v, -1, 4.4f, 16.2f, 2.1f, 0.0f, volup->x, ROCKER_Z, pt);
			shape(&v, pt, n, col(&v, v.glass, 1.0f), col(&v, 0, 0.0f), 0.0f);
			n = side_rr(&v, -1, 3.5f, 15.7f, 1.7f, 0.0f, volup->x, ROCKER_Z, pt);
			shape(&v, pt, n, key_fill(&v, TD_VOLUP), key_edge(&v, TD_VOLUP), DS_FRAME);
			n = side_rr(&v, -1, 3.6f * qu, 2.0f * volup->r * qu, 1.75f, 0.0f, volup->x, volup->z, pt);
			shape(&v, pt, n, key_fill(&v, TD_VOLUP), key_edge(&v, TD_VOLUP), DS_FRAME);
			n = side_rr(&v, -1, 3.6f * qd, 2.0f * voldn->r * qd, 1.75f, 0.0f, voldn->x, voldn->z, pt);
			shape(&v, pt, n, key_fill(&v, TD_VOLDN), key_edge(&v, TD_VOLDN), DS_FRAME);
			vol_sign(&v, TD_VOLUP, true, volup->x, volup->z, SIGN_L * qu, SIGN_TH * qu);
			vol_sign(&v, TD_VOLDN, true, voldn->x, voldn->z, SIGN_L * qd, SIGN_TH * qd);
			n = side_rr(&v, -1, 1.2f, 11.7f, 0.3f, 0.0f, -1.89f, -23.4f, pt);
			shape(&v, pt, n, col(&v, v.glass, 1.0f), col(&v, 0, 0.0f), 0.0f);
		} else {
			const float q = 1.0f - 0.08f * v.press[TD_MENU], qp = 1.0f - 0.08f * v.press[TD_POWER];
			const SDL_FPoint m = at(&v, SIDE_X, menu->x, menu->z);
			/* The dots in on-accent on the lit key, as text on accent is, in
			 * both themes; at rest a soft text on the key (4:1 and more) */
			const SDL_Color dot = col(&v, ui_mix(v.dot, v.on_accent, v.on[TD_MENU]), 1.0f);
			int d;

			for (i = 0; i < 4; i++) {
				n = side_rr(&v, 1, 10.0f, 2.15f, 1.05f, -(float)M_PI / 4.0f, power->x, LED_Z[i], pt);
				shape(&v, pt, n, col(&v, v.mark, 1.0f), col(&v, 0, 0.0f), 0.0f);
			}
			n = side_rr(&v, 1, 2.0f * power->r * qp, 2.3f * qp, 1.1f, -(float)M_PI / 4.0f, power->x, power->z, pt);
			shape(&v, pt, n, key_fill(&v, TD_POWER), key_edge(&v, TD_POWER), DS_FRAME);
			n = v.sq == 0.0f ? oval(m.x, m.y, menu->r * q * v.s * ss, menu->r * q * v.s, pt)
			                 : disc(&v, 1, SIDE_X, menu->x, menu->z, menu->r * q, pt);
			shape(&v, pt, n, key_fill(&v, TD_MENU), key_edge(&v, TD_MENU), DS_FRAME);
			for (d = -1; d <= 1; d++) {
				const SDL_FPoint c = at(&v, SIDE_X, menu->x, menu->z + d * MENU_DOT_GAP * q);

				n = v.sq == 0.0f ? oval(c.x, c.y, MENU_DOT_R * q * v.s * ss, MENU_DOT_R * q * v.s, pt)
				                 : disc(&v, 1, SIDE_X, menu->x, menu->z + d * MENU_DOT_GAP * q, MENU_DOT_R * q, pt);
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
 * when its pose changes and copied the rest of the time. The Pixel 2's in
 * the tutorial is 304 x 382 (its model's keep_tour), the column's box: every vertex of the figure, a halo's faint
 * outer edge too, is in x 0 to 284 (on the screen), y 68 to 430 in every
 * pose and every frame of every turn, the top a lit shoulder's halo as the
 * handheld turns from Settings to Music and tilts back; 464 KB.
 * --handheld-box measures it (td_box_holds), and the Controls tab's. What a
 * texture holds is premultiplied - drawn with the blend into a clear texture
 * - so it is copied with the premultiplied blend. A renderer without that
 * blend draws the handheld every frame, as before; --shot's software
 * renderer has it, so a box too small shows in the shots too.
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

void td_bounds(const td_pose *p, SDL_FRect *out)
{
	view v;

	view_of(&v, p);
	*out = (SDL_FRect){ 0.0f, 0.0f, -1.0f, -1.0f };
	g_measure = out;
	draw_view(NULL, &v);
	g_measure = NULL;
}

bool td_box_holds(const char *what, const td_pose *p, SDL_Rect box, SDL_FRect *reach)
{
	SDL_FRect b;
	float x0, y0, x1, y1;

	td_bounds(p, &b);
	if (b.w < 0.0f) return true;
	/* Off the screen nothing shows, kept or not: the tutorial's box starts
	 * at its left edge, where Brightness's tabs reach past it */
	x0 = fmaxf(b.x, 0.0f);
	y0 = fmaxf(b.y, 0.0f);
	x1 = fminf(b.x + b.w, (float)ZYPEROS_SCREEN_W);
	y1 = fminf(b.y + b.h, (float)ZYPEROS_SCREEN_H);
	if (reach) {
		const float rx0 = x0 - (float)box.x, ry0 = y0 - (float)box.y;
		const float rx1 = x1 - (float)box.x, ry1 = y1 - (float)box.y;

		if (reach->w < 0.0f) {
			*reach = (SDL_FRect){ rx0, ry0, rx1 - rx0, ry1 - ry0 };
		} else {
			const float ex = fmaxf(reach->x + reach->w, rx1), ey = fmaxf(reach->y + reach->h, ry1);

			reach->x = fminf(reach->x, rx0);
			reach->y = fminf(reach->y, ry0);
			reach->w = ex - reach->x;
			reach->h = ey - reach->y;
		}
	}
	if (x0 >= (float)box.x && y0 >= (float)box.y &&
	    x1 <= (float)(box.x + box.w) && y1 <= (float)(box.y + box.h))
		return true;
	fprintf(stderr, "HANDHELD-BOX: %s, turned %.1f tilted %.1f: x %.1f to %.1f, y %.1f to %.1f, "
	        "out of the box x %d to %d, y %d to %d\n", what, p->deg, p->pitch, x0, x1, y0, y1,
	        box.x, box.x + box.w, box.y, box.y + box.h);
	return false;
}

void td_keep_free(td_keep *k)
{
	if (k->tex) SDL_DestroyTexture(k->tex);
	k->tex = NULL;
	k->can = 0;
	k->ok = false;
}

/* ---- the tutorial's ------------------------------------------------------------- */

static td_keep g_tour_keep;

/* The tutorial's kept texture, in `m`'s box for it */
static td_keep *tour_keep(const td_model *m)
{
	g_tour_keep.box = (SDL_Rect){ m->keep_tour.l + (ZYPEROS_SCREEN_W - 1024) / 2, m->keep_tour.t,
	                              m->keep_tour.w, m->keep_tour.h };
	return &g_tour_keep;
}

void tourdev_make(SDL_Renderer *r, const tour *t, uint32_t now, float op)
{
	td_pose p;
	view v;

	if (!tour_has(t, op)) return;
	tourdev_pose(t, now, op, &p);
	view_of(&v, &p);
	keep_make(r, tour_keep(t->model), &p, &v);
}

void tourdev_draw(SDL_Renderer *r, const tour *t, uint32_t now, float op)
{
	td_pose p;

	if (!tour_has(t, op)) return;
	tourdev_pose(t, now, op, &p);
	td_draw(r, tour_keep(t->model), &p);
}

void tourdev_free(void)
{
	td_keep_free(&g_tour_keep);
}

int tourdev_box_check(const td_model *m, int *measured)
{
	static const dev_bright BRIGHT[2] = { DEV_BRIGHT_MENU_VOL, DEV_BRIGHT_KEYS };
	SDL_FRect reach = { 0.0f, 0.0f, -1.0f, -1.0f };
	/* Where a spinning last lesson can be as the next comes on: still, half
	 * way round, on its back, half way back */
	static const int PHASE[4] = { 0, DS_MS_HOLD_SPIN + DS_MS_SPIN / 2,
	                              DS_MS_HOLD_SPIN + DS_MS_SPIN + DS_MS_HOLD_SPIN / 2,
	                              2 * DS_MS_HOLD_SPIN + DS_MS_SPIN + DS_MS_SPIN / 2 };
	SDL_Rect box;
	tour t;
	td_pose p;
	char what[96];
	int b, s, ph, ms, bad = 0;

	*measured = 0;
	if (!m) return 0;
	box = tour_keep(m)->box;
	/* With MENU+VOL for brightness, and with the device's own keys */
	for (b = 0; b < 2; b++)
		for (s = TOUR_FIRST_LESSON; s < TOUR_FIRST_LESSON + TOUR_LESSONS; s++) {
			memset(&t, 0, sizeof t);
			t.model = m;
			t.bright = BRIGHT[b];
			t.menu_release = BRIGHT[b] == DEV_BRIGHT_MENU_VOL;
			t.step = (tour_step)s;
			t.step_at = 5000;
			/* Every frame of the turn into it from the last lesson, from
			 * wherever a spinning last one was in its turns */
			for (ph = 0; ph < 4; ph++) {
				t.prev_at = 5000u - (uint32_t)(DS_MS_TURN + PHASE[ph]);
				for (ms = 0; ms <= DS_MS_TURN + 100; ms += 10) {
					tourdev_pose(&t, 5000u + (uint32_t)ms, 1.0f, &p);
					snprintf(what, sizeof what, "the tutorial's lesson %d at %d ms from phase %d%s",
					         s - TOUR_FIRST_LESSON + 1, ms, ph, b ? " (brightness keys)" : "");
					bad += !td_box_holds(what, &p, box, &reach);
					(*measured)++;
				}
			}
			/* ...and at rest, or a whole round of its spin */
			for (ms = DS_MS_TURN + 100; ms <= DS_MS_TURN + 2 * (DS_MS_HOLD_SPIN + DS_MS_SPIN); ms += 20) {
				tourdev_pose(&t, 5000u + (uint32_t)ms, 1.0f, &p);
				snprintf(what, sizeof what, "the tutorial's lesson %d at %d ms%s",
				         s - TOUR_FIRST_LESSON + 1, ms, b ? " (brightness keys)" : "");
				bad += !td_box_holds(what, &p, box, &reach);
				(*measured)++;
			}
		}
	fprintf(stderr, "handheld-box: the tutorial's reaches x %.1f to %.1f, y %.1f to %.1f of its %d x %d box\n",
	        reach.x, reach.x + reach.w, reach.y, reach.y + reach.h, box.w, box.h);
	return bad;
}
