/* SPDX-License-Identifier: GPL-3.0-only */
#include "radl_screen.h"

#include "aout_route.h"
#include "app.h"
#include "cheevos.h"
#include "dlq.h"
#include "ds/flex.h"
#include "ds/layout.h"
#include "ds/mark.h"
#include "ds/page.h"
#include "home.h"
#include "net.h"
#include "panel.h"
#include "rabadge.h"
#include "radl.h"
#include "rafetch.h"
#include "sidelights.h"
#include "signin_screen.h"
#include "sys_menu.h"
#include "uisound.h"
#include "usbdrive_screen.h"
#include "zyperdraw.h"
#include "levels.h"

#include <SDL.h>
#include <SDL_image.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>

/* ---------- MENU > Achievements: one page (DESIGN.md 11.49) --------------
 *
 * Asked for on the Discord (1.0.6): "being able to cache achievements in
 * advance vs having to load the game while on WiFi ... Think of it like a
 * scraper for achievements." So the page is the Scraper's, in its look:
 *
 *   the plan     each system's icon over a tick, lit when every game of it
 *                was asked about; Ready Offline and To Download as Flash
 *                Drive's two tiles; over the keys the room it takes and the
 *                games RetroAchievements has nothing for. A downloads, Y is
 *                the account. Signed out, A signs in.
 *   the run      the game beside Zyper, its system's icon and the bar; the
 *                set that came last with its badges landing one by one; the
 *                counts and the time left over the keys, or why it waits -
 *                the network gone, a pause RetroAchievements asked for.
 *   done         the first badge of each set that came, as a wall; the counts
 *                as marks; the games ready offline and the account's unlocks
 *                said plainly.
 *
 * The run is radl.c's, stepped here a frame at a time; the account is the
 * sign-in it always had (ra_signin_screen), now behind Y. */

#define RDL_SHOW   12       /* the newest set's badges in the run: two rows of six */
#define RDL_RING   14       /* the sets that came, for done's wall: two rows of seven */
#define RDL_LAND   420      /* a badge's landing, ms, each RDL_STAGGER after the one before */
#define RDL_STAGGER 45
/* What a game to download is likely to cost, until the run's own pace says:
 * one or two requests at the run's pace, its hash and its badges */
#define RDL_S_GAME 1.3
#define RDL_S_SYSTEM 2.0
/* ...and take on the card: a set and its badges, for the seven in ten games
 * RetroAchievements knows (the test card's own share) */
#define RDL_MB_GAME 0.12

typedef struct {
	char folder[CFG_STR], name[CFG_STR], key[CFG_STR];
	radl_sys c;
	SDL_Texture *icon;
} rdl_sys;

typedef struct {
	char set[RADL_SET_PATH];
	char badge[24];
} rdl_arrival;

enum { RDL_PLAN, RDL_RUN, RDL_DONE };
/* How a run ended, for done's heading: by itself, stopped (B, a full card,
 * no answer), or by the account refused - whose fix is signing in */
enum { RDL_ENDED, RDL_STOPPED, RDL_REFUSED };

typedef struct {
	app *a;
	bool fixture;
	rdl_sys s[CFG_MAX_SYSTEMS];
	int n;
	int where;
	bool net, in;
	unsigned long long free;
	radl_progress p;
	/* the newest set, for the run's body */
	unsigned seen_seq;
	int seen_badges;
	char cur_set[RADL_SET_PATH];
	char cur_title[128];
	int cur_count;
	char cur_badge[RDL_SHOW][24];
	int ncur;
	unsigned land_at;
	/* the sets that came, newest first */
	rdl_arrival ring[RDL_RING];
	int nring;
	/* the pace, for the time left */
	unsigned t0;
	int g0;
	unsigned shot_now;      /* a shot's clock, else 0 */
	int ended;              /* RDL_ENDED... */
	char problem[160];      /* why it stopped, under the heading */
} rdl_view;

/* ---- counting ------------------------------------------------------------- */

static SDL_Texture *rdl_icon(rdl_view *v, int i)
{
	char path[CFG_STR * 2];

	if (i < 0 || i >= v->n) return NULL;
	if (!v->s[i].icon && v->s[i].key[0]) {
		snprintf(path, sizeof path, "%s/system-icons/%s-still.png", v->fixture ? "res" : P_ROOT, v->s[i].key);
		if ((v->s[i].icon = IMG_LoadTexture(v->a->r, path))) {
			SDL_SetTextureScaleMode(v->s[i].icon, SDL_ScaleModeNearest);
			SDL_SetTextureBlendMode(v->s[i].icon, SDL_BLENDMODE_BLEND);
		}
	}
	return v->s[i].icon;
}

static void rdl_icon_draw(rdl_view *v, int i, int cx, int cy, int size, Uint8 al)
{
	SDL_Texture *t = rdl_icon(v, i);
	int w, h;

	if (!t) return;
	SDL_QueryTexture(t, NULL, NULL, &w, &h);
	if (w >= h) { h = h * size / w; w = size; } else { w = w * size / h; h = size; }
	SDL_SetTextureAlphaMod(t, al);
	SDL_RenderCopy(v->a->r, t, NULL, &(SDL_Rect){ cx - w / 2, cy - h / 2, w, h });
	SDL_SetTextureAlphaMod(t, 255);
}

static int rdl_find(const rdl_view *v, const char *folder)
{
	int i;

	for (i = 0; folder && i < v->n; i++)
		if (!strcmp(v->s[i].folder, folder)) return i;
	return -1;
}

static void rdl_icons_free(rdl_view *v)
{
	int i;

	for (i = 0; i < v->n; i++)
		if (v->s[i].icon) { SDL_DestroyTexture(v->s[i].icon); v->s[i].icon = NULL; }
}

/* What the card has, system by system (radl_plan): when the page opens and
 * after a run, never a frame */
