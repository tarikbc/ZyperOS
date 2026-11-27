/* SPDX-License-Identifier: GPL-3.0-only */
/* See hudmenu_screen.h */
#include "ds/hover.h"
#include "hover_sweep.h"
#include "hudmenu_screen.h"

#include <stdio.h>
#include <string.h>

#include "db.h"
#include "game_menu.h"
#include "ingame_screen.h"
#include "panel.h"
#include "platform.h"
#include "shots.h"
#include "theme.h"
#include "menu_panel.h"
#include "menu_pick.h"
#include "ui.h"
#include "uisound.h"

/* What the band's line says: a game a little under full speed, warm and
 * busy, so every item shows, numbers and units */
static const hud_values SAMPLE = { 57.4, 0.96, 52, 34, 1296, 87, 22, 8, true };

static hm_state      g_hm;
static SDL_Texture  *g_hm_bg;   /* the paused game under the page */
/* The page's rows or the items' list's, the more of the two */
#define HM_MAX ((int)HUD_ITEMS > (int)HM_ROWS ? (int)HUD_ITEMS : (int)HM_ROWS)
static menu_choice   g_hm_choice[HM_MAX];
static unsigned char g_hm_sound[HM_MAX];

void hm_load(hm_state *s)
{
	char w[64];
	int r;

	s->on = db_get_int(db_dev(), HUD_KEY, 0) == 1;
	s->st = hud_default;
	db_get_str(db_dev(), HM_KEY_ITEMS, w, sizeof w, "");
	s->st.items = hud_items_parse(w);
	for (r = HM_CORNER; r < HM_ROWS; r++) {
		int k;

		db_get_str(db_dev(), hm_key((hm_row)r), w, sizeof w, "");
		k = hm_parse((hm_row)r, w);
		if (k >= 0) hm_set(s, (hm_row)r, k);
	}
}

static void hm_keep(const hm_state *s, hm_row r)
{
	int n;
	const char *const *ids = hm_ids(r, &n);
	int k = hm_get(s, r);

	if (r == HM_ON) {
		db_set_int(db_dev(), HUD_KEY, s->on ? 1 : 0);
	} else if (r == HM_SHOWS) {
		char w[64];

		hud_items_str(s->st.items, w, sizeof w);
		db_set_str(db_dev(), HM_KEY_ITEMS, w);
	} else if (ids && k >= 0 && k < n) {
		db_set_str(db_dev(), hm_key(r), ids[k]);
	}
}

void hm_send(const hm_state *s)
{
	char line[256];

	hm_line(s, g_clock24, line, sizeof line);
	plat_resident_line("%s", line);
}

/* The line as the band draws it, made again only when the style or the
 * theme moved */
static SDL_Texture *line_tex(SDL_Renderer *r, const hud_style *st, int *w, int *h)
{
	static SDL_Texture *tex;
	static SDL_Renderer *made_r;
	static hud_style made;
	static ds_theme made_colors;
	static bool made_clock24;
	static int tw, th;
	static uint32_t px[1024 * 128];
	const ds_theme *t = theme();
	hud_values v = SAMPLE;
	char text[160];

	if (tex && made_r == r && !memcmp(&made, st, sizeof made) &&
	    !memcmp(&made_colors, t, sizeof made_colors) && made_clock24 == g_clock24) {
		*w = tw; *h = th;
		return tex;
	}
	if (tex) SDL_DestroyTexture(tex);
	tex = NULL;
	v.clock24 = g_clock24;
	hud_text(text, sizeof text, &v, st->items);
	hud_size(text, 1.0f, st, &tw, &th);
	if (tw <= 0 || th <= 0 || tw > 1024 || th > 128) return NULL;
	hud_render(px, tw, th, tw, 1.0f, text, st, t, false);
	tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, tw, th);
	if (!tex) return NULL;
	SDL_UpdateTexture(tex, NULL, px, tw * 4);
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	made_r = r; made = *st; made_colors = *t; made_clock24 = g_clock24;
	*w = tw; *h = th;
	return tex;
}

