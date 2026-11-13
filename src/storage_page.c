/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Storage's page: src/storage_page.h says what it draws. Every size is
 * the Storage lab's (docs/design/mock/storage-lab.html), in the launcher's
 * 1024x768 units, from DESIGN.md's tokens. */
#include "storage_page.h"

#include "design.h"
#include "platform.h"
#include "theme.h"
#include "ui.h"
#include "zypermark.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SP_X0        64                     /* the numbers and the bar, from */
#define SP_X1        960                    /* ...to */
#define SP_HEAD_X0   200                    /* beside a system's sprite */
#define SP_HEAD_Y    56                     /* the heading's capitals */
#define SP_SUM_Y     128                    /* used and free */
#define SP_BAR_Y     172
#define SP_BAR_H     32
#define SP_GAP       DS_FRAME               /* between the bar's parts, on the ground */
#define SP_PART_MIN  4                      /* a part with anything in it is seen */
#define SP_LEG_Y     240                    /* the legend's capitals */
#define SP_LEG_LINE  44
#define SP_LEG_DOT   20
#define SP_LEG_GAP   26                     /* between the legend's items */
#define SP_LIST_X    48
#define SP_LIST_W    928
#define SP_LIST_Y    266
#define SP_ROWS      5
#define SP_RULE_H    16                     /* a rule and its room */
#define SP_PAD       28                     /* a row's text from its fill's edge */
#define SP_ICON      64
#define SP_NAME_X    112                    /* a row's name after its icon */
#define SP_CAP_Y     702                    /* the focused row's facts, one line */
#define SP_CAP_LINE  36                     /* ...two, from SP_CAP_Y - 10 */
#define SP_MORE      14                     /* the more-rows triangle */

static float row_h(void)
{
	return (float)ui_font_line(UI_F_MENU) * 1.5f;
}

int sp_rows_fit(void)
{
	return SP_ROWS;
}

static SDL_Color col(ds_rgb c)
{
	return ui_col(c, 255);
}

/* A row's top within the list, rules and all */
static float row_top(const sp_page *p, int i)
{
	float y = 0;
	int k;

	for (k = 0; k < i && k < p->nrows; k++) y += row_h() + (p->rows[k].rule_after ? SP_RULE_H : 0);
	return y;
}

static float list_h(const sp_page *p)
{
	return row_top(p, p->nrows);
}

static int legend_lines(const sp_page *p, int x0, int x1);

/* The list's window: SP_ROWS rows and a rule's room, a row less for each
 * line more the legend takes in a longer language */
static float window_h(const sp_page *p)
{
	int rows = SP_ROWS - (legend_lines(p, 24, 1000) - 1);

	return (rows < 2 ? 2 : rows) * row_h() + SP_RULE_H;
}

/* The legend's lines: how many it takes at the page's width */
static int legend_lines(const sp_page *p, int x0, int x1)
{
	TTF_Font *fs = ui_font(UI_F_META);
	int i, lines = 1, w = 0;

	for (i = 0; i < p->nparts; i++) {
		int iw;

		if (!p->part[i].name) continue;
		iw = SP_LEG_DOT + 10 + ui_text_width(fs, p->part[i].name);
		if (p->part[i].value[0]) iw += 12 + ui_text_tabular_width(fs, p->part[i].value);
		if (w && w + SP_LEG_GAP + iw > x1 - x0) { lines++; w = iw; }
		else w += (w ? SP_LEG_GAP : 0) + iw;
	}
	return lines;
}

static int list_y(const sp_page *p)
{
	return SP_LIST_Y + (legend_lines(p, 24, 1000) - 1) * SP_LEG_LINE;
}

static unsigned now_since(unsigned now, unsigned t)
{
	return now >= t ? now - t : 0;
}

static float glide(float from, float to, unsigned t)
{
	float k = t >= DS_MS_QUICK ? 1.0f : ds_out((float)t / DS_MS_QUICK);

	return from + (to - from) * k;
}

/* Where the cursor's row sits and where the window has to be for it, and
 * the glide to them from wherever they are now */
