/* SPDX-License-Identifier: GPL-3.0-only */
/* The pixel tape: see pixtape.h. The cells are worked out on the CPU once,
 * from the drawing's shapes sampled at each cell's middle, as the Pixel
 * Tapes lab draws them (~/Documents/ZyperOS/labs/tape-styles-pixel-lab.html). */
#include "pixtape.h"

#include "cassette.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- the materials ----------------------------------------------------------- */

/* Each style's colors, the lab's (DESIGN.md 1: a tape keeps its own, as a
 * cover does); the outline the dark navy-to-black the home's art has round
 * a light thing and near black round a dark one */
static const pt_mat MAT[MP4T_COUNT] = {
	/* Smoke is smooth (src/cassette.c); its row is here for its print */
	[MP4T_SMOKE]     = { 0xd6d4cb, 0x9c9a92, 0x1e201e, 0, 0xf4f3ee, 0, 0, 0, 0, 0x5c5a54, 0, 0, 0.55f,
	                     true, false, false },
	[MP4T_PINSTRIPE] = { 0x1c1d1c, 0x3b3d3c, 0x0e0f0e, 0xf3ecd8, 0xefeee8, 0x22201c, 0x6e6757, 0x22201c,
	                     0x22201c, 0x09090b, 0xdf442b, 0, 1, false, false, false },
	[MP4T_BAND]      = { 0x1c1d1c, 0x3b3d3c, 0x0e0f0e, 0xf4efe0, 0xefeee8, 0x22201c, 0x6e6757, 0x22201c,
	                     0xffffff, 0x09090b, 0, 0, 1, false, false, false },
	[MP4T_FERRIC]    = { 0xe9e0c9, 0xc3b796, 0x2a211c, 0xf6f0de, 0xefeee8, 0x2b241e, 0x7a6c58, 0x2b241e,
	                     0xf6f0de, 0x5a4a36, 0x5d4032, 0xa8b39a, 1, true, false, false },
	[MP4T_METAL]     = { 0x171717, 0x2e2e2d, 0x070707, 0x131313, 0xd9d6cb, 0xd8b763, 0xa98f4f, 0xd8b763,
	                     0xd8b763, 0x030303, 0, 0, 1, false, true, false },
	[MP4T_CLEAR]     = { 0xecede8, 0x969892, 0, 0xf5f4ee, 0xf1f0ea, 0x1d1f1d, 0x666a66, 0x1d1f1d,
	                     0x1d1f1d, 0x7d7f79, 0, 0, 0.22f, true, false, false },
	[MP4T_RED]       = { 0xc93a2b, 0x9a291d, 0x1a0f0c, 0xf2ead6, 0xefeee8, 0x1f1c18, 0x6c6455, 0x1f1c18,
	                     0x1f1c18, 0x5e140d, 0x1f1c18, 0, 1, false, false, false },
	[MP4T_WORN]      = { 0x1e1f1e, 0x3c3e3d, 0x0f100f, 0xf1e8cf, 0xe8e6dc, 0x2a2520, 0x5e5548, 0x2a2520,
	                     0x2a2520, 0x09090b, 0xc2513b, 0, 1, false, false, true },
};

const pt_mat *pt_material(mp4t_id style)
{
	return &MAT[style >= 0 && style < MP4T_COUNT ? style : MP4T_SMOKE];
}

/* ---- the grid -------------------------------------------------------------------- */

void pt_grid_make(pt_grid *g, int w, int h, int u)
{
	g->u = u > 0 ? u : 8;
	g->w = w;
	g->h = h;
	g->cw = w / g->u;
	g->ch = h / g->u;
	g->cx = (float)CAS_W / (float)g->cw;
	g->cy = (float)CAS_H / (float)g->ch;
}

SDL_Rect pt_snap(const pt_grid *g, float x0, float y0, float x1, float y1)
{
	int i0 = (int)lroundf(x0 / g->cx), j0 = (int)lroundf(y0 / g->cy);
	int i1 = (int)lroundf(x1 / g->cx), j1 = (int)lroundf(y1 / g->cy);

	/* Square in the drawing, square in cells: the grid's cells are as
	 * near square as the tape's proportions are to the cassette's */
	if (fabsf((x1 - x0) - (y1 - y0)) < 0.5f) j1 = j0 + (i1 - i0);
	return (SDL_Rect){ i0 * g->u, j0 * g->u, (i1 - i0) * g->u, (j1 - j0) * g->u };
}

/* Clear's label: a card along the top, its own thing on the see-through
 * shell (my brief on the Pixel Tapes lab: "we need a better way to present
 * the label and etc on this one"). Its foot no lower than the strip it
 * was, at the hubs' tops; the reels' packs pass behind it. An outline cell
 * round it, its paper's dark rows at its foot and right, and its shadow on
 * what is under it; the song's row and the line under it inside, both
 * clear of the shell and the reels. */
#define CLEAR_LY0 10.0f
#define CLEAR_LY1 118.0f
#define CLEAR_ROW_MID 46.0f
#define CLEAR_UNDER_MID 96.0f
#define CLEAR_COVER 44.0f

void pt_boxes_of(const pt_grid *g, mp4t_id style, pt_boxes *b)
{
	bool clear = style == MP4T_CLEAR;
	float ly0 = clear ? CLEAR_LY0 : CAS_LABEL_Y0, ly1 = clear ? CLEAR_LY1 : CAS_LABEL_Y1;
	float row = clear ? CLEAR_ROW_MID : CAS_ROW_MID, under = clear ? CLEAR_UNDER_MID : CAS_UNDER_MID;

	float cs = clear ? CLEAR_COVER : CAS_COVER, ss = CAS_SIDE;
	float sx = clear ? CAS_COVER_X0 + CLEAR_COVER + 16 : CAS_SIDE_X1;

	b->paper = pt_snap(g, clear ? 24 : CAS_LABEL_X0, ly0, clear ? CAS_W - 24 : CAS_LABEL_X1, ly1);
	b->cover = pt_snap(g, CAS_COVER_X0, row - cs / 2, CAS_COVER_X0 + cs, row + cs / 2);
	b->side = pt_snap(g, sx, row - ss / 2, sx + ss, row + ss / 2);
	/* The cover's frame cell, then a cell of paper, before the side's box,
	 * however coarse the grid */
	if (b->side.x < b->cover.x + b->cover.w + 2 * g->u) b->side.x = b->cover.x + b->cover.w + 2 * g->u;
	b->row_mid = row * (float)g->h / CAS_H;
	b->under_mid = under * (float)g->h / CAS_H;
}

