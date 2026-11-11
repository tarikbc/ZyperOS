/* SPDX-License-Identifier: GPL-3.0-only */
/* The tutorial on the screen: every screen of DESIGN.md 11.10, drawn from
 * src/tour.h's state and the clock, the way the tutorial lab's tour.js draws
 * it - the same sizes, places and times, so the screens I picked are the
 * screens the handheld shows. See src/tourdraw.h.
 *
 * The stage's demonstrations are shapes and tokens - a little shelf of
 * system-colored cards, a stand-in game, a menu and Muse whose words are
 * bars - never the player's library, so the tutorial is the same on a card
 * with no games. Text is never under **meta** (DESIGN.md 13): the little
 * shelf's names and the menu's rows are drawn as bars for that reason.
 *
 * Everything is drawn in float from fans of triangles, where the shelf's
 * ui_round_rect is whole units: the little cards stand at 0.62 to 0.78 of
 * their size and the menu opens from 96%, and rounded every frame they would
 * wobble by a unit as they move. SDL clips to rectangles only, so the
 * stage's round corners are drawn again over what plays inside it. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_image.h>

#include "clamp.h"
#include "config.h"      /* CFG_NO_COLOR */
#include "design.h"
#include "i18n.h"
#include "osdbar.h"
#include "platform.h"    /* plat_draw_to_screen, the levels' ranges */
#include "theme.h"
#include "tourdevice.h"
#include "tourdraw.h"
#include "ui.h"

#define SCR_W 1024
#define SCR_H 768

/* The stage, 11.10: 680 x 340 at (172, 84). */
#define STAGE_X 172
#define STAGE_Y 84
#define STAGE_W 680
#define STAGE_H 340
/* The glyph on the stage's bottom edge, the lines under it. */
#define GLYPH_Y 424
#define TITLE_Y 516
#define LINE_Y  578
#define NOTE_Y  624

/* Zyper in a step: 120 high, his middle at (96, 684), so his hop (24 up)
 * clears the line above him; up from under the bottom edge for the reward
 * and a wrong press. */
#define CORNER_X 96
#define CORNER_Y 684
#define CORNER_H 120
#define CORNER_DROP 170
/* His bubble beside him, and the welcome's and the finish's. */
#define BUBBLE_X 168
#define SAY_X 452
/* The welcome's and the finish's words come in, in **move**, this long after
 * he starts to pop in - his bubble as he lands, the lines and A after it -
 * as the lab has them */
#define SAY_IN_MS   260
#define LINES_IN_MS 420

/* The little shelf's focused system, SNES, which the stand-in game and the
 * details panel are of (TOUR_LITTLE, src/tour.c). */
#define SNES (TOUR_LITTLE[TOUR_LITTLE_FOCUS].rgb)
/* A wrong press's glyph grows this much at the top of each pulse */
#define PULSE_GROW 0.06f
/* The gap the hint bubble keeps from "START: skip" */
#define LEGEND_GAP 16.0f

/* ---- the layer --------------------------------------------------------------
 * Where the next thing is drawn from and at what opacity: the stage's
 * corner while the stage is drawn, the screen's otherwise; a part fading in
 * draws everything in it at its share. */
static float g_op = 1.0f;
static float g_ox, g_oy;

static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static int since(uint32_t now, uint32_t at) { return (int)(int32_t)(now - at); }

static unsigned rgb(ds_rgb c) { return ui_rgb(c); }
static unsigned mix(unsigned a, unsigned b, float t) { return ui_mix(a, b, clampf(t, 0.0f, 1.0f)); }

static SDL_Color col(unsigned c, float a)
{
	return (SDL_Color){ (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c,
	                    (Uint8)(255.0f * clampf(a * g_op, 0.0f, 1.0f) + 0.5f) };
}

static SDL_Color col_lerp(SDL_Color a, SDL_Color b, float t)
{
	return (SDL_Color){ (Uint8)lerpf(a.r, b.r, t), (Uint8)lerpf(a.g, b.g, t),
	                    (Uint8)lerpf(a.b, b.b, t), (Uint8)lerpf(a.a, b.a, t) };
}

/* An ease on a loop's own clock: 0 before `at`, the curve after. */
static float ek(tour_curve c, int tl, int at, int ms)
{
	if (tl < at) return 0.0f;
	return tour_ease(c, (uint32_t)tl, (uint32_t)at, ms);
}

/* ---- shapes ----------------------------------------------------------------- */

#define RR_STEPS 10   /* a corner's points */

static void geometry(SDL_Renderer *r, const SDL_Vertex *v, int nv, const int *idx, int ni)
{
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_RenderGeometry(r, NULL, v, nv, idx, ni);
}

/* The corners of a rect `rx` by `ry` round, bottom right first, clockwise on
 * the screen: corner q's center and its quarter. */
static void corner(float x, float y, float w, float h, float rx, float ry, int q, int i,
                   float *px, float *py)
{
	const float ccx = q == 0 || q == 3 ? x + w - rx : x + rx;
	const float ccy = q < 2 ? y + h - ry : y + ry;
	const float t = (float)M_PI / 2.0f * ((float)q + (float)i / (RR_STEPS - 1));

	*px = ccx + cosf(t) * rx;
	*py = ccy + sinf(t) * ry;
}

/* A rounded rect from `top` at its top to `bot` at its foot: a fan from its
 * middle, every point colored for its height, which is the gradient exactly. */
static void grad_rr(SDL_Renderer *r, float x, float y, float w, float h, float rx, float ry,
                    SDL_Color top, SDL_Color bot)
{
	SDL_Vertex v[1 + 4 * RR_STEPS];
	int idx[4 * RR_STEPS * 3], n = 4 * RR_STEPS, i, q;

	if (w <= 0.0f || h <= 0.0f || (top.a == 0 && bot.a == 0)) return;
	x += g_ox;
	y += g_oy;
	rx = clampf(rx, 0.0f, w / 2.0f);
	ry = clampf(ry, 0.0f, h / 2.0f);
	v[0] = (SDL_Vertex){ { x + w / 2.0f, y + h / 2.0f }, col_lerp(top, bot, 0.5f), { 0, 0 } };
	for (q = 0; q < 4; q++)
		for (i = 0; i < RR_STEPS; i++) {
			SDL_Vertex *p = &v[1 + q * RR_STEPS + i];

			corner(x, y, w, h, rx, ry, q, i, &p->position.x, &p->position.y);
			p->color = col_lerp(top, bot, (p->position.y - y) / h);
			p->tex_coord = (SDL_FPoint){ 0, 0 };
		}
	for (i = 0; i < n; i++) {
		idx[i * 3] = 0;
		idx[i * 3 + 1] = 1 + i;
		idx[i * 3 + 2] = 1 + (i + 1) % n;
	}
	geometry(r, v, 1 + n, idx, n * 3);
}

static void rr(SDL_Renderer *r, float x, float y, float w, float h, float rad, SDL_Color c)
{
	grad_rr(r, x, y, w, h, rad, rad, c, c);
}

/* A frame `th` wide just inside a rounded rect: DESIGN.md's 3-unit edge. */
static void ring(SDL_Renderer *r, float x, float y, float w, float h, float rad, float th,
                 SDL_Color c)
{
	SDL_Vertex v[8 * RR_STEPS];
	int idx[4 * RR_STEPS * 6], n = 4 * RR_STEPS, i, q;

	if (w <= 0.0f || h <= 0.0f || c.a == 0) return;
	x += g_ox;
	y += g_oy;
	rad = clampf(rad, 0.0f, (w < h ? w : h) / 2.0f);
	for (q = 0; q < 4; q++)
		for (i = 0; i < RR_STEPS; i++) {
			const float t = (float)M_PI / 2.0f * ((float)q + (float)i / (RR_STEPS - 1));
			const int k = q * RR_STEPS + i;
			float ox, oy;

			float ix, iy;

			corner(x, y, w, h, rad, rad, q, i, &ox, &oy);
			/* Inside, the same corner `th` tighter; a corner tighter than
			 * the frame is square inside it. */
			ix = ox - cosf(t) * th;
			iy = oy - sinf(t) * th;
			if (rad < th) {
				ix = clampf(ox, x + th, x + w - th);
				iy = clampf(oy, y + th, y + h - th);
			}
			v[k * 2] = (SDL_Vertex){ { ox, oy }, c, { 0, 0 } };
			v[k * 2 + 1] = (SDL_Vertex){ { ix, iy }, c, { 0, 0 } };
		}
	for (i = 0; i < n; i++) {
		int a = i * 2, b = ((i + 1) % n) * 2;
		int *p = &idx[i * 6];

		p[0] = a; p[1] = a + 1; p[2] = b;
		p[3] = a + 1; p[4] = b + 1; p[5] = b;
	}
	geometry(r, v, 2 * n, idx, n * 6);
}

/* What lies outside a rect's round corners, in `c`: the ground drawn back
 * over a demonstration that reached the stage's corners. */
static void corner_masks(SDL_Renderer *r, float x, float y, float w, float h, float rad,
                         SDL_Color c)
{
	static const int at[4][2] = { { 1, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 } };
	SDL_Vertex v[1 + RR_STEPS];
	int idx[(RR_STEPS - 1) * 3], q, i;

	x += g_ox;
	y += g_oy;
	for (q = 0; q < 4; q++) {
		v[0] = (SDL_Vertex){ { x + at[q][0] * w, y + at[q][1] * h }, c, { 0, 0 } };
		for (i = 0; i < RR_STEPS; i++) {
			corner(x, y, w, h, rad, rad, q, i, &v[1 + i].position.x, &v[1 + i].position.y);
			v[1 + i].color = c;
			v[1 + i].tex_coord = (SDL_FPoint){ 0, 0 };
		}
		for (i = 0; i < RR_STEPS - 1; i++) {
			idx[i * 3] = 0;
			idx[i * 3 + 1] = 1 + i;
			idx[i * 3 + 2] = 2 + i;
		}
		geometry(r, v, 1 + RR_STEPS, idx, (RR_STEPS - 1) * 3);
	}
}

static void ellipse(SDL_Renderer *r, float cx, float cy, float rx, float ry, SDL_Color c)
{
	enum { N = 40 };
	SDL_Vertex v[N + 1];
	int idx[N * 3], i;

	if (rx <= 0.0f || ry <= 0.0f || c.a == 0) return;
	cx += g_ox;
	cy += g_oy;
	v[0] = (SDL_Vertex){ { cx, cy }, c, { 0, 0 } };
	for (i = 0; i < N; i++) {
		float t = 2.0f * (float)M_PI * (float)i / N;

		v[1 + i] = (SDL_Vertex){ { cx + cosf(t) * rx, cy + sinf(t) * ry }, c, { 0, 0 } };
		idx[i * 3] = 0;
		idx[i * 3 + 1] = 1 + i;
		idx[i * 3 + 2] = 1 + (i + 1) % N;
	}
	geometry(r, v, N + 1, idx, N * 3);
}

/* A convex shape of `n` points, in the layer's place. */
static void poly(SDL_Renderer *r, const SDL_FPoint *p, int n, SDL_Color c)
{
	SDL_Vertex v[8];
	int idx[18], i;

	if (n < 3 || n > 8 || c.a == 0) return;
	for (i = 0; i < n; i++)
		v[i] = (SDL_Vertex){ { p[i].x + g_ox, p[i].y + g_oy }, c, { 0, 0 } };
	for (i = 0; i < n - 2; i++) {
		idx[i * 3] = 0;
		idx[i * 3 + 1] = i + 1;
		idx[i * 3 + 2] = i + 2;
	}
	geometry(r, v, n, idx, (n - 2) * 3);
}

/* A stroke `w` wide with round ends. */
static void stroke(SDL_Renderer *r, float x0, float y0, float x1, float y1, float w, SDL_Color c)
{
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy), nx, ny;
	SDL_FPoint p[4];

	if (l <= 0.0f) return;
	nx = -dy / l * w / 2.0f;
	ny = dx / l * w / 2.0f;
	p[0] = (SDL_FPoint){ x0 + nx, y0 + ny };
	p[1] = (SDL_FPoint){ x1 + nx, y1 + ny };
	p[2] = (SDL_FPoint){ x1 - nx, y1 - ny };
	p[3] = (SDL_FPoint){ x0 - nx, y0 - ny };
	poly(r, p, 4, c);
	ellipse(r, x0, y0, w / 2.0f, w / 2.0f, c);
	ellipse(r, x1, y1, w / 2.0f, w / 2.0f, c);
}

