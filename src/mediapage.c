/* SPDX-License-Identifier: GPL-3.0-only */
/* A game's Cover and Video page: src/mediapage.h says what it draws. Every
 * size is DESIGN.md 11.34's, as mocked and approved on 2026-11-09, in the launcher's
 * 1024x768 units, from DESIGN.md's tokens. */
#include "mediapage.h"

#include "config.h"
#include "design.h"
#include "home.h"
#include "linebreak.h"
#include "platform.h"
#include "theme.h"
#include "topbar.h"
#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define X0          64                 /* the page's left, Now's box */
#define X1          960                /* ...and right */
#define HEAD_Y      116                /* the headings' capitals */
#define CAP1_Y      420                /* the focused tile's kind */
#define CAP2_Y      462                /* ...and its source */
#define SMALL       0.75f              /* a tile without the cursor */
#define RING        7                  /* the focus fill round the focused tile */
#define RULE_Y      488
#define VHEAD_Y     524
#define VTILE_X     64
#define VTILE_Y     552
#define VTILE_W     256
#define VTILE_H     192
#define ROWS_X      344
#define ROWS_Y      504
#define ROW_PAD     28
#define MSG_Y       210                /* where the strip would be: a message */
#define BIG_X       368                /* Look Closer's picture */
#define BIG_Y       104
#define BIG_W       592
#define BIG_H       488
#define BIG_CAP1    640
#define BIG_CAP2    682
#define FOOT_Y      724

static SDL_Color col(ds_rgb c) { return ui_col(c, 255); }

static float row_h(void)
{
	return (float)ui_font_line(UI_F_MENU) * 1.5f;
}

/* A picture of shape w:h in a box, standing on its floor, centered across */
static SDL_FRect fit(float bx, float by, float bw, float bh, int w, int h)
{
	float a = (w > 0 && h > 0) ? (float)w / h : 0.75f;
	float fw = bw, fh = bw / a;

	if (fh > bh) { fh = bh; fw = bh * a; }
	return (SDL_FRect){ roundf(bx + (bw - fw) / 2), roundf(by + bh - fh), roundf(fw), roundf(fh) };
}

static SDL_Rect irect(SDL_FRect f)
{
	return (SDL_Rect){ (int)f.x, (int)f.y, (int)f.w, (int)f.h };
}

static void bar(SDL_Renderer *r, int x, int y, int w, float k)
{
	const ds_theme *th = theme();
	SDL_Rect t = { x, y, w, UI_BAR_H };

	ui_round_rect(r, &t, UI_BAR_H / 2, col(th->track));
	if (k > 0) {
		t.w = (int)lroundf(w * (k > 1 ? 1 : k));
		if (t.w < UI_BAR_H) t.w = UI_BAR_H;
		ui_round_rect(r, &t, UI_BAR_H / 2, col(th->accent_ink));
	}
}

static void chip(SDL_Renderer *r, int x, int y, const char *s)
{
	const ds_theme *th = theme();
	TTF_Font *f = page_font(PAGE_META);
	int w = ui_text_width(f, s) + 28, h = 44;
	SDL_Rect q = { x, y, w, h }, in = { x + DS_FRAME, y + DS_FRAME, w - 2 * DS_FRAME, h - 2 * DS_FRAME };

	ui_round_rect(r, &q, DS_RADIUS_CHIP, col(th->edge));
	ui_round_rect(r, &in, DS_RADIUS_CHIP - 2, col(th->surface));
	ui_text(r, f, s, x + 14, ui_cap_y(f, y + h / 2), -1, UI_TEXT_SOFT);
}

/* A glyph and its word, as the shelf's foot: A's on the accent */
static int hint_width(const char *key, const char *label)
{
	return ui_button_width(key, 40) + 12 + ui_text_width(page_font(PAGE_ACTION), label);
}

static void hint(SDL_Renderer *r, const char *key, const char *label, int x, int cy, bool primary)
{
	page_hint(r, key, label, x, cy, primary, false);
}