/* ---- colors and cells -------------------------------------------------------------- */

typedef struct { float r, g, b, a; } pc;     /* 0-255, alpha 0-1 */

static pc hex(unsigned rgb, float a)
{
	return (pc){ (float)(rgb >> 16 & 255), (float)(rgb >> 8 & 255), (float)(rgb & 255), a };
}

static pc mixc(pc a, pc b, float t)
{
	return (pc){ roundf(a.r + (b.r - a.r) * t), roundf(a.g + (b.g - a.g) * t),
	             roundf(a.b + (b.b - a.b) * t), a.a };
}

static pc with_a(pc c, float a) { c.a = a; return c; }

static const pc WHITE = { 255, 255, 255, 1 }, BLACK = { 0, 0, 0, 1 };

/* The 4 x 4 ordered dither's thresholds */
static const float BAYER[4][4] = {
	{ 0.5f / 16, 8.5f / 16, 2.5f / 16, 10.5f / 16 }, { 12.5f / 16, 4.5f / 16, 14.5f / 16, 6.5f / 16 },
	{ 3.5f / 16, 11.5f / 16, 1.5f / 16, 9.5f / 16 }, { 15.5f / 16, 7.5f / 16, 13.5f / 16, 5.5f / 16 },
};

/* A picture of cells, RGBA bytes */
typedef struct { int w, h; unsigned char *p; } cells;

static bool cells_new(cells *c, int w, int h)
{
	c->w = w;
	c->h = h;
	c->p = calloc((size_t)w * (size_t)h, 4);
	return c->p != NULL;
}

static void put(cells *c, int i, int j, pc col)
{
	unsigned char *d;

	if (i < 0 || j < 0 || i >= c->w || j >= c->h) return;
	d = c->p + ((size_t)j * c->w + i) * 4;
	d[0] = (unsigned char)col.r;
	d[1] = (unsigned char)col.g;
	d[2] = (unsigned char)col.b;
	d[3] = (unsigned char)lroundf(col.a * 255);
}

static void over(cells *c, int i, int j, pc col)
{
	unsigned char *d;
	float ba, oa;
	int k;

	if (i < 0 || j < 0 || i >= c->w || j >= c->h) return;
	d = c->p + ((size_t)j * c->w + i) * 4;
	ba = d[3] / 255.0f;
	oa = col.a + ba * (1 - col.a);
	if (oa <= 0) return;
	for (k = 0; k < 3; k++) {
		float s = k == 0 ? col.r : k == 1 ? col.g : col.b;

		d[k] = (unsigned char)lroundf((s * col.a + d[k] * ba * (1 - col.a)) / oa);
	}
	d[3] = (unsigned char)lroundf(oa * 255);
}

static void clear_cell(cells *c, int i, int j)
{
	if (i >= 0 && j >= 0 && i < c->w && j < c->h) c->p[((size_t)j * c->w + i) * 4 + 3] = 0;
}

/* A mask of cells: a shape sampled at each cell's middle */
typedef struct { int w, h; unsigned char *m; } mask;

typedef bool (*shape_fn)(const float *q, float x, float y);

static bool in_rr(float x, float y, float x0, float y0, float x1, float y1, float r)
{
	float dx, dy;

	if (x < x0 || x > x1 || y < y0 || y > y1) return false;
	dx = fmaxf(fmaxf(x0 + r - x, 0), x - (x1 - r));
	dy = fmaxf(fmaxf(y0 + r - y, 0), y - (y1 - r));
	return dx * dx + dy * dy <= r * r;
}

/* q: x0 y0 x1 y1 r */
static bool sh_rr(const float *q, float x, float y) { return in_rr(x, y, q[0], q[1], q[2], q[3], q[4]); }

/* q: the head end, a trapezoid from y 280 to the foot */
static bool sh_head(const float *q, float x, float y)
{
	(void)q;
	return y >= 280 && y <= CAS_H && x >= 176 - (y - 280) * 40 / 96 && x <= 416 + (y - 280) * 40 / 96;
}

/* q: its three openings */
static bool sh_holes(const float *q, float x, float y)
{
	(void)q;
	return (x >= 192 && x <= 232 && y >= 320 && y <= 352) || (x >= 360 && x <= 400 && y >= 320 && y <= 352) ||
	       (x >= 272 && x <= 320 && y >= 328 && y <= 352);
}

static bool mask_of(mask *m, const pt_grid *g, shape_fn fn, const float *q)
{
	int i, j;

	m->w = g->cw;
	m->h = g->ch;
	m->m = calloc((size_t)m->w * (size_t)m->h, 1);
	if (!m->m) return false;
	if (fn)
		for (j = 0; j < m->h; j++)
			for (i = 0; i < m->w; i++)
				m->m[j * m->w + i] = fn(q, (i + 0.5f) * g->cx, (j + 0.5f) * g->cy);
	return true;
}

static bool at(const mask *m, int i, int j)
{
	return i >= 0 && j >= 0 && i < m->w && j < m->h && m->m[j * m->w + i];
}

static bool edge(const mask *m, int i, int j)
{
	return !at(m, i - 1, j) || !at(m, i + 1, j) || !at(m, i, j - 1) || !at(m, i, j + 1);
}

/* ---- a seeded random: the album's name, so an album keeps its wear ---------- */

typedef struct { uint32_t h; } prng;

static void prng_seed(prng *r, const char *s)
{
	size_t n = strlen(s), i;

	r->h = 1779033703u ^ (uint32_t)n;
	for (i = 0; i < n; i++) {
		r->h = (r->h ^ (unsigned char)s[i]) * 3432918353u;
		r->h = r->h << 13 | r->h >> 19;
	}
}

static float prng_next(prng *r)
{
	uint32_t h = r->h;

	h = (h ^ h >> 16) * 2246822507u;
	h = (h ^ h >> 13) * 3266489909u;
	h ^= h >> 16;
	r->h = h;
	return (float)h / 4294967296.0f;
}

static unsigned name_hash(const char *s)
{
	unsigned h = 2166136261u;

	for (; s && *s; s++) h = (h ^ (unsigned char)*s) * 16777619u;
	return h;
}