static void rdl_count(rdl_view *v)
{
	app *a = v->a;
	radl_sys plan[CFG_MAX_SYSTEMS];
	struct statvfs vfs;
	int i, k, n;

	rdl_icons_free(v);
	v->net = net_online();
	v->in = ra_signed_in();
	v->free = statvfs(P_ROMS, &vfs) == 0 ? (unsigned long long)vfs.f_bavail * vfs.f_frsize : 0;
	n = radl_plan(&a->sys, P_ROMS, plan, CFG_MAX_SYSTEMS);
	v->n = 0;
	for (i = 0; i < n; i++) {
		rdl_sys *s = &v->s[v->n++];

		memset(s, 0, sizeof *s);
		s->c = plan[i];
		snprintf(s->folder, sizeof s->folder, "%.*s", (int)sizeof s->folder - 1, plan[i].folder);
		snprintf(s->name, sizeof s->name, "%.*s", (int)sizeof s->name - 1, plan[i].folder);
		for (k = 0; k < a->sys.count; k++)
			if (!strcmp(a->sys.systems[k].folder, plan[i].folder))
				snprintf(s->name, sizeof s->name, "%.*s", (int)sizeof s->name - 1, a->sys.systems[k].name);
		snprintf(s->key, sizeof s->key, "%s", home_art_key(plan[i].tag));
	}
}

typedef struct { int ready, todo, none, nsys, nready; } rdl_sum;

static rdl_sum rdl_totals(const rdl_view *v)
{
	rdl_sum t = { 0 };
	int i;

	for (i = 0; i < v->n; i++) {
		t.ready += v->s[i].c.ready;
		t.todo += v->s[i].c.todo;
		t.none += v->s[i].c.none;
		t.nsys++;
		if (v->s[i].c.ready) t.nready++;
	}
	return t;
}

/* A time as the corner's battery says one: "12 min", "1 h 20 min" */
static void rdl_time(double secs, char *out, size_t n)
{
	int m = (int)((secs + 59.0) / 60.0);

	if (m < 1) m = 1;
	if (m < 60) snprintf(out, n, T(S_CORNER_TIME_MIN), m);
	else if (m % 60 == 0) snprintf(out, n, T(S_CORNER_TIME_H), m / 60);
	else snprintf(out, n, T(S_CORNER_TIME_H_MIN), m / 60, m % 60);
}

/* A wait as a clock: "0:42", "1:05" */
static void rdl_wait(unsigned ms, char *out, size_t n)
{
	unsigned s = (ms + 999) / 1000;

	snprintf(out, n, "%u:%02u", s / 60, s % 60);
}

/* ---- drawing ---------------------------------------------------------------- */