/* A tile, or the empty place of one that is coming or did not come */
static void tile_draw(SDL_Renderer *r, const mpg_tile *t, float x, float bw, float bh, bool big, bool ring)
{
	const ds_theme *th = theme();

	if (!t->tex) {
		/* The shape is not known yet: a box's, three quarters as wide */
		SDL_FRect f = { x, MPG_FLOOR - bh, roundf(bh * 0.78f), bh };
		SDL_Rect q = irect(f);

		ui_round_rect(r, &q, DS_RADIUS_CHIP * 2, col(th->raised));
		if (t->load >= 0 && !t->failed) bar(r, q.x + 18, MPG_FLOOR - 26, q.w - 36, t->load);
		return;
	}
	{
		SDL_FRect f = fit(x, MPG_FLOOR - bh, bw, bh, t->w, t->h);
		SDL_Rect q = irect(f);

		if (ring) {
			SDL_Rect g = { q.x - RING, q.y - RING, q.w + 2 * RING, q.h + 2 * RING };

			ui_lift(r, &g, CFG_NO_COLOR, UI_LIFT_FOCUS_SMALL);
			ui_round_rect(r, &g, DS_RADIUS_CARD, col(th->accent));
		}
		SDL_SetTextureAlphaMod(t->tex, big ? 255 : 210);
		SDL_RenderCopyF(r, t->tex, NULL, &f);
		SDL_SetTextureAlphaMod(t->tex, 255);
		if (t->chip) chip(r, q.x + 10, q.y + 10, t->chip);
	}
}

/* The strip's x for each tile from the first shown, and whether `i` is shown */
static int strip_first(const mpg_page *p)
{
	return p->focus > 0 ? p->focus - 1 : 0;
}

bool mpg_tile_shown(const mpg_page *p, int i)
{
	float x = MPG_STRIP_X;
	int k;

	for (k = strip_first(p); k < p->ntiles && x < ZYPEROS_SCREEN_W; k++) {
		if (k == i) return true;
		x += (k == p->focus ? MPG_BOX_W : MPG_BOX_W * SMALL) + MPG_GAP;
	}
	return false;
}

static void strip_draw(SDL_Renderer *r, const mpg_page *p)
{
	float x = MPG_STRIP_X;
	int k;

	for (k = strip_first(p); k < p->ntiles && x < ZYPEROS_SCREEN_W; k++) {
		bool big = k == p->focus;
		float bw = big ? MPG_BOX_W : MPG_BOX_W * SMALL, bh = big ? MPG_BOX_H : MPG_BOX_H * SMALL;
		const mpg_tile *t = &p->tile[k];

		tile_draw(r, t, x, bw, bh, big, big && p->strip_focus);
		x += (t->tex ? bw : roundf(bh * 0.78f)) + MPG_GAP;
	}
}

/* Where the strip would be: a line, then meta lines, Zyper at the right */
static void message_draw(SDL_Renderer *r, const mpg_page *p, unsigned now)
{
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int x = MPG_STRIP_X, y = MSG_Y, k, room = X1 - x;
	char lines[3][LB_LINE];

	{
		/* The line on one line where it fits, else on two, balanced */
		char first[2][LB_LINE];
		int nf = ui_text_width(fm, p->msg) <= room ? 0 : ui_balance_lines(fm, p->msg, room, 2, first);

		if (nf == 0) {
			ui_text(r, fm, p->msg, x, ui_cap_y(fm, y), -1, UI_TEXT_SOFT);
			y += 58;
		}
		for (k = 0; k < nf; k++, y += k < nf ? 52 : 58)
			ui_text(r, fm, first[k], x, ui_cap_y(fm, y), -1, UI_TEXT_SOFT);
	}
	if (p->msg_wrap) {
		int n = ui_balance_lines(fs, p->msg_wrap, room - 120, 3, lines);

		for (k = 0; k < n; k++, y += 42) ui_text(r, fs, lines[k], x, ui_cap_y(fs, y), -1, UI_TEXT_DIM);
	}
	for (k = 0; k < p->nmsg_lines; k++, y += 42)
		ui_text(r, fs, p->msg_lines[k], x, ui_cap_y(fs, y), -1, UI_TEXT_DIM);
	if (p->zyper) {
		float bob = 2.0f * sinf((float)(now % DS_MS_ZYPER_BREATHE) * 6.2831853f / DS_MS_ZYPER_BREATHE);

		p->zyper(r, 904.0f, 412.0f + bob, 96.0f, 0.0f, true);
	}
}