/* ---- the still parts ---------------------------------------------------------------- */

#define WEAR 2              /* Medium (my pick): 1 light, 2 medium, 3 heavy */

typedef struct {
	const pt_grid *g;
	const pt_mat *m;
	mp4t_id style;
	cells B, F;
	mask body, pill, label, paper;
	SDL_Rect win;           /* cells */
} build;

/* A screw: a block of n x n cells, lit at its top-left, its slot dark */
static void screw(build *b, float x, float y)
{
	const pt_grid *g = b->g;
	int n = g->u >= 16 ? 2 : 3, a, c;
	int i0 = (int)lroundf(x / g->cx - n / 2.0f + (n == 2 ? 0.5f : 0));
	int j0 = (int)lroundf(y / g->cy - n / 2.0f + (n == 2 ? 0.5f : 0));
	pc mould = hex(b->m->rim, 1);

	for (a = 0; a < n; a++)
		for (c = 0; c < n; c++)
			put(&b->B, i0 + a, j0 + c, a + c == 0 ? mixc(mould, WHITE, 0.3f)
			                          : a == n - 1 && c == n - 1 ? mixc(mould, BLACK, 0.45f) : mould);
	if (n == 3) put(&b->B, i0 + 1, j0 + 1, mixc(mould, BLACK, 0.45f));
}

/* Paper cells clear of the window's frame */
static bool free_cell(const build *b, int i, int j)
{
	return at(&b->paper, i, j) && !at(&b->pill, i - 1, j) && !at(&b->pill, i + 1, j) &&
	       !at(&b->pill, i, j - 1) && !at(&b->pill, i, j + 1);
}

static void rows_of(const pt_grid *g, float y0, float y1, int *a, int *z)
{
	*a = (int)lroundf(y0 / g->cy);
	*z = (int)lroundf(y1 / g->cy);
	if (*z < *a + 1) *z = *a + 1;
}

static void fill_rows(build *b, float y0, float y1, pc c)
{
	int a, z, i, j;

	rows_of(b->g, y0, y1, &a, &z);
	for (j = a; j < z; j++)
		for (i = 0; i < b->g->cw; i++)
			if (free_cell(b, i, j)) put(&b->F, i, j, c);
}

/* A box of ink cells on the paper with its corners cut, one cell round
 * `in` (the side's letter, the cover's frame) */
static void ink_box(build *b, SDL_Rect r, pc ink, bool solid)
{
	const int u = b->g->u, i0 = r.x / u, j0 = r.y / u, i1 = (r.x + r.w) / u, j1 = (r.y + r.h) / u;
	int i, j;

	for (j = j0 - (solid ? 0 : 1); j < j1 + (solid ? 0 : 1); j++)
		for (i = i0 - (solid ? 0 : 1); i < i1 + (solid ? 0 : 1); i++) {
			bool corner = (i == (solid ? i0 : i0 - 1) || i == (solid ? i1 - 1 : i1)) &&
			              (j == (solid ? j0 : j0 - 1) || j == (solid ? j1 - 1 : j1));
			bool inside = i >= i0 && i < i1 && j >= j0 && j < j1;

			/* A box of four cells or fewer keeps its corners: cut, it
			 * would be a cross */
			if (corner && i1 - i0 > 4) continue;
			if (solid || !inside) put(&b->F, i, j, ink);
			else clear_cell(&b->F, i, j);
		}
}

static void wear(build *b, const char *album, const mp4t_id style);