/* The band (menu_style's header): the strip of the screen at the line's
 * corner, as wide as the panel's content and HM_BAND_H tall, the paused
 * game in it where the game is, and the line where hud_place puts it -
 * each at its own size, so what the band shows is what the corner will.
 * Framed in the theme's edge. With On-Screen Stats off, the game alone. */
static void hm_band(app *a, const SDL_Rect *r)
{
	int w_ = 0, h_ = 0;
	const hud_style *st = &g_hm.st;
	const bool right = st->corner == HUD_TOP_RIGHT || st->corner == HUD_BOTTOM_RIGHT;
	const bool top = st->corner == HUD_TOP_LEFT || st->corner == HUD_TOP_RIGHT;
	const int cy = top ? 0 : ZYPEROS_SCREEN_H - r->h;
	const ds_rgb ink = theme()->ink, edge = theme()->edge;
	SDL_Texture *tex = g_hm.on ? line_tex(a->r, st, &w_, &h_) : NULL;
	SDL_Rect game;
	int cx = right ? ZYPEROS_SCREEN_W - r->w : 0, x = 0, y = 0;

	/* The strip from the corner's side, or from a little left of a line
	 * longer than it, so the whole line shows */
	if (tex) {
		hud_place(st, 1.0f, w_, h_, 1024, 768, &x, &y);
		if (right && x - 8 < cx) cx = x - 8 > 0 ? x - 8 : 0;
	}

	SDL_RenderSetClipRect(a->r, r);
	SDL_SetRenderDrawColor(a->r, ink.r, ink.g, ink.b, 255);
	SDL_RenderFillRect(a->r, r);
	if (g_hm_bg) {
		if (!plat_resident_rect(&game)) game = (SDL_Rect){ 0, 0, 1024, 768 };
		game.x += r->x - cx;
		game.y += r->y - cy;
		SDL_RenderCopy(a->r, g_hm_bg, NULL, &game);
	}
	if (tex) SDL_RenderCopy(a->r, tex, NULL, &(SDL_Rect){ r->x + x - cx, r->y + y - cy, w_, h_ });
	SDL_RenderSetClipRect(a->r, NULL);
	/* Its frame: the edge's line, two units, inside the band */
	SDL_SetRenderDrawColor(a->r, edge.r, edge.g, edge.b, 255);
	SDL_RenderFillRect(a->r, &(SDL_Rect){ r->x, r->y, r->w, 2 });
	SDL_RenderFillRect(a->r, &(SDL_Rect){ r->x, r->y + r->h - 2, r->w, 2 });
	SDL_RenderFillRect(a->r, &(SDL_Rect){ r->x, r->y, 2, r->h });
	SDL_RenderFillRect(a->r, &(SDL_Rect){ r->x + r->w - 2, r->y, 2, r->h });
}

/* ---- the page ---- */

/* The lists of the rows of more than two (Corner, Edge, Opacity, Color):
 * the page's band kept over the list (keep_header), the stats in it where
 * and as the choice under the cursor puts them, in memory (src/ds/hover.h);
 * kept and sent to the game only as A takes one */
static void hm_show_row(hm_row r, int k)
{
	if (k >= 0 && k < hm_count(r)) hm_set(&g_hm, r, k);
}

