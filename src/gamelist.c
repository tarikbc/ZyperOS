/* SPDX-License-Identifier: GPL-3.0-only */
#include "gamelist.h"

#include "artbadge_draw.h"

#include "design.h"
#include "folderart.h"
#include "games.h"        /* games_frame: the shelf's line and keys */
#include "home.h"
#include "keyhints_draw.h"
#include "i18n.h"
#include "theme.h"
#include "ui.h"
#include "listbox.h"     /* the list and its media, laid out by rules */
#include "ds/layout.h"
#include "ds/mark.h"
#include "ds/focus.h"
#include "look.h"
#include "zyperdraw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The list's boxes (src/listbox.h, DESIGN.md 11.39): the rows down the
 * left, the focused entry's media at the right as tall as they are, its
 * details over its foot */
#define GL_PLATE_OUT 24      /* the focus plate past the list's column at its left */
static const gl_boxes *gl_layout(void)
{
	static gl_boxes b;
	static bool done;

	if (!done) {
		b = gl_boxes_for(games_frame().sys, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H);
		done = true;
	}
	return &b;
}
#define GL_X        (gl_layout()->list.x - GL_PLATE_OUT)         /* the focus plate's left (40); the text's is 64 */
#define GL_W        (gl_layout()->list.w + GL_PLATE_OUT)         /* to 464 */
#define GL_TOP      (gl_layout()->list.y)                        /* the first row's top (200) */
#define GL_MARK_X   (gl_layout()->list.x + 16)                   /* a row's mark, centered here (80) */
#define GL_NAME_X   (gl_layout()->list.x + 56)                   /* a row's name (120) */
#define GL_END_X    (gl_layout()->list.x + gl_layout()->list.w - 24) /* a row's right end (440): a folder's count, a heart */
#define GL_PANEL_X  (gl_layout()->media.x)                       /* the media's box (512), to 960 */
#define GL_PANEL_W  (gl_layout()->media.w)
#define GL_FOOT     (gl_layout()->media.y + gl_layout()->media.h) /* the box's foot, the rows' (648) */
/* The details over the media's foot: the facts and A's hint, inset from the
 * picture's sides and the box's foot (gl_details_col), on the ground fading
 * up from it */
static int g_col_x, g_col_w;
#define GL_FACT_X   g_col_x
#define GL_FACT_W   g_col_w                                      /* 400 over a snap */
#define GL_FACT_STEP 32
#define GL_A_Y      (gl_layout()->a_y)                           /* A's hint (604) */
#define GL_A_GAP    (gl_layout()->a_gap)
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
                   bool focus, bool on_fill, uint32_t phase, uint32_t *wait)
{
	TTF_Font *f = ui_font(UI_F_META), *small = page_font(PAGE_CHROME);
	const char *name = in->title_of(ctx, i);
	int games = in->folder_of ? in->folder_of(ctx, i) : 0;
	int cy = y + GL_ROW_H / 2, end = GL_END_X, room;
	/* On the fill (Highlight's Bar) the focused row's words are on-accent;
	 * on a ring, a raised plate or nothing, **text** (src/ds/focus.h) */
	SDL_Color ink = focus ? (on_fill ? ui_col(theme()->on_accent, 255) : UI_TEXT) : UI_TEXT_SOFT;
	SDL_Color dim = focus && on_fill ? ui_col(theme()->on_accent, 255) : UI_TEXT_DIM;

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
	} else if (in->badge_of && in->badge_of(ctx, i) != AB_NONE) {
		/* What it is, in the mark's place (MP4's All): the row's badge */
		ab_draw(r, (ab_kind)in->badge_of(ctx, i), GL_MARK_X, cy, 1.0f, 255);
	} else {
		game_mark(r, GL_MARK_X, cy, in->accent_of ? in->accent_of(ctx, i) : in->accent, name);
		if (in->fav_of && in->fav_of(ctx, i)) {
			float hr = 11;

			draw_heart(r, GL_END_X - hr, (float)cy, hr,
			           focus && on_fill ? ui_col(theme()->on_accent, 255) : ui_col(theme()->favorite, 255));
			end -= (int)(hr * 2) + 16;
		}
	}
	/* None (Highlight): no plate, so the focused name stands in a little,
	 * in **text** */
	const int nx = GL_NAME_X + (focus && look_focus() == LOOK_FOCUS_NONE ? 12 : 0);

	room = end - nx;
	if (focus && ui_text_width(f, name) > room) {
		unsigned next;

		ui_text_marquee(r, f, name, nx, ui_cap_y(f, cy), room, phase, ink);
		next = ui_pingpong_wait(ui_text_width(f, name) - room, phase);
		if (next < *wait) *wait = next;
	} else {
		char fit[512];

		ui_fit_text(f, name, fit, sizeof fit, room);
		ui_text(r, f, fit, nx, ui_cap_y(f, cy), -1, ink);
	}
}