static bool build_still(build *b, const char *album, unsigned band_rgb)
{
	const pt_grid *g = b->g;
	const pt_mat *m = b->m;
	const bool clear = b->style == MP4T_CLEAR;
	const float body_q[5] = { 0, 0, CAS_W, CAS_H, 16 };
	const float pill_q[5] = { CAS_WIN_X0, CAS_WIN_Y0, CAS_WIN_X1, CAS_WIN_Y1, (CAS_WIN_Y1 - CAS_WIN_Y0) / 2.0f };
	pt_boxes bx;
	float label_q[5];
	pc shell = hex(m->shell, m->shell_a), mould = hex(m->rim, clear ? 0.95f : 1), ink, light, dark;
	pc paper = hex(m->paper, 1), paper_dark, hole = { 18, 20, 19, 1 };
	mask head, holes;
	int i, j, W = g->cw, H = g->ch;

	pt_boxes_of(g, b->style, &bx);
	label_q[0] = (float)bx.paper.x / g->u * g->cx;
	label_q[1] = (float)bx.paper.y / g->u * g->cy;
	label_q[2] = (float)(bx.paper.x + bx.paper.w) / g->u * g->cx;
	label_q[3] = (float)(bx.paper.y + bx.paper.h) / g->u * g->cy;
	label_q[4] = 12;
	ink = with_a(hex(m->outline, 1), fmaxf(shell.a, 0.9f));
	light = mixc(shell, WHITE, m->light_shell ? 0.32f : 0.16f);
	dark = mixc(shell, BLACK, 0.28f);
	if (m->worn) paper = mixc(paper, (pc){ 217, 184, 112, 1 }, 0.22f);
	paper_dark = mixc(paper, BLACK, 0.13f);

	if (!mask_of(&b->body, g, sh_rr, body_q) || !mask_of(&b->pill, g, clear ? NULL : sh_rr, pill_q) ||
	    !mask_of(&b->label, g, sh_rr, label_q) || !mask_of(&b->paper, g, NULL, NULL) ||
	    !mask_of(&head, g, sh_head, NULL))
		return false;
	if (!mask_of(&holes, g, sh_holes, NULL)) { free(head.m); return false; }

	/* The shell: its outline, the light and dark rows, the flat body; the
	 * Metal's fine grid a checker of two near-blacks */
	for (j = 0; j < H; j++)
		for (i = 0; i < W; i++) {
			pc c;

			if (!at(&b->body, i, j)) continue;
			c = edge(&b->body, i, j) ? ink
			  : (!at(&b->body, i, j - 2) || !at(&b->body, i - 2, j)) ? light
			  : (!at(&b->body, i, j + 2) || !at(&b->body, i + 2, j)) ? dark : shell;
			if (b->style == MP4T_METAL && c.r == shell.r && c.a == shell.a && (i + j) % 2 == 0)
				c = mixc(shell, WHITE, 0.05f);
			put(&b->B, i, j, c);
		}
	if (clear) {
		/* A clear shell's inner wall, a line of cells */
		const float wall_q[5] = { 14, 12, CAS_W - 14, CAS_H - 12, 10 };
		mask wall;

		if (mask_of(&wall, g, sh_rr, wall_q)) {
			for (j = 0; j < H; j++)
				for (i = 0; i < W; i++)
					if (at(&wall, i, j) && edge(&wall, i, j)) over(&b->B, i, j, with_a(mould, 0.5f));
			free(wall.m);
		}
	}
	screw(b, 24, 24);
	screw(b, CAS_W - 24, 24);
	screw(b, 24, CAS_H - 24);
	screw(b, CAS_W - 24, CAS_H - 24);
	/* The head end, its openings, the tape across them; on the Clear
	 * shell in front of the tape, see-through */
	for (j = 0; j < H; j++)
		for (i = 0; i < W; i++)
			if (at(&head, i, j))
				over(clear ? &b->F : &b->B, i, j, !at(&head, i, j - 1) ? ink : with_a(mould, clear ? 0.55f : 1));
	if (!clear) screw(b, CAS_W / 2.0f, 300);
	for (j = 0; j < H; j++)
		for (i = 0; i < W; i++)
			if (at(&holes, i, j)) put(clear ? &b->F : &b->B, i, j, hole);
	if (!clear) {
		int jt = (int)lroundf(354 / g->cy - 0.5f);

		for (i = 0; i < W; i++)
			if ((i + 0.5f) * g->cx >= 180 && (i + 0.5f) * g->cx <= 412) put(&b->B, i, jt, (pc){ 70, 51, 42, 1 });
	} else {
		/* The tape's fixed way under the head, roller to roller */
		static const float P[4][2] = { { 50, 330 }, { 104, 356 }, { 488, 356 }, { 542, 330 } };
		int k;

		for (k = 0; k < 3; k++) {
			int x0 = (int)floorf(P[k][0] / g->cx), y0 = (int)floorf(P[k][1] / g->cy);
			int x1 = (int)floorf(P[k + 1][0] / g->cx), y1 = (int)floorf(P[k + 1][1] / g->cy);
			int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
			int e = dx + dy, n;

			for (n = 0; n < 1000; n++) {
				int e2 = 2 * e;

				put(&b->B, x0, y0, (pc){ 107, 75, 57, 1 });
				if (x0 == x1 && y0 == y1) break;
				if (e2 >= dy) { e += dy; x0 += sx; }
				if (e2 <= dx) { e += dx; y0 += sy; }
			}
		}
	}
	free(head.m);
	free(holes.m);

	/* The window's inside, dark through the glass, and its box */
	b->win = (SDL_Rect){ 1, 1, W - 2, H - 2 };
	if (!clear) {
		int x0 = W, y0 = H, x1 = -1, y1 = -1;
		pc in = hex(m->inside, 1);

		for (j = 0; j < H; j++)
			for (i = 0; i < W; i++)
				if (at(&b->pill, i, j)) {
					put(&b->B, i, j, in);
					if (i < x0) x0 = i;
					if (j < y0) y0 = j;
					if (i > x1) x1 = i;
					if (j > y1) y1 = j;
				}
		b->win = (SDL_Rect){ x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
	}

	/* The label: paper, a darker row at its foot and right, the window's
	 * moulded frame round the pill. Clear's is a card: an outline cell
	 * round it, so it stands on the shell and the reels behind it as a
	 * thing of its own, not a strip of paper on glass. */
	for (j = 0; j < H; j++)
		for (i = 0; i < W; i++) {
			bool frame;

			if (!at(&b->label, i, j) || at(&b->pill, i, j)) continue;
			if (clear && edge(&b->label, i, j)) { put(&b->F, i, j, hex(m->outline, 1)); continue; }
			b->paper.m[j * W + i] = 1;
			frame = at(&b->pill, i - 1, j) || at(&b->pill, i + 1, j) || at(&b->pill, i, j - 1) ||
			        at(&b->pill, i, j + 1);
			put(&b->F, i, j, frame ? mould
			                 : (!at(&b->label, i, j + 1) || !at(&b->label, i + 1, j) ||
			                    (clear && (!at(&b->label, i, j + 2) || !at(&b->label, i + 2, j)))) ? paper_dark
			                 : paper);
		}

	if (clear) {
		/* Its shadow on the shell and the reels under its foot */
		int jf = -1;

		for (j = 0; j < H; j++)
			for (i = 0; i < W; i++)
				if (at(&b->label, i, j) && !at(&b->label, i, j + 1) && j > jf) jf = j;
		for (i = 0; i < W; i++)
			if (at(&b->label, i - 1, jf)) over(&b->F, i, jf + 1, (pc){ 0, 0, 0, 0.22f });
	}

	/* Its print */
	switch (b->style) {
	case MP4T_PINSTRIPE:
	case MP4T_RED: {
		/* Five 5-unit stripes as one-cell ones a cell apart (three on the
		 * coarser grid); Red's three */
		int j0 = (int)lroundf(150 / g->cy), k, n = b->style == MP4T_RED ? 3 : g->u >= 16 ? 3 : 5;

		for (k = 0; k < n; k++)
			for (i = 0; i < W; i++)
				if (free_cell(b, i, j0 + 2 * k)) put(&b->F, i, j0 + 2 * k, hex(m->deco, 1));
		break;
	}
	case MP4T_BAND: {
		/* The cover's main color in two tones, its top row lighter */
		pc c = hex(band_rgb, 1);
		int a, z;

		fill_rows(b, CAS_WIN_Y0 - 6, CAS_WIN_Y1 + 6, c);
		rows_of(g, CAS_WIN_Y0 - 6, CAS_WIN_Y1 + 6, &a, &z);
		for (i = 0; i < W; i++)
			if (free_cell(b, i, a)) put(&b->F, i, a, mixc(c, WHITE, 0.22f));
		break;
	}
	case MP4T_FERRIC: {
		int a, z;

		fill_rows(b, CAS_LABEL_Y0, CAS_LABEL_Y0 + 12, hex(m->deco2, 1));
		fill_rows(b, CAS_WIN_Y0 - 6, CAS_WIN_Y1 + 6, hex(m->deco, 1));
		rows_of(g, CAS_WIN_Y0 - 6, CAS_WIN_Y1 + 6, &a, &z);
		for (i = 0; i < W; i++)
			if (free_cell(b, i, z - 1)) put(&b->F, i, z - 1, mixc(hex(m->deco, 1), BLACK, 0.3f));
		break;
	}
	case MP4T_METAL: {
		/* A gold ring round the label's upper part (the label's outer
		 * cells at 16 units, one in at 8), and a line under the song */
		pc gold = hex(m->ink, 1);
		bool outer = g->u >= 16;
		int a, z;

		for (j = 0; j < H; j++)
			for (i = 0; i < W; i++) {
				bool ring = outer ? edge(&b->label, i, j)
				              : !edge(&b->label, i, j) && (!at(&b->label, i - 2, j) || !at(&b->label, i + 2, j) ||
				                                           !at(&b->label, i, j - 2) || !at(&b->label, i, j + 2));

				if (at(&b->paper, i, j) && ring && (outer || free_cell(b, i, j)) &&
				    (j + 0.5f) * g->cy < CAS_UNDER_MID - 22)
					put(&b->F, i, j, with_a(gold, 0.85f));
			}
		rows_of(g, CAS_STRIPE_Y0 + 2, CAS_STRIPE_Y0 + 4, &a, &z);
		for (i = 0; i < W; i++)
			if (free_cell(b, i, a) && at(&b->label, i - 2, a) && at(&b->label, i + 2, a))
				put(&b->F, i, a, with_a(gold, 0.7f));
		break;
	}
	case MP4T_WORN: {
		/* A ruled card: a red band through the window, two blue rules
		 * every other cell */
		static const float RULE[2] = { 92, CAS_LABEL_Y1 - 8 };
		int k;

		fill_rows(b, 152, 168, with_a(hex(m->deco, 1), 0.9f));
		for (k = 0; k < 2; k++) {
			int jr = (int)lroundf(RULE[k] / g->cy - 0.5f);

			for (i = 0; i < W; i++)
				if (i % 2 == 0 && free_cell(b, i, jr)) put(&b->F, i, jr, (pc){ 142, 162, 194, 1 });
		}
		break;
	}
	default:
		break;
	}

	/* The side's letter's box in the ink, the cover's frame round where
	 * the cover goes */
	ink_box(b, bx.side, hex(m->ink, m->worn ? 0.9f : 1), true);
	ink_box(b, bx.cover, hex(m->outline, 1), false);

	/* The glass: a light row along its top, a two-cell shine */
	if (!clear) {
		int top = b->win.y, si = (int)lroundf((CAS_WIN_X0 + 56) / g->cx);

		for (i = 0; i < W; i++) {
			if (at(&b->pill, i, top)) over(&b->F, i, top, (pc){ 255, 255, 255, 0.22f });
			if (at(&b->pill, i, top + 1) && !at(&b->pill, i, top)) over(&b->F, i, top + 1, (pc){ 255, 255, 255, 0.22f });
		}
		over(&b->F, si, top + 1, (pc){ 255, 255, 255, 0.35f });
		over(&b->F, si + 1, top + 1, (pc){ 255, 255, 255, 0.35f });
		over(&b->F, si, top + 2, (pc){ 255, 255, 255, 0.2f });
	} else {
		/* The rollers, in front of the tape */
		static const float X[2] = { 50, 542 };
		int q = g->u >= 16 ? 2 : 3, k, a, c;

		for (k = 0; k < 2; k++) {
			int i0 = (int)lroundf(X[k] / g->cx - q / 2.0f), j0 = (int)lroundf(330 / g->cy - q / 2.0f);

			for (a = 0; a < q; a++)
				for (c = 0; c < q; c++)
					put(&b->F, i0 + a, j0 + c, a + c == 0 ? WHITE : a + c == 2 * (q - 1) ? with_a(mould, 1)
					                                                      : (pc){ 241, 240, 234, 1 });
		}
	}
	if (m->worn) wear(b, album, b->style);
	return true;
}

/* The wear (Worn, Medium): drawn into the two pictures once an album, as
 * dither and cells gone, from a seed of the album's name */
static void wear(build *b, const char *album, mp4t_id style)
{
	const pt_grid *g = b->g;
	const pt_mat *m = b->m;
	const int W = g->cw, H = g->ch, lv = WEAR;
	char seed[300];
	prng R;
	pt_boxes bx;
	float lx0, ly0, lx1, ly1;
	int i, j, k;

	(void)style;
	snprintf(seed, sizeof seed, "px%s%s", album ? album : "", "Worn");
	prng_seed(&R, seed);
	pt_boxes_of(g, b->style, &bx);
	lx0 = (float)bx.paper.x / g->u * g->cx;
	ly0 = (float)bx.paper.y / g->u * g->cy;
	lx1 = (float)(bx.paper.x + bx.paper.w) / g->u * g->cx;
	ly1 = (float)(bx.paper.y + bx.paper.h) / g->u * g->cy;

	/* Yellowed toward the paper's edges, in an ordered dither */
	if (!m->dark_paper)
		for (j = 0; j < H; j++)
			for (i = 0; i < W; i++) {
				int d = 9, r;
				float want;

				if (!at(&b->paper, i, j)) continue;
				for (r = 1; r <= 3 && d == 9; r++)
					if (!at(&b->label, i - r, j) || !at(&b->label, i + r, j) || !at(&b->label, i, j - r) ||
					    !at(&b->label, i, j + r))
						d = r;
				want = d == 1 ? 0.45f : d == 2 ? 0.25f : d == 3 ? 0.1f : 0;
				if (want > BAYER[j % 4][i % 4]) over(&b->F, i, j, (pc){ 168, 128, 66, 0.45f });
			}
	/* A coffee ring: a ring of cells, a few left out, a faint stain inside */
	{
		float w = lx1 - lx0, hh = ly1 - ly0;
		float rx = lx0 + 70 + prng_next(&R) * (w - 140), ry = ly0 + 30 + prng_next(&R) * (hh - 50);
		float rr = 40 + prng_next(&R) * 14;

		for (j = 0; j < H; j++)
			for (i = 0; i < W; i++) {
				float d = hypotf((i + 0.5f) * g->cx - rx, (j + 0.5f) * g->cy - ry), band = fabsf(d - rr);

				if (!at(&b->paper, i, j)) continue;
				if (band < g->cx * 0.55f && prng_next(&R) > 0.18f) over(&b->F, i, j, (pc){ 150, 104, 52, 0.5f });
				else if (d < rr - g->cx && BAYER[j % 4][i % 4] < 0.1f) over(&b->F, i, j, (pc){ 150, 104, 52, 0.2f });
			}
	}
	/* The peeled corner, top right: its cells gone (the shell shows), the
	 * flap folded over in the paper's back; a few cells gone along the
	 * label's foot */
	{
		float a = 34 / g->cx, bb = 30 / g->cy, ci = lx1 / g->cx, cj = ly0 / g->cy;

		for (j = 0; j < H; j++)
			for (i = 0; i < W; i++) {
				float u = (ci - (i + 0.5f)) / a, v = ((j + 0.5f) - cj) / bb;

				if (u >= 0 && v >= 0 && u + v <= 1) {
					clear_cell(&b->F, i, j);
					if (v > u * 1.1f) over(&b->F, i, j, (pc){ 230, 219, 189, 1 });
				}
			}
		for (k = 0; k < 5; k++) {
			int fi = (int)lroundf(lx0 / g->cx + prng_next(&R) * (lx1 - lx0) / g->cx);
			int fj = (int)lroundf(ly1 / g->cy) - 1;

			if (at(&b->paper, fi, fj)) clear_cell(&b->F, fi, fj);
		}
	}
	(void)lv;
	/* Scuffs on the shell's edge, scratches on the window, dust */
	{
		int n = 0, *ring = malloc(sizeof *ring * (size_t)W * (size_t)H);

		if (ring) {
			for (j = 0; j < H; j++)
				for (i = 0; i < W; i++)
					if (at(&b->body, i, j) && !edge(&b->body, i, j) &&
					    (edge(&b->body, i - 1, j) || edge(&b->body, i + 1, j) || edge(&b->body, i, j - 1) ||
					     edge(&b->body, i, j + 1)))
						ring[n++] = j * W + i;
			for (k = 0; k < 18 && n; k++) {
				int c = ring[(int)(prng_next(&R) * (float)n)];

				over(&b->B, c % W, c / W, m->light_shell ? (pc){ 80, 64, 48, 0.4f } : (pc){ 255, 255, 255, 0.28f });
			}
			free(ring);
		}
		for (k = 0; k < 2; k++) {
			int si = (int)lroundf(CAS_WIN_X0 / g->cx + 3 + prng_next(&R) * ((CAS_WIN_X1 - CAS_WIN_X0) / g->cx - 10));
			int sj = (int)lroundf(CAS_WIN_Y0 / g->cy + 1 + prng_next(&R) * 5), len = 3 + (int)(prng_next(&R) * 5);
			int dj = prng_next(&R) < 0.5f ? 1 : 0, q;

			for (q = 0; q < len; q++) {
				if (at(&b->pill, si, sj)) over(&b->F, si, sj, (pc){ 255, 255, 255, 0.16f });
				si++;
				if (q % 2) sj += dj;
			}
		}
		for (k = 0; k < 10; k++) {
			int di = (int)(prng_next(&R) * W), dj = (int)(prng_next(&R) * H);

			if (at(&b->body, di, dj) && !edge(&b->body, di, dj))
				over(at(&b->pill, di, dj) || at(&b->paper, di, dj) ? &b->F : &b->B, di, dj,
				     m->light_shell ? (pc){ 70, 58, 44, 0.26f } : (pc){ 255, 255, 255, 0.16f });
		}
	}
}

/* ---- what turns ------------------------------------------------------------------------- */

/* A pack of tape `rc` cells round: darker toward its edge, its rim the
 * darkest, the light up and left on it as it turns */
static void pack_cells(cells *c, int ox, int oy, int s, float rc)
{
	int i, j;

	for (j = 0; j < s; j++)
		for (i = 0; i < s; i++) {
			float dx = i + 0.5f - s / 2.0f, dy = j + 0.5f - s / 2.0f, d = hypotf(dx, dy), t, a;
			pc col;

			if (d > rc) continue;
			t = d / rc;
			a = atan2f(dy, dx);
			col = d > rc - 1 ? (pc){ 42, 30, 24, 1 } : t < 0.55f ? (pc){ 90, 66, 50, 1 }
			    : t < 0.85f ? (pc){ 70, 51, 42, 1 } : (pc){ 52, 38, 31, 1 };
			if (d <= rc - 1 && t > 0.58f && t < 0.84f && a > -2.5f && a < -1.75f) col = (pc){ 118, 88, 66, 1 };
			put(c, ox + i, oy + j, col);
		}
}

/* Where a line from a point outside a circle touches it, on the side
 * `side` (-1 left, 1 right) */
static void tangent(float px, float py, float cx, float cy, float r, int side, float *tx, float *ty)
{
	float dx = px - cx, dy = py - cy, d = hypotf(dx, dy), a, bb, x1, y1, x2, y2;

	if (d <= r) { *tx = cx + side * r; *ty = cy; return; }
	a = atan2f(dy, dx);
	bb = acosf(r / d);
	x1 = cx + r * cosf(a + bb);
	y1 = cy + r * sinf(a + bb);
	x2 = cx + r * cosf(a - bb);
	y2 = cy + r * sinf(a - bb);
	if (side < 0 ? x1 < x2 : x1 > x2) { *tx = x1; *ty = y1; }
	else { *tx = x2; *ty = y2; }
}

/* The hub's part at (dx, dy) from its middle, in the drawing's units, turned
 * back by the frame's angle: 0 none, 1 its rim, 2 its plastic, 3 the
 * spindle's hole, 4 the clamp in its rim */
static int hub_part(float dx, float dy, float ca, float sa, float rim_w)
{
	float rx = dx * ca - dy * sa, ry = dx * sa + dy * ca, d = hypotf(rx, ry);
	int k;

	if (d > CAS_HUB_R) return 0;
	if (rx >= 36 && rx <= CAS_HUB_R - 6 && fabsf(ry) < 6) return 4;
	if (d > CAS_HUB_R - rim_w) return 1;
	if (d <= 24) {
		for (k = 0; k < 6; k++) {
			float t = (float)(k * 60 + 30) * (float)M_PI / 180;
			float al = rx * cosf(t) + ry * sinf(t), pe = -rx * sinf(t) + ry * cosf(t);

			if (al >= 14 && fabsf(pe) < 7) return 2;
		}
		return 3;
	}
	return 2;
}

/* A hub at frame `fr`: each cell the part most of its 4 x 4 samples fall
 * in, so a turned tooth stays one clean shape, not scattered cells */
static void hub_cells(cells *c, int ox, int oy, int s, const pt_grid *g, int fr, pc hub, pc mould)
{
	float ang = (float)fr * MP4T_HUB_STEP * (float)M_PI / 180, ca = cosf(-ang), sa = sinf(-ang);
	pc pal[5] = { { 0 }, mixc(mould, BLACK, 0.1f), hub, { 30, 32, 31, 1 }, mixc(mould, BLACK, 0.25f) };
	int i, j, a, bb, k;

	pal[1].a = pal[4].a = 1;
	for (j = 0; j < s; j++)
		for (i = 0; i < s; i++) {
			int n[5] = { 0 }, best = 0;

			for (bb = 0; bb < 4; bb++)
				for (a = 0; a < 4; a++)
					n[hub_part((i + (a + 0.5f) / 4 - s / 2.0f) * g->cx, (j + (bb + 0.5f) / 4 - s / 2.0f) * g->cy, ca,
					           sa, g->cx * 0.9f)]++;
			for (k = 1; k < 5; k++)
				if (n[k] > n[best] || (k == 4 && n[4] >= 5)) best = k;
			if (best) put(c, ox + i, oy + j, pal[best]);
		}
}

/* Line cells from (x0, y0) to (x1, y1) */
static void line_cells(cells *c, int x0, int y0, int x1, int y1, pc col)
{
	int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy, n;

	for (n = 0; n < 2000; n++) {
		int e2 = 2 * e;

		put(c, x0, y0, col);
		if (x0 == x1 && y0 == y1) break;
		if (e2 >= dy) { e += dy; x0 += sx; }
		if (e2 <= dx) { e += dx; y0 += sy; }
	}
}

/* ---- the textures -------------------------------------------------------------------------- */

static SDL_Texture *texture_of(SDL_Renderer *r, const cells *c)
{
	SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, c->w, c->h);

	if (!t) return NULL;
	SDL_UpdateTexture(t, NULL, c->p, c->w * 4);
	SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
	SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
	return t;
}