static void rows_draw(SDL_Renderer *r, const mpg_page *p)
{
	const ds_theme *th = theme();
	TTF_Font *fm = ui_font(UI_F_MENU);
	float h = row_h();
	int i;

	for (i = 0; i < p->nrows; i++) {
		const mpg_row *row = &p->rows[i];
		bool sel = i == p->row;
		int y = (int)lroundf(ROWS_Y + i * h), mid = (int)lroundf(ROWS_Y + i * h + h / 2);
		SDL_Color name = sel ? col(th->on_accent) : row->live ? UI_TEXT_SOFT : UI_TEXT_DIM;
		SDL_Color value = sel ? ui_col(th->on_accent, 184) : UI_TEXT_DIM;

		if (sel) {
			SDL_Rect f = { ROWS_X, y, X1 - ROWS_X, (int)lroundf(h) };

			ui_round_rect(r, &f, DS_RADIUS_ROW, col(th->accent));
		}
		ui_text(r, fm, row->name, ROWS_X + ROW_PAD, ui_cap_y(fm, mid), -1, name);
		if (row->value && row->value[0]) {
			/* Where the label and the value would meet, the value falls to
			 * meta, as a tile's words fall to the smaller size */
			int room = X1 - ROWS_X - 2 * ROW_PAD - ui_text_width(fm, row->name) - ROW_PAD;
			TTF_Font *fv = ui_text_tabular_width(fm, row->value) <= room ? fm : ui_font(UI_F_META);

			ui_text_tabular(r, fv, row->value, X1 - ROW_PAD, ui_cap_y(fv, mid), 1, value);
		}
	}
}

static void video_draw(SDL_Renderer *r, const mpg_page *p)
{
	const ds_theme *th = theme();
	TTF_Font *fs = ui_font(UI_F_META);
	SDL_Rect rule = { X0, RULE_Y, X1 - X0, DS_RULE }, t = { VTILE_X, VTILE_Y, VTILE_W, VTILE_H };

	SDL_SetRenderDrawColor(r, th->edge.r, th->edge.g, th->edge.b, 255);
	SDL_RenderFillRect(r, &rule);
	if (p->video_head) ui_text(r, fs, p->video_head, X0, ui_cap_y(fs, VHEAD_Y), -1, UI_TEXT_DIM);
	if (p->vid_tex) {
		/* The video's own shape inside the 4:3 tile, never stretched */
		SDL_FRect f = fit(t.x, t.y, t.w, t.h, p->vid_w, p->vid_h);
		SDL_Rect q = irect(f);

		/* Standing at the tile's left, under its heading, and in its middle */
		f.x = t.x;
		f.y = t.y + (t.h - f.h) / 2;
		q.x = (int)f.x;
		q.y = (int)f.y;
		if (p->row >= 0) ui_lift(r, &q, CFG_NO_COLOR, UI_LIFT_FOCUS_SMALL);
		SDL_RenderCopyF(r, p->vid_tex, p->vid_src.w > 0 ? &p->vid_src : NULL, &f);
		if (p->vid_disc) {
			int d = 40;
			SDL_Rect disc = { q.x + q.w - 12 - d, q.y + q.h - 12 - d, d, d };

			ui_round_rect(r, &disc, d / 2, col(th->accent));
			ui_glyph_draw(r, UI_GLYPH_PLAY, disc.x + d / 2 + d / 16, disc.y + d / 2, d / 2, col(th->on_accent));
		}
	} else {
		char lines[2][LB_LINE];
		int n, k;

		ui_round_rect(r, &t, DS_RADIUS_CHIP * 2, col(th->raised));
		if (p->vid_status) {
			n = ui_balance_lines(fs, p->vid_status, VTILE_W - 32, 2, lines);
			for (k = 0; k < n; k++)
				ui_text(r, fs, lines[k], t.x + t.w / 2,
				        ui_cap_y(fs, t.y + t.h / 2 + (k * 2 - (n - 1)) * 21), 0, UI_TEXT_DIM);
		}
	}
	if (p->vid_load >= 0) bar(r, t.x + 20, t.y + t.h - 24, t.w - 40, p->vid_load);
	rows_draw(r, p);
	/* No rows: why, where they would be, as the strip's message says why
	 * it has no pictures (2026-11-09: Download stood there with nothing to
	 * download) */
	if (!p->nrows && p->vid_note) {
		TTF_Font *fm = ui_font(UI_F_META);
		char lines[3][LB_LINE];
		int n = ui_balance_lines(fm, p->vid_note, X1 - ROWS_X - ROW_PAD, 3, lines), k;

		for (k = 0; k < n; k++)
			ui_text(r, fm, lines[k], ROWS_X, ui_cap_y(fm, VTILE_Y + 24 + k * 42), -1, UI_TEXT_DIM);
	}
}