/* The details are laid out twice a frame: once only to count their lines,
 * so they stand on A's hint at the media's foot whatever their number, then
 * drawn; and drawn at an alpha, as they go down at rest (gl_details) */
static bool g_dry;
static Uint8 g_alpha = 255;
static ds_rgb g_warning;     /* the firmware line's color over the media (gl_details_ground) */

static void gl_say(const char *s)
{
	if (!g_dry) ui_fit_drawn(s);
}

/* A fact over the media's foot, at most the details' width */
static int gl_fact_in(SDL_Renderer *r, const char *s, int y, SDL_Color c)
{
	TTF_Font *f = page_font(PAGE_CHROME);
	char fit[256];

	if (!s || !*s) return y;
	if (!g_dry) {
		ui_fit_text(f, s, fit, sizeof fit, GL_FACT_W);
		c.a = (Uint8)(c.a * g_alpha / 255);
		ui_text(r, f, fit, GL_FACT_X, ui_cap_y(f, y), -1, c);
	}
	return y + GL_FACT_STEP;
}

/* In **text-soft**, a step up from the ground's **text-dim**: it needs
 * the least ground under it to keep 4.5:1 over any picture (about 75%, where
 * text-dim needs 87 to 94%) */
static int gl_fact(SDL_Renderer *r, const char *s, int y)
{
	return gl_fact_in(r, s, y, UI_TEXT_SOFT);
}

/* A fact that may take two lines, balanced, in the panel's width: the
 * firmware a game needs ("Nécessite disksys.rom dans Bios" is wider than
 * the panel), never cut */
static int gl_fact_lines(SDL_Renderer *r, const char *s, int y, SDL_Color c)
{
	TTF_Font *f = page_font(PAGE_CHROME);
	char lines[2][LB_LINE];
	int n, i;

	if (!s || !*s) return y;
	if (ui_text_width(f, s) <= GL_FACT_W) return gl_fact_in(r, s, y, c);
	n = ui_balance_lines(f, s, GL_FACT_W, 2, lines);
	if (n < 2) return gl_fact_in(r, s, y, c);
	gl_say(s);    /* drawn in lines, said whole to the host's check */
	for (i = 0; i < n; i++) y = gl_fact_in(r, lines[i], y, c);
	return y;
}

/* The first fact, the line under the name elsewhere: on Recents a game's
 * system and when it was last played, on Screenshots when the shot was
 * taken ("Game Boy Advance · today 09:12"). Wider than the panel, it takes
 * two lines at its last dot, the system and then the time, where the facts
 * after it leave the room (`two`); where they do not, the system's name is
 * the part cut. The time is never cut. */
static int gl_fact_origin(SDL_Renderer *r, const char *s, int y, bool two)
{
	static const char DOT[] = " \xc2\xb7 ";
	TTF_Font *f = page_font(PAGE_CHROME);
	const char *dot = NULL, *p;
	char head[256], line[512];
	int tail;

	if (!s || !*s || ui_text_width(f, s) <= GL_FACT_W) return gl_fact(r, s, y);
	for (p = s; (p = strstr(p, DOT)); p++) dot = p;
	if (!dot || dot == s || !dot[sizeof DOT - 1]) return gl_fact(r, s, y);
	gl_say(s);    /* drawn in parts, said whole to the host's check */
	snprintf(head, sizeof head, "%.*s", (int)(dot - s), s);
	if (two) {
		y = gl_fact(r, head, y);
		return gl_fact(r, dot + sizeof DOT - 1, y);
	}
	tail = ui_text_width(f, dot);
	if (tail >= GL_FACT_W) return gl_fact(r, s, y);
	ui_fit_text(f, head, line, sizeof line, GL_FACT_W - tail);
	snprintf(line + strlen(line), sizeof line - strlen(line), "%s", dot);
	if (!g_dry) {
		SDL_Color c = UI_TEXT_SOFT;

		c.a = (Uint8)(c.a * g_alpha / 255);
		ui_text(r, f, line, GL_FACT_X, ui_cap_y(f, y), -1, c);
	}
	return y + GL_FACT_STEP;
}