void pt_free(pt_tex *t)
{
	SDL_Texture **all[5] = { &t->back, &t->front, &t->packs[0], &t->packs[1], &t->hubs };
	int k;

	for (k = 0; k < 5; k++)
		if (*all[k]) { SDL_DestroyTexture(*all[k]); *all[k] = NULL; }
	t->made = false;
}

/* A sheet's room: sprites side by side in rows no wider than SHEET_W */
#define SHEET_W 512

static void sheet_place(SDL_Rect *src, int n, int *w, int *h)
{
	int x = 0, y = 0, row = 0, k;

	*w = 0;
	for (k = 0; k < n; k++) {
		if (x + src[k].w > SHEET_W) { x = 0; y += row + 1; row = 0; }
		src[k].x = x;
		src[k].y = y;
		x += src[k].w + 1;
		if (src[k].h > row) row = src[k].h;
		if (x > *w) *w = x;
	}
	*h = y + row;
}

/* The pack sheet for `reel` (0 left, 1 right): a disc for every half cell
 * from the hub's to a full pack's; on the Clear shell, whole, with the
 * tape's way from its edge to its roller */
static SDL_Texture *packs_sheet(SDL_Renderer *r, pt_tex *t, const pt_grid *g, int reel, bool clear)
{
	SDL_Rect src[PT_PACKS_MAX];
	float rx = (float)CAS_W / 2 + (reel ? CAS_REEL_DX : -CAS_REEL_DX), roller = reel ? CAS_W - 50.0f : 50.0f;
	int k, w, h;
	cells c;
	SDL_Texture *tex;

	for (k = 0; k < t->pack_n; k++) {
		float rc = (float)(t->pack_first + k) / 2;
		int s = 2 * (int)ceilf(rc) + 1, half = (s - 1) / 2;
		int x0 = -half, y0 = -half, x1 = half, y1 = half;

		if (clear) {
			int ri = (int)floorf(roller / g->cx) - t->reel_ci[reel], rj = (int)floorf(330 / g->cy) - t->reel_cj;

			if (ri < x0) x0 = ri;
			if (ri > x1) x1 = ri;
			if (rj > y1) y1 = rj;
		}
		src[k] = (SDL_Rect){ 0, 0, x1 - x0 + 1, y1 - y0 + 1 };
		t->pack[reel][k].ox = x0;
		t->pack[reel][k].oy = y0;
	}
	sheet_place(src, t->pack_n, &w, &h);
	if (!cells_new(&c, w, h)) return NULL;
	for (k = 0; k < t->pack_n; k++) {
		float rc = (float)(t->pack_first + k) / 2;
		int s = 2 * (int)ceilf(rc) + 1, half = (s - 1) / 2;
		int cx0 = src[k].x - t->pack[reel][k].ox, cy0 = src[k].y - t->pack[reel][k].oy;

		t->pack[reel][k].src = src[k];
		if (clear) {
			/* From the roller to where the tape leaves the pack, in cells
			 * from the reel's middle cell */
			float r_units = rc * g->cx, tx, ty;
			int ri = (int)floorf(roller / g->cx), rj = (int)floorf(330 / g->cy);

			tangent(roller, 330, rx, CAS_REEL_Y, r_units, reel ? 1 : -1, &tx, &ty);
			line_cells(&c, cx0 + (int)floorf(tx / g->cx) - t->reel_ci[reel],
			           cy0 + (int)floorf(ty / g->cy) - t->reel_cj, cx0 + ri - t->reel_ci[reel],
			           cy0 + rj - t->reel_cj, (pc){ 107, 75, 57, 1 });
		}
		pack_cells(&c, cx0 - half, cy0 - half, s, rc);
	}
	tex = texture_of(r, &c);
	free(c.p);
	return tex;
}

