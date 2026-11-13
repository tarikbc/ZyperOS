/* SPDX-License-Identifier: GPL-3.0-only */
#include "gamelist.h"

#include "design.h"
#include "folderart.h"
#include "home.h"
#include "keyhints_draw.h"
#include "i18n.h"
#include "theme.h"
#include "ui.h"
#include "zyperdraw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The list's geometry, in the launcher's 1024 x 768 (DESIGN.md 11.39). The
 * heading and the foot are the cover row's, so turning View over moves
 * nothing but the middle. */
#define GL_X        40      /* the focus plate's left; the text's is 64 */
#define GL_W        552     /* to 592 */
#define GL_TOP      200     /* the first row's top, under the heading: room for
                             * the triangle that says more is above */
#define GL_ROW_H    56      /* a row: meta's 37 and room either side */
#define GL_MARK_X   80      /* a row's mark, centered here */
#define GL_NAME_X   120     /* a row's name */
#define GL_END_X    568     /* a row's right end: a folder's count, a heart */
#define GL_PANEL_X  640     /* the focused entry's panel, to 960 */
#define GL_PANEL_W  320
#define GL_COVER_H  320     /* the cover's box under the heading */
#define GL_FACT_Y   552     /* the first fact's capitals; the next 32 down */
#define GL_A_Y      664     /* A's hint, under three facts */
/* The mark of a game: a small card, its band in the system's color and the
 * name's first letter on it */
#define GL_MARK_W   36
#define GL_MARK_H   46

void gl_reset(gl_motion *m, int cursor, int count)
{
	int top = gl_window(cursor, count, -1);

	*m = (gl_motion){ .set = true, .cursor = cursor, .count = count,
	                  .top_from = (float)top, .top_to = (float)top,
	                  .plate_from = (float)cursor, .plate_to = (float)cursor };
}

int gl_window(int cursor, int count, int top)
{
	int most = count - GL_ROWS;

	if (count <= GL_ROWS) return 0;
	/* Placed again: the cursor in the middle, as far as the ends allow */
	if (top < 0) top = cursor - GL_ROWS / 2;
	/* Else moved only as far as the cursor needs, a row of the list kept
	 * beyond it so what comes next is in sight */
	if (cursor < top + 1) top = cursor - 1;
	if (cursor > top + GL_ROWS - 2) top = cursor - GL_ROWS + 2;
	if (top > most) top = most;
	if (top < 0) top = 0;
	return top;
}

static float lerp_out(float from, float to, uint32_t since)
{
	float k = (float)since / DS_MS_MOVE;

	if (k >= 1) return to;
	return from + (to - from) * ds_out(k);
}

bool gl_moving(const gl_motion *m, uint32_t now)
{
	return m->set && now - m->moved < DS_MS_MOVE &&
	       (m->top_from != m->top_to || m->plate_from != m->plate_to);
}

/* The cursor moved: both glide from where they are now to where it put them.
 * A jump of more than a row - a letter, a page, round past an end - shows the
 * place it lands at and a short glide into it, as the cover row's jumps do,
 * so no row races past. */
static void gl_follow(gl_motion *m, int cursor, int count, uint32_t now)
{
	float top, plate;
	int to;

	if (!m->set || m->count != count) { gl_reset(m, cursor, count); return; }
	if (m->cursor == cursor) return;
	top = lerp_out(m->top_from, m->top_to, now - m->moved);
	plate = lerp_out(m->plate_from, m->plate_to, now - m->moved);
	to = gl_window(cursor, count, (int)lroundf(m->top_to));
	if (abs(cursor - m->cursor) > 1) {
		int dir = cursor > m->cursor ? 1 : -1;

		top = (float)to - (to != (int)lroundf(m->top_to) ? dir * .2f : 0);
		plate = (float)cursor - dir * .2f;
	}
	m->top_from = top;
	m->top_to = (float)to;
	m->plate_from = plate;
	m->plate_to = (float)cursor;
	m->cursor = cursor;
	m->moved = now;
}

static void triangle(SDL_Renderer *r, float cx, float cy, float w, bool up, SDL_Color c)
{
	float h = w * .55f, d = up ? -1 : 1;
	SDL_Vertex v[3] = {
		{ { cx - w / 2, cy - d * h / 2 }, c, { 0, 0 } },
		{ { cx + w / 2, cy - d * h / 2 }, c, { 0, 0 } },
		{ { cx, cy + d * h / 2 }, c, { 0, 0 } },
	};

	SDL_RenderGeometry(r, NULL, v, 3, NULL, 0);
}