/* ---- clips ------------------------------------------------------------------- */

static SDL_Rect clips[4];
static int nclips;

static void clip_push(SDL_Renderer *r, float x, float y, float w, float h)
{
	SDL_Rect q = { (int)floorf(x + g_ox), (int)floorf(y + g_oy),
	               (int)ceilf(w), (int)ceilf(h) }, in;

	if (nclips > 0) q = SDL_IntersectRect(&q, &clips[nclips - 1], &in) ? in : (SDL_Rect){ 0, 0, 0, 0 };
	if (nclips < 4) clips[nclips++] = q;
	SDL_RenderSetClipRect(r, &q);
}

static void clip_pop(SDL_Renderer *r)
{
	if (nclips > 0) nclips--;
	SDL_RenderSetClipRect(r, nclips ? &clips[nclips - 1] : NULL);
}

/* ---- words ------------------------------------------------------------------- */

/* A line whose capitals' middle is on `cy`: centered on x (anchor 0), from x
 * (-1), or ending at x (1). */
static void words(SDL_Renderer *r, ui_font_role role, const char *s, float x, float cy,
                  int anchor, ds_rgb c, float a)
{
	TTF_Font *f = ui_font(role);

	if (!f || !s || !s[0] || a * g_op <= 0.004f) return;
	ui_text(r, f, s, (int)lroundf(x + g_ox), ui_cap_y(f, (int)lroundf(cy + g_oy)), anchor,
	        ui_fade(ui_col(c, 255), a * g_op));
}

static float words_w(ui_font_role role, const char *s)
{
	return (float)ui_text_width(ui_font(role), s);
}

/* A lesson's title, line or note: centered under the stage, or as near as
 * keeps it the inset from the right edge. Only the Pixel 2's block, moved
 * right for the handheld, has a line that needs it (Music's is wider than
 * the stage); the Brick's are all centered. */
static void lesson_words(SDL_Renderer *r, ui_font_role role, const char *s, float cy, ds_rgb c)
{
	float cx = 512.0f;

	if (!s) return;
	cx = fminf(cx, SCR_W - DS_INSET - g_ox - words_w(role, s) / 2.0f);
	words(r, role, s, cx, cy, 0, c, 1.0f);
}

/* ---- Zyper ------------------------------------------------------------------- */

/* Zyper `h` high in the box centered on (cx, cy), posed by the rig (`p`),
 * `scale` of his size about the foot of his box (the hello's pop-in), toward
 * the ground by `dim`, at the layer's opacity. His shadow is the hook's, on
 * Beard, and is drawn only at his size at rest: it is baked for the height
 * he stands at. */
static void zyper(const tour_hooks *hk, float cx, float cy, float h, const zr_pose *p,
                  float scale, float dim)
{
	tour_zyper z;
	const bool rest = scale == 1.0f;
	const float hs = rest ? h : h * clampf(scale, 0.0f, 2.0f);

	if (!hk || !hk->zyper || hs <= 0.5f || g_op <= 0.004f) return;
	z.pose = *p;
	z.dim = dim;
	z.shadow = rest ? clampf(g_op, 0.0f, 1.0f) : 0.0f;
	z.alpha = (Uint8)(255.0f * clampf(g_op, 0.0f, 1.0f) + 0.5f);
	hk->zyper(hk->ctx, cx + g_ox, cy + h / 2.0f - hs / 2.0f + g_oy, hs, &z);
}

/* Where s's second sentence starts, after ". " or a Chinese full stop with
 * more after it, and how long the first is; NULL for one sentence. */
static const char *second_sentence(const char *s, int *first_len)
{
	const char *dot = strstr(s, ". "), *zh = strstr(s, "\xe3\x80\x82");   /* U+3002 */

	if (zh && !zh[3]) zh = NULL;
	if (dot && (!zh || dot < zh)) {
		*first_len = (int)(dot - s + 1);
		return dot + 2;
	}
	if (zh) {
		*first_len = (int)(zh - s + 3);
		return zh + 3;
	}
	return NULL;
}

/* His bubble, DESIGN.md 11.10: **meta** in **ground** on **text**, 60 high
 * and 24 in at each end, its line's middle on `cy`. One that would be wider
 * than `room` takes its second sentence to a second line above the bottom
 * one's place, growing upward, so it stays clear of what is at its right:
 * "That's SELECT. Try VOL+ or VOL-." beside "START: skip". */
static void bubble(SDL_Renderer *r, const char *s, float x, float cy, float op, float room)
{
	const ds_theme *t = theme();
	const float pitch = (float)ui_font_line(UI_F_META) * 1.25f;
	int cut = 0;
	const char *rest = second_sentence(s, &cut);
	float w = words_w(UI_F_META, s) + 48.0f;

	if (w > room && rest) {
		char first[128];
		float w1, w2;

		snprintf(first, sizeof first, "%.*s", cut, s);
		w1 = words_w(UI_F_META, first);
		w2 = words_w(UI_F_META, rest);
		w = (w1 > w2 ? w1 : w2) + 48.0f;
		rr(r, x, cy - 30.0f - pitch, w, 60.0f + pitch, 30.0f, col(rgb(t->text), op));
		words(r, UI_F_META, first, x + 24.0f, cy - pitch, -1, t->ground, op);
		words(r, UI_F_META, rest, x + 24.0f, cy, -1, t->ground, op);
		return;
	}
	rr(r, x, cy - 30.0f, w, 60.0f, 30.0f, col(rgb(t->text), op));
	words(r, UI_F_META, s, x + 24.0f, cy, -1, t->ground, op);
}

/* The welcome's and the finish's: the same bubble at **title**, a corner of
 * radius.panel and a tail toward him, so the "!" lines are his (section 12). */
static void say_bubble(SDL_Renderer *r, const char *s, float x, float top, float op)
{
	const ds_theme *t = theme();
	/* The words the bubble holds, a line or, where they are wider than the
	 * room from the bubble to the screen's inset (the pseudo-locale's), two,
	 * balanced; the bubble grows upward by the second line, so the tail
	 * stays where it points at him and the lines under it stay clear */
	const float room = SCR_W - DS_INSET - x - 80.0f;
	char line[2][LB_LINE];
	int rows = ui_balance_lines(ui_font(UI_F_TITLE), s, (int)room, 2, line);
	const float lh = (float)ui_font_line(UI_F_TITLE), grow = rows > 1 ? lh : 0.0f;
	float lw = 0.0f;
	int i;
	SDL_FPoint tail[3] = { { x + 2.0f, top + 66.0f }, { x + 2.0f, top + 94.0f },
	                       { x - 14.0f, top + 80.0f } };

	for (i = 0; i < rows; i++)
		if (words_w(UI_F_TITLE, line[i]) > lw) lw = words_w(UI_F_TITLE, line[i]);
	if (rows < 1) {
		lw = words_w(UI_F_TITLE, s);
		rows = 0;
	}
	rr(r, x, top - grow, lw + 80.0f, 120.0f + grow, DS_RADIUS_PANEL, col(rgb(t->text), op));
	poly(r, tail, 3, col(rgb(t->text), op));
	if (rows < 1) {
		words(r, UI_F_TITLE, s, x + 40.0f, top + 60.0f, -1, t->ground, op);
		return;
	}
	for (i = 0; i < rows; i++)
		words(r, UI_F_TITLE, line[i], x + 40.0f,
		      top + 60.0f - grow / 2.0f + ((float)i - (rows - 1) / 2.0f) * lh, -1, t->ground, op);
}

/* ---- the little things on the stage -------------------------------------------- */

/* A card the shelf's cards at 0.45 make - 150 x 200, its mark, band and name
 * as bars - centered on (cx, cy) at `s` of that, `sx` of its width more (the
 * details' turn), and lifted by its share of the focus. */
static void lcard(SDL_Renderer *r, unsigned sys, float cx, float cy, float s, float op,
                  float focus, float sx)
{
	const ds_theme *t = theme();
	const bool dark = theme_index() == DS_DARK;
	const unsigned mark = ui_sys_mark(sys);
	const float ex = s * sx, ey = s, w = 150.0f * ex, h = 200.0f * ey;
	const float x0 = cx - w / 2.0f, y0 = cy - h / 2.0f;
	static const float part[4][5] = {
		{ 43, 46, 64, 40, 9 }, { 0, 116, 150, 6, 0 }, { 30, 140, 90, 10, 5 }, { 47, 158, 56, 8, 4 } };
	SDL_Color pc[4];
	int i;

	if (w <= 0.5f || op <= 0.0f) return;
	pc[0] = pc[1] = col(mark, op);
	pc[2] = col(rgb(t->text_dim), 0.6f * op);
	pc[3] = col(rgb(t->text_dim), 0.35f * op);
	if (focus > 0.01f) {
		SDL_Rect q = { (int)lroundf(x0 + g_ox), (int)lroundf(y0 + g_oy),
		               (int)lroundf(w), (int)lroundf(h) };

		ui_lift_k(r, &q, sys, UI_LIFT_FOCUS_SMALL, focus * op * g_op);
	}
	grad_rr(r, x0, y0, w, h, 10.0f * ex, 10.0f * ey,
	        col(dark ? mix(rgb(t->surface), rgb(t->raised), 0.25f) : rgb(t->surface), op),
	        col(mix(rgb(t->surface), rgb(t->ground), dark ? 0.3f : 0.35f), op));
	for (i = 0; i < 4; i++)
		grad_rr(r, x0 + part[i][0] * ex, y0 + part[i][1] * ey, part[i][2] * ex,
		        part[i][3] * ey, part[i][4] * ex, part[i][4] * ey, pc[i], pc[i]);
}