static SDL_Texture *hubs_sheet(SDL_Renderer *r, pt_tex *t, const pt_grid *g, const pt_mat *m)
{
	SDL_Rect src[MP4T_HUB_FRAMES];
	int s = 2 * (int)ceilf(CAS_HUB_R / g->cx) + 1, k, w, h;
	cells c;
	SDL_Texture *tex;

	for (k = 0; k < MP4T_HUB_FRAMES; k++) src[k] = (SDL_Rect){ 0, 0, s, s };
	sheet_place(src, MP4T_HUB_FRAMES, &w, &h);
	if (!cells_new(&c, w, h)) return NULL;
	for (k = 0; k < MP4T_HUB_FRAMES; k++) {
		t->hub[k] = (pt_sprite){ src[k], -(s - 1) / 2, -(s - 1) / 2 };
		hub_cells(&c, src[k].x, src[k].y, s, g, k, hex(m->hub, 1), hex(m->rim, 1));
	}
	tex = texture_of(r, &c);
	free(c.p);
	return tex;
}

static void build_free(build *b)
{
	free(b->B.p);
	free(b->F.p);
	free(b->body.m);
	free(b->pill.m);
	free(b->label.m);
	free(b->paper.m);
}

static bool build_make(build *b, mp4t_id style, const pt_grid *g, const char *album, unsigned band_rgb)
{
	memset(b, 0, sizeof *b);
	b->g = g;
	b->m = pt_material(style);
	b->style = style;
	if (!cells_new(&b->B, g->cw, g->ch) || !cells_new(&b->F, g->cw, g->ch) ||
	    !build_still(b, album, band_rgb)) {
		build_free(b);
		return false;
	}
	return true;
}

