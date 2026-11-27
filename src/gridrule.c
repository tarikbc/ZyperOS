/* SPDX-License-Identifier: GPL-3.0-only */
/* The grid's rule, pure: see gridrule.h. tools/gridrule-check.c holds it. */
#include "gridrule.h"

#include <math.h>
#include <stdlib.h>

static int group_of(float a)
{
	return a < GG_PORTRAIT_UNDER ? 0 : a <= GG_SQUARE_TO ? 1 : 2;
}

static int by_value(const void *x, const void *y)
{
	float a = *(const float *)x, b = *(const float *)y;

	return a < b ? -1 : a > b;
}

float gg_shape(const float *art, int n, int cutouts)
{
	float in[256];
	int count[3] = { 0, 0, 0 }, m, g, k;

	if (n <= 0) return cutouts > 0 ? GG_SHAPE_CUTOUTS : GG_SHAPE_NONE;
	if (n > 256) n = 256;
	for (k = 0; k < n; k++) count[group_of(art[k])]++;
	for (g = 0; g < 3; g++) {
		if (count[g] * 2 < n) continue;
		for (m = 0, k = 0; k < n; k++)
			if (group_of(art[k]) == g) in[m++] = art[k];
		qsort(in, (size_t)m, sizeof *in, by_value);
		return fminf(2.0f, fmaxf(.5f, in[m / 2]));
	}
	return 1.0f;
}

gg_geom gg_layout(int left, int top, int w, int h, float shape, int gap, int least)
{
	float chmax = (h - gap) / 2.0f, best = 1e9f, cw, ch;
	int pick = 0, c;
	gg_geom g;

	if (shape <= 0) shape = GG_SHAPE_NONE;
	for (c = 2; c <= 9; c++) {
		float off;

		cw = (float)(w - (c - 1) * gap) / c;
		ch = fminf(cw / shape, chmax);
		off = fabsf(logf(cw / ch / shape));
		if (GG_ROWS * c < least) continue;
		if (cw / ch <= shape * GG_NEAR) { pick = c; break; }
		if (off < best) { best = off; pick = c; }
	}
	cw = (float)(w - (pick - 1) * gap) / pick;
	ch = fminf(cw / shape, chmax);
	g = (gg_geom){ left, top, pick, GG_ROWS, (int)floorf(cw), (int)floorf(ch), gap };
	g.y = top + (h - (GG_ROWS * g.ch + (GG_ROWS - 1) * gap)) / 2;
	return g;
}

int gg_page(int cursor, int n, int page, int dir, bool fresh)
{
	if (n <= 0) return 0;
	if (cursor < 0) cursor = 0;
	if (cursor > n - 1) cursor = n - 1;
	if (page < 1) page = 1;
	if (dir > 0) return cursor >= n - 1 ? (fresh ? 0 : cursor) : cursor + page < n ? cursor + page : n - 1;
	if (dir < 0) return cursor <= 0 ? (fresh ? n - 1 : cursor) : cursor - page >= 0 ? cursor - page : 0;
	return cursor;
}

static int round_row(int row, int nrows)
{
	return (row % nrows + nrows) % nrows;
}

/* How far round the ends the rows are kept, before the window and after it:
 * GG_AHEAD, and one more round an end the window is at, since only there a
 * press comes round, and its glide in shows a sliver of the row past the
 * page it lands on */
static void reach(int top, int nrows, int *before, int *after)
{
	*before = GG_AHEAD + (top <= 0);
	*after = GG_AHEAD + (top + GG_ROWS >= nrows);
}

bool gg_row_kept(int row, int top, int nrows)
{
	int after, before, rb, ra;

	if (nrows <= 0 || row < 0 || row >= nrows) return false;
	if (row >= top && row < top + GG_ROWS) return true;
	reach(top, nrows, &rb, &ra);
	after = round_row(row - (top + GG_ROWS - 1), nrows);
	before = round_row(top - row, nrows);
	return (after >= 1 && after <= ra) || (before >= 1 && before <= rb);
}

int gg_rows_ahead(int top, int nrows, int *rows, int max)
{
	int near[2 * (GG_AHEAD + 1)], n = 0, rb, ra;

	if (nrows <= GG_ROWS || max <= 0) return 0;
	reach(top, nrows, &rb, &ra);
	/* nearest first, the row under the window before the one over it */
	for (int d = 1; d <= GG_AHEAD + 1; d++) {
		int cand[2] = { round_row(top + GG_ROWS - 1 + d, nrows), round_row(top - d, nrows) };
		bool in[2] = { d <= ra, d <= rb };

		for (int k = 0; k < 2; k++) {
			bool seen = !in[k] || (cand[k] >= top && cand[k] < top + GG_ROWS);

			for (int j = 0; j < n && !seen; j++) seen = near[j] == cand[k];
			if (!seen) near[n++] = cand[k];
		}
	}
	if (n > max) n = max;
	for (int j = 0; j < n; j++) rows[j] = near[n - 1 - j];
	return n;
}