static void title_draw(SDL_Renderer *r, const mpg_page *p)
{
	TTF_Font *fh = page_font(PAGE_ACTION);
	int half = p->title_half > 0 ? p->title_half : 300, tw;

	if (!p->title) return;
	tw = ui_text_width(fh, p->title);
	if (tw <= 2 * half)
		ui_text(r, fh, p->title, ZYPEROS_SCREEN_W / 2, ui_cap_y(fh, TOPBAR_MID), 0, UI_TEXT);
	else
		ui_text_marquee(r, fh, p->title, ZYPEROS_SCREEN_W / 2 - half, ui_cap_y(fh, TOPBAR_MID),
		                2 * half, p->title_phase, UI_TEXT);
}

static void now_draw(SDL_Renderer *r, const mpg_page *p, float floor)
{
	TTF_Font *fs = ui_font(UI_F_META);

	if (p->now_head) ui_text(r, fs, p->now_head, X0, ui_cap_y(fs, HEAD_Y), -1, UI_TEXT_DIM);
	if (p->now_tex) {
		SDL_FRect f = fit(X0, floor - MPG_BOX_H, MPG_BOX_W, MPG_BOX_H, p->now_w, p->now_h);

		SDL_RenderCopyF(r, p->now_tex, NULL, &f);
	}
}

static void dots(SDL_Renderer *r, int x, int cy, int step)
{
	const ds_theme *th = theme();
	int i;

	for (i = 0; i < 3; i++) {
		SDL_Rect d = { x + i * 18, cy - 5, 10, 10 };

		ui_round_rect(r, &d, 5, i <= step % 4 && step % 4 < 3 ? col(th->text_soft) : col(th->track));
	}
}