static float rdl_ease(float t)
{
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

/* A badge, the card's or the plain one (rb_icon), `size` square at (x, y),
 * landing by `e` (0 to 1): from 80% and clear to whole */
static void rdl_badge(rdl_view *v, const char *set, const char *name, float x, float y, float size, float e)
{
	SDL_Texture *tex = rb_icon(v->a->r, set, name, false, theme());
	float s = size * (0.8f + 0.2f * e);

	if (!tex || e <= 0) return;
	SDL_SetTextureAlphaMod(tex, (Uint8)(255 * e));
	SDL_RenderCopyF(v->a->r, tex, NULL, &(SDL_FRect){ x + (size - s) / 2, y + (size - s) / 2 + 24 * (1 - e), s, s });
	SDL_SetTextureAlphaMod(tex, 255);
}

/* The counts as marks centred on `cx`: ready, new, none, and the time left
 * beside a clock when `eta` is given */
typedef struct { ui_glyph g; const char *s; SDL_Color c, ink; } rdl_stat;

static void rdl_stats(app *a, int cx, int cy, int ready, int fetched, int none, const char *eta)
{
	const ds_theme *t = theme();
	TTF_Font *f = ui_font(UI_F_MENU);
	rdl_stat st[4];
	char num[3][16];
	int i, w = 0, x, ns = 0;

	snprintf(num[0], sizeof num[0], "%d", ready);
	snprintf(num[1], sizeof num[1], "%d", fetched);
	snprintf(num[2], sizeof num[2], "%d", none);
	st[ns++] = (rdl_stat){ UI_GLYPH_CHECK, num[0], ui_col(t->accent_ink, 255), UI_TEXT };
	st[ns++] = (rdl_stat){ UI_GLYPH_TO_CARD, num[1], ui_col(t->accent_ink, 255), UI_TEXT };
	st[ns++] = (rdl_stat){ UI_GLYPH_CANCEL, num[2], UI_TEXT_DIM, UI_TEXT_DIM };
	if (eta) st[ns++] = (rdl_stat){ UI_GLYPH_CLOCK, eta, UI_TEXT_SOFT, UI_TEXT_SOFT };
	for (i = 0; i < ns; i++) w += 44 + 8 + ui_text_width(f, st[i].s) + (i < ns - 1 ? 40 : 0);
	x = cx - w / 2;
	for (i = 0; i < ns; i++) {
		ui_glyph_draw(a->r, st[i].g, x + 22, cy, 44, st[i].c);
		x += 52;
		ui_text(a->r, f, st[i].s, x, ui_cap_y(f, cy), -1, st[i].ink);
		ui_fit_drawn(st[i].s);
		x += ui_text_width(f, st[i].s) + 40;
	}
}

static bool g_rdl_back;

/* B's word: Close from MENU, Back from Downloads' Achievements row */
static const char *rdl_close_word(void)
{
	return g_rdl_back ? T(S_DOWNLOADS_KEY_BACK) : T(S_SCRAPER_KEY_CLOSE);
}

/* A tree for a page's body (src/ds/flex.h), as the Scraper's */
static int rdl_body(ds_flex *F, const ds_page_frame *pf, ds_dir dir, int gap)
{
	return ds_flex_begin(F, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H,
	                     &(ds_box){ .dir = dir, .gap = gap,
	                                .pad = { pf->body.x, pf->body.y,
	                                         ZYPEROS_SCREEN_W - pf->body.x - pf->body.w,
	                                         ZYPEROS_SCREEN_H - pf->body.y - pf->body.h } });
}

/* The plan's body, as the Scraper's: a slot a system, an icon's band over
 * its mark's, and the two tiles. The slot's width and the tiles' height are
 * the Scraper plan's own (SCP_SLOT_W, SCP_PLAN_TILE_H), typed there too, so
 * the two pages stay one picture; they move to tokens together. */
#define RDL_SLOT_W       62
#define RDL_BAND         56
#define RDL_PLAN_TILE_H  220

static void rdl_plan_draw(rdl_view *v)
{
	app *a = v->a;
	const ds_theme *t = theme();
	TTF_Font *ft = ui_font(UI_F_TITLE), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	rdl_sum sum = rdl_totals(v);
	static ds_flex F;
	SDL_Rect sr[9];
	int i, shown, more, slot0 = 0, tile0 = 0;
	char rb[16], rl[96], tb[16], tplus[48] = "", tl[96], sz[32], eta[64];
	ds_line_item it[2];
	int nit = 0;
	char room[160], nonet[96];

	/* The heading's second line: what stands in the way, else how long */
	if (!v->in)
		ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_SYSMENU_CHEEVOS), T(S_RADL_SIGN_IN_FIRST), UI_GLYPH_COUNT, 0);
	else if (!v->net)
		ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_SYSMENU_CHEEVOS), T(S_MENU_NEEDS_WIFI), UI_GLYPH_COUNT, 0);
	else if (sum.todo) {
		rdl_time(sum.todo * RDL_S_GAME + sum.nsys * RDL_S_SYSTEM, sz, sizeof sz);
		snprintf(eta, sizeof eta, T(S_SCRAPER_ABOUT_TIME), sz);
		ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_SYSMENU_CHEEVOS), eta, UI_GLYPH_CLOCK, 0);
	} else {
		ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_SYSMENU_CHEEVOS), T(S_RADL_EVERY_CHECKED), UI_GLYPH_CHECK, 0);
	}
	{
		const int root = rdl_body(&F, &pf, DS_COL, 0);
		const int cell = (pf.body.w - RDL_SLOT_W) / 8;
		const int slots = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .gap = cell - RDL_SLOT_W,
		                                                   .h = 2 * RDL_BAND });
		int tiles;

		for (i = 0; i < 9; i++) {
			int id = ds_flex_add(&F, slots, &(ds_box){ .w = RDL_SLOT_W, .h = 2 * RDL_BAND });

			if (!i) slot0 = id;
		}
		tiles = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .justify = DS_J_BETWEEN,
		                                         .pad = { 0, 8, 0, 0 }, .h = 8 + RDL_PLAN_TILE_H });
		tile0 = ds_flex_add(&F, tiles, &(ds_box){ .w = ds_page_tile_w(), .h = RDL_PLAN_TILE_H });
		ds_flex_add(&F, tiles, &(ds_box){ .w = ds_page_tile_w(), .h = RDL_PLAN_TILE_H });
		ds_flex_solve(&F);
		for (i = 0; i < 9; i++) sr[i] = ds_flex_rect(&F, slot0 + i);
	}
	/* The systems: each icon over a tick, lit when every game of it was
	 * asked about; nine at most, and how many more */
	shown = v->n > 9 ? 8 : v->n;
	more = v->n - shown;
	for (i = 0; i < shown; i++) {
		int cx = sr[i].x + RDL_SLOT_W / 2, iy = sr[i].y + RDL_BAND / 2, my = iy + RDL_BAND;
		bool all = v->s[i].c.todo == 0;

		rdl_icon_draw(v, i, cx, iy, 60, 255);
		ds_switch(a->r, cx, my, 28, UI_GLYPH_CHECK, all);
	}
	if (more > 0) {
		char m[16];

		snprintf(m, sizeof m, "+%d", more);
		ui_text(a->r, fm, m, sr[8].x + RDL_SLOT_W / 2, ui_cap_y(fm, sr[8].y + RDL_BAND / 2), 0, UI_TEXT_SOFT);
	}
	snprintf(rb, sizeof rb, "%d", sum.ready);
	snprintf(rl, sizeof rl, TN(S_SCRAPER_ON_N_SYSTEMS, sum.nready), sum.nready);
	snprintf(tb, sizeof tb, "%d", sum.todo);
	snprintf(tl, sizeof tl, "%s", sum.todo ? T(S_RADL_NEVER_CHECKED) : T(S_RADL_ALL_CHECKED));
	if (sum.todo) {
		sys_menu_bytes((unsigned long long)(sum.todo * RDL_MB_GAME * 1e6), sz, sizeof sz);
		snprintf(tplus, sizeof tplus, "+%s", sz);
	}
	{
		struct { ui_glyph g; const char *name, *big, *plus, *line; bool on; } TL[2] = {
			{ UI_GLYPH_CHECK, T(S_RADL_READY), rb, "", rl, sum.ready > 0 },
			{ UI_GLYPH_TO_CARD, T(S_RADL_TO_DOWNLOAD), tb, tplus, tl, sum.todo > 0 },
		};

		for (i = 0; i < 2; i++) {
			SDL_Rect q = ds_flex_rect(&F, tile0 + i);

			ui_round_rect(a->r, &q, DS_RADIUS_CARD, ui_col(t->raised, 255));
			ui_glyph_draw(a->r, TL[i].g, q.x + UD_PAD + 28, q.y + 50, 56,
			              TL[i].on ? ui_col(t->accent_ink, 255) : UI_TEXT_DIM);
			{
				char nm[96];

				ui_fit_text(fm, TL[i].name, nm, sizeof nm, q.w - 2 * UD_PAD - 72);
				ui_text(a->r, fm, nm, q.x + UD_PAD + 72, ui_cap_y(fm, q.y + 50), -1, UI_TEXT);
			}
			ui_text(a->r, ft, TL[i].big, q.x + UD_PAD, ui_cap_y(ft, q.y + 128), -1, TL[i].on ? UI_TEXT : UI_TEXT_DIM);
			if (TL[i].plus[0])
				ui_text(a->r, fm, TL[i].plus, q.x + q.w - UD_PAD, ui_cap_y(fm, q.y + 128), 1, UI_TEXT_SOFT);
			ui_text(a->r, fs, TL[i].line, q.x + UD_PAD, ui_cap_y(fs, q.y + 180), -1, UI_TEXT_SOFT);
		}
	}
	/* Over the keys: the room, and the games RetroAchievements has nothing for */
	if (sum.todo) {
		char tf[64];

		sys_menu_bytes((unsigned long long)(sum.todo * RDL_MB_GAME * 1e6), sz, sizeof sz);
		snprintf(tf, sizeof tf, T(S_SCRAPER_TO_FETCH), sz);
		/* The card's room only when it is getting short: a set and its
		 * badges are a few hundred KB, and the line has the games with
		 * none to say too */
		if (v->free && v->free < 2000ULL * 1000 * 1000) {
			char fr[32], f2[48];

			sys_menu_bytes(v->free, fr, sizeof fr);
			snprintf(f2, sizeof f2, T(S_STORAGE_FREE), fr);
			snprintf(room, sizeof room, "%s \xc2\xb7 %s", tf, f2);
		} else {
			snprintf(room, sizeof room, "%s", tf);
		}
		it[nit++] = (ds_line_item){ UI_GLYPH_TO_CARD, room, DS_TONE_INFO };
	}
	if (sum.none) {
		snprintf(nonet, sizeof nonet, TN(S_RADL_HAVE_NONE, sum.none), sum.none);
		it[nit++] = (ds_line_item){ UI_GLYPH_CANCEL, nonet, DS_TONE_INFO };
	}
	if (nit) ds_page_line(a->r, &pf, it, nit);
	if (!v->in) {
		const char *K[2] = { "A", "B" }, *L[2] = { T(S_RADL_KEY_SIGN_IN), rdl_close_word() };

		ds_page_foot(a->r, &pf, K, L, 2, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
	} else {
		const char *K[3] = { "A", "Y", "B" };
		const char *L[3] = { T(S_RADL_KEY_DOWNLOAD), T(S_RADL_KEY_ACCOUNT), rdl_close_word() };

		ds_page_foot(a->r, &pf, K, L, 3, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
	}
}

/* The time left at the run's own pace, once it has one; before, the guess */
static double rdl_eta(rdl_view *v, unsigned now)
{
	int left = v->p.games - v->p.games_done;
	double spg = RDL_S_GAME;

	if (left < 0) left = 0;
	if (v->p.games_done - v->g0 >= 6 && now - v->t0 >= 15000)
		spg = (now - v->t0) / 1000.0 / (v->p.games_done - v->g0);
	return left * spg;
}

/* The run's body: the newest set's name and count, and its badges, two rows
 * of six, landing one after another */
#define RDL_BADGE 112
#define RDL_BADGE_GAP DS_SECTION

static void rdl_run_draw(rdl_view *v, unsigned now)
{
	app *a = v->a;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	int hx = pf.head_x, si, k, cap = 0, grid = 0;
	char fit[160], cnt[48], eta[48], line[160], w[24];
	static ds_flex F;
	const char *K[2] = { "B", "Y" }, *L[2] = { T(S_UPDATE_BACK_GOES_ON), T(S_SCRAPER_KEY_STOP) };
	const char *title;
	SDL_Rect gr, cr;

	si = rdl_find(v, v->p.folder_now);
	/* The game being asked about; while a system's new games are read, the system */
	title = v->p.state == RADL_HASHING || !v->p.now[0] ? (si >= 0 ? v->s[si].name : "") : v->p.now;
	ui_fit_text(ft, title, fit, sizeof fit, pf.x1 - hx);
	ui_text(a->r, ft, fit, hx, ui_cap_y(ft, DS_PAGE_TITLE_Y), -1, UI_TEXT);
	rdl_icon_draw(v, si, hx + 26, DS_PAGE_SUB_Y, 52, 255);
	snprintf(cnt, sizeof cnt, "%d / %d", v->p.games_done, v->p.games);
	{
		int bw = pf.x1 - hx - 72 - ui_text_width(fs, cnt) - 24;

		ds_bar(a->r, hx + 72, DS_PAGE_SUB_Y - UI_BAR_H / 2, bw,
		       v->p.games > 0 ? (int)((long long)bw * (v->p.games_done > v->p.games ? v->p.games : v->p.games_done)
		                              / v->p.games) : 0, 255);
	}
	ui_text(a->r, fs, cnt, pf.x1, ui_cap_y(fs, DS_PAGE_SUB_Y), 1, UI_TEXT_DIM);

	/* The body, laid out by rules: a caption over two rows of six */
	{
		const int root = rdl_body(&F, &pf, DS_COL, 16);
		const int gw = 6 * RDL_BADGE + 5 * RDL_BADGE_GAP;

		cap = ds_flex_add(&F, root, &(ds_box){ .h = DS_LINE_META });
		grid = ds_flex_add(&F, root, &(ds_box){ .w = gw, .h = 2 * RDL_BADGE + RDL_BADGE_GAP });
		ds_flex_solve(&F);
		cr = ds_flex_rect(&F, cap);
		gr = ds_flex_rect(&F, grid);
		gr.x = pf.body.x + (pf.body.w - gw) / 2;
	}
	if (v->ncur) {
		char n[48], c2[200];

		char set[180];

		/* Labeled: the heading has moved on to the next game by now */
		snprintf(n, sizeof n, TN(S_RADL_N_ACHIEVEMENTS, v->cur_count), v->cur_count);
		snprintf(set, sizeof set, "%s \xc2\xb7 %s", v->cur_title, n);
		snprintf(c2, sizeof c2, T(S_RADL_JUST_ADDED), set);
		ui_fit_text(fs, c2, fit, sizeof fit, pf.body.w);
		ui_text(a->r, fs, fit, ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, cr.y + cr.h / 2), 0, UI_TEXT_SOFT);
		for (k = 0; k < v->ncur; k++) {
			float e = rdl_ease((float)((int)(now - v->land_at) - k * RDL_STAGGER) / RDL_LAND);

			rdl_badge(v, v->cur_set, v->cur_badge[k], (float)(gr.x + (k % 6) * (RDL_BADGE + RDL_BADGE_GAP)),
			          (float)(gr.y + (k / 6) * (RDL_BADGE + RDL_BADGE_GAP)), RDL_BADGE, e);
		}
	}
	/* Over the keys: why it waits, or the counts and the time left */
	if (v->p.state == RADL_NO_NETWORK) {
		ds_line_item it = { UI_GLYPH_WIFI, T(S_RADL_WAITING_WIFI), DS_TONE_WARN };

		ds_page_line(a->r, &pf, &it, 1);
	} else if (v->p.state == RADL_SLOWED || v->p.state == RADL_RETRYING) {
		unsigned left = v->p.resume_at > now ? v->p.resume_at - now : 0;
		ds_line_item it;

		rdl_wait(left, w, sizeof w);
		snprintf(line, sizeof line, v->p.state == RADL_SLOWED ? T(S_RADL_PAUSED) : T(S_RADL_RETRYING), w);
		it = (ds_line_item){ UI_GLYPH_CLOCK, line, DS_TONE_WARN };
		ds_page_line(a->r, &pf, &it, 1);
	} else if (v->p.state == RADL_HASHING) {
		ds_line_item it;

		snprintf(line, sizeof line, T(S_RADL_IDENTIFYING), v->p.hashed, v->p.hashing);
		it = (ds_line_item){ UI_GLYPH_FINISH, line, DS_TONE_INFO };
		ds_page_line(a->r, &pf, &it, 1);
	} else {
		rdl_time(rdl_eta(v, now), eta, sizeof eta);
		rdl_stats(a, ZYPEROS_SCREEN_W / 2, pf.line_y, v->p.ready, v->p.fetched + v->p.updated, v->p.none, eta);
	}
	ds_page_foot(a->r, &pf, K, L, 2, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
}

/* Done: the first badge of each set that came, as a wall; the counts; the
 * games ready offline and the account's unlocks, said plainly */
#define RDL_WALL 104
static void rdl_done_draw(rdl_view *v)
{
	app *a = v->a;
	TTF_Font *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	rdl_sum sum = rdl_totals(v);
	static ds_flex F;
	SDL_Rect wr, sr;
	int m, n, wall, said;
	float gap;
	char l1[96], l2[96];
	const char *K[2] = { "A", "B" };
	const char *L[2] = { v->ended == RDL_REFUSED ? T(S_RADL_KEY_SIGN_IN) : T(S_SCRAPER_KEY_AGAIN),
	                     rdl_close_word() };
	/* Titled by what happened: a run that stopped does not say All done */
	const char *head = v->ended == RDL_REFUSED ? T(S_RADL_SIGN_IN_AGAIN)
	                 : v->ended == RDL_STOPPED ? T(S_RADL_STOPPED) : T(S_SCRAPER_ALL_DONE);

	ds_page_head(a->r, &pf, DS_HEAD_SUB, head, v->problem[0] ? v->problem : NULL, UI_GLYPH_COUNT, 0);
	{
		const int root = rdl_body(&F, &pf, DS_COL, 24);

		wall = ds_flex_add(&F, root, &(ds_box){ .h = 2 * RDL_WALL + 24 });
		said = ds_flex_add(&F, root, &(ds_box){ .h = DS_LINE_META });
		ds_flex_solve(&F);
		wr = ds_flex_rect(&F, wall);
		sr = ds_flex_rect(&F, said);
		gap = (wr.w - 7.0f * RDL_WALL) / 6;
	}
	n = v->nring;
	for (m = 0; m < n; m++) {
		int k = n - 1 - m;     /* oldest first */

		rdl_badge(v, v->ring[k].set, v->ring[k].badge, wr.x + (m % 7) * (RDL_WALL + gap),
		          (float)(wr.y + (m / 7) * (RDL_WALL + 24)), RDL_WALL, 1.0f);
	}
	if (!n) sr.y = wr.y + wr.h / 2 - sr.h / 2;   /* nothing came: the counts in the middle */
	rdl_stats(a, ZYPEROS_SCREEN_W / 2, sr.y + sr.h / 2, sum.ready, v->p.fetched + v->p.updated, sum.none, NULL);
	snprintf(l1, sizeof l1, TN(S_RADL_READY_N, sum.ready), sum.ready);
	if (v->p.unlocks) {
		/* Two facts, each its own text, a dot between, centred together */
		const char *dot = " \xc2\xb7 ";
		int w1, wd, w2, x;

		snprintf(l2, sizeof l2, TN(S_RADL_UNLOCKS_N, v->p.unlocks), v->p.unlocks);
		w1 = ui_text_width(fs, l1);
		wd = ui_text_width(fs, dot);
		w2 = ui_text_width(fs, l2);
		x = ZYPEROS_SCREEN_W / 2 - (w1 + wd + w2) / 2;
		ui_text(a->r, fs, l1, x, ui_cap_y(fs, pf.line_y), -1, UI_TEXT_SOFT);
		ui_text(a->r, fs, dot, x + w1, ui_cap_y(fs, pf.line_y), -1, UI_TEXT_SOFT);
		ui_text(a->r, fs, l2, x + w1 + wd, ui_cap_y(fs, pf.line_y), -1, UI_TEXT_SOFT);
	} else {
		ui_text(a->r, fs, l1, ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, pf.line_y), 0, UI_TEXT_SOFT);
	}
	ds_page_foot(a->r, &pf, K, L, 2, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
}

static void rdl_draw(rdl_view *v, unsigned now)
{
	switch (v->where) {
	case RDL_RUN:  rdl_run_draw(v, now); break;
	case RDL_DONE: rdl_done_draw(v); break;
	default:       rdl_plan_draw(v); break;
	}
}

/* The page under a panel opened from it (g_base_page) */
static void rdl_base(app *a, void *ctx)
{
	(void)a;
	rdl_draw(ctx, anim_now());
}

/* ---- the run ------------------------------------------------------------------ */

/* A set that came: its badges for the run, its first for done's wall */
static void rdl_take_arrival(rdl_view *v, unsigned now)
{
	char names[CHV_MAX][24];
	int count = 0, k, n;

	if (v->p.set_seq == v->seen_seq) return;
	v->seen_seq = v->p.set_seq;
	if (!radl_read_set(v->p.set_path, NULL, &count, NULL, names, CHV_MAX, NULL)) return;
	snprintf(v->cur_set, sizeof v->cur_set, "%s", v->p.set_path);
	snprintf(v->cur_title, sizeof v->cur_title, "%s", v->p.set_title);
	v->cur_count = count;
	n = count < CHV_MAX ? count : CHV_MAX;
	for (v->ncur = 0, k = 0; k < n && v->ncur < RDL_SHOW; k++)
		snprintf(v->cur_badge[v->ncur++], sizeof v->cur_badge[0], "%.23s", names[k]);
	v->land_at = now;
	if (v->nring == RDL_RING) v->nring--;
	memmove(&v->ring[1], &v->ring[0], (size_t)v->nring * sizeof v->ring[0]);
	v->nring++;
	snprintf(v->ring[0].set, sizeof v->ring[0].set, "%s", v->cur_set);
	snprintf(v->ring[0].badge, sizeof v->ring[0].badge, "%.23s", n ? names[0] : "");
}

/* ---- the run, as a lane of the downloads' queue (src/dlq.h) ---------------
 * The queue's, not this page's: it goes on when the page closes (B,
 * "Continue in background") and the page shows it again when it opens. Its
 * systems are kept here (radl reads them for the whole run), and so is what
 * it did once it has ended. */
static systems_cfg g_ra_sys;
static radl_progress g_ra_last;        /* what the last run did, as it ended */
static unsigned g_rdl_seen;            /* the lane's ends this page has shown */
static unsigned g_ra_t0;               /* its begin's clock */
static int g_ra_badges;                /* badges the list was told of */

static char g_ra_only[CFG_STR];        /* a console's run, or "" */

static bool ra_lane_begin(void)
{
	return radl_begin(&g_ra_sys, P_ROMS, g_ra_only[0] ? g_ra_only : NULL, g_ra_t0);
}

static int ra_lane_step(unsigned now_ms)
{
	radl_progress p;
	int r;

	(void)now_ms;
	r = radl_step(plat_now_ms());      /* the clock its pauses are in */
	radl_status(&p);
	/* Badges landed: the lists' textures look for them again, whichever
	 * screen is up */
	if (p.badges != g_ra_badges) {
		g_ra_badges = p.badges;
		rb_note_landed();
	}
	return r;
}

static void ra_lane_done(dlq_end how)
{
	radl_status(&g_ra_last);
	if (radl_running()) radl_cancel();
	if (how == DLQ_ENDED && g_ra_last.problem == RADL_OK) plat_side_lights_say(SIDE_LIGHTS_DONE);
}

static int ra_lane_pct(void)
{
	radl_progress p;

	radl_status(&p);
	return p.games > 0 ? (int)((long long)p.games_done * 100 / p.games) : 0;
}

/* Its card is set as it starts: P_ROMS is the device's, read at run time */
static dlq_lane_def RA_LANE = {
	.name = "achievements", .slot = NET_SLOT_RA,
	.step = ra_lane_step, .done = ra_lane_done, .pct = ra_lane_pct,
	.every_ms = 8,
};

static void rdl_start(rdl_view *v, unsigned now)
{
	/* Its own network slot (src/dlq.h): an account's answer after a game,
	 * on the screens' slot, goes on beside it */
	g_ra_sys = v->a->sys;
	g_ra_only[0] = '\0';
	g_ra_t0 = now;
	g_ra_badges = 0;
	RA_LANE.card = P_ROMS;
	if (!dlq_start(DLQ_RA, &RA_LANE, ra_lane_begin)) {
		uisound_play(DS_SOUND_DENY);
		result_message(v->a, T(S_SYSMENU_CHEEVOS), MSG_NEEDS_WIFI);
		return;
	}
	memset(&v->p, 0, sizeof v->p);
	v->seen_seq = 0;
	v->seen_badges = 0;
	v->ncur = 0;
	v->nring = 0;
	v->problem[0] = '\0';
	v->t0 = now;
	v->g0 = 0;
	v->where = RDL_RUN;
}

/* Back on a run that went on behind the screens: from where it is, its pace
 * counted again from here; the set that came last shown as it lands */
static void rdl_rejoin(rdl_view *v, unsigned now)
{
	radl_status(&v->p);
	v->seen_seq = 0;
	v->seen_badges = v->p.badges;
	v->ncur = 0;
	v->nring = 0;
	v->problem[0] = '\0';
	v->t0 = now;
	v->g0 = v->p.games_done;
	v->where = RDL_RUN;
}

/* The run is over - by its end, stopped, or the card nearly full; here or
 * behind the screens - and the page shows how, from what the lane kept as it
 * ended (ra_lane_done) */
static void rdl_finish(rdl_view *v)
{
	dlq_end how = dlq_last(DLQ_RA);

	v->p = g_ra_last;
	v->problem[0] = '\0';
	v->ended = how == DLQ_ENDED ? RDL_ENDED : RDL_STOPPED;
	if (how == DLQ_FULL) snprintf(v->problem, sizeof v->problem, "%s", T(S_SCRAPER_NO_ROOM));
	g_rdl_seen = dlq_ends(DLQ_RA);
	switch (v->p.problem) {
	case RADL_SIGN_IN:
		v->ended = RDL_REFUSED;
		snprintf(v->problem, sizeof v->problem, "%s", T(S_RADL_REFUSED));
		break;
	case RADL_NO_ROOM:
		v->ended = RDL_STOPPED;
		snprintf(v->problem, sizeof v->problem, "%s", T(S_SCRAPER_NO_ROOM));
		break;
	case RADL_NO_ANSWER:
		v->ended = RDL_STOPPED;
		snprintf(v->problem, sizeof v->problem, "%s", T(S_RADL_NO_ANSWER));
		break;
	default: break;
	}
	rdl_count(v);
	v->where = RDL_DONE;
}

/* The account: the sign-in it always had, then the card counted again */
static void rdl_account(rdl_view *v)
{
	ra_signin_screen(v->a);
	rdl_count(v);
}

static void rdl_page(app *a)
{
	rdl_view *v = calloc(1, sizeof *v);
	int base;
	unsigned beat = 0;
	bool dirty = true;

	if (!v) return;
	v->a = a;
	rdl_count(v);
	/* A run behind the screens is shown where it is; one that ended while
	 * the page was closed, as done (src/dlq.h) */
	if (dlq_running(DLQ_RA)) rdl_rejoin(v, plat_now_ms());
	else if (dlq_ends(DLQ_RA) != g_rdl_seen) rdl_finish(v);
	dlq_page(DLQ_RA, true);
	base = panel_base();
	g_base_page = rdl_base;
	g_base_ctx = v;
	while (!want_quit && a->running) {
		unsigned now = anim_now();
		bool anim;

		plat_input_poll(&a->in);
		mp4_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			power_off(a);              /* the run with everything else */
			break;
		}
		levels_keys(&a->in, BOOST_HIDE);
		for (int b = 0; b < IN_COUNT; b++) if (a->in.pressed[b] || a->in.down[b]) dirty = true;
		if (a->in.pressed[IN_MENU]) {
			menu_leaving(a);           /* a run goes on behind the screens */
			break;
		}

		if (v->where == RDL_RUN) {
			radl_progress last = v->p;

			/* The lane steps here once a frame, and behind every screen
			 * once the page has closed (src/dlq.h) */
			dlq_tick();
			if (!dlq_running(DLQ_RA)) {
				rdl_finish(v);
				dirty = true;
			} else {
				radl_status(&v->p);
				if (v->p.set_seq != v->seen_seq) { rdl_take_arrival(v, now); dirty = true; }
				/* Badges landed (the lane told the lists): the page's own
				 * textures follow */
				if (v->p.badges != v->seen_badges) { v->seen_badges = v->p.badges; dirty = true; }
				if (v->p.games_done != last.games_done || v->p.state != last.state ||
				    v->p.hashed != last.hashed || strcmp(v->p.now, last.now))
					dirty = true;
				if (a->in.pressed[IN_BACK]) {
					/* Continue in background: the page closes and the run
					 * goes on, the status corner's ring showing it */
					uisound_play(DS_SOUND_BACK);
					break;
				}
				if (a->in.pressed[IN_Y]) {
					uisound_play(DS_SOUND_BACK);
					dlq_stop(DLQ_RA);
					rdl_finish(v);
					dirty = true;
				}
			}
		} else if (a->in.pressed[IN_BACK]) {
			uisound_play(DS_SOUND_BACK);
			break;
		} else if (a->in.pressed[IN_ACCEPT]) {
			if (v->where == RDL_DONE) rdl_count(v);
			if (v->where == RDL_DONE && v->ended == RDL_REFUSED) {
				/* The fix: sign in, then the plan as it is now */
				uisound_play(DS_SOUND_ACCEPT);
				rdl_account(v);
				v->where = RDL_PLAN;
			} else if (!v->in) {
				uisound_play(DS_SOUND_ACCEPT);
				rdl_account(v);
			} else if (!v->net) {
				uisound_play(DS_SOUND_DENY);
				result_message(a, T(S_SYSMENU_CHEEVOS), MSG_NEEDS_WIFI);
			} else {
				uisound_play(DS_SOUND_ACCEPT);
				rdl_start(v, plat_now_ms());
			}
			dirty = true;
		} else if (a->in.pressed[IN_Y] && v->where == RDL_PLAN && v->in) {
			uisound_play(DS_SOUND_ACCEPT);
			rdl_account(v);
			dirty = true;
		}
		if (g_menu_closing) break;

		/* A frame while badges land, a pause counts down, or once a second */
		anim = v->where == RDL_RUN &&
		       (now - v->land_at < (unsigned)(RDL_LAND + RDL_SHOW * RDL_STAGGER + 40) ||
		        v->p.state == RADL_SLOWED || v->p.state == RADL_RETRYING);
		if (now - beat >= 1000) { beat = now; dirty = true; }
		if (!dirty && !anim) {
			plat_input_wait(v->where == RDL_RUN ? 16 : 80);
			continue;
		}
		dirty = false;
		tick_tint(a);
		tick_theme(a);
		rdl_draw(v, now);
		panel_leave_draw(a, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	dlq_page(DLQ_RA, false);
	mm_close(base, anim_now(), 0, g_menu_closing);
	rdl_icons_free(v);
	rb_textures_free();
	g_base_page = NULL;
	g_base_ctx = NULL;
	free(v);
}

/* ---- for the Downloads page (src/dl_screen.h) -----------------------------
 * The page's plan and its run, with no page drawn */
void radl_plan_of(app *a, radl_counts *out)
{
	rdl_view *v = calloc(1, sizeof *v);
	rdl_sum t;

	memset(out, 0, sizeof *out);
	if (!v) return;
	v->a = a;
	rdl_count(v);
	t = rdl_totals(v);
	out->ready = t.ready;
	out->todo = t.todo;
	out->none = t.none;
	out->in = v->in;
	out->net = v->net;
	out->secs = t.todo ? t.todo * RDL_S_GAME + t.nsys * RDL_S_SYSTEM : 0;
	out->bytes = t.todo * RDL_MB_GAME * 1e6;
	rdl_icons_free(v);
	free(v);
}

bool radl_run(app *a)
{
	rdl_view *v;
	bool ok;

	if (dlq_running(DLQ_RA)) return true;
	if (!(v = calloc(1, sizeof *v))) return false;
	v->a = a;
	rdl_count(v);
	ok = v->in && v->net;
	if (ok) rdl_start(v, plat_now_ms());
	ok = ok && dlq_running(DLQ_RA);
	rdl_icons_free(v);
	free(v);
	return ok;
}

menu_result radl_screen(app *a)
{
	rdl_page(a);
	return MENU_STAY;
}

menu_result radl_screen_from_downloads(app *a)
{
	g_rdl_back = true;
	rdl_page(a);
	g_rdl_back = false;
	return MENU_STAY;
}

bool radl_start_run(app *a, const systems_cfg *cfg, const char *only)
{
	if (dlq_running(DLQ_RA)) return true;
	if (!ra_signed_in() || !net_online()) return false;
	g_ra_sys = *cfg;
	snprintf(g_ra_only, sizeof g_ra_only, "%s", only ? only : "");
	g_ra_t0 = plat_now_ms();
	g_ra_badges = 0;
	RA_LANE.card = P_ROMS;
	(void)a;
	return dlq_start(DLQ_RA, &RA_LANE, ra_lane_begin);
}

void radl_last(radl_progress *out)
{
	*out = g_ra_last;
}

/* ---- a shot's page (--radl-page STATE) -------------------------------------
 * The real drawing on stand-in systems, without a card or a run: 0 the plan,
 * 1 the plan with nothing left to download, 2 signed out, 3 no Wi-Fi, 4 the
 * run, 5 the run waiting for Wi-Fi, 6 the run paused by RetroAchievements,
 * 7 the run identifying a system's new games, 8 done, 9 stopped by the
 * account refused, 10 stopped by B, 11 stopped by no answer, 12 the run
 * asking again after no answer, with a set of one achievement, 13 the plan
 * of one system with one game that has none, 14 done with one game and one
 * unlock (the counts' singulars). Badges from ZYPEROS_LOOK_BADGES, a folder whose
 * badges/ holds PNGs, when a shot has one; the plain badge otherwise. */
int shot_radl_page = -1;

static void rdl_fixture(rdl_view *v, app *a, int state)
{
	static const struct { const char *folder, *name, *tag; int ready, todo, none; } SYS[10] = {
		{ "NES", "NES", "NES", 84, 0, 6 },
		{ "SNES", "SNES", "SFC", 61, 12, 3 },
		{ "Game Boy", "Game Boy", "GB", 40, 0, 2 },
		{ "Game Boy Advance", "Game Boy Advance", "GBA", 52, 18, 4 },
		{ "Genesis", "Genesis", "MD", 47, 9, 5 },
		{ "PlayStation", "PlayStation", "PS1", 15, 31, 2 },
		{ "Nintendo 64", "Nintendo 64", "N64", 13, 22, 1 },
		{ "Neo Geo Pocket Color", "Neo Geo Pocket Color", "NGPC", 0, 12, 0 },
		{ "Arcade", "Arcade", "ARCADE", 0, 16, 0 },
		{ "PSP", "PSP", "PSP", 0, 0, 0 },
	};
	static const char *GAMES[] = { "Mega Man 3", "Mega Man 2" };
	const char *dir = getenv("ZYPEROS_LOOK_BADGES");
	char names[RDL_RING][24];
	int nn = 0, i;

	memset(v, 0, sizeof *v);
	v->a = a;
	v->fixture = true;
	v->in = state != 2;
	v->net = state != 3;
	v->free = 11200000000ULL;
	for (i = 0; i < 10; i++) {
		rdl_sys *s = &v->s[v->n++];

		snprintf(s->folder, sizeof s->folder, "%s", SYS[i].folder);
		snprintf(s->name, sizeof s->name, "%s", SYS[i].name);
		snprintf(s->key, sizeof s->key, "%s", home_art_key(SYS[i].tag));
		s->c.ready = SYS[i].ready;
		s->c.todo = state == 1 || state == 8 || state == 9 ? 0 : SYS[i].todo;
		s->c.none = SYS[i].none;
		if (state == 8 || state == 9) s->c.ready += SYS[i].todo * 7 / 10, s->c.none += SYS[i].todo * 3 / 10;
		s->c.games = s->c.ready + s->c.todo + s->c.none;
	}
	/* The badges: a folder's, or names the card has no art for */
	if (dir && *dir) {
		char bdir[1024];
		DIR *d;
		struct dirent *e;

		snprintf(bdir, sizeof bdir, "%s/badges", dir);
		if ((d = opendir(bdir))) {
			while ((e = readdir(d)) && nn < RDL_RING) {
				const char *dot = strrchr(e->d_name, '.');

				if (e->d_name[0] == '.' || !dot || strcmp(dot, ".png")) continue;
				snprintf(names[nn++], sizeof names[0], "%.*s",
				         (int)(dot - e->d_name < 23 ? dot - e->d_name : 23), e->d_name);
			}
			closedir(d);
		}
		snprintf(v->cur_set, sizeof v->cur_set, "%s/fixture.set", dir);
	} else {
		snprintf(v->cur_set, sizeof v->cur_set, "/nonexistent/fixture.set");
	}
	for (; nn < RDL_RING; nn++) snprintf(names[nn], sizeof names[0], "none%d", nn);

	v->where = (state >= 4 && state <= 7) || state == 12 ? RDL_RUN
	         : (state >= 8 && state <= 11) || state == 14 ? RDL_DONE : RDL_PLAN;
	v->ended = state == 9 ? RDL_REFUSED : state == 10 || state == 11 ? RDL_STOPPED : RDL_ENDED;
	v->p.games = 553;
	v->p.games_done = 214;
	v->p.ready = 341;
	v->p.fetched = 41;
	v->p.updated = 2;
	v->p.none = 23;
	v->p.unlocks = 57;
	v->p.state = state == 5 ? RADL_NO_NETWORK : state == 6 ? RADL_SLOWED : state == 7 ? RADL_HASHING
	           : state == 12 ? RADL_RETRYING : RADL_WORKING;
	v->p.hashing = 31;
	v->p.hashed = 12;
	snprintf(v->p.folder_now, sizeof v->p.folder_now, "%s", state == 7 ? "PlayStation" : "NES");
	snprintf(v->p.now, sizeof v->p.now, "%s", GAMES[0]);
	snprintf(v->cur_title, sizeof v->cur_title, "%s", GAMES[1]);
	v->cur_count = 82;
	for (v->ncur = 0; v->ncur < RDL_SHOW; v->ncur++)
		snprintf(v->cur_badge[v->ncur], sizeof v->cur_badge[0], "%.23s", names[v->ncur]);
	for (v->nring = 0; v->nring < RDL_RING; v->nring++) {
		snprintf(v->ring[v->nring].set, sizeof v->ring[0].set, "%s", v->cur_set);
		snprintf(v->ring[v->nring].badge, sizeof v->ring[0].badge, "%.23s", names[RDL_RING - 1 - v->nring]);
	}
	v->land_at = 1000000;
	v->shot_now = v->land_at + 5000;
	v->t0 = v->shot_now - 120000;
	v->g0 = 100;
	v->p.resume_at = v->shot_now + 42000;
	if (state == 12) {
		v->cur_count = 1;
		v->ncur = 1;
		v->p.resume_at = v->shot_now + 12000;
	}
	if (state == 13 || state == 14) {
		/* One system, one of each */
		v->n = 1;
		v->s[0].c.ready = state == 14 ? 1 : 3;
		v->s[0].c.todo = state == 14 ? 0 : 2;
		v->s[0].c.none = 1;
		v->s[0].c.games = v->s[0].c.ready + v->s[0].c.todo + 1;
		v->p.unlocks = 1;
		v->p.fetched = 1;
		v->nring = 1;
	}
	if (state >= 9 && state <= 11) {
		v->p.problem = state == 9 ? RADL_SIGN_IN : state == 11 ? RADL_NO_ANSWER : RADL_OK;
		snprintf(v->problem, sizeof v->problem, "%s",
		         state == 9 ? T(S_RADL_REFUSED) : state == 11 ? T(S_RADL_NO_ANSWER) : "");
		v->p.unlocks = 0;
		v->nring = 5;
	}
}

void radl_page_shot(app *a)
{
	static rdl_view v;

	rdl_fixture(&v, a, shot_radl_page);
	rdl_draw(&v, v.shot_now);
	rdl_icons_free(&v);
}