unsigned gg_key(const char *file)
{
	unsigned h = 2166136261u;

	for (const unsigned char *p = (const unsigned char *)(file ? file : ""); *p; p++)
		h = (h ^ *p) * 16777619u;
	return h;
}

static Uint32 px_at(const SDL_Surface *s, int x, int y)
{
	return *(const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch + x * 4);
}

/* The home's steps: a still and how many rows it takes */
static const struct { int still, rows; } gh_step[GH_STEPS] = {
	{ 256, 1 }, { 176, 2 }, { 128, 2 }, { GH_STILL_FULL, 2 },
};

gh_geom gh_layout(int n, int left, int top, int w, int h, int gap)
{
	gh_geom g = { 0 };
	int k, used, bh, bw;

	if (n < 1) n = 1;
	g.n = n;
	g.gap = gap;
	for (k = 0; k < GH_STEPS; k++) {
		int fit = (w + gap) / (gh_step[k].still + GH_CELL_SIDE + gap);

		if (fit < 1 || fit * gh_step[k].rows < n) continue;
		g.still = gh_step[k].still;
		g.rows = (n + fit - 1) / fit;
		g.cols = (n + g.rows - 1) / g.rows;
		g.cw = g.still + GH_CELL_SIDE;
		g.win = g.rows;
		break;
	}
	if (k == GH_STEPS) {
		g.still = GH_STILL_FULL;
		g.cols = (w + gap) / (GH_CELL_MIN + gap);
		if (g.cols < 3) g.cols = 3;
		g.cw = (w - (g.cols - 1) * gap) / g.cols;
		g.rows = (n + g.cols - 1) / g.cols;
		g.win = GH_WIN_FULL;
	}
	g.name = g.still >= 256 ? GH_NAME_META : g.still >= 176 ? GH_NAME_SUPPORT : GH_NAME_CHROME;
	g.box = g.name == GH_NAME_META ? 40 : g.name == GH_NAME_SUPPORT ? 36 : 28;
	g.ch = g.still + GH_NAME_GAP + g.box;
	used = g.rows < g.win ? g.rows : g.win;
	if (k == GH_STEPS) used = g.win;
	bh = used * g.ch + (used - 1) * gap;
	bw = g.cols * g.cw + (g.cols - 1) * gap;
	g.x = left + (w - bw) / 2;
	g.y = top + (h - bh) / 2;
	return g;
}

void gh_cell(const gh_geom *g, int i, int *x, int *y)
{
	int row = i / g->cols, col = i % g->cols, in = g->cols;

	if (row == g->rows - 1) in = g->n - row * g->cols;
	*x = g->x + (g->cols - in) * (g->cw + g->gap) / 2 + col * (g->cw + g->gap);
	*y = g->y + row * (g->ch + g->gap);
}

int gh_row_step(const gh_geom *g, int cursor, int dir, int anchor)
{
	int row = cursor / g->cols + (dir < 0 ? -1 : 1), best = -1, near = 0;

	if (row < 0 || row >= g->rows) return -1;
	for (int i = row * g->cols; i < g->n && i < (row + 1) * g->cols; i++) {
		int x, y, d;

		gh_cell(g, i, &x, &y);
		d = abs(x + g->cw / 2 - anchor);
		/* within a unit is as near: a short row's half-cell offset
		 * rounds down, and the first of two still wins */
		if (best < 0 || d + 1 < near) { best = i; near = d; }
	}
	return best;
}

bool gg_cutout(const SDL_Surface *s)
{
	enum { N = 32 };
	int clear = 0, edge = 0, all = 0, x, y;

	if (!s || s->w <= 0 || s->h <= 0 || s->format->format != SDL_PIXELFORMAT_ARGB8888) return false;
	for (y = 0; y < N; y++)
		for (x = 0; x < N; x++) {
			int sx = (int)((x + .5f) * s->w / N), sy = (int)((y + .5f) * s->h / N);
			bool see = (px_at(s, sx, sy) >> 24) < 128;

			all += see;
			if (x > 1 && y > 1 && x < N - 2 && y < N - 2) continue;
			edge++;
			clear += see;
		}
	return clear * 4 > edge || all * 6 > N * N;
}