/* A game's mark: a little card, the theme's **surface** with an **edge**,
 * its band the system's mark color and the name's first letter or digit in
 * it - the generated card, small, so a list of games with no art and a list
 * with all of it read alike */
static void game_mark(SDL_Renderer *r, int cx, int cy, unsigned accent, const char *title)
{
	SDL_Rect card = { cx - GL_MARK_W / 2, cy - GL_MARK_H / 2, GL_MARK_W, GL_MARK_H };
	SDL_Rect inner = { card.x + 2, card.y + 2, card.w - 4, card.h - 4 };
	unsigned mark = ui_sys_mark(accent);
	SDL_Color mc = { (Uint8)(mark >> 16), (Uint8)(mark >> 8), (Uint8)mark, 255 };
	char ch[2] = { 0, 0 };
	const char *p = title ? title : "";
	TTF_Font *f = page_font(PAGE_ACTION);

	ui_round_rect(r, &card, 6, ui_col(theme()->edge, 255));
	ui_round_rect(r, &inner, 5, ui_col(theme()->surface, 255));
	SDL_SetRenderDrawColor(r, mc.r, mc.g, mc.b, 255);
	SDL_RenderFillRect(r, &(SDL_Rect){ inner.x + 3, inner.y + 3, inner.w - 6, 4 });
	while (*p && (unsigned char)*p <= ' ') p++;
	if (*p && (unsigned char)*p < 128) {
		ch[0] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
		ui_text(r, f, ch, cx, ui_cap_y(f, cy + 4), 0, mc);
	}
}

/* One row: its plate when focused, its mark, its name, and at its end a
 * folder's count or a favorite's heart */
static void gl_row(SDL_Renderer *r, const gl_info *in, void *ctx, int i, int y,
                   bool focus, uint32_t phase, uint32_t *wait)
{
	TTF_Font *f = ui_font(UI_F_META), *small = page_font(PAGE_CHROME);
	const char *name = in->title_of(ctx, i);
	int games = in->folder_of ? in->folder_of(ctx, i) : 0;
	int cy = y + GL_ROW_H / 2, end = GL_END_X, room;
	SDL_Color ink = focus ? ui_col(theme()->on_accent, 255) : UI_TEXT_SOFT;
	SDL_Color dim = focus ? ui_col(theme()->on_accent, 255) : UI_TEXT_DIM;

	if (games > 0) {
		char num[16];

		/* The folder in the system's color (src/folderart.h), two units a
		 * cell, as the card's is four */
		if (in->folder_icon)
			SDL_RenderCopy(r, in->folder_icon, NULL,
			               &(SDL_Rect){ GL_MARK_X - FA_ICON_W, cy - FA_ICON_H, FA_ICON_W * 2, FA_ICON_H * 2 });
		snprintf(num, sizeof num, "%d", games);
		end -= ui_text_tabular_width(small, num) + 24;
		ui_text_tabular(r, small, num, GL_END_X, ui_cap_y(small, cy), 1, dim);
	} else {
		game_mark(r, GL_MARK_X, cy, in->accent_of ? in->accent_of(ctx, i) : in->accent, name);
		if (in->fav_of && in->fav_of(ctx, i)) {
			float hr = 11;

			draw_heart(r, GL_END_X - hr, (float)cy, hr,
			           focus ? ui_col(theme()->on_accent, 255) : ui_col(theme()->favorite, 255));
			end -= (int)(hr * 2) + 16;
		}
	}
	room = end - GL_NAME_X;
	if (focus && ui_text_width(f, name) > room) {
		unsigned next;

		ui_text_marquee(r, f, name, GL_NAME_X, ui_cap_y(f, cy), room, phase, ink);
		next = ui_pingpong_wait(ui_text_width(f, name) - room, phase);
		if (next < *wait) *wait = next;
	} else {
		char fit[512];

		ui_fit_text(f, name, fit, sizeof fit, room);
		ui_text(r, f, fit, GL_NAME_X, ui_cap_y(f, cy), -1, ink);
	}
}

/* A fact under the cover, at most the panel wide */
static int gl_fact(SDL_Renderer *r, const char *s, int y)
{
	TTF_Font *f = page_font(PAGE_CHROME);
	char fit[256];

	if (!s || !*s) return y;
	ui_fit_text(f, s, fit, sizeof fit, GL_PANEL_W);
	ui_text(r, f, fit, GL_PANEL_X, ui_cap_y(f, y), -1, UI_TEXT_DIM);
	return y + 32;
}