/* Played, wider than the panel where `two` leaves room: its word on one
 * line and the time under it ("Tiempo de juego:" over "12 h 34 min"), as
 * the string puts the time last in every language; a string with words
 * after the time is balanced over two lines instead */
static int gl_fact_played(SDL_Renderer *r, const char *played, int y, bool two)
{
	TTF_Font *f = page_font(PAGE_CHROME);
	const char *fmt = T(S_GAMES_PLAYED), *at = strstr(fmt, "%s");
	char line[128], head[96];
	size_t hn;

	snprintf(line, sizeof line, fmt, played);
	if (!two || ui_text_width(f, line) <= GL_FACT_W) return gl_fact(r, line, y);
	if (!at || at[2] || (hn = (size_t)(at - fmt)) == 0) return gl_fact_lines(r, line, y, UI_TEXT_SOFT);
	while (hn > 0 && fmt[hn - 1] == ' ') hn--;
	snprintf(head, sizeof head, "%.*s", (int)hn, fmt);
	gl_say(line);    /* drawn in lines, said whole to the host's check */
	y = gl_fact(r, head, y);
	return gl_fact(r, played, y);
}

/* The facts, from `y` down, one GL_FACT_STEP a line; where the next line
 * would be. Three fit as they always did: a fact takes a second line only
 * where the ones after it still fit in three. */
static int gl_facts(SDL_Renderer *r, const gl_info *in, int games, int y)
{
	TTF_Font *chrome = page_font(PAGE_CHROME);
	char line[96];

	if (games > 0) {
		snprintf(line, sizeof line, TN(S_SHELF_GAMES, games), games);
		return gl_fact(r, line, y);
	}
	bool cheats = in->cheats && *in->cheats;
	int later = (cheats ? 1 + in->cheats_crashed : 0) + in->has_achievements;
	int played = in->played && *in->played;
	int needs = in->needs && *in->needs ? 1 + (ui_text_width(chrome, in->needs) > GL_FACT_W) : 0;
	const int last = y + 2 * GL_FACT_STEP;

	y = gl_fact_origin(r, in->origin, y,
	                   y + (1 + (in->install || in->howto) + needs + played + later) * GL_FACT_STEP <= last);
	if (in->install || in->howto) y = gl_fact(r, T(S_PORTMASTER_NOT_INSTALLED), y);
	/* Firmware it cannot start without, in **warning** */
	y = gl_fact_lines(r, in->needs, y, ui_col(g_warning, 255));
	/* Played on two lines where they fit: Spanish's "Tiempo de juego: 12 h
	 * 34 min" is wider than the details */
	if (played) y = gl_fact_played(r, in->played, y, y + (1 + later) * GL_FACT_STEP <= last);
	if (in->has_achievements) {
		snprintf(line, sizeof line, T(S_GAMES_ACHIEVEMENTS), in->earned, in->total);
		y = gl_fact(r, line, y);
	}
	/* and how many of its cheats are on (DESIGN.md 11.66) */
	if (cheats) y = gl_fact(r, in->cheats, y);
	if (cheats && in->cheats_crashed) y = gl_fact(r, T(S_CHEATS_SHELF_CRASHED_LINE), y);
	return y;
}

/* The ground over the picture's foot, as the Video view's over the top of
 * its snap (DESIGN.md 11.57): none at `from`, `alpha` at `full` and on to
 * the box's foot, across the picture */