bool pt_cells(mp4t_id style, const pt_grid *g, const char *album, unsigned band_rgb, int which,
              unsigned char *out)
{
	build b;

	if (!build_make(&b, style, g, album, band_rgb)) return false;
	memcpy(out, which ? b.F.p : b.B.p, (size_t)g->cw * (size_t)g->ch * 4);
	build_free(&b);
	return true;
}

bool pt_ready(SDL_Renderer *r, pt_tex *t, mp4t_id style, const pt_grid *g, const char *album,
              unsigned band_rgb)
{
	unsigned key = name_hash(album);
	bool clear = style == MP4T_CLEAR;
	const pt_mat *m = pt_material(style);
	Uint32 t0;
	build b;
	int k;

	/* Only Cover Band's band and Worn's wear follow the album */
	if (style != MP4T_BAND) band_rgb = 0;
	if (style != MP4T_WORN) key = 0;
	if (t->made && t->style == style && t->cw == g->cw && t->ch == g->ch && t->u == g->u && t->album == key &&
	    t->band_rgb == band_rgb)
		return true;
	t0 = SDL_GetTicks();
	pt_free(t);
	t->style = style;
	t->cw = g->cw;
	t->ch = g->ch;
	t->u = g->u;
	t->album = key;
	t->band_rgb = band_rgb;
	t->reel_cj = (int)floorf(CAS_REEL_Y / g->cy);
	t->reel_ci[0] = (int)floorf(((float)CAS_W / 2 - CAS_REEL_DX) / g->cx);
	t->reel_ci[1] = (int)floorf(((float)CAS_W / 2 + CAS_REEL_DX) / g->cx);
	t->pack_first = mp4t_pack_half(CAS_HUB_R / g->cx);
	t->pack_n = mp4t_pack_half(CAS_PACK_R / g->cx) - t->pack_first + 1;
	if (t->pack_n > PT_PACKS_MAX) t->pack_n = PT_PACKS_MAX;
	if (!build_make(&b, style, g, album, band_rgb)) return false;
	t->clip = b.win;
	t->back = texture_of(r, &b.B);
	t->front = texture_of(r, &b.F);
	build_free(&b);
	/* One sheet serves both reels but on Clear, where each pack carries
	 * its own reel's tape to its roller */
	t->packs[0] = packs_sheet(r, t, g, 0, clear);
	if (clear) t->packs[1] = packs_sheet(r, t, g, 1, true);
	else memcpy(t->pack[1], t->pack[0], sizeof t->pack[0]);
	t->hubs = hubs_sheet(r, t, g, m);
	t->made = t->back && t->front && t->packs[0] && (t->packs[1] || !clear) && t->hubs;
	/* Once a style, a grid or an album: what it costs, for the device's log */
	{
		SDL_Texture *all[5] = { t->back, t->front, t->packs[0], t->packs[1], t->hubs };
		long bytes = 0;
		int w, h;

		for (k = 0; k < 5; k++)
			if (all[k] && SDL_QueryTexture(all[k], NULL, NULL, &w, &h) == 0) bytes += (long)w * h * 4;
		fprintf(stderr, "mp4: the %s tape drawn in %u ms, %ld KB of textures\n", mp4t_key(style),
		        (unsigned)(SDL_GetTicks() - t0), bytes / 1024);
	}
	return t->made;
}