/* The little shelf's spacing: 136 to a neighbour, 119 more to the next, 95
 * past that; and its sizes, 0.78 and 0.62 of the focus. */
static float off_x(float d)
{
	float a = fabsf(d), s = d < 0.0f ? -1.0f : 1.0f;

	return s * (a <= 1.0f ? 136.0f * a : a <= 2.0f ? 136.0f + 119.0f * (a - 1.0f)
	                                                : 255.0f + 95.0f * (a - 2.0f));
}

static float scl(float d)
{
	float a = fabsf(d);

	if (a <= 1.0f) return 1.0f - 0.22f * a;
	if (a <= 2.0f) return 0.78f - 0.16f * (a - 1.0f);
	return fmaxf(0.4f, 0.62f - 0.12f * (a - 2.0f));
}

/* The focused card at rest, on the stage. */
#define CARD_X 265
#define CARD_Y 50
#define CARD_W 150
#define CARD_H 200

typedef struct {
	float dim;          /* the cards around the focus, at this of their light */
	bool  hide_focus;
	float focus_sx;     /* the focus squashed across, the details' turn */
	float heart;        /* the favorite's heart, popped this far */
} shelf_opt;

static void shelf(SDL_Renderer *r, float fpos, const shelf_opt *o, const tour_hooks *hk)
{
	const shelf_opt plain = { 1.0f, false, 1.0f, 0.0f };
	const int f = TOUR_LITTLE_FOCUS + (int)floorf(fpos + 0.5f);
	int pass, i;

	if (!o) o = &plain;
	for (pass = 0; pass < 2; pass++)
		for (i = 0; i < TOUR_LITTLE_N; i++) {
			float d = (float)(i - TOUR_LITTLE_FOCUS) - fpos, a = fabsf(d), op;
			bool focus = i == f;

			if (a > 2.9f || focus != (pass == 1) || (focus && o->hide_focus)) continue;
			op = a <= 2.0f ? 1.0f - 0.12f * a : fmaxf(0.0f, 0.76f - 0.76f * (a - 2.0f) / 0.9f);
			if (!focus) op *= o->dim;
			lcard(r, TOUR_LITTLE[i].rgb, 340.0f + off_x(d), 150.0f, scl(d), op, fmaxf(0.0f, 1.0f - a),
			      focus ? o->focus_sx : 1.0f);
		}
	/* draw_heart's, 41 across, at the focused card's top left corner, popped
	 * in **quick** about its middle. */
	if (o->heart > 0.0f && hk && hk->heart && g_op > 0.004f) {
		const float k = o->heart, rad = 20.7f * k;

		hk->heart(hk->ctx, 272.0f + g_ox, 58.0f - 2.5f * rad / 16.0f + g_oy, rad,
		          col(rgb(theme()->favorite), 1.0f));
	}
}

/* The focused card's face filling whatever window holds it, the launch
 * zoom's: its mark and name centered, its band across. */
static void card_fill(SDL_Renderer *r, float x, float y, float w, float h, float rad, float op)
{
	const ds_theme *t = theme();
	const bool dark = theme_index() == DS_DARK;
	const unsigned mark = ui_sys_mark(SNES);

	grad_rr(r, x, y, w, h, rad, rad,
	        col(dark ? mix(rgb(t->surface), rgb(t->raised), 0.25f) : rgb(t->surface), op),
	        col(mix(rgb(t->surface), rgb(t->ground), dark ? 0.3f : 0.35f), op));
	rr(r, x + w / 2.0f - 32.0f, y + 46.0f, 64.0f, 40.0f, 9.0f, col(mark, op));
	rr(r, x, y + 116.0f, w, 6.0f, 0.0f, col(mark, op));
	rr(r, x + w / 2.0f - 45.0f, y + 140.0f, 90.0f, 10.0f, 5.0f, col(rgb(t->text_dim), 0.6f * op));
	rr(r, x + w / 2.0f - 28.0f, y + 158.0f, 56.0f, 8.0f, 4.0f, col(rgb(t->text_dim), 0.35f * op));
}

/* A stand-in game, in shades of its system's color - content, not chrome:
 * a sky, two hills, ground tiles that scroll, a row of blocks, three coins
 * that spin and a hero who hops. In the stage's units, 680 x 340. */
static void game(SDL_Renderer *r, unsigned sys, uint32_t now)
{
	const unsigned white = 0xFFFFFF, black = 0x000000;
	const unsigned sky1 = mix(sys, white, 0.8f), sky2 = mix(sys, white, 0.55f);
	const unsigned hill = mix(sys, white, 0.32f), hill2 = mix(sys, white, 0.12f);
	const unsigned mid = sys, dark = mix(sys, black, 0.5f), hero = mix(white, sys, 0.08f);
	const double t = (double)now;
	const float scroll = (float)fmod(t * 0.08, 40.0);
	static const float clouds[3][3] = { { 60, 52, 120 }, { 380, 90, 90 }, { 620, 40, 110 } };
	float px, jt, jump, hy;
	int i, leg;

	grad_rr(r, 0, 0, 680, 340, 0, 0, col(sky1, 1.0f), col(sky2, 1.0f));
	for (i = 0; i < 3; i++) {
		float x = (float)(fmod(fmod(clouds[i][0] - t * (0.015 + 0.006 * i), 860.0) + 860.0, 860.0)) - 120.0f;

		rr(r, x, clouds[i][1], clouds[i][2], 26, 13, col(white, 0.85f));
	}
	ellipse(r, -80 + 190, 180 + 190, 190, 190, col(hill, 1.0f));
	ellipse(r, 360 + 230, 200 + 230, 230, 230, col(hill2, 1.0f));
	rr(r, 0, 276, 680, 64, 0, col(dark, 1.0f));
	for (i = -1; i < 19; i++) rr(r, i * 40.0f - scroll, 276, 36, 22, 0, col(mid, 1.0f));
	px = (float)fmod(fmod(560.0 - t * 0.08, 900.0) + 900.0, 900.0) - 120.0f;
	for (i = 0; i < 3; i++) {
		rr(r, px + i * 36.0f, 176, 34, 30, 0, col(mid, 1.0f));
		rr(r, px + i * 36.0f, 200, 34, 6, 0, col(dark, 1.0f));
	}
	for (i = 0; i < 3; i++) {
		float cx = (float)fmod(fmod(420.0 + i * 46 - t * 0.08, 900.0) + 900.0, 900.0) - 60.0f;
		float sp = fabsf(cosf((float)fmod(t / 260.0 + i, 2.0 * M_PI)));
		float w = 18.0f * sp + 2.0f;

		ellipse(r, cx + 1.0f, 143, w / 2.0f, 11, col(mid, 1.0f));
		ellipse(r, cx + 1.0f, 143, w / 2.0f - 4.0f, 7, col(hero, 1.0f));
	}
	jt = (float)fmod(t, 1400.0) / 500.0f;
	jump = jt < 1.0f ? 90.0f * 4.0f * jt * (1.0f - jt) : 0.0f;
	leg = (int)fmod(t / 110.0, 2.0);
	hy = 276.0f - 40.0f - jump;
	rr(r, 190, hy, 30, 32, 0, col(dark, 1.0f));
	rr(r, 193, hy + 3, 24, 26, 0, col(hero, 1.0f));
	rr(r, 208, hy + 9, 6, 8, 0, col(dark, 1.0f));
	rr(r, leg ? 192.0f : 198.0f, hy + 32, 8, 8, 0, col(dark, 1.0f));
	rr(r, leg ? 210.0f : 204.0f, hy + 32, 8, 8, 0, col(dark, 1.0f));
}

/* The focused card grown `z` of the way into its game, z 0 the card and 1
 * the whole stage: the launch zoom as the stage shows it. Its window's
 * corners go square as it grows. */
static void zoom(SDL_Renderer *r, float z, uint32_t now, const tour_hooks *hk)
{
	const shelf_opt o = { 1.0f - z, true, 1.0f, 0.0f };
	const float x = lerpf(CARD_X, 0, z), y = lerpf(CARD_Y, 0, z);
	const float w = lerpf(CARD_W, STAGE_W, z), h = lerpf(CARD_H, STAGE_H, z);
	const float card_op = clampf(1.0f - z * 1.8f, 0.0f, 1.0f);
	const float game_op = clampf(z * 1.8f, 0.0f, 1.0f);
	const float was = g_op;

	shelf(r, 0.0f, &o, hk);
	if (game_op > 0.0f) {
		clip_push(r, x, y, w, h);
		g_op = was * game_op;
		game(r, SNES, now);
		g_op = was;
		clip_pop(r);
	}
	if (card_op > 0.0f) card_fill(r, x, y, w, h, lerpf(10.0f, 0.0f, z), card_op);
}

/* The details panel the card turns into (X), drawn once into a texture of
 * its own and drawn from there squashed across: SDL can scale a picture in
 * x, and the words on the panel turn with it. */
#define DETAILS_W 488
#define DETAILS_H 228

static SDL_Texture *details_tex;
static int details_theme = -1;
static lang details_lang;   /* its words' language, with the theme its key */
/* A render target that would not be made or set is not asked for again on
 * every frame, and is said once: the panel is then not drawn. */
static bool details_failed;