#define HM_HOVER(name, row) \
	static void name##_show(void *app_, int k) { (void)app_; hm_show_row(row, k); } \
	static void name##_undo(void *app_, int kept) { (void)app_; hm_show_row(row, kept); } \
	static const ds_hover name = { .show = name##_show, .undo = name##_undo, .keep_header = true };
HM_HOVER(HM_CORNER_HOVER, HM_CORNER)
HM_HOVER(HM_EDGE_HOVER, HM_EDGE)
HM_HOVER(HM_OPACITY_HOVER, HM_OPACITY)
HM_HOVER(HM_COLOR_HOVER, HM_COLOR)
#undef HM_HOVER

static const ds_hover *hm_hover(hm_row r)
{
	return r == HM_CORNER ? &HM_CORNER_HOVER : r == HM_EDGE ? &HM_EDGE_HOVER
	     : r == HM_OPACITY ? &HM_OPACITY_HOVER : r == HM_COLOR ? &HM_COLOR_HOVER : NULL;
}

static int hm_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static const char *lab[HM_ROWS][HUD_COLORS];
	static char pct[HUD_OPACITIES][8];
	static char shows[96];
	int r, k, n = 0;

	(void)ctx;
	*heading = T(S_GAMEMENU_HUD);
	for (r = 0; r < HM_ROWS && n < max; r++, n++) {
		const int cnt = hm_count((hm_row)r);

		if (r == HM_SHOWS) {
			hm_shows_value(g_hm.st.items, shows, sizeof shows);
			rows[n] = (menu_row){ hm_label(HM_SHOWS), shows, true };
			g_hm_sound[n] = 0;
			g_hm_choice[n] = (menu_choice){ 0 };
			continue;
		}
		for (k = 0; k < cnt; k++) {
			if (r == HM_OPACITY) {
				/* hm_value's own buffer holds one at a time */
				snprintf(pct[k], sizeof pct[k], "%d%%", hud_opacity_pct[k]);
				lab[r][k] = pct[k];
			} else {
				lab[r][k] = hm_value((hm_row)r, k);
			}
		}
		rows[n] = (menu_row){ hm_label((hm_row)r), lab[r][hm_get(&g_hm, (hm_row)r)], true };
		g_hm_sound[n] = MENU_SND_RING;
		g_hm_choice[n] = (menu_choice){ .n = (unsigned char)cnt,
		                                .cur = (unsigned char)hm_get(&g_hm, (hm_row)r),
		                                .labels = lab[r], .hover = hm_hover((hm_row)r) };
	}
	return n;
}

static void hm_style(app *a, menu_style *st);



/* ---- the items' list ---- */

static int items_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static const char *onoff[2];
	int i, n = 0;

	(void)ctx;
	*heading = hm_label(HM_SHOWS);
	onoff[0] = T(S_MENU_ON_TITLE);
	onoff[1] = T(S_MENU_OFF_TITLE);
	for (i = 0; i < HUD_ITEMS && n < max; i++, n++) {
		const bool on = g_hm.st.items & (1u << i);

		rows[n] = (menu_row){ hm_item_label(i), onoff[on ? 0 : 1], true };
		g_hm_sound[n] = MENU_SND_RING;
		g_hm_choice[n] = (menu_choice){ .n = 2, .cur = (unsigned char)(on ? 0 : 1), .labels = onoff };
	}
	return n;
}

static menu_result items_key(app *a, void *ctx, in_button key, int sel)
{
	(void)a; (void)ctx;
	if (sel < 0 || sel >= HUD_ITEMS) return MENU_STAY;
	if (key == IN_LEFT || key == IN_RIGHT) {
		const uint8_t to = hm_item_flip(g_hm.st.items, sel);

		/* The last item on stays on: nothing to show is the switch's
		 * Off, a row above */
		if (to == g_hm.st.items) {
			uisound_play(DS_SOUND_DENY);
			return MENU_STAY;
		}
		g_hm.st.items = to;
		hm_keep(&g_hm, HM_SHOWS);
		hm_send(&g_hm);
	}
	return MENU_STAY;
}

static void items_list(app *a)
{
	menu_style st;

	hm_style(a, &st);
	menu_run(a, &st, items_build, items_key, NULL);
}

static menu_result hm_on_key(app *a, void *ctx, in_button key, int sel)
{
	(void)ctx;
	/* X quits from the in-game menu's own page; here it does nothing */
	if (sel < 0 || sel >= HM_ROWS) return MENU_STAY;
	if (sel == HM_SHOWS) {
		if (key == IN_ACCEPT) items_list(a);
		return MENU_STAY;
	}
	if (key == IN_LEFT || key == IN_RIGHT) {
		const hm_row r = (hm_row)sel;
		const int n = hm_count(r);

		hm_set(&g_hm, r, (hm_get(&g_hm, r) + (key == IN_LEFT ? n - 1 : 1)) % n);
		hm_keep(&g_hm, r);
		hm_send(&g_hm);
	}
	return MENU_STAY;
}

static void hm_backdrop(app *a, void *ctx, float k)
{
	(void)ctx;
	paused_backdrop(a, g_hm_bg, k);
}