static void follow(const sp_page *p, sp_motion *m, unsigned now)
{
	float y = row_top(p, p->sel), wh = window_h(p), top, max;

	/* The window moves a whole row at a time, so its top is a row's top and
	 * no half a row stands under the legend */
	top = m->ready ? m->top_to : 0;
	if (y < top) top = y;
	if (y + row_h() > top + wh) {
		int i;

		for (i = 0; i <= p->sel; i++)
			if (y + row_h() - row_top(p, i) <= wh + 0.5f) { top = row_top(p, i); break; }
	}
	max = list_h(p) - wh;
	if (max > 0 && top > max) {
		/* At the end, the last rows fill the window from a row's top */
		int i;

		for (i = 0; i < p->nrows; i++)
			if (row_top(p, i) >= max - 0.5f) { top = row_top(p, i); break; }
	}
	if (max <= 0 || top < 0) top = 0;
	/* Nothing moved: the same row, where it was (the rows can move under
	 * the cursor too, as a recommendation comes or goes) */
	if (m->ready && m->sel == p->sel && fabsf(y - m->fill_to) < 0.5f && fabsf(top - m->top_to) < 0.5f)
		return;
	if (!m->ready) {
		m->fill_from = m->fill_to = y;
		m->top_from = m->top_to = top;
		m->t0 = 0;
		m->sel_at = now;
	} else {
		unsigned t = now_since(now, m->t0);

		m->fill_from = glide(m->fill_from, m->fill_to, t);
		m->top_from = glide(m->top_from, m->top_to, t);
		m->fill_to = y;
		m->top_to = top;
		m->t0 = now;
		if (m->sel != p->sel) m->sel_at = now;
	}
	m->sel = p->sel;
	m->ready = true;
}

bool sp_moving(const sp_motion *m, unsigned now)
{
	return m->ready && now_since(now, m->t0) < DS_MS_QUICK;
}

/* ---- the bar --------------------------------------------------------------- */