uint32_t gl_draw(gl_motion *m, SDL_Renderer *r, const gl_info *in,
                 cf_tex_fn texture, void *ctx, uint32_t now, uint32_t phase)
{
	SDL_BlendMode blend;
	Uint8 red, green, blue, alpha;
	TTF_Font *meta = page_font(PAGE_META), *chrome = page_font(PAGE_CHROME);
	char count[40];
	uint32_t wait = UINT32_MAX;
	float top, plate;
	int countw, sw, room, first, i;

	SDL_GetRenderDrawBlendMode(r, &blend);
	SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	gl_follow(m, in->cursor, in->count, now);
	if (gl_moving(m, now)) wait = 0;
	top = lerp_out(m->top_from, m->top_to, now - m->moved);
	plate = lerp_out(m->plate_from, m->plate_to, now - m->moved);

	/* The heading and the count, as the cover row has them */
	snprintf(count, sizeof count, "%d / %d", in->count ? in->cursor + 1 : 0, in->count);
	countw = ui_text_tabular_width(chrome, count);
	sw = ui_text_width(meta, in->heading);
	room = 960 - 64 - countw - 32;
	if (sw > room) {
		unsigned next;

		ui_text_marquee(r, meta, in->heading, 64, ui_cap_y(meta, 152), room, phase, UI_TEXT_SOFT);
		next = ui_pingpong_wait(sw - room, phase);
		if (next < wait) wait = next;
	} else {
		ui_text(r, meta, in->heading, 64, ui_cap_y(meta, 152), -1, UI_TEXT_SOFT);
	}
	ui_text_tabular(r, chrome, count, 960, ui_cap_y(chrome, 152), 1, UI_TEXT_DIM);

	/* The rows, in a window the list scrolls through */
	SDL_RenderSetClipRect(r, &(SDL_Rect){ 0, GL_TOP, GL_PANEL_X - 16, GL_ROWS * GL_ROW_H });
	if (in->count > 0) {
		SDL_Rect pl = { GL_X, GL_TOP + (int)lroundf((plate - top) * GL_ROW_H), GL_W, GL_ROW_H };

		ui_round_rect(r, &pl, 14, ui_col(theme()->accent, 255));
	}
	first = (int)floorf(top);
	for (i = first; i <= first + GL_ROWS && i < in->count; i++) {
		int y = GL_TOP + (int)lroundf((i - top) * GL_ROW_H);

		if (i < 0) continue;
		gl_row(r, in, ctx, i, y, i == in->cursor, phase, &wait);
	}
	SDL_RenderSetClipRect(r, NULL);
	/* More above, more below: the menus' small triangles */
	if (top > 0.01f)
		triangle(r, GL_X + GL_W / 2.0f, GL_TOP - 10, 18, true, UI_TEXT_DIM);
	if (top + GL_ROWS < in->count - 0.01f)
		triangle(r, GL_X + GL_W / 2.0f, GL_TOP + GL_ROWS * GL_ROW_H + 10, 18, false, UI_TEXT_DIM);

	/* The focused entry: its card, whole, at the top of the panel, and its
	 * facts under it */
	if (in->count > 0) {
		int w = 0, h = 0, y = GL_FACT_Y;
		float cb = 1;
		SDL_Texture *t = texture(ctx, in->cursor, &w, &h, &cb);
		int games = in->folder_of ? in->folder_of(ctx, in->cursor) : 0;
		char line[96];

		if (t && w > 0 && h > 0) {
			float f = fminf((float)GL_PANEL_W / w, (float)GL_COVER_H / h);
			SDL_FRect dst = { GL_PANEL_X + (GL_PANEL_W - w * f) / 2, GL_TOP, w * f, h * f };
			float k = in->snap && in->snap_k > 0 && in->snap_w > 0 && in->snap_h > 0
			        ? fminf(in->snap_k, 1) : 0;
			Uint8 was;

			SDL_GetTextureAlphaMod(t, &was);
			SDL_SetTextureAlphaMod(t, (Uint8)lroundf(was * (1 - k)));
			if (k < 1) SDL_RenderCopyF(r, t, NULL, &dst);
			SDL_SetTextureAlphaMod(t, was);
			if (k > 0) {
				/* The cover turns into its video, as on the cover row: the
				 * snap, whole at its own shape in the panel's box, through a
				 * frame that grows from the cover's rect to its own */
				float sf = fminf((float)GL_PANEL_W / in->snap_w, (float)GL_COVER_H / in->snap_h);
				SDL_FRect vr = { GL_PANEL_X + (GL_PANEL_W - in->snap_w * sf) / 2, GL_TOP,
				                 in->snap_w * sf, in->snap_h * sf };
				SDL_Rect clip = { (int)floorf(dst.x + (vr.x - dst.x) * k),
				                  (int)floorf(dst.y + (vr.y - dst.y) * k),
				                  (int)ceilf(dst.w + (vr.w - dst.w) * k) + 1,
				                  (int)ceilf(dst.h + (vr.h - dst.h) * k) + 1 };

				SDL_RenderSetClipRect(r, &clip);
				SDL_SetTextureAlphaMod(in->snap, (Uint8)lroundf(255 * k));
				SDL_RenderCopyF(r, in->snap, in->snap_src.w > 0 ? &in->snap_src : NULL, &vr);
				SDL_SetTextureAlphaMod(in->snap, 255);
				SDL_RenderSetClipRect(r, NULL);
			}
			/* Its snap's mark, the cover row's, fading as the snap comes */
			if (in->has_snap && k < 1) {
				int d = 40, inset = 12;
				Uint8 ba = (Uint8)lroundf(255 * (1 - k));
				SDL_Rect disc = { (int)lroundf(dst.x + dst.w) - inset - d,
				                  (int)lroundf(dst.y + dst.h) - inset - d, d, d };

				ui_round_rect(r, &disc, d / 2, ui_col(theme()->accent, ba));
				ui_glyph_draw(r, UI_GLYPH_PLAY, disc.x + d / 2 + d / 16, disc.y + d / 2, d / 2,
				              ui_col(theme()->on_accent, ba));
			}
			/* And the wait before it, a bar under the cover filling */
			if (in->snap_wait > 0 && k < 1) {
				int bw = (int)lroundf(dst.w), bx = (int)lroundf(dst.x);
				int by = (int)lroundf(dst.y + dst.h) + 12, fw = (int)lroundf(bw * fminf(in->snap_wait, 1));
				Uint8 fa = (Uint8)lroundf(255 * (1 - k));

				ui_round_rect(r, &(SDL_Rect){ bx, by, bw, UI_BAR_H }, UI_BAR_H / 2, ui_col(theme()->track, fa));
				if (fw > 0)
					ui_round_rect(r, &(SDL_Rect){ bx, by, fw, UI_BAR_H }, UI_BAR_H / 2,
					              ui_col(theme()->accent_ink, fa));
			}
		}
		if (games > 0) {
			snprintf(line, sizeof line, TN(S_SHELF_GAMES, games), games);
			y = gl_fact(r, line, y);
		} else {
			y = gl_fact(r, in->origin, y);
			if (in->install) y = gl_fact(r, T(S_PORTMASTER_NOT_INSTALLED), y);
			if (in->played && *in->played) {
				snprintf(line, sizeof line, T(S_GAMES_PLAYED), in->played);
				y = gl_fact(r, line, y);
			}
			if (in->has_achievements) {
				snprintf(line, sizeof line, T(S_GAMES_ACHIEVEMENTS), in->earned, in->total);
				y = gl_fact(r, line, y);
			}
		}
		page_hint(r, "A", T(games > 0 ? S_GAMES_OPEN : in->install ? S_PORTMASTER_INSTALL
		                    : in->resumable ? S_GAMES_RESUME : S_GAMES_PLAY),
		          GL_PANEL_X, GL_A_Y, true, false);
	}
	/* The foot, as the cover row's (DESIGN.md 11.4): B, START back to the
	 * covers, Y and X for a game */
	{
		key_hint foot[4];
		int nf = 0, games = in->count > 0 && in->folder_of ? in->folder_of(ctx, in->cursor) : 0;

		foot[nf++] = (key_hint){ "B", T(in->inside ? S_GAMES_BACK : S_GAMES_SYSTEMS) };
		if (in->count > 0) foot[nf++] = (key_hint){ "START", T(S_GAMES_TO_COVERS) };
		if (in->count > 0 && in->resumable && !in->install && !games)
			foot[nf++] = (key_hint){ "Y", T(S_GAMES_FRESH) };
		if (in->count > 0 && !games) foot[nf++] = (key_hint){ "X", T(S_GAMES_DETAILS) };
		keyhints_draw(r, foot, nf, 64, 960, 720, KEYHINTS_SPREAD, true, 255);
	}
	SDL_SetRenderDrawColor(r, red, green, blue, alpha);
	SDL_SetRenderDrawBlendMode(r, blend);
	return wait;
}