static void big_draw(SDL_Renderer *r, const mpg_page *p)
{
	const ds_theme *th = theme();
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	float floor = BIG_Y + BIG_H;
	int cx = BIG_X + BIG_W / 2;

	/* Now, the same size as on the page, standing on the big one's floor */
	if (p->now_tex) {
		SDL_FRect f = fit(X0, floor - MPG_BOX_H, MPG_BOX_W, MPG_BOX_H, p->now_w, p->now_h);

		if (p->now_head) ui_text(r, fs, p->now_head, X0, ui_cap_y(fs, (int)f.y - 30), -1, UI_TEXT_DIM);
		SDL_RenderCopyF(r, p->now_tex, NULL, &f);
		{
			/* A chevron: from Now to this one */
			int ax = (int)(f.x + f.w) + 12, ay = (int)(f.y + f.h / 2);
			SDL_Vertex v[3];
			int i;

			v[0].position = (SDL_FPoint){ (float)ax, (float)ay - 12 };
			v[1].position = (SDL_FPoint){ (float)ax + 12, (float)ay };
			v[2].position = (SDL_FPoint){ (float)ax, (float)ay + 12 };
			for (i = 0; i < 3; i++) { v[i].color = col(th->text_dim); v[i].tex_coord = (SDL_FPoint){ 0, 0 }; }
			SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
		}
	}
	if (p->big_tex) {
		SDL_FRect f = fit(BIG_X, BIG_Y, BIG_W, BIG_H, p->big_w, p->big_h);
		SDL_Rect q;

		f.y = roundf(BIG_Y + (BIG_H - f.h) / 2);
		q = irect(f);
		ui_lift(r, &q, CFG_NO_COLOR, UI_LIFT_FOCUS);
		SDL_RenderCopyF(r, p->big_tex, NULL, &f);
		if (p->big_load >= 0 && p->big_load < 1) bar(r, q.x + 24, q.y + q.h + 16, q.w - 48, p->big_load);
	} else {
		SDL_Rect q = { BIG_X + BIG_W / 2 - 160, BIG_Y + BIG_H / 2 - 200, 320, 400 };

		ui_round_rect(r, &q, DS_RADIUS_CARD, col(th->raised));
		if (p->big_load >= 0) bar(r, q.x + 24, q.y + q.h - 30, q.w - 48, p->big_load);
	}
	if (p->cap1) {
		TTF_Font *f = ui_text_width(fm, p->cap1) <= BIG_W ? fm : fs;

		ui_text(r, f, p->cap1, cx, ui_cap_y(f, BIG_CAP1), 0, UI_TEXT);
	}
	if (p->cap2) ui_text(r, fs, p->cap2, cx, ui_cap_y(fs, BIG_CAP2), 0, UI_TEXT_DIM);
	if (p->back) hint(r, "B", p->back, X0, FOOT_Y, false);
	if (p->use) hint(r, "A", p->use, X1 - hint_width("A", p->use), FOOT_Y, true);
}

void mpg_draw(SDL_Renderer *r, const mpg_page *p, unsigned now)
{
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);

	title_draw(r, p);
	if (p->big) {
		big_draw(r, p);
		return;
	}

	now_draw(r, p, MPG_FLOOR);
	if (p->now_credit) ui_text(r, fs, p->now_credit, X0, ui_cap_y(fs, CAP1_Y), -1, UI_TEXT_DIM);

	if (p->found_head) ui_text(r, fs, p->found_head, MPG_STRIP_X, ui_cap_y(fs, HEAD_Y), -1, UI_TEXT_DIM);
	if (p->found_right[0]) {
		int x = X1, left = MPG_STRIP_X + (p->found_head ? ui_text_width(fs, p->found_head) : 0) + 32;
		const char *right = p->found_right;

		if (p->found_dots) {
			x -= 46;
			dots(r, x + 10, HEAD_Y, p->dots_step);
			x -= 12;
		}
		/* "Asking ScreenScraper" where it fits beside the heading, the
		 * source's name alone where it does not */
		if (p->found_right_short && x - ui_text_tabular_width(fs, right) < left) right = p->found_right_short;
		ui_text_tabular(r, fs, right, x, ui_cap_y(fs, HEAD_Y), 1,
		                p->found_dots ? UI_TEXT_SOFT : UI_TEXT_DIM);
	}
	if (p->msg) {
		message_draw(r, p, now);
	} else {
		int hw = p->hint && p->strip_focus ? hint_width("A", p->hint) + 24 : 0;

		strip_draw(r, p);
		/* The kind on one line in menu, or in meta where menu does not
		 * fit; the source beside A's hint, or its name alone where the
		 * artist's would meet it */
		if (p->cap1) {
			TTF_Font *f = ui_text_width(fm, p->cap1) <= X1 - MPG_STRIP_X ? fm : fs;

			ui_text(r, f, p->cap1, MPG_STRIP_X, ui_cap_y(f, CAP1_Y), -1, UI_TEXT);
		}
		if (p->cap2) {
			const char *c2 = p->cap2;

			if (p->cap2_short && ui_text_width(fs, c2) > X1 - MPG_STRIP_X - hw) c2 = p->cap2_short;
			ui_text(r, fs, c2, MPG_STRIP_X, ui_cap_y(fs, CAP2_Y), -1, UI_TEXT_DIM);
		}
		if (hw) hint(r, "A", p->hint, X1 - hint_width("A", p->hint), CAP2_Y, true);
	}
	video_draw(r, p);
}
