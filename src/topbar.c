/* SPDX-License-Identifier: GPL-3.0-only */
/* See topbar.h. */
#include "topbar.h"

#include <stdint.h>
#include <string.h>

#include "design.h"
#include "home.h"
#include "look.h"
#include "theme.h"
#include "ui.h"

static bool marks_same(const status_marks *a, const status_marks *b)
{
	return a->wifi == b->wifi && a->bt == b->bt && a->batt == b->batt &&
	       a->charging == b->charging && a->held == b->held &&
	       a->blink_off == b->blink_off && a->pct_w == b->pct_w &&
	       a->calc == b->calc && a->calc_step == b->calc_step &&
	       a->xfer == b->xfer && a->xfer_moving == b->xfer_moving &&
	       a->xfer_pct == b->xfer_pct && a->phones == b->phones;
}

/* The marks as a texture: painted by status_marks_render when what they show,
 * the theme or the renderer changes - a minute's clock does not touch it, a
 * blink does, once a second - and blitted every frame at the row's right end.
 * One for the launcher, as the volume bar's is. */
static void marks_blit(SDL_Renderer *r, const status_marks *m, int theme)
{
	/* 192 for all five marks (File Transfer's and headphones' too) and the
	 * room of the battery's number or time, and the gap between them */
	static uint32_t      px[(STATUS_PAD * 2 + 192 + STATUS_PCT_GAP + STATUS_PCT_MAX) *
	                        (STATUS_PAD * 2 + STATUS_ROW)];
	static SDL_Texture  *tex;
	static SDL_Renderer *tex_for;
	static status_marks  painted;
	static int           painted_theme = -1, tw, th;
	static unsigned      painted_gen;
	int w, h, mw = status_marks_width(m);

	if (!mw) return;
	status_marks_size(m, 1.0f, &w, &h);
	if ((size_t)w * h > sizeof px / sizeof px[0]) return;
	if (tex_for != r || w != tw || h != th) {
		if (tex) SDL_DestroyTexture(tex);
		tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
		tex_for = NULL;
		if (!tex) return;
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		tex_for = r;
		tw = w;
		th = h;
		painted_theme = -1;
	}
	/* and again when the look changed under the same theme (look_gen) */
	if (theme != painted_theme || !marks_same(m, &painted) || look_gen() != painted_gen) {
		status_marks_render(px, w, h, w, 1.0f, theme, m);
		SDL_UpdateTexture(tex, NULL, px, w * 4);
		painted = *m;
		painted_theme = theme;
		painted_gen = look_gen();
	}
	SDL_RenderCopy(r, tex, NULL, &(SDL_Rect){ TOPBAR_RIGHT - mw - STATUS_PAD,
	                                          TOPBAR_TOP - STATUS_PAD, w, h });
}

void topbar_song_mark(SDL_Renderer *r, int x, int theme, bool paused)
{
	static uint32_t      px[(STATUS_NOTE + STATUS_PAD * 2) * (STATUS_ROW + STATUS_PAD * 2)];
	static SDL_Texture  *tex;
	static SDL_Renderer *tex_for;
	static int           painted = -1;     /* theme * 2 + paused */
	static unsigned      painted_gen;
	int w, h, want = theme * 2 + (paused ? 1 : 0);

	status_note_size(1.0f, &w, &h);
	if ((size_t)w * h > sizeof px / sizeof px[0]) return;
	if (tex_for != r) {
		if (tex) SDL_DestroyTexture(tex);
		tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
		tex_for = NULL;
		if (!tex) return;
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
		tex_for = r;
		painted = -1;
	}
	if (want != painted || look_gen() != painted_gen) {
		status_note_render(px, w, h, w, 1.0f, theme, paused);
		SDL_UpdateTexture(tex, NULL, px, w * 4);
		painted = want;
		painted_gen = look_gen();
	}
	SDL_RenderCopy(r, tex, NULL, &(SDL_Rect){ x - STATUS_PAD, TOPBAR_TOP - STATUS_PAD, w, h });
}

void topbar_draw(SDL_Renderer *r, topbar *t, topbar_drawn *out)
{
	const ds_theme *th = theme_at(t->theme);
	TTF_Font *fb = page_font(PAGE_CHROME), *fc = page_font(PAGE_ACTION);
	int mw;

	memset(out, 0, sizeof *out);
	/* MENU > Battery Icon > Percentage or Time Left: room left of the mark
	 * for "82%" or "3 h 20 min", digits in cells, so 9% to 10% moves only
	 * itself; the three dots' room while a time is on its way */
	t->marks.pct_w = 0;
	if (t->marks.calc) {
		t->marks.pct_w = STATUS_CALC_W;
	} else if (t->batt && t->batt[0]) {
		out->batt_natural = ui_text_tabular_width(fb, t->batt);
		t->marks.pct_w = out->batt_natural > STATUS_PCT_MAX ? STATUS_PCT_MAX : out->batt_natural;
	}
	marks_blit(r, &t->marks, t->theme);
	if (t->marks.pct_w > 0 && !t->marks.calc) {
		/* In the mark's own colour: the caller's, or warning when it is low */
		SDL_Color c = status_batt_level(t->marks.batt, t->marks.charging)
		            ? ui_col(th->warning, 255) : t->soft;

		ui_text_tabular(r, fb, t->batt, TOPBAR_RIGHT - STATUS_BATT_W - STATUS_PCT_GAP,
		                ui_cap_y(fb, TOPBAR_MID), 1, c);
	}
	mw = status_marks_width(&t->marks);
	out->marks_w = mw;
	out->marks = (SDL_Rect){ TOPBAR_RIGHT - mw, TOPBAR_TOP, mw, STATUS_ROW };
	if (t->clock && t->clock[0]) {
		/* Digits in cells, so 9:59 to 10:00 moves nothing but the hour */
		int w = ui_text_tabular(r, fc, t->clock, TOPBAR_CLOCK_X, ui_cap_y(fc, TOPBAR_MID), -1,
		                        ui_col(th->text, 255));

		out->clock = (SDL_Rect){ TOPBAR_CLOCK_X, TOPBAR_TOP, w, STATUS_ROW };
	}
}

int topbar_half(const topbar_drawn *d)
{
	int half = 1024 / 2, gap = DS_GRID * 3;

	if (d->clock.w && 1024 / 2 - (d->clock.x + d->clock.w + gap) < half)
		half = 1024 / 2 - (d->clock.x + d->clock.w + gap);
	if (d->marks.w && d->marks.x - gap - 1024 / 2 < half)
		half = d->marks.x - gap - 1024 / 2;
	return half < 0 ? 0 : half;
}