void pt_draw(SDL_Renderer *r, const pt_tex *t, const pt_grid *g, int x, int y, float f, const float ang[2])
{
	const int u = g->u;
	SDL_Rect clip = { x + t->clip.x * u, y + t->clip.y * u, t->clip.w * u, t->clip.h * u };
	int k;

	if (!t->made) return;
	SDL_RenderCopy(r, t->back, NULL, &(SDL_Rect){ x, y, g->w, g->h });
	SDL_RenderSetClipRect(r, &clip);
	for (k = 0; k < 2; k++) {
		float rc = mp4l_tape_r(f, k, CAS_HUB_R, CAS_PACK_R) / g->cx;
		int i = mp4t_pack_half(rc) - t->pack_first;
		const pt_sprite *s;

		if (i < 0) i = 0;
		if (i >= t->pack_n) i = t->pack_n - 1;
		s = &t->pack[k][i];
		SDL_RenderCopy(r, t->packs[k] ? t->packs[k] : t->packs[0], &s->src,
		               &(SDL_Rect){ x + (t->reel_ci[k] + s->ox) * u, y + (t->reel_cj + s->oy) * u, s->src.w * u,
		                            s->src.h * u });
	}
	for (k = 0; k < 2; k++) {
		const pt_sprite *s = &t->hub[mp4t_hub_frame(ang[k])];

		SDL_RenderCopy(r, t->hubs, &s->src,
		               &(SDL_Rect){ x + (t->reel_ci[k] + s->ox) * u, y + (t->reel_cj + s->oy) * u, s->src.w * u,
		                            s->src.h * u });
	}
	SDL_RenderSetClipRect(r, NULL);
	SDL_RenderCopy(r, t->front, NULL, &(SDL_Rect){ x, y, g->w, g->h });
}