static void gl_ground(SDL_Renderer *r, SDL_FRect pic, int from, int full, Uint8 alpha)
{
	const ds_rgb g = theme()->ground;
	const float x0 = pic.x, x1 = pic.x + pic.w;
	const SDL_Color none = { g.r, g.g, g.b, 0 }, on = { g.r, g.g, g.b, alpha };
	const SDL_Vertex v[6] = {
		{ { x0, (float)from }, none, { 0, 0 } }, { { x1, (float)from }, none, { 0, 0 } },
		{ { x1, (float)full }, on, { 0, 0 } },   { { x0, (float)full }, on, { 0, 0 } },
		{ { x1, (float)GL_FOOT }, on, { 0, 0 } }, { { x0, (float)GL_FOOT }, on, { 0, 0 } },
	};
	const int idx[12] = { 0, 1, 2, 0, 2, 3, 3, 2, 4, 3, 4, 5 };

	if (alpha > 0) SDL_RenderGeometry(r, NULL, v, 6, idx, 12);
}

/* How much ground the details' lines need under them (src/listbox.h): as
 * much as **text-soft** needs to keep 4.5:1 over any picture, and with a
 * firmware line in **warning** as much as it needs, up to the Video view's
 * band's 92%; past that the warning line is walked toward **text** as far
 * as it must (g_warning). Worked out from the theme that is on, its tone and
 * all, once a look. */
#define GL_GROUND_MOST 235   /* 92%, the Video view's band's surface */
static int gl_details_ground(const gl_info *in, int games)
{
	static unsigned gen = ~0u;
	static int tone = -1, soft, warn;
	const ds_theme *th = theme();

	if (gen != look_gen() || tone != theme_index()) {
		gen = look_gen();
		tone = theme_index();
		soft = gl_ground_alpha(th->ground, th->text_soft);
		warn = gl_ground_alpha(th->ground, th->warning);
		if (warn < soft) warn = soft;
		if (warn > GL_GROUND_MOST) warn = GL_GROUND_MOST;
		g_warning = gl_line_on(th->ground, warn, th->warning, th->text);
	}
	return !games && in->needs && *in->needs ? warn : soft;
}

/* The details over the media's foot (my pick in the List View lab,
 * 2026-11-23: "show the details on top of the video in a very subtle way,
 * maybe with a gradient, and collapse the details in 2s so u can see the
 * full media"): over the picture at `pic` (its frame, as the cover turns
 * into the snap), A's hint inset from the box's foot and the picture's
 * left, the facts standing on it, on the ground fading up from the foot
 * over GL_FADE above them. At rest, `down` of the way: they fade and sink
 * a DS_INSET as they go. */