static void details_make(SDL_Renderer *r)
{
	const ds_theme *t = theme();
	const float was_op = g_op, was_x = g_ox, was_y = g_oy;

	if (details_failed || (details_tex && details_theme == theme_index() &&
	                       details_lang == i18n_lang()))
		return;
	if (!details_tex)
		details_tex = SDL_CreateTexture(r, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
		                                DETAILS_W, DETAILS_H);
	if (!details_tex || SDL_SetRenderTarget(r, details_tex) != 0) {
		fprintf(stderr, "tour: no details panel, drawn without: %s\n", SDL_GetError());
		details_failed = true;
		plat_draw_to_screen();
		return;
	}
	SDL_SetTextureBlendMode(details_tex, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
	SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
	SDL_RenderClear(r);
	g_op = 1.0f;
	g_ox = g_oy = 0.0f;
	rr(r, 0, 0, DETAILS_W, DETAILS_H, DS_RADIUS_ROW, col(rgb(t->surface), 1.0f));
	ring(r, 0, 0, DETAILS_W, DETAILS_H, DS_RADIUS_ROW, DS_FRAME, col(rgb(t->edge), 1.0f));
	lcard(r, SNES, 84, DETAILS_H / 2.0f, 0.8f, 1.0f, 0.0f, 1.0f);
	words(r, UI_F_META, "Chrono Trigger", 150, 64, -1, t->text, 1.0f);
	rr(r, 150, 92, 48, DS_BAR, 0, col(ui_sys_mark(SNES), 1.0f));
	words(r, UI_F_META, "SNES \xc2\xb7 1995", 150, 132, -1, t->text_dim, 1.0f);
	words(r, UI_F_META, T(S_TOUR_DEMO_PLAYED), 150, 180, -1, t->text_dim, 1.0f);
	g_op = was_op;
	g_ox = was_x;
	g_oy = was_y;
	plat_draw_to_screen();
	details_theme = theme_index();
	details_lang = i18n_lang();
}

static void details(SDL_Renderer *r, float sx)
{
	const float w = DETAILS_W * sx, x = 340.0f - w / 2.0f, y = 158.0f - DETAILS_H / 2.0f;

	if (!details_tex || w <= 0.5f) return;
	ui_lift_k(r, &(SDL_Rect){ (int)lroundf(x + g_ox), (int)lroundf(y + g_oy), (int)lroundf(w),
	                          DETAILS_H }, SNES, UI_LIFT_FOCUS_SMALL, g_op);
	SDL_SetTextureAlphaMod(details_tex, (Uint8)(255.0f * clampf(g_op, 0.0f, 1.0f) + 0.5f));
	SDL_RenderCopyF(r, details_tex, NULL,
	                &(SDL_FRect){ x + g_ox, y + g_oy, w, (float)DETAILS_H });
}

/* A menu over the little shelf (MENU), open `z` of the way: 8.2's open, from
 * 96% and clear to full and opaque, over **scrim.menu**. Its rows are bars;
 * the first is the **accent** fill and the fourth is dead. */
static void menu_panel(SDL_Renderer *r, float z)
{
	static const float lab_w[5] = { 118, 96, 132, 108, 84 }, val_w[5] = { 0, 34, 70, 58, 46 };
	const ds_theme *t = theme();
	const float k = 0.96f + 0.04f * z, cx = 340.0f, cy = 172.0f;
	const float was = g_op;
	int i;

#define PX(u) (cx + ((u) - 174.0f) * k)
#define PY(v) (cy + ((v) - 150.0f) * k)
	rr(r, 0, 0, STAGE_W, STAGE_H, 0, col(rgb(t->ink), t->scrim_menu / 255.0f * z));
	ui_lift_k(r, &(SDL_Rect){ (int)lroundf(PX(0) + g_ox), (int)lroundf(PY(0) + g_oy),
	                          (int)lroundf(348 * k), (int)lroundf(300 * k) }, CFG_NO_COLOR, UI_LIFT_PANEL, z * g_op);
	g_op = was * z;
	rr(r, PX(0), PY(0), 348 * k, 300 * k, 20 * k, col(rgb(t->surface), 1.0f));
	ring(r, PX(0), PY(0), 348 * k, 300 * k, 20 * k, DS_FRAME * k, col(rgb(t->edge), 1.0f));
	rr(r, PX(114), PY(26), 120 * k, 14 * k, 7 * k, col(rgb(t->text_soft), 0.6f));
	rr(r, PX(24), PY(58), 300 * k, DS_RULE * k, 0, col(rgb(t->edge), 1.0f));
	for (i = 0; i < 5; i++) {
		const float y = 78.0f + i * 44.0f;
		const bool sel = i == 0, dead = i == 3;
		SDL_Color lc = sel ? col(rgb(t->on_accent), 0.7f)
		             : col(rgb(dead ? t->text_dim : t->text_soft), dead ? 0.45f : 0.6f);
		SDL_Color vc = sel ? col(rgb(t->on_accent), 0.45f) : col(rgb(t->text_dim), 0.45f);

		if (sel) rr(r, PX(14), PY(y), 320 * k, 40 * k, 10 * k, col(rgb(t->accent), 1.0f));
		rr(r, PX(34), PY(y + 15), lab_w[i] * k, 10 * k, 5 * k, lc);
		if (val_w[i] > 0)
			rr(r, PX(348 - 34 - val_w[i]), PY(y + 15), val_w[i] * k, 10 * k, 5 * k, vc);
	}
#undef PX
#undef PY
	g_op = was;
}

/* Muse's Now Playing (SELECT), faded in `z`. */
static void now_playing(SDL_Renderer *r, float z, uint32_t now)
{
	const ds_theme *t = theme();
	const float prog = (float)fmod((double)now / 60000.0 + 0.33, 1.0);
	const float was = g_op;

	g_op = was * z;
	rr(r, 0, 0, STAGE_W, STAGE_H, 0, col(mix(rgb(t->ground), rgb(t->surface), 1.0f / 3.0f), 1.0f));
	ui_lift_k(r, &(SDL_Rect){ (int)(52 + g_ox), (int)(62 + g_oy), 200, 200 },
	          rgb(t->accent), UI_LIFT_FOCUS_SMALL, g_op);
	rr(r, 52, 62, 200, 200, 0, col(rgb(t->raised), 1.0f));
	ellipse(r, 152, 162, 70, 70, col(rgb(t->edge), 1.0f));
	ellipse(r, 152, 162, 16, 16, col(rgb(t->text_dim), 1.0f));
	words(r, UI_F_META, T(S_TOUR_DEMO_NOW_PLAYING), 288, 92, -1, t->text_dim, 1.0f);
	words(r, UI_F_LABEL, T(S_TOUR_DEMO_MIX), 288, 150, -1, t->text, 1.0f);
	rr(r, 288, 204, 340, DS_BAR, 3, col(rgb(t->track), 1.0f));
	rr(r, 288, 204, 340 * prog, DS_BAR, 3, col(rgb(t->accent_ink), 1.0f));
	words(r, UI_F_META, "1:12", 288, 250, -1, t->text_dim, 1.0f);
	words(r, UI_F_META, "3:40", 628, 250, 1, t->text_dim, 1.0f);
	g_op = was;
}

/* The volume or brightness bar at 0.8, src/osdbar.c's own pixels: the
 * stand-in the loop slides onto the stage, the pill's top left at (x, y). */
#define STANDIN_K 0.8f
static SDL_Texture *bar_tex;
static int bar_key = -1;

static void standin_bar(SDL_Renderer *r, int kind, int level, float x, float y, float op)
{
	const int max = kind == OSDBAR_VOLUME ? PLAT_VOL_MAX : PLAT_BRIGHT_MAX;
	const int key = (theme_index() * 4 + kind) * 64 + level;
	int w, h;

	osdbar_size(STANDIN_K, &w, &h);
	if (!bar_tex) {
		bar_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
		if (!bar_tex) return;
		SDL_SetTextureBlendMode(bar_tex, SDL_BLENDMODE_BLEND);
		bar_key = -1;
	}
	if (key != bar_key) {
		uint32_t *px = malloc((size_t)w * h * sizeof *px);

		if (!px) return;
		osdbar_render(px, w, h, w, STANDIN_K, theme_index(), kind, level, max, 1.0f, false);
		SDL_UpdateTexture(bar_tex, NULL, px, w * 4);
		free(px);
		bar_key = key;
	}
	SDL_SetTextureAlphaMod(bar_tex, (Uint8)(255.0f * clampf(op * g_op, 0.0f, 1.0f) + 0.5f));
	/* The buffer's margin is 12 units each side of the pill, at 0.8 */
	SDL_RenderCopyF(r, bar_tex, NULL,
	                &(SDL_FRect){ x - 12.0f * STANDIN_K + g_ox, y - 12.0f * STANDIN_K + g_oy,
	                              (float)w, (float)h });
}

/* The power-off screen in small, `t` ms into it: the shelf fades to the
 * ground in **fade-out**, and then the power-off itself plays on the stage,
 * the launcher's own frames (poweroff_frame, through the hook): Zyper comes
 * in, his tongue goes in as his head straightens, and he dims. The real one
 * has no words; this says them once he is in. `zyper`: false once the step's
 * press has landed, when the corner Zyper is the one (section 10). */
static void power_demo(SDL_Renderer *r, float t, bool zyper_on, const tour_hooks *hk)
{
	const ds_theme *th = theme();
	const float shelf_op = 1.0f - clampf(t / DS_MS_FADE_OUT, 0.0f, 1.0f);
	const float was = g_op;

	if (shelf_op > 0.0f) {
		g_op = was * shelf_op;
		shelf(r, 0.0f, NULL, hk);
		g_op = was;
	}
	rr(r, 0, 0, STAGE_W, STAGE_H, 0, col(rgb(th->ground), 1.0f - shelf_op));
	if (zyper_on && t > DS_MS_FADE_OUT && hk && hk->poweroff && g_op > 0.004f)
		hk->poweroff(hk->ctx, (unsigned)(t - DS_MS_FADE_OUT), 340.0f + g_ox, 118.0f + g_oy,
		             160.0f, -120.0f + g_ox, (Uint8)(255.0f * clampf(g_op, 0.0f, 1.0f) + 0.5f));
	words(r, UI_F_META, T(S_TOUR_DEMO_SAVED), 340, 244, 0, th->text_soft,
	      clampf((t - 2.0f * DS_MS_FADE_OUT) / DS_MS_FADE_OUT, 0.0f, 1.0f));
}

/* ---- a step's stage --------------------------------------------------------------- */

/* What the step's press does, `a` of the way. */
static void stage_at(SDL_Renderer *r, const tour *t, float a, uint32_t now, const tour_hooks *hk)
{
	shelf_opt o = { 1.0f, false, 1.0f, 0.0f };

	switch (t->step) {
	case TOUR_A: zoom(r, a, now, hk); break;
	case TOUR_B: zoom(r, 1.0f - a, now, hk); break;
	case TOUR_Y:
		o.heart = a;
		shelf(r, 0.0f, &o, hk);
		break;
	case TOUR_X:
		/* The card squashes across to nothing in the first half of turn,
		 * and the panel opens from nothing in the second. */
		if (a < 0.5f) {
			o.dim = 1.0f - 1.5f * a;
			o.focus_sx = 1.0f - 2.0f * a;
			shelf(r, 0.0f, &o, hk);
		} else {
			o.dim = 0.25f;
			o.hide_focus = true;
			shelf(r, 0.0f, &o, hk);
			details(r, 2.0f * a - 1.0f);
		}
		break;
	case TOUR_MENU:
		shelf(r, 0.0f, NULL, hk);
		if (a > 0.0f) menu_panel(r, a);
		break;
	case TOUR_SELECT:
		shelf(r, 0.0f, NULL, hk);
		if (a > 0.0f) now_playing(r, a, now);
		break;
	case TOUR_POWER:
		power_demo(r, a * (float)tour_step_info(TOUR_POWER)->loop->dur, !t->done, hk);
		break;
	default: shelf(r, 0.0f, NULL, hk); break;
	}
}

static void stage_inner(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *hk)
{
	const tour_info *st = tour_step_info(t->step);

	if (t->step == TOUR_MOVE) {
		int tl;
		float p;

		if (t->moves > 0 || t->done) {
			shelf(r, tour_shelf_pos(t, now), NULL, hk);
			return;
		}
		/* The loop: a step right and back, as the d-pad's arms press. */
		tl = tour_loop_time(t, 2800, now);
		p = tl < 1700 ? ek(TC_OUT, tl, 600, DS_MS_MOVE) : 1.0f - ek(TC_OUT, tl, 1700, DS_MS_MOVE);
		shelf(r, p, NULL, hk);
		return;
	}
	if (t->step == TOUR_VOL || t->step == TOUR_BRIGHT) {
		const bool vol = t->step == TOUR_VOL;
		int tl, lv;
		float in, out;

		shelf(r, 0.0f, NULL, hk);
		if (t->done) return;
		/* A stand-in bar slides down onto the stage and steps up twice; the
		 * real bar is the launcher's own, at the top. */
		tl = tour_loop_time(t, 3000, now);
		in = ek(TC_OUT, tl, 600, DS_MS_QUICK);
		out = clampf((float)(tl - 2500) / DS_MS_FADE_OUT, 0.0f, 1.0f);
		lv = (vol ? 11 : 6) + (tl >= 600) + (tl >= 1000);
		if (in * (1.0f - out) > 0.0f)
			standin_bar(r, vol ? OSDBAR_VOLUME : OSDBAR_BRIGHT, lv,
			            (STAGE_W - 452.0f * STANDIN_K) / 2.0f, 22.0f - 14.0f * (1.0f - in),
			            in * (1.0f - out));
		return;
	}
	if (!st->loop) {
		shelf(r, 0.0f, NULL, hk);
		return;
	}
	if (t->done) {
		stage_at(r, t, tour_real_a(t, now), now, hk);
	} else {
		tour_loop_at L = tour_loop_now(t, now);

		if (L.back > 0.0f) {
			const float was = g_op;

			stage_at(r, t, 0.0f, now, hk);
			g_op = was * (1.0f - L.back);
			stage_at(r, t, 1.0f, now, hk);
			g_op = was;
		} else {
			stage_at(r, t, L.a, now, hk);
		}
	}
}

/* ---- a step's frame ----------------------------------------------------------------- */

/* Ten 40 x 6 segments at the top center: done **accent-ink**, current
 * **text**, to come **track**. `cur` the lesson on, `done` its press. */
static void progress(SDL_Renderer *r, int n, int cur, bool done)
{
	const ds_theme *t = theme();
	const float left = (SCR_W - (n * 40.0f + (n - 1) * 8.0f)) / 2.0f;
	int i;

	for (i = 0; i < n; i++) {
		ds_rgb c = i < cur || (i == cur && done) ? t->accent_ink : i == cur ? t->text : t->track;

		rr(r, left + i * 48.0f, 32, 40, DS_BAR, 3, col(rgb(c), 1.0f));
	}
}

/* The reward's check: a 76 **accent** disc with the **on-accent** check
 * centered on (cx, cy), a lesson's at the stage's top right, (852, 84), and a
 * choice's on the kept one's top right corner. */
static void check_at(SDL_Renderer *r, float cx, float cy, float op, float sc)
{
	const ds_theme *t = theme();
	const float u = 46.0f / 24.0f * sc;
	const SDL_Color ink = col(rgb(t->on_accent), op);

#define CXY(a, b) cx + ((a) - 12.0f) * u, cy + ((b) - 12.0f) * u
	if (op <= 0.0f) return;
	ellipse(r, cx, cy, 38.0f * sc, 38.0f * sc, col(rgb(t->accent), op));
	stroke(r, CXY(5.0f, 12.5f), CXY(9.5f, 17.0f), 3.4f * u, ink);
	stroke(r, CXY(9.5f, 17.0f), CXY(19.0f, 7.5f), 3.4f * u, ink);
#undef CXY
}

static void check(SDL_Renderer *r, float op, float sc)
{
	check_at(r, 852.0f, 84.0f, op, sc);
}

/* A step's button, 11.9, centered on (cx, cy): pressed by the loop and by
 * the hand, pulsing twice after a wrong press. */
static void glyph(SDL_Renderer *r, const tour *t, tour_step s, float cx, float cy,
                  uint32_t now, float pulse)
{
	const tour_info *st = tour_step_info(s);
	const float k = 1.0f + PULSE_GROW * pulse, op = g_op;
	const int x = (int)lroundf(cx + g_ox), y = (int)lroundf(cy + g_oy);

#define P(p) tour_part_press(t, (p), now)
	switch (st->glyph) {
	case TG_FACE: {
		static const tour_part face[4] = { TP_A, TP_B, TP_X, TP_Y };
		int i = st->label[0] == 'B' ? 1 : st->label[0] == 'X' ? 2 : st->label[0] == 'Y' ? 3 : 0;

		ui_button_pressed(r, st->label, x, y, 96, UI_BUTTON_PLAIN, P(face[i]), k, op);
		break;
	}
	case TG_PILL:
		ui_button_pressed(r, st->label, x, y, 64, UI_BUTTON_PLAIN,
		                  P(!strcmp(st->label, "MENU") ? TP_MENU : TP_SELECT), k, op);
		break;
	case TG_DPAD: {
		const float arm[UI_DPAD_ARMS] = { P(TP_UP), P(TP_DOWN), P(TP_LEFT), P(TP_RIGHT) };

		ui_dpad(r, x, y, 120, arm, k, op);
		break;
	}
	case TG_ROCKER:
		ui_rocker(r, x, y, 64, P(TP_VOLDN), P(TP_VOLUP), k, op);
		break;
	case TG_POWER:
		ui_power(r, x, y, 96, P(TP_POWER), k, op);
		break;
	case TG_CHORD:
		/* The Pixel 2's MENU, a plus and the rocker; the Brick's F1 and F2 */
		if (t->fn_volume) {
			const float wm = (float)ui_button_width("MENU", 64), wp = 52.0f;
			const float wr = (float)ui_rocker_width(64), w = wm + wp + wr;
			const float x0 = cx - w / 2.0f;

			ui_button_pressed(r, "MENU", (int)lroundf(cx + (x0 + wm / 2.0f - cx) * k + g_ox), y,
			                  64, UI_BUTTON_PLAIN, P(TP_MENU), k, op);
			words(r, UI_F_META, "+", cx + (x0 + wm + wp / 2.0f - cx) * k, cy, 0,
			      theme()->text_dim, 1.0f);
			ui_rocker(r, (int)lroundf(cx + (x0 + wm + wp + wr / 2.0f - cx) * k + g_ox), y, 64,
			          P(TP_VOLDN), P(TP_VOLUP), k, op);
		} else {
			const float w1 = (float)ui_button_width("F1", 64), w2 = (float)ui_button_width("F2", 64);
			const float x0 = cx - (w1 + 16.0f + w2) / 2.0f;

			ui_button_pressed(r, "F1", (int)lroundf(cx + (x0 + w1 / 2.0f - cx) * k + g_ox), y,
			                  64, UI_BUTTON_PLAIN, P(TP_F1), k, op);
			ui_button_pressed(r, "F2",
			                  (int)lroundf(cx + (x0 + w1 + 16.0f + w2 / 2.0f - cx) * k + g_ox), y,
			                  64, UI_BUTTON_PLAIN, P(TP_F2), k, op);
		}
		break;
	}
#undef P
}

/* START's hint, "(START) skip" (DESIGN.md 11.4), at the bottom right; and
 * where it starts, which his bubble keeps clear of. Drawn as every button
 * hint is (keyhints.h) through the hook the launcher sets
 * (tour_hint_draw_set); without it - a check that links this file alone -
 * a glyph and the word in **meta**. */
static int  (*g_hint_w)(const char *key, const char *label);
static void (*g_hint)(SDL_Renderer *r, const char *key, const char *label, int x, int cy);

void tour_hint_draw_set(int (*width)(const char *key, const char *label),
                        void (*draw)(SDL_Renderer *r, const char *key, const char *label, int x, int cy))
{
	g_hint_w = width;
	g_hint = draw;
}

static float legend_w(void)
{
	return g_hint_w ? (float)g_hint_w("START", T(S_KEYHINT_SKIP))
	                : (float)(ui_button_width("START", 32) + 12) + words_w(UI_F_META, T(S_KEYHINT_SKIP));
}

static float legend_left(void)
{
	return SCR_W - DS_INSET - legend_w();
}

static void legend(SDL_Renderer *r)
{
	int x = (int)legend_left(), cy = SCR_H - 44, bw = ui_button_width("START", 32);

	if (g_hint) {
		g_hint(r, "START", T(S_KEYHINT_SKIP), x, cy);
		return;
	}
	ui_button(r, "START", x + bw / 2, cy, 32, UI_BUTTON_PLAIN);
	words(r, UI_F_META, T(S_KEYHINT_SKIP), SCR_W - DS_INSET, cy, 1, theme()->text_soft, 1.0f);
}

/* The handheld's opacity: on through the lessons, it fades in with the
 * first one's stage */
static float device_op(const tour *t, uint32_t now)
{
	return t->step == TOUR_FIRST_LESSON ? tour_ease(TC_OUT, now, t->step_at, DS_MS_MOVE) : 1.0f;
}

/* Zyper's cheer for a step's reward: a lesson's, or a choice's "Saved!" */
static const char *cheer_of(tour_step s)
{
	return tour_choice_of(s) >= 0 ? T(S_TOUR_CHEER_SAVED) : tour_cheer(s - TOUR_FIRST_LESSON);
}

/* Zyper's corner, his bubble and "START: skip", where they are in every
 * step, a lesson's or a choice's. */
static void step_corner(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *hk)
{
	const int in_step = since(now, t->step_at);
	const bool wrong_on = tour_wrong_on(t, now);
	const tour_zyper_at z = tour_zyper_now(t, now);
	const char *say = NULL;
	uint32_t say_at = 0, say_end = 0;
	bool say_held = false;
	float dy = 0.0f;

	g_op = 1.0f;
	g_ox = g_oy = 0.0f;

	/* Zyper, only for the reward and a wrong press: up from under the
	 * bottom edge in move, and down in exit when it is over, from wherever
	 * he is (tour_zyper_now). One Zyper at a time (section 10): in the power
	 * step the stage's is the one, until the press. */
	if (z.corner) {
		dy = CORNER_DROP * z.drop;
		zyper(hk, CORNER_X, CORNER_Y + dy, CORNER_H, &z.pose, 1.0f, 0.0f);
	}

	/* His bubble: the cheer, the last step's going, or which button that
	 * was and which to try. */
	if (t->done) {
		say = cheer_of(t->step);
		say_at = t->done_at + DS_MS_MOVE;
		say_held = true;
	} else if (t->had_reward && in_step < DS_MS_FADE_OUT) {
		say = cheer_of(t->last_step);
		say_at = now - 100000u;
		say_end = t->step_at;
	} else if (wrong_on) {
		say = t->hint;
		say_at = t->wrong_at + DS_MS_MOVE;
		say_end = t->wrong_at + DS_MS_HOLD_SHORT;
	}
	/* With him, or in the power step, where he is on the stage, alone: every
	 * wrong press gets its answer. */
	if (say && (z.corner || (t->step == TOUR_POWER && wrong_on))) {
		float in = tour_ease(TC_OUT, now, say_at, DS_MS_QUICK);
		float op = in * (say_held ? 1.0f
		                 : 1.0f - clampf((float)since(now, say_end) / DS_MS_FADE_OUT, 0.0f, 1.0f));

		bubble(r, say, BUBBLE_X, CORNER_Y + 2.0f + dy + 6.0f * (1.0f - in), op,
		       legend_left() - LEGEND_GAP - BUBBLE_X);
	}
	legend(r);
}

static void lesson(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *hk)
{
	const ds_theme *th = theme();
	const tour_info *st = tour_step_info(t->step);
	const float fade_in = tour_ease(TC_OUT, now, t->step_at, DS_MS_MOVE);
	const int in_step = since(now, t->step_at);
	const char *note;
	/* The handheld's column at the left, on the Pixel 2, and the lesson
	 * block moved right for it: everything to Zyper's corner is drawn from
	 * g_ox = lx (src/tourdevice.h). */
	const float lx = tourdev_shown(t) ? TOURDEV_SHIFT : 0.0f;

	g_ox = lx;
	progress(r, tour_segments(t), tour_segment(t), t->done);
	/* The handheld: tourdev_make kept it at the frame's start */
	tourdev_draw(r, t, now, device_op(t, now));

	/* The stage: the ground a third toward surface, radius-panel, and its
	 * edge drawn last over what plays inside it. */
	g_op = 1.0f;
	rr(r, STAGE_X, STAGE_Y, STAGE_W, STAGE_H, DS_RADIUS_PANEL,
	   col(mix(rgb(th->ground), rgb(th->surface), 1.0f / 3.0f), 1.0f));
	clip_push(r, STAGE_X, STAGE_Y, STAGE_W, STAGE_H);
	g_ox = STAGE_X + lx;
	g_oy = STAGE_Y;
	g_op = fade_in;
	stage_inner(r, t, now, hk);
	g_ox = lx;
	g_oy = 0.0f;
	g_op = 1.0f;
	clip_pop(r);
	corner_masks(r, STAGE_X, STAGE_Y, STAGE_W, STAGE_H, DS_RADIUS_PANEL, col(rgb(th->ground), 1.0f));
	ring(r, STAGE_X, STAGE_Y, STAGE_W, STAGE_H, DS_RADIUS_PANEL, DS_FRAME, col(rgb(th->edge), 1.0f));

	/* The reward's check, appearing in quick; the last one's going. */
	if (t->done) {
		float k = tour_ease(TC_OUT, now, t->done_at, DS_MS_QUICK);

		check(r, k, 0.3f + 0.7f * k);
	} else if (t->had_reward && in_step < DS_MS_FADE_OUT) {
		check(r, 1.0f - (float)in_step / DS_MS_FADE_OUT, 1.0f);
	}

	/* The button and its words, fading in with the stage. */
	g_op = fade_in;
	glyph(r, t, t->step, 512, GLYPH_Y, now, tour_wrong_pulse(t, now));
	lesson_words(r, UI_F_TITLE, st->title, TITLE_Y, th->text);
	lesson_words(r, UI_F_META, t->fn_volume && st->line_fn ? st->line_fn : st->line, LINE_Y,
	             th->text_soft);
	note = t->done && st->note_done ? st->note_done : st->note;
	lesson_words(r, UI_F_META, note, NOTE_Y, th->text_dim);
	g_op = 1.0f;
	/* Zyper's corner, his bubble and "START: skip" stay where they are */
	step_corner(r, t, now, hk);
}

/* ---- the welcome and the finish -------------------------------------------------------- */

/* The finish's confetti: falling, turning bits of lime - success (3.3); a
 * system's color would be decoration. */
static void confetti(SDL_Renderer *r, uint32_t now, float w, float hgt, int n)
{
	const ds_theme *t = theme();
	const bool dark = theme_index() == DS_DARK;
	const SDL_Color c[3] = {
		dark ? col(rgb(t->accent), 1.0f) : col(rgb(t->accent_ink), 1.0f),
		dark ? col(rgb(t->accent), 0.6f) : col(rgb(t->accent), 1.0f),
		dark ? col(mix(rgb(t->accent), rgb(t->ground), 0.3f), 1.0f)
		     : col(mix(rgb(t->accent_ink), rgb(t->accent), 0.5f), 1.0f) };
	int i;

	for (i = 0; i < n; i++) {
		const double r1 = fabs(fmod(sin(i * 12.9898) * 43758.5453, 1.0));
		const double r2 = fabs(fmod(sin(i * 78.233) * 12345.678, 1.0));
		const double r3 = fabs(fmod(sin(i * 39.425) * 24634.6345, 1.0));
		const double sp = 0.05 + 0.07 * r2, tt = (double)now + r3 * 20000.0;
		const float y = (float)(fmod(tt * sp, hgt + 60.0) - 30.0);
		const float x = (float)(r1 * w + 18.0 * sin(tt / 400.0 + i));
		const float rot = (float)(fmod(tt / (3.0 + 6.0 * r2), 360.0) * M_PI / 180.0);
		/* Turning about its middle, and over across as it spins */
		const float bw = (float)(10.0 + 8.0 * r3), sh = (float)(6.0 + 4.0 * r1);
		const float hx = bw * (float)cos(fmod(tt / 300.0 + i, 2.0 * M_PI)) / 2.0f, hy = sh / 2.0f;
		const float cr = cosf(rot), sr = sinf(rot);
		const float cx = x + bw / 2.0f, cy = y + sh / 2.0f;
		SDL_FPoint p[4] = {
			{ cx - hx * cr + hy * sr, cy - hx * sr - hy * cr },
			{ cx + hx * cr + hy * sr, cy + hx * sr - hy * cr },
			{ cx + hx * cr - hy * sr, cy + hx * sr + hy * cr },
			{ cx - hx * cr - hy * sr, cy - hx * sr + hy * cr } };

		poly(r, p, 4, c[i % 3]);
	}
}

/* "Zyper says it", my pick: Zyper at the left, his words in a big
 * bubble beside him, the two lines and A under it. */
static void welcome(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *hk)
{
	const ds_theme *th = theme();
	const bool finish = t->step == TOUR_FINISH;
	const float sop = tour_ease(TC_OUT, now, t->say_at + SAY_IN_MS, DS_MS_MOVE);
	const float lop = tour_ease(TC_OUT, now, t->say_at + LINES_IN_MS, DS_MS_MOVE);
	/* The step's line, as two: "Every button is in" / "MENU > Controls." */
	const char *lines = tour_step_info(t->step)->line;
	const char *l2 = strchr(lines, '\n');
	char l1[128];

	snprintf(l1, sizeof l1, "%.*s", l2 ? (int)(l2 - lines) : (int)strlen(lines), lines);
	if (l2) l2++;

	if (finish) {
		progress(r, tour_segments(t), tour_segments(t), false);
		confetti(r, now, SCR_W, SCR_H, 40);
	}
	{
		const tour_zyper_at z = tour_zyper_now(t, now);

		/* The last reward's corner Zyper leaving, then his hello */
		if (z.corner)
			zyper(hk, CORNER_X, CORNER_Y + CORNER_DROP * z.drop, CORNER_H, &z.pose, 1.0f, 0.0f);
		if (z.hello) zyper(hk, 268.0f, 372.0f, 300.0f, &z.pose, z.scale, 0.0f);
	}
	say_bubble(r, tour_step_info(t->step)->title, SAY_X, 214.0f + 8.0f * (1.0f - sop), sop);
	g_op = lop;
	words(r, UI_F_META, l1, SAY_X, 392, -1, th->text_soft, 1.0f);
	words(r, UI_F_META, l2, SAY_X, 440, -1, th->text_soft, 1.0f);
	glyph(r, t, t->step, SAY_X + 48.0f, 536, now, tour_wrong_pulse(t, now));
	words(r, UI_F_MENU, T(finish ? S_TOUR_FINISH_GO : S_TOUR_WELCOME_GO), SAY_X + 120.0f,
	      536, -1, th->text_soft, 1.0f);
	g_op = 1.0f;

	/* A wrong press on the welcome: the same answer as in a step. */
	if (!finish && tour_wrong_on(t, now)) {
		float op = tour_ease(TC_OUT, now, t->wrong_at, DS_MS_QUICK)
		         * (1.0f - clampf((float)(since(now, t->wrong_at) - DS_MS_HOLD_SHORT)
		                          / DS_MS_FADE_OUT, 0.0f, 1.0f));

		bubble(r, t->hint, SAY_X, 140.0f + 6.0f * (1.0f - op), op, SCR_W - DS_INSET - SAY_X);
	}
	if (!finish) legend(r);
}

/* ---- the language ------------------------------------------------------------------ */

/* DESIGN.md 11.30 (My pick in the languages lab, "Zyper says hello"):
 * Zyper where the welcome has him, his bubble saying hello in the language
 * under the cursor, and the languages beside him in **menu** rows of the
 * lab's 75, each in its own words and its own face, the focused one on the
 * accent with an A at its end. Five rows show; with more, the window moves
 * with the cursor, the others behind a mark at each end that has more. */
#define LANG_X     430.0f
#define LANG_W     540.0f
#define LANG_TOP   262.0f
#define LANG_ROW   75.0f
#define LANG_SHOWN 5

static void lang_step(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *hk)
{
	const ds_theme *th = theme();
	const int n = TOUR_LANGS, f = tour_lang_focus(t, now);
	const float sop = tour_ease(TC_OUT, now, t->say_at + SAY_IN_MS, DS_MS_MOVE);
	const float lop = tour_ease(TC_OUT, now, t->say_at + LINES_IN_MS, DS_MS_MOVE);
	int first = f - LANG_SHOWN / 2, i;

	if (first > n - LANG_SHOWN) first = n - LANG_SHOWN;
	if (first < 0) first = 0;
	{
		const tour_zyper_at z = tour_zyper_now(t, now);

		if (z.hello) zyper(hk, 268.0f, 372.0f, 300.0f, &z.pose, z.scale, 0.0f);
	}
	say_bubble(r, tour_step_info(TOUR_LANG)->title, SAY_X, 96.0f + 8.0f * (1.0f - sop), sop);
	g_op = lop;
	for (i = first; i < first + LANG_SHOWN && i < n; i++) {
		const float y = LANG_TOP + (float)(i - first) * LANG_ROW, cy = y + LANG_ROW / 2.0f;
		TTF_Font *fm = ui_font_lang(UI_F_MENU, (lang)i);
		ds_rgb ink = th->text_soft;

		if (i == f) {
			rr(r, LANG_X, y, LANG_W, LANG_ROW, DS_RADIUS_ROW, col(rgb(th->accent), 1.0f));
			ink = th->on_accent;
			ui_button_pressed(r, "A", (int)lroundf(LANG_X + LANG_W - 46.0f + g_ox),
			                  (int)lroundf(cy + g_oy), 52, UI_BUTTON_ON_ACCENT, 0.0f, 1.0f, g_op);
		}
		if (fm)
			ui_text(r, fm, lang_name((lang)i), (int)lroundf(LANG_X + 26.0f + g_ox),
			        ui_cap_y(fm, (int)lroundf(cy + g_oy)), -1, ui_fade(ui_col(ink, 255), g_op));
	}
	/* More above, more below: the panel's marks (11.3), small */
	{
		const SDL_Color c = col(rgb(th->text_dim), 1.0f);

		if (first > 0) {
			SDL_FPoint up[3] = { { LANG_X + LANG_W / 2.0f - 12.0f, LANG_TOP - 10.0f },
			                     { LANG_X + LANG_W / 2.0f + 12.0f, LANG_TOP - 10.0f },
			                     { LANG_X + LANG_W / 2.0f, LANG_TOP - 22.0f } };
			poly(r, up, 3, c);
		}
		if (first + LANG_SHOWN < n) {
			const float b = LANG_TOP + LANG_SHOWN * LANG_ROW;
			SDL_FPoint dn[3] = { { LANG_X + LANG_W / 2.0f - 12.0f, b + 10.0f },
			                     { LANG_X + LANG_W / 2.0f + 12.0f, b + 10.0f },
			                     { LANG_X + LANG_W / 2.0f, b + 22.0f } };
			poly(r, dn, 3, c);
		}
	}
	g_op = 1.0f;
	legend(r);
}

/* ---- the choices ------------------------------------------------------------------- */

/* DESIGN.md 11.10, "The choices": side by side, no stage and no glyph. Two
 * choices are previews 380 x 285 at y 96, 56 apart; the four sounds are
 * tiles 180 x 180 at y 130, 32 apart. */
#define CH_W        380.0f
#define CH_H        285.0f
#define CH_GAP      56.0f
#define CH_Y        96.0f
#define CH_TILE     180.0f
#define CH_TILE_GAP 32.0f
#define CH_TILE_Y   130.0f
/* A name's middle under its choice's bottom edge, and its pill */
#define CH_NAME_DY  48.0f
#define CH_PILL_H   52.0f
#define CH_PILL_PAD 22.0f
/* The focus ring: 6 wide, its inside 10 out from the choice */
#define CH_RING_OUT 10.0f
#define CH_RING_W   6.0f
/* A voice's waves ping in this long as it is focused */
#define CH_PING_MS  420

typedef struct { float x, y, w, h, rad; } ch_box;

static ch_box choice_box(int n, float i)
{
	if (n > 2) {
		const float x0 = (SCR_W - (n * CH_TILE + (n - 1) * CH_TILE_GAP)) / 2.0f;

		return (ch_box){ x0 + i * (CH_TILE + CH_TILE_GAP), CH_TILE_Y, CH_TILE, CH_TILE,
		                 DS_RADIUS_CARD };
	}
	return (ch_box){ (SCR_W - (n * CH_W + (n - 1) * CH_GAP)) / 2.0f + i * (CH_W + CH_GAP),
	                 CH_Y, CH_W, CH_H, DS_RADIUS_ROW };
}

/* The home screen previews, res/tour/home-LAYOUT-THEME.png: loaded the first
 * time one is drawn and kept until tour_draw_free. [layout][theme]. */
static SDL_Texture *prev_tex[2][DS_THEMES];
static bool prev_tried[2][DS_THEMES];

static SDL_Texture *preview(SDL_Renderer *r, const tour_hooks *hk, int layout, int th)
{
	static const char *const LAYOUT[2] = { "spotlight", "gallery" };
	char path[1024];
	SDL_Surface *s;

	if (prev_tried[layout][th]) return prev_tex[layout][th];
	if (!hk || !hk->root) return NULL;
	prev_tried[layout][th] = true;
	snprintf(path, sizeof path, "%s/res/tour/home-%s-%s.png", hk->root, LAYOUT[layout],
	         th == DS_DARK ? "dark" : "light");
	s = IMG_Load(path);
	if (!s) {
		fprintf(stderr, "tour: no preview %s: %s\n", path, IMG_GetError());
		return NULL;
	}
	prev_tex[layout][th] = SDL_CreateTextureFromSurface(r, s);
	SDL_FreeSurface(s);
	if (prev_tex[layout][th]) SDL_SetTextureBlendMode(prev_tex[layout][th], SDL_BLENDMODE_BLEND);
	return prev_tex[layout][th];
}

/* An arc `w` wide about (cx, cy), radius `rad` to its middle, from a0 to a1
 * (radians, 0 to the right, down positive), with round ends. */
static void arc(SDL_Renderer *r, float cx, float cy, float rad, float a0, float a1, float w,
                SDL_Color c)
{
	enum { N = 16 };
	SDL_Vertex v[2 * (N + 1)];
	int idx[N * 6], i;

	if (c.a == 0) return;
	for (i = 0; i <= N; i++) {
		const float a = a0 + (a1 - a0) * (float)i / N, ca = cosf(a), sa = sinf(a);

		v[2 * i] = (SDL_Vertex){ { cx + g_ox + ca * (rad - w / 2.0f), cy + g_oy + sa * (rad - w / 2.0f) },
		                         c, { 0, 0 } };
		v[2 * i + 1] = (SDL_Vertex){ { cx + g_ox + ca * (rad + w / 2.0f), cy + g_oy + sa * (rad + w / 2.0f) },
		                             c, { 0, 0 } };
	}
	for (i = 0; i < N; i++) {
		int *q = &idx[i * 6];

		q[0] = 2 * i; q[1] = 2 * i + 1; q[2] = 2 * i + 2;
		q[3] = 2 * i + 1; q[4] = 2 * i + 3; q[5] = 2 * i + 2;
	}
	geometry(r, v, 2 * (N + 1), idx, N * 6);
	ellipse(r, cx + cosf(a0) * rad, cy + sinf(a0) * rad, w / 2.0f, w / 2.0f, c);
	ellipse(r, cx + cosf(a1) * rad, cy + sinf(a1) * rad, w / 2.0f, w / 2.0f, c);
}

/* A speaker `k` units to the lab's 48, its middle on (cx, cy): its body
 * (`body`), its waves `wave` of the way in (scale and opacity), or crossed
 * out. Where its waves turn about, from its middle: */
#define WAVES_DX 0.3f    /* lab units, times k */
static void speaker(SDL_Renderer *r, float cx, float cy, float k, bool body, float wave, bool off,
                    SDL_Color c)
{
#define P(a, b) (SDL_FPoint){ cx + ((a) - 24.0f) * k, cy + ((b) - 24.0f) * k }
	const SDL_FPoint box[4] = { P(8, 19), P(15, 19), P(15, 29), P(8, 29) };
	const SDL_FPoint cone[4] = { P(15, 19), P(25, 11), P(25, 37), P(15, 29) };

	if (body) {
		poly(r, box, 4, c);
		poly(r, cone, 4, c);
	}
	if (off) {
		stroke(r, P(32, 18).x, P(32, 18).y, P(42, 30).x, P(42, 30).y, 3.0f * k, c);
		stroke(r, P(42, 18).x, P(42, 18).y, P(32, 30).x, P(32, 30).y, 3.0f * k, c);
	} else if (wave > 0.0f) {
		const float s = 0.7f + 0.3f * wave;
		SDL_Color cw = c;

		cw.a = (Uint8)(c.a * clampf(wave, 0.0f, 1.0f));
		arc(r, P(24.3f, 24).x, P(24.3f, 24).y, 9.0f * k * s, -0.73f, 0.73f, 3.0f * k, cw);
		arc(r, P(24.3f, 24).x, P(24.3f, 24).y, 16.0f * k * s, -0.76f, 0.76f, 3.0f * k, cw);
	}
#undef P
}

/* Two notes on a beam, `k` units to the lab's 48, crossed through for Off */
static void notes(SDL_Renderer *r, float cx, float cy, float k, bool off, SDL_Color c)
{
#define P(a, b) cx + ((a) - 24.0f) * k, cy + ((b) - 24.0f) * k
	const SDL_FPoint beam[4] = { { P(16, 11) }, { P(36, 7) }, { P(36, 10.5f) }, { P(16, 14.5f) } };

	ellipse(r, P(12.5f, 34), 5.2f * k, 4.4f * k, c);
	ellipse(r, P(30.5f, 30), 5.2f * k, 4.4f * k, c);
	stroke(r, P(16.8f, 12.5f), P(16.8f, 33), 2.6f * k, c);
	stroke(r, P(34.8f, 8.5f), P(34.8f, 29), 2.6f * k, c);
	poly(r, beam, 4, c);
	if (off) stroke(r, P(8, 8), P(40, 40), 3.4f * k, c);
#undef P
}

/* The sounds' and the music's tiles - the surface, its edge and its icon,
 * and the speaker's waves on their own - drawn once a theme into textures.
 * Drawn every frame, the four speakers' some 2,500 points held the Pixel 2's
 * Sounds step to 41.5 fps; copied, it is a few rects. Everything in them is
 * opaque, so a plain blend copies them as they were drawn. A renderer that
 * cannot draw into a texture draws them every frame. */
enum { TILE_SPEAKER, TILE_MUTED, TILE_WAVES, TILE_NOTES, TILE_NOTES_OFF, TILES };
#define SPEAKER_K (112.0f / 48.0f)
#define NOTES_K   (200.0f / 48.0f)
static SDL_Texture *tile_tex[TILES];
static int tile_theme = -1;
static bool tile_failed;

static void tile_draw(SDL_Renderer *r, int k, float w, float h)
{
	const ds_theme *th = theme();
	const SDL_Color soft = col(rgb(th->text_soft), 1.0f);
	const float rad = k >= TILE_NOTES ? DS_RADIUS_ROW : DS_RADIUS_CARD;

	if (k != TILE_WAVES) {
		rr(r, 0, 0, w, h, rad, col(rgb(th->surface), 1.0f));
		ring(r, 0, 0, w, h, rad, DS_FRAME, col(rgb(th->edge), 1.0f));
	}
	if (k <= TILE_WAVES)
		speaker(r, w / 2.0f, h / 2.0f, SPEAKER_K, k != TILE_WAVES, k == TILE_WAVES ? 1.0f : 0.0f,
		        k == TILE_MUTED, soft);
	else
		notes(r, w / 2.0f, h / 2.0f, NOTES_K, k == TILE_NOTES_OFF, soft);
}

/* The tiles for the theme that is on, before anything is drawn to the screen:
 * a texture drawn into is a render target, and the screen is given back */
static void tiles_make(SDL_Renderer *r)
{
	SDL_Texture *was;
	int k;

	if (tile_failed || tile_theme == theme_index()) return;
	was = SDL_GetRenderTarget(r);
	for (k = 0; k < TILES && !tile_failed; k++) {
		const ch_box b = choice_box(k >= TILE_NOTES ? 2 : 4, 0.0f);

		if (!tile_tex[k]) {
			tile_tex[k] = SDL_CreateTexture(r, PLAT_TARGET_FORMAT, SDL_TEXTUREACCESS_TARGET,
			                                (int)b.w, (int)b.h);
			if (tile_tex[k]) SDL_SetTextureBlendMode(tile_tex[k], SDL_BLENDMODE_BLEND);
		}
		if (!tile_tex[k] || SDL_SetRenderTarget(r, tile_tex[k]) != 0) {
			tile_failed = true;
			break;
		}
		SDL_SetRenderDrawColor(r, 0, 0, 0, 0);
		SDL_RenderClear(r);
		g_ox = g_oy = 0.0f;
		g_op = 1.0f;
		tile_draw(r, k, b.w, b.h);
	}
	SDL_SetRenderTarget(r, was);
	if (!tile_failed) tile_theme = theme_index();
}

/* Tile `k` with its top left at (x, y), `s` of its size about (ax, ay), at `a`
 * of the layer's opacity */
static void tile_copy(SDL_Renderer *r, int k, float x, float y, float ax, float ay, float s, float a)
{
	int w, h;

	if (SDL_QueryTexture(tile_tex[k], NULL, NULL, &w, &h) != 0) return;
	SDL_SetTextureAlphaMod(tile_tex[k], (Uint8)(255.0f * clampf(a * g_op, 0.0f, 1.0f) + 0.5f));
	SDL_RenderCopyF(r, tile_tex[k], NULL, &(SDL_FRect){ ax + (x - ax) * s + g_ox, ay + (y - ay) * s + g_oy,
	                                                     w * s, h * s });
}

static void choice(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *hk)
{
	const ds_theme *th = theme();
	const tour_info *st = tour_step_info(t->step);
	const int n = tour_choice_count(t->step), c = tour_choice_of(t->step);
	const float fade_in = tour_ease(TC_OUT, now, t->step_at, DS_MS_MOVE);
	const float pos = clampf(tour_focus_pos(t, now), 0.0f, (float)(n - 1));
	const SDL_Color soft = col(rgb(th->text_soft), 1.0f);
	int i;

	g_ox = g_oy = 0.0f;
	g_op = 1.0f;
	progress(r, tour_segments(t), tour_segment(t), t->done);

	g_op = fade_in;
	for (i = 0; i < n; i++) {
		const ch_box b = choice_box(n, (float)i);

		if (t->step == TOUR_THEME || t->step == TOUR_HOME) {
			/* The home screen: Spotlight in Light and in Dark, or both
			 * layouts in the theme that is on */
			const int lay = t->step == TOUR_HOME ? i : 0;
			const int pth = t->step == TOUR_THEME ? (i ? DS_DARK : DS_LIGHT) : theme_index();
			SDL_Texture *tex = preview(r, hk, lay, pth);

			if (tex) {
				SDL_SetTextureAlphaMod(tex, (Uint8)(255.0f * clampf(g_op, 0.0f, 1.0f) + 0.5f));
				SDL_RenderCopyF(r, tex, NULL, &(SDL_FRect){ b.x + g_ox, b.y + g_oy, b.w, b.h });
			} else {
				rr(r, b.x, b.y, b.w, b.h, b.rad, col(rgb(th->surface), 1.0f));
			}
			ring(r, b.x, b.y, b.w, b.h, b.rad, DS_FRAME, col(rgb(th->edge), 1.0f));
		} else if (t->step == TOUR_SOUNDS) {
			const bool off = i == n - 1;
			/* The focused voice's waves ping as it is heard */
			const int ms = since(now, t->moved_at);
			const float wave = i == t->pick[c] && ms >= 0 && ms < CH_PING_MS && t->moved_at != t->step_at
			                   ? tour_ease(TC_OUT, now, t->moved_at, CH_PING_MS) : 1.0f;
			const float cx = b.x + b.w / 2.0f, cy = b.y + b.h / 2.0f;

			if (tile_theme == theme_index()) {
				tile_copy(r, off ? TILE_MUTED : TILE_SPEAKER, b.x, b.y, 0.0f, 0.0f, 1.0f, 1.0f);
				if (!off)
					tile_copy(r, TILE_WAVES, b.x, b.y, cx + WAVES_DX * SPEAKER_K, cy,
					          0.7f + 0.3f * wave, wave);
			} else {
				rr(r, b.x, b.y, b.w, b.h, b.rad, col(rgb(th->surface), 1.0f));
				ring(r, b.x, b.y, b.w, b.h, b.rad, DS_FRAME, col(rgb(th->edge), 1.0f));
				speaker(r, cx, cy, SPEAKER_K, true, wave, off, soft);
			}
		} else if (tile_theme == theme_index()) {
			tile_copy(r, i ? TILE_NOTES_OFF : TILE_NOTES, b.x, b.y, 0.0f, 0.0f, 1.0f, 1.0f);
		} else {
			rr(r, b.x, b.y, b.w, b.h, b.rad, col(rgb(th->surface), 1.0f));
			ring(r, b.x, b.y, b.w, b.h, b.rad, DS_FRAME, col(rgb(th->edge), 1.0f));
			notes(r, b.x + b.w / 2.0f, b.y + b.h / 2.0f, NOTES_K, i == 1, soft);
		}
	}

	/* The focus: the ring sliding to the one looked at, in move */
	{
		const ch_box b = choice_box(n, pos);
		const float o = CH_RING_OUT + CH_RING_W;

		ring(r, b.x - o, b.y - o, b.w + 2.0f * o, b.h + 2.0f * o, b.rad + o, CH_RING_W,
		     col(rgb(th->accent), 1.0f));
	}

	/* The names, the focused one on an accent pill: as near to it as the
	 * ring is, so the pill slides over with it */
	for (i = 0; i < n; i++) {
		const ch_box b = choice_box(n, (float)i);
		const char *name = tour_choice_name(t->step, i);
		const float on = clampf(1.0f - fabsf(pos - (float)i), 0.0f, 1.0f);
		const float w = words_w(UI_F_META, name) + 2.0f * CH_PILL_PAD, cy = b.y + b.h + CH_NAME_DY;

		if (on > 0.0f)
			rr(r, b.x + b.w / 2.0f - w / 2.0f, cy - CH_PILL_H / 2.0f, w, CH_PILL_H, CH_PILL_H / 2.0f,
			   col(rgb(th->accent), on));
		{
			const unsigned ink = mix(rgb(th->text_dim), rgb(th->on_accent), on);

			words(r, UI_F_META, name, b.x + b.w / 2.0f, cy, 0,
			      (ds_rgb){ (Uint8)(ink >> 16), (Uint8)(ink >> 8), (Uint8)ink }, 1.0f);
		}
	}

	/* The reward's check, on the kept one's top right corner */
	if (t->done) {
		const ch_box b = choice_box(n, (float)t->kept[c]);
		const float k = tour_ease(TC_OUT, now, t->done_at, DS_MS_QUICK);

		g_op = 1.0f;
		check_at(r, b.x + b.w, b.y, k, 0.3f + 0.7f * k);
		g_op = fade_in;
	}

	lesson_words(r, UI_F_TITLE, st->title, TITLE_Y, th->text);
	lesson_words(r, UI_F_META, st->line, LINE_Y, th->text_soft);
	lesson_words(r, UI_F_META, st->note, NOTE_Y, th->text_dim);
	step_corner(r, t, now, hk);
}

/* ---- the whole screen ------------------------------------------------------------------ */

void tour_draw(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *hk)
{
	const ds_rgb g = theme()->ground;

	if (!t || t->over) return;
	/* Before anything is drawn to the screen: a texture drawn into is a
	 * render target, and the screen is given back after it. */
	if (t->step == TOUR_X) details_make(r);
	if (t->step == TOUR_SOUNDS || t->step == TOUR_MUSIC) tiles_make(r);
	tourdev_make(r, t, now, device_op(t, now));
	nclips = 0;
	SDL_RenderSetClipRect(r, NULL);
	g_op = 1.0f;
	g_ox = g_oy = 0.0f;
	SDL_SetRenderDrawColor(r, g.r, g.g, g.b, 255);
	SDL_RenderClear(r);
	if (t->step == TOUR_LANG) lang_step(r, t, now, hk);
	else if (t->step == TOUR_WELCOME || t->step == TOUR_FINISH) welcome(r, t, now, hk);
	else if (tour_choice_of(t->step) >= 0) choice(r, t, now, hk);
	else lesson(r, t, now, hk);
}

void tour_message(SDL_Renderer *r, const char *s, float op)
{
	const ds_theme *t = theme();
	const float k = 0.96f + 0.04f * op, w = words_w(UI_F_MENU, s) + 112.0f, h = 108.0f;
	const float x = SCR_W / 2.0f - w * k / 2.0f, y = SCR_H / 2.0f - h * k / 2.0f;

	if (op <= 0.0f) return;
	g_op = 1.0f;
	g_ox = g_oy = 0.0f;
	rr(r, 0, 0, SCR_W, SCR_H, 0, col(rgb(t->ink), t->scrim_menu / 255.0f * op));
	ui_lift_k(r, &(SDL_Rect){ (int)lroundf(x), (int)lroundf(y), (int)lroundf(w * k),
	                          (int)lroundf(h * k) }, CFG_NO_COLOR, UI_LIFT_PANEL, op);
	rr(r, x, y, w * k, h * k, DS_RADIUS_PANEL * k, col(rgb(t->surface), op));
	ring(r, x, y, w * k, h * k, DS_RADIUS_PANEL * k, DS_FRAME * k, col(rgb(t->edge), op));
	words(r, UI_F_MENU, s, SCR_W / 2.0f, SCR_H / 2.0f, 0, t->text_soft, op);
}

void tour_draw_free(void)
{
	if (details_tex) SDL_DestroyTexture(details_tex);
	if (bar_tex) SDL_DestroyTexture(bar_tex);
	details_tex = bar_tex = NULL;
	details_theme = bar_key = -1;
	tourdev_free();
	for (int k = 0; k < TILES; k++) {
		if (tile_tex[k]) SDL_DestroyTexture(tile_tex[k]);
		tile_tex[k] = NULL;
	}
	tile_theme = -1;
	tile_failed = false;
	for (int l = 0; l < 2; l++)
		for (int k = 0; k < DS_THEMES; k++) {
			if (prev_tex[l][k]) SDL_DestroyTexture(prev_tex[l][k]);
			prev_tex[l][k] = NULL;
			prev_tried[l][k] = false;
		}
}