/* Diagonal stripes in `c` over `q`: a part not measured yet */
static void stripes(SDL_Renderer *r, const SDL_Rect *q, SDL_Color c)
{
	SDL_Rect was;
	bool had = SDL_RenderIsClipEnabled(r);
	int x;

	if (had) SDL_RenderGetClipRect(r, &was);
	SDL_RenderSetClipRect(r, q);
	for (x = q->x - q->h; x < q->x + q->w; x += 16) {
		SDL_Vertex v[4];
		int i;
		const int idx[6] = { 0, 1, 2, 0, 2, 3 };

		v[0].position = (SDL_FPoint){ (float)x, (float)(q->y + q->h) };
		v[1].position = (SDL_FPoint){ (float)(x + 8), (float)(q->y + q->h) };
		v[2].position = (SDL_FPoint){ (float)(x + 8 + q->h), (float)q->y };
		v[3].position = (SDL_FPoint){ (float)(x + q->h), (float)q->y };
		for (i = 0; i < 4; i++) { v[i].color = c; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
		SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
	}
	SDL_RenderSetClipRect(r, had ? &was : NULL);
}

/* One part, square where it meets another and round at the bar's ends */
static void part_draw(SDL_Renderer *r, int a, int b, int x0, int x1, const sp_part *pt)
{
	const ds_theme *th = theme();
	int rad = SP_BAR_H / 2;
	SDL_Rect q = { a, SP_BAR_Y, b - a, SP_BAR_H };

	if (q.w <= 0) return;
	if (pt->stripes) {
		SDL_Color dim = ui_col(th->text_dim, 115);

		stripes(r, &q, dim);
		return;
	}
	if (a <= x0 || b >= x1) {
		ui_round_rect(r, &q, rad, pt->col);
		/* Squared off where it meets the next part */
		if (a <= x0 && b < x1 && q.w > rad) {
			SDL_Rect sq = { b - rad, SP_BAR_Y, rad, SP_BAR_H };
			SDL_SetRenderDrawColor(r, pt->col.r, pt->col.g, pt->col.b, 255);
			SDL_RenderFillRect(r, &sq);
		}
		if (b >= x1 && a > x0 && q.w > rad) {
			SDL_Rect sq = { a, SP_BAR_Y, rad, SP_BAR_H };
			SDL_SetRenderDrawColor(r, pt->col.r, pt->col.g, pt->col.b, 255);
			SDL_RenderFillRect(r, &sq);
		}
	} else {
		SDL_SetRenderDrawColor(r, pt->col.r, pt->col.g, pt->col.b, 255);
		SDL_RenderFillRect(r, &q);
	}
}

static void bar_draw(SDL_Renderer *r, const sp_page *p, int x0, int x1)
{
	const ds_theme *th = theme();
	SDL_Rect whole = { x0, SP_BAR_Y, x1 - x0, SP_BAR_H };
	int w[SP_PARTS], i, at, sum = 0, big = -1, span = x1 - x0;
	unsigned long long total = p->whole;

	/* The track is the free room, under everything */
	ui_round_rect(r, &whole, SP_BAR_H / 2, col(th->track));
	if (!total) return;
	for (i = 0; i < p->nparts; i++) {
		double f = (double)p->part[i].bytes / (double)total;

		w[i] = p->part[i].bytes ? (int)(f * span + 0.5) : 0;
		if (p->part[i].bytes && w[i] < SP_PART_MIN) w[i] = SP_PART_MIN;
		sum += w[i];
		if (big < 0 || w[i] > w[big]) big = i;
	}
	/* Over the whole by rounding and the least widths: the largest gives */
	if (sum > span && big >= 0) w[big] -= sum - span;
	for (i = 0, at = x0; i < p->nparts; i++) {
		int b;

		if (!w[i]) continue;
		b = at + w[i];
		if (b > x1) b = x1;
		part_draw(r, at, b, x0, x1, &p->part[i]);
		at = b;
		/* The ground between it and what follows, the free track too */
		if (at < x1) {
			SDL_Rect g = { at, SP_BAR_Y, SP_GAP, SP_BAR_H };

			SDL_SetRenderDrawColor(r, th->ground.r, th->ground.g, th->ground.b, 255);
			SDL_RenderFillRect(r, &g);
			at += SP_GAP;
		}
	}
}

static void legend_draw(SDL_Renderer *r, const sp_page *p, int x0, int x1)
{
	const ds_theme *th = theme();
	TTF_Font *fs = ui_font(UI_F_META);
	int i, line = 0, start = 0;

	/* Line by line, each centered on the page */
	while (start < p->nparts) {
		int w = 0, end = start, x, cy = SP_LEG_Y + line * SP_LEG_LINE;

		for (i = start; i < p->nparts; i++) {
			int iw;

			if (!p->part[i].name) { if (i == end) end = i + 1; continue; }
			iw = SP_LEG_DOT + 10 + ui_text_width(fs, p->part[i].name);
			if (p->part[i].value[0]) iw += 12 + ui_text_tabular_width(fs, p->part[i].value);
			if (w && w + SP_LEG_GAP + iw > x1 - x0) break;
			w += (w ? SP_LEG_GAP : 0) + iw;
			end = i + 1;
		}
		if (end == start) end = start + 1;
		x = (ZYPEROS_SCREEN_W - w) / 2;
		for (i = start; i < end; i++) {
			const sp_part *pt = &p->part[i];
			SDL_Rect dot = { x, cy - SP_LEG_DOT / 2, SP_LEG_DOT, SP_LEG_DOT };

			if (!pt->name) continue;
			if (pt->stripes) {
				ui_round_rect(r, &dot, SP_LEG_DOT / 2, col(th->track));
				dot.x += 5; dot.y += 5; dot.w -= 10; dot.h -= 10;
				ui_round_rect(r, &dot, dot.w / 2, ui_col(th->text_dim, 160));
			} else {
				ui_round_rect(r, &dot, SP_LEG_DOT / 2, pt->col);
			}
			x += SP_LEG_DOT + 10;
			x += ui_text(r, fs, pt->name, x, ui_cap_y(fs, cy), -1, UI_TEXT_DIM);
			if (pt->value[0])
				x += 12 + ui_text_tabular(r, fs, pt->value, x + 12, ui_cap_y(fs, cy), -1, UI_TEXT_SOFT);
			x += SP_LEG_GAP;
		}
		start = end;
		line++;
	}
}

/* ---- the rows ------------------------------------------------------------- */

/* A recommendation's mark: two four-point sparkles */
static void spark(SDL_Renderer *r, float cx, float cy, SDL_Color c)
{
	static const float S[2][3] = { { -4, -4, 20 }, { 15, 15, 10 } };   /* x, y, reach */
	int s, i;

	for (s = 0; s < 2; s++) {
		SDL_Vertex v[9];
		int idx[24], n = 0;
		float x = cx + S[s][0], y = cy + S[s][1], k = S[s][2], in = k * 0.24f;

		v[0].position = (SDL_FPoint){ x, y };
		for (i = 0; i < 8; i++) {
			float ang = (float)i * 3.14159265f / 4.0f - 3.14159265f / 2.0f;
			float rad = (i % 2) ? in : k;

			v[i + 1].position = (SDL_FPoint){ x + cosf(ang) * rad, y + sinf(ang) * rad };
		}
		for (i = 0; i < 9; i++) { v[i].color = c; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
		for (i = 0; i < 8; i++) { idx[n++] = 0; idx[n++] = i + 1; idx[n++] = (i + 1) % 8 + 1; }
		SDL_RenderGeometry(r, NULL, v, 9, idx, n);
	}
}

static void row_draw(SDL_Renderer *r, const sp_page *p, int i, float y, bool sel, unsigned phase)
{
	const ds_theme *th = theme();
	const sp_row *row = &p->rows[i];
	TTF_Font *fm = ui_font(UI_F_MENU);
	float h = row_h(), mid = y + h / 2;
	SDL_Color name = sel ? col(th->on_accent) : UI_TEXT_SOFT;
	SDL_Color value = sel ? ui_col(th->on_accent, 184) : UI_TEXT_DIM;
	int x = SP_LIST_X + (row->icon == SP_ICON_NONE ? SP_PAD : SP_NAME_X);
	int vr = SP_LIST_X + SP_LIST_W - SP_PAD, vw = 0;
	float iy = mid - SP_ICON / 2.0f;

	switch (row->icon) {
	case SP_ICON_TEX:
		if (row->tex) {
			SDL_FRect dst = { (float)(SP_LIST_X + 24), iy, SP_ICON, SP_ICON };

			SDL_RenderCopyF(r, row->tex, NULL, &dst);
		}
		break;
	case SP_ICON_ZYPER:
		if (p->zyper) p->zyper(r, SP_LIST_X + 24 + SP_ICON / 2.0f, mid, 56.0f, 0.0f, false);
		break;
	case SP_ICON_SPARK:
		spark(r, SP_LIST_X + 24 + SP_ICON / 2.0f, mid, sel ? col(th->on_accent) : col(th->accent_ink));
		break;
	default:
		break;
	}
	if (row->value[0]) {
		vw = ui_text_tabular_width(fm, row->value);
		ui_text_tabular(r, fm, row->value, vr, ui_cap_y(fm, (int)(mid + 0.5f)), 1, value);
	}
	/* A name too long for its room slides on the focused row and rests at
	 * its start on the others, between two letters (ui_text_marquee) */
	ui_text_marquee(r, fm, row->name, x, ui_cap_y(fm, (int)(mid + 0.5f)),
	                vr - (vw ? vw + SP_PAD : 0) - x, sel ? phase : 0, name);
}

static void more_mark(SDL_Renderer *r, int y, bool up, SDL_Color c)
{
	SDL_Vertex v[3];
	int i;
	float cx = ZYPEROS_SCREEN_W / 2.0f, h = up ? 10.0f : SP_MORE;

	v[0].position = (SDL_FPoint){ cx - h, up ? y + h : y };
	v[1].position = (SDL_FPoint){ cx + h, up ? y + h : y };
	v[2].position = (SDL_FPoint){ cx, up ? y : y + h };
	for (i = 0; i < 3; i++) { v[i].color = c; v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
	SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

static void caption_draw(SDL_Renderer *r, const char *text, int y)
{
	TTF_Font *fs = ui_font(UI_F_META);
	char lines[2][LB_LINE];
	int n, k;

	if (!text || !text[0]) return;
	n = ui_balance_lines(fs, text, SP_X1 - SP_X0, 2, lines);
	/* Two lines sit higher, so the second clears the screen's inset and the
	 * first the more-rows mark */
	for (k = 0; k < n; k++)
		ui_text(r, fs, lines[k], ZYPEROS_SCREEN_W / 2,
		        ui_cap_y(fs, (n > 1 ? y - 10 : y) + k * SP_CAP_LINE), 0, UI_TEXT_DIM);
}

/* ---- the page ------------------------------------------------------------- */

void sp_draw(SDL_Renderer *r, const sp_page *p, sp_motion *m, unsigned now)
{
	const ds_theme *th = theme();
	TTF_Font *ft = ui_font(UI_F_TITLE), *fl = ui_font(UI_F_LABEL),
	         *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int x0 = p->head_tex ? SP_HEAD_X0 : SP_X0, x;

	/* The heading, and the card's size where a panel of pages keeps its
	 * count (DESIGN.md 11.3) */
	ui_text(r, fl, p->heading, ZYPEROS_SCREEN_W / 2, ui_cap_y(fl, SP_HEAD_Y), 0, UI_TEXT_SOFT);
	if (p->heading_right[0])
		ui_text_tabular(r, fs, p->heading_right, SP_X1, ui_cap_y(fs, SP_HEAD_Y), 1, UI_TEXT_DIM);

	/* A system's sprite where System Update has Zyper */
	if (p->head_tex) {
		SDL_Rect d = { 56, 92, 112, 112 };

		SDL_RenderCopy(r, p->head_tex, NULL, &d);
	}

	/* Used, its number as a title and its words in the format's places */
	x = x0;
	if (p->used_fmt && p->used[0]) {
		const char *at = strstr(p->used_fmt, "%s");
		char before[96] = "", after[96] = "";

		if (at) {
			snprintf(before, sizeof before, "%.*s", (int)(at - p->used_fmt), p->used_fmt);
			snprintf(after, sizeof after, "%s", at + 2);
		}
		if (before[0]) x += ui_text(r, fm, before, x, ui_cap_y(fm, SP_SUM_Y), -1, UI_TEXT_SOFT);
		x += ui_text_tabular(r, ft, p->used, x, ui_cap_y(ft, SP_SUM_Y), -1, UI_TEXT);
		if (after[0]) x += ui_text(r, fm, after, x, ui_cap_y(fm, SP_SUM_Y), -1, UI_TEXT_SOFT);
	}
	if (p->right[0]) {
		TTF_Font *f = fm;

		/* Where the two would meet, the room left steps down to meta, and
		 * under the used number if even that is too wide */
		if (x + 32 > SP_X1 - ui_text_tabular_width(fm, p->right)) f = fs;
		if (x + 32 > SP_X1 - ui_text_tabular_width(fs, p->right))
			ui_text_tabular(r, fs, p->right, SP_X1, ui_cap_y(fs, SP_SUM_Y + 26), 1, UI_TEXT_SOFT);
		else
			ui_text_tabular(r, f, p->right, SP_X1, ui_cap_y(f, SP_SUM_Y), 1, UI_TEXT_SOFT);
	}

	if (!p->whole && !p->measuring && p->nrows <= 0) return;   /* nothing known yet */
	bar_draw(r, p, x0, SP_X1);
	legend_draw(r, p, 24, 1000);

	if (p->measuring) {
		/* The first measure: Zyper, his head cocked, breathing, over what
		 * the walk is in */
		float bob = 2.0f * sinf((float)(now % DS_MS_ZYPER_BREATHE) * 6.2831853f / DS_MS_ZYPER_BREATHE);

		if (p->zyper) p->zyper(r, 512.0f, 398.0f + bob, 150.0f, ZYPER_TILT_DEG, true);
		if (p->measure_line)
			ui_text(r, fm, p->measure_line, ZYPEROS_SCREEN_W / 2, ui_cap_y(fm, 540), 0, UI_TEXT_SOFT);
		if (p->measure_at[0])
			ui_text_tabular(r, fs, p->measure_at, ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, 594), 0, UI_TEXT_DIM);
		caption_draw(r, p->note, SP_CAP_Y);
		return;
	}
	if (p->nrows <= 0) {
		caption_draw(r, p->note, SP_CAP_Y);
		return;
	}

	/* The list, in its window: the fill glides to the cursor's row and the
	 * window to keep it in view, both from where they are (DESIGN.md 8) */
	{
		int ly = list_y(p), i;
		float t, fill, top, y, wh = window_h(p), last = 0;
		bool moving = sp_moving(m, now);
		SDL_Rect clip = { SP_LIST_X, ly, SP_LIST_W, (int)(wh + 0.5f) };
		SDL_Rect was;
		bool had = SDL_RenderIsClipEnabled(r);

		follow(p, m, now);
		t = (float)now_since(now, m->t0);
		fill = glide(m->fill_from, m->fill_to, (unsigned)t);
		top = glide(m->top_from, m->top_to, (unsigned)t);
		if (had) SDL_RenderGetClipRect(r, &was);
		SDL_RenderSetClipRect(r, &clip);
		{
			SDL_Rect f = { SP_LIST_X, (int)(ly + fill - top + 0.5f), SP_LIST_W, (int)(row_h() + 0.5f) };

			ui_round_rect(r, &f, DS_RADIUS_ROW, col(th->accent));
		}
		for (i = 0, y = 0; i < p->nrows; i++) {
			float ry = ly + y - top;

			/* At rest only rows wholly in the window, so no sliver of the
			 * next one shows under the last; moving, all it crosses */
			if (moving ? (ry + row_h() >= ly && ry <= ly + wh)
			           : (ry >= ly - 0.5f && ry + row_h() <= ly + wh + 0.5f)) {
				row_draw(r, p, i, ry, i == p->sel, now_since(now, m->sel_at));
				if (ry + row_h() - ly > last) last = ry + row_h() - ly;
			}
			y += row_h();
			if (p->rows[i].rule_after) {
				SDL_Rect rule = { SP_LIST_X + SP_PAD, (int)(ly + y - top + (SP_RULE_H - DS_RULE) / 2.0f),
				                  SP_LIST_W - 2 * SP_PAD, DS_RULE };

				SDL_SetRenderDrawColor(r, th->edge.r, th->edge.g, th->edge.b, 255);
				SDL_RenderFillRect(r, &rule);
				y += SP_RULE_H;
			}
		}
		/* Rows above: the mark in the top row's own room, over its fill when
		 * that row is the cursor's; rows below: under the window */
		if (m->top_to > 0.5f) {
			bool on_fill = fabsf(fill - top) < 1.0f;

			more_mark(r, ly + 3, true, on_fill ? ui_col(th->on_accent, 140) : UI_TEXT_DIM);
		}
		SDL_RenderSetClipRect(r, had ? &was : NULL);
		if (m->top_to + wh < list_h(p) - 0.5f)
			more_mark(r, ly + (int)((moving ? wh - SP_RULE_H : last) + 0.5f) + 6, false, UI_TEXT_DIM);
		/* The list's order and the button that changes it, on the more
		 * mark's line at the right, clear of the line under it: X's glyph,
		 * then the order it is now (DESIGN.md 11.32) */
		if (p->sort_label) {
			int cy = ly + (int)(wh + 0.5f) + 14, gs = 28, tw = ui_text_width(fs, p->sort_label);
			int gw = ui_button_width("X", gs);

			ui_text(r, fs, p->sort_label, SP_X1, ui_cap_y(fs, cy), 1, UI_TEXT_DIM);
			ui_button(r, "X", SP_X1 - tw - 10 - gw / 2, cy, gs, UI_BUTTON_PLAIN);
		}
		caption_draw(r, p->rows[p->sel].caption, SP_CAP_Y);
	}
}