#define GL_FADE (2 * GL_FACT_STEP)
static void gl_details(SDL_Renderer *r, const gl_info *in, int games, float down, SDL_FRect pic)
{
	const int dy = (int)lroundf(DS_INSET * down);
	int lines, y, top, ground;

	if (down >= 1) return;
	gl_details_col(pic, &g_col_x, &g_col_w);
	ground = gl_details_ground(in, games);
	g_dry = true;
	lines = gl_facts(r, in, games, 0) / GL_FACT_STEP;
	g_dry = false;
	y = GL_A_Y - GL_A_GAP - (lines - 1) * GL_FACT_STEP;
	top = lines > 0 ? y - GL_FACT_STEP / 2 : GL_A_Y - DS_HINT_GLYPH_FOCUS / 2;
	g_alpha = (Uint8)lroundf(255 * (1 - down));
	gl_ground(r, pic, top - GL_FADE, top, (Uint8)(ground * g_alpha / 255));
	if (lines > 0) gl_facts(r, in, games, y + dy);
	page_hint_fade(r, "A", in->a_word ? in->a_word
	                       : T(games > 0 || in->opens ? S_GAMES_OPEN : in->howto ? S_PICO8_HOW_TO_ADD
	                           : in->install ? S_PORTMASTER_INSTALL
	                           : in->resumable ? S_GAMES_RESUME : S_GAMES_PLAY),
	               GL_FACT_X, GL_A_Y + dy, true, false, g_alpha);
	g_alpha = 255;
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
	const ds_focus_marks mk = ds_focus_marks_of(DS_FOCUS_COVER);

	SDL_GetRenderDrawBlendMode(r, &blend);
	SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	gl_follow(m, in->cursor, in->count, now);
	if (gl_moving(m, now)) wait = 0;
	top = lerp_out(m->top_from, m->top_to, now - m->moved);
	plate = lerp_out(m->plate_from, m->plate_to, now - m->moved);

	/* The heading and the count, as the cover row has them (games_frame) */
	const shelf_frame sf = games_frame();
	const int sys_cy = sf.sys.y + sf.sys.h / 2;
	snprintf(count, sizeof count, "%d / %d", in->count ? in->cursor + 1 : 0, in->count);
	countw = ui_text_tabular_width(chrome, count);
	sw = ui_text_width(meta, in->heading);
	room = sf.sys.w - countw - 32;
	if (sw > room) {
		unsigned next;

		ui_text_marquee(r, meta, in->heading, sf.sys.x, ui_cap_y(meta, sys_cy), room, phase, UI_TEXT_SOFT);
		next = ui_pingpong_wait(sw - room, phase);
		if (next < wait) wait = next;
	} else {
		ui_text(r, meta, in->heading, sf.sys.x, ui_cap_y(meta, sys_cy), -1, UI_TEXT_SOFT);
	}
	ui_text_tabular(r, chrome, count, sf.sys.x + sf.sys.w, ui_cap_y(chrome, sys_cy), 1, UI_TEXT_DIM);

	/* The rows, in a window the list scrolls through */
	SDL_RenderSetClipRect(r, &(SDL_Rect){ 0, GL_TOP, GL_PANEL_X - 16, GL_ROWS * GL_ROW_H });
	/* The focus, gliding (Highlight, src/ds/focus.h): Bar's is the fill as
	 * it always was; Border's a ring of the row's corners inside its plate;
	 * Lift's a plate of **surface** on a soft lift; None's nothing, the name
	 * in **text** */
	if (in->count > 0 && mk.focus != LOOK_FOCUS_NONE) {
		SDL_Rect pl = { GL_X, GL_TOP + (int)lroundf((plate - top) * GL_ROW_H), GL_W, GL_ROW_H };

		if (mk.focus == LOOK_FOCUS_BORDER)
			ds_focus_ring_in(r, (SDL_FRect){ (float)pl.x, (float)pl.y, (float)pl.w, (float)pl.h }, 14, 255);
		else if (mk.focus == LOOK_FOCUS_LIFT) {
			ui_lift_focus(r, &pl, false, 1);
			ui_round_rect(r, &pl, 14, ui_col(theme()->surface, 255));
		} else
			ui_round_rect(r, &pl, 14, ui_col(theme()->accent, 255));
	}
	first = (int)floorf(top);
	for (i = first; i <= first + GL_ROWS && i < in->count; i++) {
		int y = GL_TOP + (int)lroundf((i - top) * GL_ROW_H);

		if (i < 0) continue;
		gl_row(r, in, ctx, i, y, i == in->cursor, mk.focus == LOOK_FOCUS_BAR, phase, &wait);
	}
	SDL_RenderSetClipRect(r, NULL);
	/* More above, more below: the menus' small triangles */
	if (top > 0.01f)
		ds_more(r, GL_X + GL_W / 2.0f, GL_TOP - 10 - DS_MORE_H / 2.0f, true, UI_TEXT_DIM);
	if (top + GL_ROWS < in->count - 0.01f)
		ds_more(r, GL_X + GL_W / 2.0f, GL_TOP + GL_ROWS * GL_ROW_H + 10 - DS_MORE_H / 2.0f, false, UI_TEXT_DIM);

	/* The focused entry: its card, whole in the box at its own shape, its
	 * foot on the box's; its snap the same; its details over that foot */
	if (in->count > 0) {
		int w = 0, h = 0;
		float cb = 1, down;
		SDL_Texture *t = texture(ctx, in->cursor, &w, &h, &cb);
		int games = in->folder_of ? in->folder_of(ctx, in->cursor) : 0;
		float k = in->snap && in->snap_k > 0 && in->snap_w > 0 && in->snap_h > 0
		        ? fminf(in->snap_k, 1) : 0;
		uint32_t next = UINT32_MAX;
		/* The picture's frame, the details' place: the box until the art
		 * is in, the cover's, and as it turns, the frame growing to the
		 * snap's */
		SDL_FRect pic = { (float)GL_PANEL_X, (float)GL_TOP, (float)GL_PANEL_W, (float)(GL_FOOT - GL_TOP) };

		if (t && w > 0 && h > 0) {
			SDL_FRect dst = gl_media_fit(gl_layout(), (float)w, (float)h);
			Uint8 was;

			pic = dst;

			SDL_GetTextureAlphaMod(t, &was);
			SDL_SetTextureAlphaMod(t, (Uint8)lroundf(was * (1 - k)));
			if (k < 1) SDL_RenderCopyF(r, t, NULL, &dst);
			SDL_SetTextureAlphaMod(t, was);
			if (k > 0) {
				/* The cover turns into its video, as on the cover row: the
				 * snap, whole at its own shape in the box, through a frame
				 * that grows from the cover's rect to its own */
				SDL_FRect vr = gl_media_fit(gl_layout(), (float)in->snap_w, (float)in->snap_h);
				SDL_Rect clip = { (int)floorf(dst.x + (vr.x - dst.x) * k),
				                  (int)floorf(dst.y + (vr.y - dst.y) * k),
				                  (int)ceilf(dst.w + (vr.w - dst.w) * k) + 1,
				                  (int)ceilf(dst.h + (vr.h - dst.h) * k) + 1 };

				pic = (SDL_FRect){ dst.x + (vr.x - dst.x) * k, dst.y + (vr.y - dst.y) * k,
				                   dst.w + (vr.w - dst.w) * k, dst.h + (vr.h - dst.h) * k };
				SDL_RenderSetClipRect(r, &clip);
				SDL_SetTextureAlphaMod(in->snap, (Uint8)lroundf(255 * k));
				SDL_RenderCopyF(r, in->snap, in->snap_src.w > 0 ? &in->snap_src : NULL, &vr);
				SDL_SetTextureAlphaMod(in->snap, 255);
				SDL_RenderSetClipRect(r, NULL);
			}
		}
		/* The details: down at rest while the snap plays (hold-band), up
		 * at a press or as the snap goes */
		down = in->details > 0 ? fminf(in->details, 1)
		                       : snaprest_step(&m->details, k >= 1, now, &next);
		if (next < wait) wait = next;
		gl_details(r, in, games, down, pic);
		/* Its snap's mark, the cover row's, over the details' ground, fading
		 * as the snap comes */
		if (t && w > 0 && h > 0 && in->has_snap && k < 1) {
			SDL_FRect dst = gl_media_fit(gl_layout(), (float)w, (float)h);

			ds_play_disc(r, (int)lroundf(dst.x + dst.w) - DS_BADGE_INSET - DS_BADGE,
			             (int)lroundf(dst.y + dst.h) - DS_BADGE_INSET - DS_BADGE, DS_BADGE,
			             (Uint8)lroundf(255 * (1 - k)));
		}
	}
	/* The foot, as the cover row's (DESIGN.md 11.4): B, START back to the
	 * covers, Y and X for a game */
	{
		key_hint foot[4];
		int nf = 0, games = in->count > 0 && in->folder_of ? in->folder_of(ctx, in->cursor) : 0;

		foot[nf++] = (key_hint){ "B", T(in->inside ? S_GAMES_BACK : S_GAMES_SYSTEMS) };
		/* START on to the Video view, the fourth (DESIGN.md 11.57) */
		if (in->count > 0) foot[nf++] = (key_hint){ "START", T(S_GAMES_TO_VIDEO) };
		if (in->count > 0 && in->resumable && !in->install && !in->howto && !in->opens && !games)
			foot[nf++] = (key_hint){ "Y", T(S_GAMES_FRESH) };
		if (in->count > 0 && !games) foot[nf++] = (key_hint){ "X", T(S_GAMES_DETAILS) };
		keyhints_draw(r, in->foot ? in->foot : foot, in->foot ? in->nfoot : nf, sf.keys.x,
		              sf.keys.x + sf.keys.w, sf.keys.y + sf.keys.h / 2,
		              KEYHINTS_SPREAD, true, 255);   /* as the shelf's */
	}
	SDL_SetRenderDrawColor(r, red, green, blue, alpha);
	SDL_SetRenderDrawBlendMode(r, blend);
	return wait;
}