static menu_result hm_power(app *a, void *ctx)
{
	(void)a; (void)ctx;
	plat_note_power_pressed();
	plat_resident_line("STOP");
	return MENU_DONE;
}

static void hm_style(app *a, menu_style *st)
{
	memset(st, 0, sizeof *st);
	st->fixed_w = gm_width(a);
	st->follow_tint = true;
	st->backdrop = hm_backdrop;
	st->on_power = hm_power;
	st->sound = g_hm_sound;
	st->choices = g_hm_choice;
	st->header = hm_band;
	/* The runner draws the header its height less half a pad, the gap
	 * under the rule */
	st->header_h = HM_BAND_H + menu_pad() / 2;
}

void hud_page(app *a, SDL_Texture *bg)
{
	menu_style st;

	g_hm_bg = bg;
	hm_load(&g_hm);
	hm_style(a, &st);
	menu_run(a, &st, hm_build, hm_on_key, NULL);
	g_hm_bg = NULL;
}

void hud_rows_report(app *a)
{
	menu_row rows[HM_MAX];
	const char *h;
	int n;

	(void)a;
	hm_load(&g_hm);
	n = hm_build(NULL, rows, HM_MAX, &h);
	menu_rows_print("hud", rows, n, g_hm_sound, g_hm_choice);
	n = items_build(NULL, rows, HM_MAX, &h);
	menu_rows_print("hud-shows", rows, n, g_hm_sound, g_hm_choice);
}

void hud_page_shot(app *a, SDL_Texture *bg, int sel, bool items, int pick, const char *shows)
{
	menu_row rows[HM_MAX];
	menu_style st;
	const char *heading;
	int n;

	g_hm_bg = bg;
	hm_load(&g_hm);
	if (shows) g_hm.st.items = hud_items_parse(shows);
	if (shot_hud_alt) {   /* the two-choice rows' other values */
		g_hm.st.background = HUD_SHADOW;
		g_hm.st.size = HUD_SMALL;
	}
	hm_style(a, &st);
	n = items ? items_build(NULL, rows, HUD_ITEMS, &heading) : hm_build(NULL, rows, HM_ROWS, &heading);
	hm_backdrop(a, NULL, 1.0f);
	if (!items && pick >= 0 && pick < n && g_hm_choice[pick].n > 2) {
		/* A on an option row of more than two: its list, as menu_pick
		 * draws it over the paused game, the one on ticked */
		const menu_choice *ch = &g_hm_choice[pick];
		const int kept = ch->cur, at = shot_pick_at >= 0 && shot_pick_at < ch->n ? shot_pick_at : kept;
		static char pw[8][64];
		menu_row pr[8];
		menu_style ls;
		int k;

		for (k = 0; k < ch->n && k < 8; k++) {
			menu_pick_word(ch->labels[k], pw[k], (int)sizeof pw[k]);
			pr[k] = (menu_row){ pw[k], k == kept ? MENU_TICK : NULL, true };
		}
		/* The band kept over the list, the choice under the cursor in it
		 * (keep_header, src/ds/hover.h) */
		if (at != kept && ch->hover && ch->hover->show) ch->hover->show(a, at);
		memset(&ls, 0, sizeof ls);
		ls.fixed_w = gm_width(a);
		ls.follow_tint = true;
		ls.header = hm_band;
		ls.header_h = st.header_h;
		g_menu_epoch++;
		menu_draw_styled(a, &ls, rows[pick].label, pr, k, at);
	} else {
		menu_draw_styled(a, &st, heading, rows, n, sel >= 0 && sel < n ? sel : 0);
	}
	g_hm_bg = NULL;
}

/* On-Screen Stats' lists through what their cursors show (src/hover_sweep.h) */
static int sweep_hud(app *a, void *ctx, menu_row *rows, int max, const menu_choice **choices)
{
	const char *h;

	(void)a; (void)ctx;
	*choices = g_hm_choice;
	return hm_build(NULL, rows, max < HM_ROWS ? max : HM_ROWS, &h);
}

void hud_hover_sweep(app *a)
{
	hm_load(&g_hm);
	hover_sweep_page(a, "hud", sweep_hud, NULL);
}
