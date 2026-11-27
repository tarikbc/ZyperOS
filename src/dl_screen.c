/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Downloads: src/dl_screen.h */
#include "dl_screen.h"

#include "aout_route.h"
#include "artscrape.h"
#include "db.h"
#include "dlcount.h"
#include "dlq.h"
#include "ds/flex.h"
#include "ds/layout.h"
#include "ds/mark.h"
#include "ds/page.h"
#include "home.h"
#include "media_screen.h"
#include "mediatex.h"
#include "menu_pick.h"
#include "net.h"
#include "cardgen.h"
#include "cheatsdl.h"
#include "cheevos.h"
#include "ssrun.h"
#include "radl.h"
#include "radl_screen.h"
#include "rahashjob.h"
#include "rafetch.h"
#include "scraper_screen.h"
#include "sgdb.h"
#include "signin_screen.h"
#include "snaps.h"
#include "ss.h"
#include "sys_menu.h"
#include "uisound.h"
#include "usbdrive_screen.h"
#include "zyperdraw.h"

#include <SDL.h>
#include <SDL_image.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---------- MENU > Downloads: one page (DESIGN.md 11.53) ----------------------
 *
 * The page in the downloads lab, as I picked it (2026-11-19): a page drawn on
 * the ground with Zyper beside its heading (the Flash Drive pages' frame,
 * src/ds/page.h), not a MENU panel. Its views, one over another:
 *
 *   the hub      Download on top, a rule, then each kind with its mark and
 *                what it lacks; Left and Right take a kind in or out of
 *                Download and its size and time change at once; A opens a
 *                kind; over the keys the room a download takes, or where a
 *                kind comes from and its room on the card
 *   a kind       its systems' icons over their ticks and two tiles, what
 *                the card has and what a download adds (Achievements is its
 *                own 1.0.6 page, radl_screen.c)
 *   Systems      a row per system, a column per kind: a count, a tick or a
 *                dash; A switches a cell, X deletes it, Y opens its games
 *   a system's   its games, each with a mark per kind: lit, dim, struck
 *   games        out; A opens a game's panel
 *   the run      one stream for every lane: the game being asked about
 *                beside Zyper, the newest arrival landing big, the six
 *                before it moving along, each with its kind's mark; the
 *                counts per kind and the time left over the keys
 *   done         what came as a wall, the counts per kind, what is left
 *
 * Settings (START), its accounts, Video Snaps, Names and Details and On the
 * Card, and a game's panel are panels over the page.
 *
 * The page counts the card a system a frame (src/dlcount.h): it draws at
 * once, says Counting, and fills in; a count it made before stays up while
 * a new one goes. */

#define DL_ROW      DS_LINE_MENU   /* a row of the hub's list */
#define DL_RULE_H   DS_LIST_PAD_Y  /* the rule between Download and the kinds */
#define DL_MARK     40             /* a kind's mark before its name */
#define DL_ROW_PAD  DS_SECTION     /* a row's words in from its fill */
#define DL_COUNT_MS 6              /* the count's share of a frame */
#define DL_RING     14             /* arrivals kept: the run shows 7 (11 on 16:9), done 14 */
#define DL_LAND     520            /* an arrival's landing, ms (the Scraper's) */
#define DL_BIG      360            /* the newest's square */
#define DL_CELL     168            /* the ones before it, two rows */
#define DL_CELL_MIN_GAP 8          /* between them at least: 3 a row on 4:3, 5 on 16:9 */
#define DL_CELLS    ((DL_RING - 1) / 2 * 2)   /* the most the grid shows */
#define DL_CELL_GAP DS_GRID_GAP
#define DL_WALL     120            /* done's wall, two rows of seven */
/* A kind page's system slot and its tiles' height: the Scraper's and the
 * achievements page's own (SCP_SLOT_W, RDL_SLOT_W...), typed in all three so
 * the pages stay one picture; they move to tokens together */
#define DL_SLOT_W   62
#define DL_BAND     56
#define DL_TILE_H   220
#define DL_SYS_ROWS 5              /* Systems' rows on the screen */
#define DL_CELL_W   (DS_PAGE_TILE_W / DLK_KINDS)   /* a Systems column: the kinds share a tile's width */
#define DL_GAME_ROWS 5             /* a system's games on the screen */

static const ui_glyph KIND_MARK[DLK_KINDS] = { UI_GLYPH_COVER, UI_GLYPH_FILM, UI_GLYPH_BOOK, UI_GLYPH_TROPHY,
                                               UI_GLYPH_WAND };

static const char *kind_name(dl_kind k)
{
	switch (k) {
	case DLK_COVERS: return T(S_SCRAPER_PAGE_COVERS);
	case DLK_VIDEOS: return T(S_SCRAPER_PAGE_VIDEOS);
	case DLK_NAMES:  return T(S_SCRAPER_NAMES_DETAILS);
	case DLK_CHEATS: return T(S_DOWNLOADS_KIND_CHEATS);
	default:         return T(S_SYSMENU_CHEEVOS);
	}
}

enum { V_HUB, V_KIND, V_SYSTEMS, V_GAMES, V_RUN, V_DONE };
enum { END_DONE, END_STOPPED, END_SIGN_IN };

typedef struct {
	int v;
	int sel;                 /* the hub's row (0 Download, 1 + a kind), a games row */
	int kind;                /* a kind page's */
	int row, col, win;       /* Systems' cell and window; a games list's window */
	char sys[CFG_STR];       /* a system's games: its folder */
	int fill_from;           /* the focus fill's glide: from this y, since t0 */
	unsigned fill_t0;
} dl_frame;

typedef struct {
	SDL_Texture *tex;
	int w, h;
	unsigned key;
	int kind;                /* dl_kind */
	int count;               /* a set's achievements */
	char game[128];
	char folder[CFG_STR];    /* its system, for its name card */
	char path[LIB_PATH * 3]; /* its picture's file, "" for its card */
	bool card;               /* its picture is its name card */
	bool asked;              /* its picture asked of this opening's threads */
} dl_arrival;

/* A game of a system's games list, as the card has it */
enum { GS_HAVE, GS_MISS, GS_NONE, GS_OFF };
typedef struct {
	char name[LIB_NAME], title[LIB_NAME], file[LIB_PATH];
	unsigned char st[DLK_KINDS];
	int year;
	int sets;                /* its set's achievements, 0 not known */
	int gi;                  /* its place in the shelf's list */
} dl_game;

typedef struct {
	app *a;
	char only[CFG_STR];      /* a console's page: its folder */
	bool fixture;
	dl_frame st[8];
	int depth;
	const dl_counts *c;
	unsigned gen;
	struct { char key[CFG_STR]; SDL_Texture *t; } icon[CFG_MAX_SYSTEMS + 4];
	int nicon;
	/* the run this page shows; what came is g_rec's */
	int run_kind;            /* -1 every kind that is in, else the one */
	bool art, ra, cht;       /* its lanes */
	art_progress ap;
	radl_progress rp;
	cheats_progress cp;
	int art_total, art_looks;
	unsigned rec_key;        /* the newest arrival it has taken */
	unsigned open_key;       /* the newest when it opened: older ones do not land again */
	unsigned land_at;
	unsigned test_pass;      /* its loop's passes since it opened (dl_test) */
	unsigned t0;
	int done0;
	int end;
	char why[160];
	int pending;             /* a download to start once counted: -2 none */
	/* a system's games */
	dl_game *games;
	int ngames;
	unsigned shot_now;
} dl_view;

/* The lanes' ends this page (or the news) has already said */
static unsigned g_shown_art, g_shown_ra, g_shown_cht;

/* ---- the cheats' lane (src/dlq.h): cheatsdl.c's run, stepped on its own
 * network slot, side by side with the other two ---- */
static systems_cfg g_cht_sys;
static char g_cht_only[CFG_STR];
static cheats_progress g_cht_last;

static int g_cht_games;

static bool cht_lane_begin(void)
{
	return cheats_begin(&g_cht_sys, g_cht_only[0] ? g_cht_only : NULL, g_cht_games);
}

static int cht_lane_step(unsigned now_ms)
{
	(void)now_ms;
	return cheats_step();
}

static void cht_lane_done(dlq_end how)
{
	(void)how;
	cheats_status(&g_cht_last);
	if (cheats_running()) cheats_cancel();
}

static int cht_lane_pct(void)
{
	cheats_progress p;

	cheats_status(&p);
	return p.games > 0 ? (int)((long long)p.games_done * 100 / p.games) : 0;
}

static dlq_lane_def CHT_LANE = {
	.name = "cheats", .slot = NET_SLOT_CHT,
	.step = cht_lane_step, .done = cht_lane_done, .pct = cht_lane_pct,
	.every_ms = 8,
};

/* ---- a stand-in run, for a test of the page (dl_test's fake=N) ----
 * N arrivals on the art lane, one every DL_FAKE_MS, of the card's own games
 * in shelf order: a cover where the game has one, else its names (a card).
 * The page, the queue and what the page keeps run as they do, with no
 * network; only what the art run would have said comes from here. It can
 * hold after its Kth (hold=K) and go on (away=), so a test knows what the
 * page had when it was left by what it did, not by a clock. */
#define DL_FAKE_MS   150
#define DL_FAKE_MAX  40
static struct {
	bool on, was;            /* running; ran since the launcher started */
	bool held;               /* holding after its hold_at'th arrival */
	int n, i, ngames, hold_at;
	unsigned at;
	art_progress ap, last;
	struct { char folder[CFG_STR], name[LIB_NAME]; } g[DL_FAKE_MAX];
} g_fake;

static void fake_next(void)
{
	snprintf(g_fake.ap.now, sizeof g_fake.ap.now, "%s", g_fake.g[g_fake.i % g_fake.ngames].name);
	snprintf(g_fake.ap.folder_now, sizeof g_fake.ap.folder_now, "%s", g_fake.g[g_fake.i % g_fake.ngames].folder);
}

static bool fake_begin(void)
{
	memset(&g_fake.ap, 0, sizeof g_fake.ap);
	g_fake.i = 0;
	g_fake.at = plat_now_ms();
	g_fake.on = g_fake.was = true;
	g_fake.held = false;
	g_fake.hold_at = 0;
	fake_next();
	return true;
}

static int fake_step(unsigned now_ms)
{
	char path[LIB_PATH * 3] = "";
	const char *folder, *name;

	(void)now_ms;
	if (g_fake.held) g_fake.at = plat_now_ms();
	if (g_fake.held || plat_now_ms() - g_fake.at < DL_FAKE_MS) return 1;
	g_fake.at = plat_now_ms();
	folder = g_fake.g[g_fake.i % g_fake.ngames].folder;
	name = g_fake.g[g_fake.i % g_fake.ngames].name;
	if (has_box_art(folder, name)) box_art_path(folder, name, path, sizeof path);
	snprintf(g_fake.ap.found_path, sizeof g_fake.ap.found_path, "%s", path);
	snprintf(g_fake.ap.found_game, sizeof g_fake.ap.found_game, "%s", name);
	snprintf(g_fake.ap.found_folder, sizeof g_fake.ap.found_folder, "%s", folder);
	g_fake.ap.found_names = !path[0];
	if (path[0]) g_fake.ap.found++; else g_fake.ap.named++;
	g_fake.ap.found_seq++;
	g_fake.ap.looked++;
	g_fake.i++;
	if (g_fake.i >= g_fake.n) return 0;
	if (g_fake.i == g_fake.hold_at) g_fake.held = true;
	fake_next();
	return 1;
}

static void fake_done(dlq_end how)
{
	(void)how;
	g_fake.last = g_fake.ap;
	g_fake.on = false;
}

static int fake_pct(void)
{
	return g_fake.n ? g_fake.i * 100 / g_fake.n : 0;
}

static dlq_lane_def FAKE_LANE = {
	.name = "box art (a test's stand-in)", .slot = NET_SLOT_ART,
	.step = fake_step, .done = fake_done, .pct = fake_pct,
	.every_ms = 8,
};

/* The art lane's progress, and its last run's: the art run's, or the
 * stand-in's while it runs (and after, for its done) */
static void art_lane_status(art_progress *p)
{
	if (g_fake.on) *p = g_fake.ap;
	else art_status(p);
}

static void art_lane_last(art_progress *p)
{
	if (g_fake.was) *p = g_fake.last;
	else scraper_last(p);
}

static dl_frame *top_of(dl_view *v) { return &v->st[v->depth - 1]; }

static void push(dl_view *v, int view)
{
	if (v->depth >= (int)(sizeof v->st / sizeof v->st[0])) return;
	memset(&v->st[v->depth], 0, sizeof v->st[0]);
	v->st[v->depth].v = view;
	v->st[v->depth].fill_from = -1;
	v->depth++;
}

/* A shot's network, lanes and progress (downloads_shot), -1 the real ones */
static int g_fx_net = -1, g_fx_art = -1, g_fx_ra = -1, g_fx_cht = -1, g_fx_pct = -1;
static const char *g_fx_user;

static bool online(void)
{
	return g_fx_net >= 0 ? g_fx_net != 0 : net_online();
}

static bool lane_on(dlq_lane l)
{
	int fx = l == DLQ_ART ? g_fx_art : l == DLQ_RA ? g_fx_ra : g_fx_cht;

	return fx >= 0 ? fx != 0 : dlq_running(l);
}

static bool dl_live(void)
{
	return lane_on(DLQ_ART) || lane_on(DLQ_RA) || lane_on(DLQ_CHT);
}

/* ---- the systems' icons --------------------------------------------------------- */

static SDL_Texture *icon_of(dl_view *v, const char *key)
{
	char path[CFG_STR * 2];
	int i;

	if (!key || !key[0]) return NULL;
	for (i = 0; i < v->nicon; i++) if (!strcmp(v->icon[i].key, key)) return v->icon[i].t;
	if (v->nicon >= (int)(sizeof v->icon / sizeof v->icon[0])) return NULL;
	snprintf(path, sizeof path, "%s/system-icons/%s-still.png", v->fixture ? "res" : P_ROOT, key);
	snprintf(v->icon[v->nicon].key, sizeof v->icon[0].key, "%s", key);
	if ((v->icon[v->nicon].t = IMG_LoadTexture(v->a->r, path))) {
		SDL_SetTextureScaleMode(v->icon[v->nicon].t, SDL_ScaleModeNearest);
		SDL_SetTextureBlendMode(v->icon[v->nicon].t, SDL_BLENDMODE_BLEND);
	}
	return v->icon[v->nicon++].t;
}

static void icon_draw(dl_view *v, const char *key, int cx, int cy, int size, Uint8 al)
{
	SDL_Texture *t = icon_of(v, key);
	int w, h;

	if (!t) return;
	SDL_QueryTexture(t, NULL, NULL, &w, &h);
	if (w >= h) { h = h * size / w; w = size; } else { w = w * size / h; h = size; }
	SDL_SetTextureAlphaMod(t, al);
	SDL_RenderCopy(v->a->r, t, NULL, &(SDL_Rect){ cx - w / 2, cy - h / 2, w, h });
	SDL_SetTextureAlphaMod(t, 255);
}

static void icons_free(dl_view *v)
{
	int i;

	for (i = 0; i < v->nicon; i++) if (v->icon[i].t) SDL_DestroyTexture(v->icon[i].t);
	v->nicon = 0;
}

static const dl_sys *sys_of(const dl_view *v, const char *folder)
{
	int i;

	for (i = 0; v->c && folder && i < v->c->n; i++)
		if (!strcmp(v->c->s[i].folder, folder)) return &v->c->s[i];
	return NULL;
}

/* ---- words ------------------------------------------------------------------------ */

static void about_time(double secs, char *out, size_t n)
{
	char t[32];
	int m = (int)((secs + 59.0) / 60.0);

	if (m < 1) m = 1;
	if (m < 60) snprintf(t, sizeof t, T(S_CORNER_TIME_MIN), m);
	else if (m % 60 == 0) snprintf(t, sizeof t, T(S_CORNER_TIME_H), m / 60);
	else snprintf(t, sizeof t, T(S_CORNER_TIME_H_MIN), m / 60, m % 60);
	snprintf(out, n, T(S_SCRAPER_ABOUT_TIME), t);
}

static void time_left(double secs, char *out, size_t n)
{
	int m = (int)((secs + 59.0) / 60.0);

	if (m < 1) m = 1;
	if (m < 60) snprintf(out, n, T(S_CORNER_TIME_MIN), m);
	else if (m % 60 == 0) snprintf(out, n, T(S_CORNER_TIME_H), m / 60);
	else snprintf(out, n, T(S_CORNER_TIME_H_MIN), m / 60, m % 60);
}

static void bytes_text(double b, char *out, size_t n)
{
	sys_menu_bytes((unsigned long long)(b > 0 ? b : 0), out, n);
}

/* How far the running lanes are, together */
static int run_pct(void)
{
	return g_fx_pct >= 0 ? g_fx_pct : dlq_pct();
}

static int lane_pct(dlq_lane l)
{
	return g_fx_pct >= 0 ? (l == DLQ_ART ? 37 : l == DLQ_RA ? 52 : 64) : dlq_lane_pct(l);
}

/* The Download row's value and MENU's (downloads_menu_value) */
static void download_value(const dl_counts *c, char *out, size_t n)
{
	dl_plan pl;
	char sz[24], tm[48];

	if (dl_live()) { snprintf(out, n, T(S_DOWNLOADS_RUNNING), run_pct()); return; }
	if (!c) { snprintf(out, n, "%s", T(S_DOWNLOADS_COUNTING)); return; }
	if (!online()) { snprintf(out, n, "%s", T(S_MENU_NEEDS_WIFI)); return; }
	dl_plan_of(c, -1, &pl);
	if (!pl.n) { snprintf(out, n, "%s", T(S_DOWNLOADS_NOTHING)); return; }
	bytes_text(pl.bytes, sz, sizeof sz);
	about_time(pl.secs, tm, sizeof tm);
	snprintf(out, n, "%s \xc2\xb7 %s", sz, tm);
}

/* The lane a kind goes on, and whether it runs */
static dlq_lane lane_of(dl_kind k)
{
	return k == DLK_ACH ? DLQ_RA : k == DLK_CHEATS ? DLQ_CHT : DLQ_ART;
}

static bool kind_running(dl_kind k)
{
	return lane_on(lane_of(k));
}

static void kind_value(const dl_counts *c, dl_kind k, char *out, size_t n)
{
	dl_sum s;
	const char *b;

	if (!c) { snprintf(out, n, "%s", T(S_DOWNLOADS_COUNTING)); return; }
	if (!dl_kind_in(k)) { snprintf(out, n, "%s", T(S_SCRAPER_OFF)); return; }
	if ((b = dl_kind_blocked(c, k))) { snprintf(out, n, "%s", b); return; }
	dl_sum_of(c, k, &s);
	if (!s.on) { snprintf(out, n, "%s", T(S_SCRAPER_OFF)); return; }
	if (kind_running(k)) {
		/* the number alone: Download's row over it says what goes on */
		snprintf(out, n, "%d%%", lane_pct(lane_of(k)));
		return;
	}
	if (!s.todo) {
		snprintf(out, n, "%s", k == DLK_ACH || k == DLK_CHEATS ? T(S_DOWNLOADS_READY) : T(S_SCRAPER_COMPLETE));
		return;
	}
	switch (k) {
	case DLK_COVERS: snprintf(out, n, TN(S_DOWNLOADS_MISSING_N, s.todo), s.todo); break;
	case DLK_VIDEOS: snprintf(out, n, TN(S_DOWNLOADS_TO_FETCH_N, s.todo), s.todo); break;
	case DLK_NAMES:  snprintf(out, n, TN(S_DOWNLOADS_TO_LOOK_UP_N, s.todo), s.todo); break;
	case DLK_CHEATS: snprintf(out, n, TN(S_DOWNLOADS_TO_CHECK_N, s.todo), s.todo); break;
	default:         snprintf(out, n, TN(S_DOWNLOADS_TO_DOWNLOAD_N, s.todo), s.todo); break;
	}
}

/* Where a kind comes from and its room on the card: the line over the keys
 * on its row */
static void kind_facts(const dl_counts *c, dl_kind k, char *out, size_t n)
{
	dl_sum s;
	char sz[24], oc[64];

	dl_sum_of(c, k, &s);
	bytes_text((double)s.bytes, sz, sizeof sz);
	snprintf(oc, sizeof oc, T(S_DOWNLOADS_ON_CARD), sz);
	/* a line of two facts: each said drawn, for the shots' check */
	ui_fit_drawn(oc);
	switch (k) {
	case DLK_COVERS:
		snprintf(out, n, "%s \xc2\xb7 %s", oc, T(S_DOWNLOADS_LIBRETRO_FIRST));
		ui_fit_drawn(T(S_DOWNLOADS_LIBRETRO_FIRST));
		break;
	case DLK_VIDEOS: {
		static const str_id FOR[SNAPS_FOR_COUNT] = { S_SCRAPER_ALL_GAMES, S_LIBRARY_FAVORITES, S_SCRAPER_PLAYED };
		char on[96];

		if (!s.on) { snprintf(out, n, "%s", oc); break; }
		snprintf(on, sizeof on, TN(S_DOWNLOADS_FOR_ON_N, s.on), T(FOR[snaps_for()]), s.on);
		snprintf(out, n, "%s \xc2\xb7 %s", on, oc);
		ui_fit_drawn(on);
		break;
	}
	case DLK_NAMES:
		snprintf(out, n, "%s", T(S_DOWNLOADS_ONE_REQUEST));
		break;
	case DLK_CHEATS:
		snprintf(out, n, "%s", T(S_DOWNLOADS_FROM_LIBRETRO_DB));
		break;
	default:
		snprintf(out, n, "%s \xc2\xb7 %s", T(S_DOWNLOADS_FROM_RA), oc);
		ui_fit_drawn(T(S_DOWNLOADS_FROM_RA));
		break;
	}
}

/* The line a Pixel 2 with no network adds: it has no Wi-Fi inside */
static const char *no_net_line(void)
{
	return plat_profile()->wifi == DEV_WIFI_BUILTIN ? T(S_DOWNLOADS_NO_NET_BUILTIN) : T(S_DOWNLOADS_NO_NET_ADAPTER);
}

/* ---- drawing: the parts ------------------------------------------------------------ */

static float ease_out(float t)
{
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
}

static void foot(dl_view *v, const ds_page_frame *pf, const char *const *k, const char *const *w, int n)
{
	(void)v;
	ds_page_foot(v->a->r, pf, k, w, n, DS_PAGE_FOOT_GAP, DS_PAGE_FOOT_GAP_TIGHT);
}

/* A row of the hub's list (and a system's games): its fill when it is the
 * cursor's, a mark, its words and its value */
typedef struct {
	ui_glyph mark;
	const char *label, *value;
	bool off;
} dl_row;

static void rows_draw(dl_view *v, dl_frame *f, const dl_row *rows, int n, int rule_after, int y0, int sel,
                      unsigned now)
{
	app *a = v->a;
	const ds_theme *t = theme();
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	int x0 = DS_INSET + 8, x1 = ZYPEROS_SCREEN_W - DS_INSET - 8;
	int i, y = y0, fy = y0;
	char lab[160], val[96];

	for (i = 0; i < n; i++) {
		if (i == sel) fy = y;
		y += DL_ROW + (i == rule_after ? DL_RULE_H : 0);
	}
	/* the fill glides to its row over quick */
	if (sel >= 0) {
		float e = f->fill_from >= 0 ? ease_out((float)(now - f->fill_t0) / DS_MS_QUICK) : 1.0f;
		int yy = f->fill_from >= 0 ? (int)(f->fill_from + (fy - f->fill_from) * e) : fy;

		ui_round_rect(a->r, &(SDL_Rect){ x0, yy, x1 - x0, DL_ROW }, DS_RADIUS_ROW, ui_col(t->accent, 255));
		if (e >= 1.0f) f->fill_from = -1;
	}
	for (y = y0, i = 0; i < n; i++) {
		bool on = i == sel;
		SDL_Color ink = on ? ui_col(t->on_accent, 255) : rows[i].off ? UI_TEXT_DIM : UI_TEXT_SOFT;
		SDL_Color vk = on ? ui_col(t->on_accent, 255) : UI_TEXT_DIM;
		int cy = y + DL_ROW / 2, lx = x0 + DL_ROW_PAD, vw = 0;

		if (rows[i].value && rows[i].value[0]) {
			ui_fit_text(fs, rows[i].value, val, sizeof val, (x1 - x0) / 2);
			vw = ui_text_width(fs, val);
			ui_text(a->r, fs, val, x1 - DL_ROW_PAD, ui_cap_y(fs, cy), 1, vk);
		}
		if (rows[i].mark != UI_GLYPH_COUNT) {
			ui_glyph_draw(a->r, rows[i].mark, lx + DL_MARK / 2, cy, DL_MARK, ink);
			lx += DL_MARK + DS_LIST_ROW_GAP;
		}
		ui_fit_text(fm, rows[i].label, lab, sizeof lab, x1 - DL_ROW_PAD - vw - 16 - lx);
		ui_text(a->r, fm, lab, lx, ui_cap_y(fm, cy), -1, ink);
		y += DL_ROW;
		if (i == rule_after) {
			SDL_SetRenderDrawColor(a->r, t->edge.r, t->edge.g, t->edge.b, 255);
			SDL_RenderFillRect(a->r, &(SDL_Rect){ x0 + DL_ROW_PAD, y + DL_RULE_H / 2 - 1, x1 - x0 - 2 * DL_ROW_PAD, 2 });
			y += DL_RULE_H;
		}
	}
}

static void glide(dl_frame *f, int from_y, unsigned now)
{
	f->fill_from = from_y;
	f->fill_t0 = now;
}

/* A tile's colour for a game with no picture: a hue of its name, the run's
 * stand-in and done's */
static SDL_Color hue_of(const char *s, int light)
{
	unsigned h = 7;
	float hue, r, g, b, c, x;
	int i;

	for (; s && *s; s++) h = h * 31 + (unsigned char)*s;
	hue = (float)(h % 360) / 60.0f;
	c = 0.42f;
	x = c * (1.0f - (float)fabs(fmodf(hue, 2.0f) - 1.0f));
	i = (int)hue;
	r = g = b = 0;
	switch (i) {
	case 0: r = c; g = x; break;
	case 1: r = x; g = c; break;
	case 2: g = c; b = x; break;
	case 3: g = x; b = c; break;
	case 4: r = x; b = c; break;
	default: r = c; b = x; break;
	}
	return (SDL_Color){ (Uint8)((r + 0.18f + light * 0.04f) * 255), (Uint8)((g + 0.18f) * 255),
	                    (Uint8)((b + 0.18f) * 255), 255 };
}

/* An arrival in `box`: its cover whole (never cut, never stretched), or a
 * tile of its name's hue; its kind's mark on the top right corner, with the
 * set's count on the big one */
static void arrival_draw(dl_view *v, const dl_arrival *it, SDL_FRect box, Uint8 al, bool lit)
{
	app *a = v->a;
	const ds_theme *t = theme();
	TTF_Font *fc = ui_font(UI_F_META);
	SDL_Rect q;
	float sw = box.w, sh = box.h, sc;
	bool big = box.w >= 200;

	if (!al) return;
	if (it->tex && it->w > 0 && it->h > 0) {
		sc = box.w / (float)it->w;
		if ((float)it->h * sc > box.h) sc = box.h / (float)it->h;
		sw = it->w * sc;
		sh = it->h * sc;
	}
	q = (SDL_Rect){ (int)(box.x + (box.w - sw) / 2), (int)(box.y + (box.h - sh) / 2), (int)sw, (int)sh };
	if (lit) ui_round_rect(a->r, &(SDL_Rect){ q.x - 7, q.y - 7, q.w + 14, q.h + 14 }, 16, ui_col(t->accent, al));
	if (it->tex) {
		SDL_SetTextureAlphaMod(it->tex, al);
		SDL_RenderCopy(a->r, it->tex, NULL, &q);
		SDL_SetTextureAlphaMod(it->tex, 255);
	} else {
		SDL_Color c = hue_of(it->game, 0);
		char nm[96];

		c.a = al;
		ui_round_rect(a->r, &q, DS_RADIUS_ROW, c);
		ui_fit_text(fc, it->game, nm, sizeof nm, q.w - 32);
		if (big) ui_text(a->r, fc, nm, q.x + 24, ui_cap_y(fc, q.y + q.h - 40), -1, ui_fade(ui_col((ds_rgb){ 255, 255, 255 }, 255), al / 255.0f));
	}
	if (it->kind == DLK_COVERS) return;
	if (big && it->count > 0) {
		/* the trophy and the count, a lime pill */
		char n[16];
		int tw, pw, px, py = q.y + 14;

		snprintf(n, sizeof n, "%d", it->count);
		tw = ui_text_width(fc, n);
		pw = 36 + 8 + tw + 28;
		px = q.x + q.w - 14 - pw;
		ui_round_rect(a->r, &(SDL_Rect){ px, py, pw, 56 }, 28, ui_col(t->accent, al));
		ui_glyph_draw(a->r, KIND_MARK[it->kind], px + 14 + 18, py + 28, 36, ui_col(t->on_accent, al));
		ui_text(a->r, fc, n, px + 14 + 36 + 8, ui_cap_y(fc, py + 28), -1, ui_col(t->on_accent, al));
	} else {
		int d = big ? DS_BADGE : DS_BADGE_SMALL;

		ds_badge(a->r, q.x + q.w - d / 2 - 8, q.y + d / 2 + 8, d, KIND_MARK[it->kind], true, al);
	}
}

/* Marks and numbers in a line centred on `cx`: the counts per kind, and the
 * time left beside a clock when `eta` is given */
static void stats_draw(dl_view *v, int cx, int cy, const int got[DLK_KINDS], const bool shown[DLK_KINDS],
                       const char *eta)
{
	app *a = v->a;
	TTF_Font *f = ui_font(UI_F_MENU);
	char num[DLK_KINDS][16];
	int i, w = 0, x;

	for (i = 0; i < DLK_KINDS; i++) {
		snprintf(num[i], sizeof num[i], "%d", got[i]);
		if (shown[i]) w += DS_MARK_STAT + 8 + ui_text_width(f, num[i]) + 32;
	}
	if (eta) w += DS_MARK_STAT + 8 + ui_text_width(f, eta);
	else w -= 32;
	x = cx - w / 2;
	for (i = 0; i < DLK_KINDS; i++) {
		if (!shown[i]) continue;
		ui_glyph_draw(a->r, KIND_MARK[i], x + DS_MARK_STAT / 2, cy, DS_MARK_STAT, UI_TEXT_SOFT);
		x += DS_MARK_STAT + 8;
		ui_text(a->r, f, num[i], x, ui_cap_y(f, cy), -1, UI_TEXT);
		ui_fit_drawn(num[i]);
		x += ui_text_width(f, num[i]) + 32;
	}
	if (eta) {
		ui_glyph_draw(a->r, UI_GLYPH_CLOCK, x + DS_MARK_STAT / 2, cy, DS_MARK_STAT, UI_TEXT_SOFT);
		x += DS_MARK_STAT + 8;
		ui_text(a->r, f, eta, x, ui_cap_y(f, cy), -1, UI_TEXT);
	}
}

/* ---- the hub ------------------------------------------------------------------------ */

static const char *page_title(dl_view *v)
{
	const dl_sys *s = v->only[0] ? sys_of(v, v->only) : NULL;
	int i;

	if (!v->only[0]) return T(S_DOWNLOADS_HEADING);
	if (s) return s->name;
	for (i = 0; i < v->a->sys.count; i++)
		if (!strcmp(v->a->sys.systems[i].folder, v->only)) return v->a->sys.systems[i].name;
	return v->only;
}

/* The heading's second line, as every view of the page says it */
static void sub_of(dl_view *v, int only, char *out, size_t n, ui_glyph *mark)
{
	dl_plan pl;

	*mark = UI_GLYPH_COUNT;
	if (dl_live()) {
		*mark = UI_GLYPH_TO_CARD;
		snprintf(out, n, T(S_DOWNLOADS_RUNNING), run_pct());
		return;
	}
	if (!v->c) {
		int d, of;

		dl_count_progress(&d, &of);
		snprintf(out, n, "%s", T(S_DOWNLOADS_COUNTING));
		return;
	}
	if (!online()) {
		*mark = UI_GLYPH_WIFI;
		snprintf(out, n, "%s", T(S_MENU_NEEDS_WIFI));
		return;
	}
	dl_plan_of(v->c, only, &pl);
	if (!pl.n) {
		*mark = UI_GLYPH_CHECK;
		snprintf(out, n, "%s", T(S_DOWNLOADS_EVERYTHING_HERE));
		return;
	}
	*mark = UI_GLYPH_CLOCK;
	about_time(pl.secs, out, n);
}

static void hub_draw(dl_view *v, dl_frame *f, unsigned now)
{
	app *a = v->a;
	ds_page_frame pf = page_top(a, 1.0f);
	dl_row rows[1 + DLK_KINDS];
	char vals[1 + DLK_KINDS][96], sub[96], line[192];
	ui_glyph mark;
	int k, sel = f->sel;

	sub_of(v, -1, sub, sizeof sub, &mark);
	ds_page_head(a->r, &pf, DS_HEAD_SUB, page_title(v), sub, mark, now);
	download_value(v->c, vals[0], sizeof vals[0]);
	rows[0] = (dl_row){ UI_GLYPH_COUNT, T(S_RADL_KEY_DOWNLOAD), vals[0], false };
	for (k = 0; k < DLK_KINDS; k++) {
		kind_value(v->c, (dl_kind)k, vals[1 + k], sizeof vals[0]);
		rows[1 + k] = (dl_row){ KIND_MARK[k], kind_name((dl_kind)k), vals[1 + k],
		                        v->c && !strcmp(vals[1 + k], T(S_SCRAPER_OFF)) };
	}
	rows_draw(v, f, rows, 1 + DLK_KINDS, 0, pf.body_y, sel, now);
	/* Over the keys: why it cannot, the room a download takes, or where the
	 * kind under the cursor comes from */
	if (v->c && !online()) {
		ds_line_item it = { UI_GLYPH_COUNT, no_net_line(), DS_TONE_INFO };

		ds_page_line(a->r, &pf, &it, 1);
	} else if (v->c && sel == 0) {
		dl_plan pl;

		dl_plan_of(v->c, -1, &pl);
		if (pl.n) {
			char sz[24], fr[24], tf[64], fr2[64];
			ds_line_item it;

			bytes_text(pl.bytes, sz, sizeof sz);
			snprintf(tf, sizeof tf, T(S_SCRAPER_TO_FETCH), sz);
			if (v->c->free) {
				bytes_text((double)v->c->free, fr, sizeof fr);
				snprintf(fr2, sizeof fr2, T(S_STORAGE_FREE), fr);
				snprintf(line, sizeof line, "%s \xc2\xb7 %s", tf, fr2);
			} else {
				snprintf(line, sizeof line, "%s", tf);
			}
			it = (ds_line_item){ UI_GLYPH_TO_CARD, line, pl.fits ? DS_TONE_INFO : DS_TONE_WARN };
			ds_page_line(a->r, &pf, &it, 1);
		} else {
			ds_line_item it = { UI_GLYPH_CHECK, T(S_DOWNLOADS_OFFLINE_NOW), DS_TONE_INFO };

			ds_page_line(a->r, &pf, &it, 1);
		}
	} else if (v->c && sel > 0) {
		ds_line_item it = { UI_GLYPH_COUNT, line, DS_TONE_INFO };

		kind_facts(v->c, (dl_kind)(sel - 1), line, sizeof line);
		ds_page_line(a->r, &pf, &it, 1);
	}
	if (sel == 0) {
		const char *K[4] = { "A", "Y", "START", "B" };
		const char *W[4] = { dl_live() ? T(S_DOWNLOADS_KEY_OPEN) : T(S_RADL_KEY_DOWNLOAD), T(S_DOWNLOADS_KEY_SYSTEMS),
		                     T(S_HOME_SETTINGS), T(S_SCRAPER_KEY_CLOSE) };

		foot(v, &pf, K, W, 4);
	} else {
		const char *K[5] = { "A", "< >", "Y", "START", "B" };
		const char *W[5] = { T(S_DOWNLOADS_KEY_OPEN), T(S_DOWNLOADS_KEY_ON_OFF), T(S_DOWNLOADS_KEY_SYSTEMS),
		                     T(S_HOME_SETTINGS), T(S_SCRAPER_KEY_CLOSE) };

		foot(v, &pf, K, W, 5);
	}
}

/* ---- a kind's page: its systems and two tiles ------------------------------------ */

static void kind_draw(dl_view *v, dl_frame *f, unsigned now)
{
	app *a = v->a;
	const ds_theme *t = theme();
	TTF_Font *ft = ui_font(UI_F_TITLE), *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	dl_kind k = (dl_kind)f->kind;
	static ds_flex F;
	SDL_Rect sr[9], tq[2];
	dl_sum s = { 0 };
	dl_plan pl = { 0 };
	char sub[96], line[192], big[2][24], sz[2][32], ts[2][96];
	ui_glyph mark;
	int i, shown, more, slot0 = 0, tile0 = 0, n = v->c ? v->c->n : 0;
	const char *blocked = v->c ? dl_kind_blocked(v->c, k) : NULL;
	static const str_id LEFT[DLK_KINDS] = { S_DOWNLOADS_TILE_ON_CARD, S_DOWNLOADS_TILE_ON_CARD,
	                                        S_DOWNLOADS_TILE_NAMED, S_RADL_READY, S_RADL_READY };
	static const str_id RIGHT[DLK_KINDS] = { S_DOWNLOADS_TILE_MISSING, S_RADL_TO_DOWNLOAD,
	                                         S_DOWNLOADS_TILE_TO_LOOK_UP, S_RADL_TO_DOWNLOAD, S_RADL_TO_DOWNLOAD };

	sub_of(v, k, sub, sizeof sub, &mark);
	if (blocked && !dl_live()) { snprintf(sub, sizeof sub, "%s", blocked); mark = UI_GLYPH_COUNT; }
	ds_page_head(a->r, &pf, DS_HEAD_SUB, kind_name(k), sub, mark, now);
	if (v->c) {
		dl_sum_of(v->c, k, &s);
		dl_plan_of(v->c, k, &pl);
	}
	{
		const int root = ds_flex_begin(&F, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H,
		                               &(ds_box){ .dir = DS_COL, .pad = { pf.body.x, pf.body.y,
		                                          ZYPEROS_SCREEN_W - pf.body.x - pf.body.w,
		                                          ZYPEROS_SCREEN_H - pf.body.y - pf.body.h } });
		const int cell = (pf.body.w - DL_SLOT_W) / 8;
		const int slots = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .gap = cell - DL_SLOT_W, .h = 2 * DL_BAND });
		int tiles;

		for (i = 0; i < 9; i++) {
			int id = ds_flex_add(&F, slots, &(ds_box){ .w = DL_SLOT_W, .h = 2 * DL_BAND });

			if (!i) slot0 = id;
		}
		tiles = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .justify = DS_J_BETWEEN, .pad = { 0, 8, 0, 0 },
		                                         .h = 8 + DL_TILE_H });
		tile0 = ds_flex_add(&F, tiles, &(ds_box){ .w = ds_page_tile_w(), .h = DL_TILE_H });
		ds_flex_add(&F, tiles, &(ds_box){ .w = ds_page_tile_w(), .h = DL_TILE_H });
		ds_flex_solve(&F);
		for (i = 0; i < 9; i++) sr[i] = ds_flex_rect(&F, slot0 + i);
		tq[0] = ds_flex_rect(&F, tile0);
		tq[1] = ds_flex_rect(&F, tile0 + 1);
	}
	/* The systems: each icon over a tick, lit when it lacks nothing; dim
	 * when its switch is off. Nine at most, and how many more. */
	{
		int idx[CFG_MAX_SYSTEMS], m = 0;

		for (i = 0; i < n; i++) {
			const dl_sys *d = &v->c->s[i];

			if (k == DLK_ACH && !d->ach_sys) continue;
			if (k == DLK_CHEATS && !d->cht_sys) continue;
			if (k == DLK_NAMES && !d->names_ok && v->c->ss_in) continue;
			idx[m++] = i;
		}
		shown = m > 9 ? 8 : m;
		more = m - shown;
		for (i = 0; i < shown; i++) {
			const dl_sys *d = &v->c->s[idx[i]];
			bool on;
			int todo = dl_sys_todo(v->c, idx[i], k, &on);
			int cx = sr[i].x + DL_SLOT_W / 2, iy = sr[i].y + DL_BAND / 2, my = iy + DL_BAND;

			icon_draw(v, d->key, cx, iy, 60, on ? 255 : 90);
			ds_switch(a->r, cx, my, 28, UI_GLYPH_CHECK, on && !todo);
		}
		if (more > 0) {
			char mm[16];

			snprintf(mm, sizeof mm, "+%d", more);
			ui_text(a->r, fm, mm, sr[8].x + DL_SLOT_W / 2, ui_cap_y(fm, sr[8].y + DL_BAND / 2), 0, UI_TEXT_SOFT);
		}
	}
	/* The two tiles: what the card has, and what a download adds */
	snprintf(big[0], sizeof big[0], "%d", s.have);
	snprintf(big[1], sizeof big[1], "%d", s.todo);
	sz[0][0] = sz[1][0] = '\0';
	if (k != DLK_NAMES) bytes_text((double)s.bytes, sz[0], sizeof sz[0]);
	if (k != DLK_NAMES && s.todo && pl.bytes > 0) {
		char b[24];

		bytes_text(pl.bytes, b, sizeof b);
		snprintf(sz[1], sizeof sz[1], "+%s", b);
	}
	if (s.have_sys) snprintf(ts[0], sizeof ts[0], TN(S_SCRAPER_ON_N_SYSTEMS, s.have_sys), s.have_sys);
	else snprintf(ts[0], sizeof ts[0], "%s", T(S_DOWNLOADS_NONE_YET));
	if (!s.on) snprintf(ts[1], sizeof ts[1], "%s", T(S_DOWNLOADS_ALL_OFF));
	else if (!s.todo) snprintf(ts[1], sizeof ts[1], "%s", k == DLK_ACH || k == DLK_CHEATS ? T(S_RADL_ALL_CHECKED) : T(S_DOWNLOADS_COMPLETE_LOW));
	else if (k == DLK_VIDEOS) {
		static const str_id FOR[SNAPS_FOR_COUNT] = { S_SCRAPER_ALL_GAMES, S_LIBRARY_FAVORITES, S_SCRAPER_PLAYED };

		snprintf(ts[1], sizeof ts[1], "%s", T(FOR[snaps_for()]));
	} else if (k == DLK_ACH || k == DLK_CHEATS) snprintf(ts[1], sizeof ts[1], "%s", T(S_RADL_NEVER_CHECKED));
	else snprintf(ts[1], sizeof ts[1], TN(S_SCRAPER_ON_N_SYSTEMS, s.todo_sys), s.todo_sys);
	for (i = 0; i < 2; i++) {
		SDL_Rect q = tq[i];
		ui_glyph g = i == 0 ? UI_GLYPH_CHECK : k == DLK_NAMES ? UI_GLYPH_BOOK : UI_GLYPH_TO_CARD;
		bool lit = i == 0 ? s.have > 0 : s.todo > 0;
		char nm[96];

		ui_round_rect(a->r, &q, DS_RADIUS_CARD, ui_col(t->raised, 255));
		ui_glyph_draw(a->r, g, q.x + UD_PAD + 28, q.y + 50, 56, lit ? ui_col(t->accent_ink, 255) : UI_TEXT_DIM);
		ui_fit_text(fm, T(i == 0 ? LEFT[k] : RIGHT[k]), nm, sizeof nm, q.w - 2 * UD_PAD - 72);
		ui_text(a->r, fm, nm, q.x + UD_PAD + 72, ui_cap_y(fm, q.y + 50), -1, UI_TEXT);
		ui_text(a->r, ft, v->c ? big[i] : "\xe2\x80\xa6", q.x + UD_PAD, ui_cap_y(ft, q.y + 128), -1, lit ? UI_TEXT : UI_TEXT_DIM);
		if (v->c && sz[i][0]) ui_text(a->r, fm, sz[i], q.x + q.w - UD_PAD, ui_cap_y(fm, q.y + 128), 1, UI_TEXT_SOFT);
		if (v->c) {
			ui_fit_text(fs, ts[i], nm, sizeof nm, q.w - 2 * UD_PAD);
			ui_text(a->r, fs, nm, q.x + UD_PAD, ui_cap_y(fs, q.y + 180), -1, UI_TEXT_SOFT);
		}
	}
	/* Over the keys: what it takes, and where from */
	if (v->c && !online()) {
		ds_line_item it = { UI_GLYPH_COUNT, no_net_line(), DS_TONE_INFO };

		ds_page_line(a->r, &pf, &it, 1);
	} else if (v->c) {
		char b[24], tf[64];
		ds_line_item it;

		bytes_text(pl.bytes, b, sizeof b);
		snprintf(tf, sizeof tf, T(S_SCRAPER_TO_FETCH), pl.bytes > 0 ? b : "0 MB");
		if (k == DLK_COVERS) {
			snprintf(line, sizeof line, "%s \xc2\xb7 %s", tf, T(S_DOWNLOADS_LIBRETRO_FIRST));
			ui_fit_drawn(T(S_DOWNLOADS_LIBRETRO_FIRST));
			it = (ds_line_item){ UI_GLYPH_TO_CARD, line, DS_TONE_INFO };
		} else if (k == DLK_VIDEOS) {
			char fr[24], f2[48];

			bytes_text((double)v->c->free, fr, sizeof fr);
			snprintf(f2, sizeof f2, T(S_STORAGE_FREE), fr);
			snprintf(line, sizeof line, "%s \xc2\xb7 %s", tf, f2);
			it = (ds_line_item){ UI_GLYPH_TO_CARD, line, pl.fits ? DS_TONE_INFO : DS_TONE_WARN };
		} else if (k == DLK_CHEATS) {
			/* the source and its license, and the games it has none for */
			if (s.none) {
				char nn[96];

				snprintf(nn, sizeof nn, TN(S_RADL_HAVE_NONE, s.none), s.none);
				snprintf(line, sizeof line, "CC BY-SA 4.0 \xc2\xb7 %s", nn);
			} else {
				snprintf(line, sizeof line, "%s", T(S_DOWNLOADS_FROM_LIBRETRO_DB));
			}
			it = (ds_line_item){ UI_GLYPH_COUNT, line, DS_TONE_INFO };
		} else {
			snprintf(line, sizeof line, TN(S_DOWNLOADS_NAMES_REQUESTS, s.todo), s.todo);
			it = (ds_line_item){ UI_GLYPH_BOOK, line, DS_TONE_INFO };
		}
		ds_page_line(a->r, &pf, &it, 1);
	}
	{
		const char *K[4] = { "A", "X", "Y", "B" };
		const char *W[4] = { dl_live() ? T(S_DOWNLOADS_KEY_OPEN) : T(S_RADL_KEY_DOWNLOAD), T(S_SCRAPER_VIDEOS_FOR),
		                     T(S_DOWNLOADS_KEY_SYSTEMS), T(S_DOWNLOADS_KEY_BACK) };

		if (k == DLK_VIDEOS) foot(v, &pf, K, W, 4);
		else if (k == DLK_CHEATS) {
			const char *K2[2] = { "A", "B" };
			const char *W2[2] = { W[0], W[3] };

			foot(v, &pf, K2, W2, 2);
		} else {
			const char *K3[3] = { "A", "Y", "B" };
			const char *W3[3] = { W[0], W[2], W[3] };

			foot(v, &pf, K3, W3, 3);
		}
	}
}

/* ---- Systems: a row per system, a column per kind -------------------------------- */

static void cell_words(dl_view *v, int i, dl_kind k, char *out, size_t n)
{
	bool on;
	int todo = dl_sys_todo(v->c, i, k, &on);
	const char *b = dl_kind_blocked(v->c, k);

	if (b) { snprintf(out, n, "%s", b); return; }
	if (!on || !dl_kind_in(k)) { snprintf(out, n, "%s", T(S_SCRAPER_OFF)); return; }
	if ((k == DLK_ACH && !v->c->s[i].ach_sys) || (k == DLK_CHEATS && !v->c->s[i].cht_sys)) {
		snprintf(out, n, "%s", T(S_SCRAPER_OFF));
		return;
	}
	if (!todo) {
		snprintf(out, n, "%s", k == DLK_ACH || k == DLK_CHEATS ? T(S_DOWNLOADS_READY) : T(S_SCRAPER_COMPLETE));
		return;
	}
	switch (k) {
	case DLK_COVERS: snprintf(out, n, TN(S_DOWNLOADS_MISSING_N, todo), todo); break;
	case DLK_VIDEOS: snprintf(out, n, TN(S_DOWNLOADS_TO_FETCH_N, todo), todo); break;
	case DLK_NAMES:  snprintf(out, n, TN(S_DOWNLOADS_TO_LOOK_UP_N, todo), todo); break;
	case DLK_CHEATS: snprintf(out, n, TN(S_DOWNLOADS_TO_CHECK_N, todo), todo); break;
	default:         snprintf(out, n, TN(S_DOWNLOADS_TO_DOWNLOAD_N, todo), todo); break;
	}
}

static bool cell_lit(dl_view *v, int i, dl_kind k, int *todo)
{
	bool on;

	*todo = dl_sys_todo(v->c, i, k, &on);
	if (dl_kind_blocked(v->c, k) || !dl_kind_in(k) || !on) return false;
	if (k == DLK_ACH && !v->c->s[i].ach_sys) return false;
	if (k == DLK_CHEATS && !v->c->s[i].cht_sys) return false;
	if (k == DLK_NAMES && !v->c->s[i].names_ok) return false;
	return true;
}

static void systems_draw(dl_view *v, dl_frame *f, unsigned now)
{
	app *a = v->a;
	const ds_theme *t = theme();
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	int n = v->c ? v->c->n : 0, r, k, cx = pf.x1 - 8 - DLK_KINDS * DL_CELL_W, y0 = pf.body_y + 52;
	const dl_sys *cur = n ? &v->c->s[f->row] : NULL;
	char line[192], w[96], nm[96];

	ds_page_head(a->r, &pf, DS_HEAD_SUB, T(S_DOWNLOADS_KEY_SYSTEMS), cur ? cur->name : "", UI_GLYPH_COUNT, now);
	if (f->row < f->win) f->win = f->row;
	if (f->row >= f->win + DL_SYS_ROWS) f->win = f->row - DL_SYS_ROWS + 1;
	/* the kinds' marks over their columns, the cursor's in text */
	for (k = 0; k < DLK_KINDS; k++)
		ui_glyph_draw(a->r, KIND_MARK[k], cx + k * DL_CELL_W + DL_CELL_W / 2, pf.body_y + 18, 40,
		              k == f->col ? UI_TEXT : UI_TEXT_DIM);
	for (r = f->win; r < n && r < f->win + DL_SYS_ROWS; r++) {
		const dl_sys *s = &v->c->s[r];
		int y = y0 + (r - f->win) * DL_ROW, cy = y + DL_ROW / 2;

		icon_draw(v, s->key, pf.x0 + 48, cy, 52, 255);
		ui_fit_text(fs, s->name, nm, sizeof nm, cx - pf.x0 - 96 - 16);
		ui_text(a->r, fs, nm, pf.x0 + 96, ui_cap_y(fs, cy), -1, UI_TEXT_SOFT);
		for (k = 0; k < DLK_KINDS; k++) {
			int todo, x = cx + k * DL_CELL_W + DL_CELL_W / 2;

			if (!cell_lit(v, r, (dl_kind)k, &todo)) {
				ui_text(a->r, fm, "\xe2\x80\x93", x, ui_cap_y(fm, cy), 0, UI_TEXT_DIM);
			} else if (!todo) {
				ui_glyph_draw(a->r, UI_GLYPH_CHECK, x, cy, 34, ui_col(t->accent_ink, 255));
			} else {
				char c[16];

				snprintf(c, sizeof c, "%d", todo);
				ui_text(a->r, fs, c, x, ui_cap_y(fs, cy), 0, UI_TEXT);
			}
		}
	}
	if (f->win > 0) ds_more(a->r, cx + DLK_KINDS * DL_CELL_W / 2.0f, (float)(y0 - DS_MORE_H - 4), true, UI_TEXT_DIM);
	if (f->win + DL_SYS_ROWS < n)
		ds_more(a->r, cx + DLK_KINDS * DL_CELL_W / 2.0f, (float)(y0 + DL_SYS_ROWS * DL_ROW + 4), false, UI_TEXT_DIM);
	/* the cell under the cursor: a ring, and its words over the keys */
	if (cur) {
		SDL_Rect q = { cx + f->col * DL_CELL_W + 4, y0 + (f->row - f->win) * DL_ROW + 2, DL_CELL_W - 8, DL_ROW - 4 };
		int e = DS_FOCUS_RING;

		ui_round_rect(a->r, &(SDL_Rect){ q.x - e, q.y - e, q.w + 2 * e, q.h + 2 * e }, 16 + e, ui_col(t->accent_ink, 255));
		ui_round_rect(a->r, &q, 16, ui_col(t->ground, 255));
		{
			int todo, x = q.x + q.w / 2, cy = q.y + q.h / 2;

			if (!cell_lit(v, f->row, (dl_kind)f->col, &todo))
				ui_text(a->r, fm, "\xe2\x80\x93", x, ui_cap_y(fm, cy), 0, UI_TEXT_DIM);
			else if (!todo) ui_glyph_draw(a->r, UI_GLYPH_CHECK, x, cy, 34, ui_col(t->accent_ink, 255));
			else {
				char c[16];

				snprintf(c, sizeof c, "%d", todo);
				ui_text(a->r, fs, c, x, ui_cap_y(fs, cy), 0, UI_TEXT);
			}
		}
		cell_words(v, f->row, (dl_kind)f->col, w, sizeof w);
		snprintf(line, sizeof line, "%s \xc2\xb7 %s \xc2\xb7 %s", cur->name, kind_name((dl_kind)f->col), w);
		{
			ds_line_item it = { UI_GLYPH_COUNT, line, DS_TONE_INFO };

			ds_page_line(a->r, &pf, &it, 1);
		}
	}
	{
		bool del = cur && (f->col == DLK_VIDEOS ? cur->vid_have > 0 : f->col == DLK_ACH ? cur->ach_ready > 0
		                   : f->col == DLK_CHEATS ? cur->cht_have + cur->cht_none > 0 : false);
		const char *K[4] = { "A", "X", "Y", "B" };
		const char *W[4] = { T(S_DOWNLOADS_KEY_ON_OFF), T(S_DOWNLOADS_KEY_DELETE), T(S_DOWNLOADS_KEY_GAMES),
		                     T(S_DOWNLOADS_KEY_BACK) };

		if (del) foot(v, &pf, K, W, 4);
		else {
			const char *K3[3] = { "A", "Y", "B" };
			const char *W3[3] = { W[0], W[2], W[3] };

			foot(v, &pf, K3, W3, 3);
		}
	}
}

/* ---- a system's games: each with a mark per kind --------------------------------- */

static void sysgames_free(dl_view *v)
{
	free(v->games);
	v->games = NULL;
	v->ngames = 0;
}

static int name_cmp(const void *x, const void *y)
{
	return strcmp(*(char *const *)x, *(char *const *)y);
}

static char **list_dir(const char *dir, int *n)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char **v = NULL;
	int cap = 0;

	*n = 0;
	if (!d) return NULL;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (*n == cap) {
			char **nv = realloc(v, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *v);

			if (!nv) break;
			v = nv;
		}
		if (!(v[*n] = strdup(e->d_name))) break;
		(*n)++;
	}
	closedir(d);
	if (*n) qsort(v, (size_t)*n, sizeof *v, name_cmp);
	return v;
}

static void list_free(char **v, int n)
{
	int i;

	for (i = 0; i < n; i++) free(v[i]);
	free(v);
}

static bool listed(char **v, int n, const char *name, const char *ext)
{
	char k[LIB_NAME + 16];
	const char *p = k;

	snprintf(k, sizeof k, "%s%s", name, ext);
	return n && bsearch(&p, v, (size_t)n, sizeof *v, name_cmp);
}

/* The system's games and what each has: its folder's list read once, its
 * covers', its videos' and its sets' folders read once, its titles once */
static void sysgames_load(dl_view *v, const char *folder)
{
	app *a = v->a;
	int i, si = -1, nset = 0, nmp4 = 0, npng = 0;
	const game_list *gl;
	char dir[LIB_PATH * 2], **sets, **mp4, **png;
	db_titles titles = { 0 };
	const dl_sys *s = sys_of(v, folder);

	sysgames_free(v);
	for (i = 0; i < a->sys.count; i++) if (!strcmp(a->sys.systems[i].folder, folder)) si = i;
	if (si < 0) return;
	gl = sys_games(&a->view[si]);
	if (!gl->count || !(v->games = calloc((size_t)gl->count, sizeof *v->games))) return;
	snprintf(dir, sizeof dir, "%s/%s/.cheevos", P_ROMS, folder);
	sets = list_dir(dir, &nset);
	mp4 = snaps_stems(P_ROMS, folder, &nmp4);
	png = snaps_cover_stems(P_ROMS, folder, &npng);
	db_titles_load(db_lib(), folder, &titles);
	snprintf(dir, sizeof dir, "%s/%s", P_ROMS, folder);
	for (i = 0; i < gl->count; i++) {
		const game_entry *g = &gl->items[i];
		dl_game *d = &v->games[v->ngames++];
		const char *stem = g->name, *t;

		snprintf(d->name, sizeof d->name, "%s", g->name);
		snprintf(d->title, sizeof d->title, "%s", g->title[0] ? g->title : g->name);
		snprintf(d->file, sizeof d->file, "%s", g->file);
		d->gi = i;
		d->st[DLK_COVERS] = (npng && bsearch(&stem, png, (size_t)npng, sizeof *png, name_cmp)) ||
		                    es_art_cover(dir, g->file, g->name) ? GS_HAVE : GS_MISS;
		if ((nmp4 && bsearch(&stem, mp4, (size_t)nmp4, sizeof *mp4, name_cmp)) || es_art_video(dir, g->file, g->name))
			d->st[DLK_VIDEOS] = GS_HAVE;
		else d->st[DLK_VIDEOS] = dl_sys_on(DLK_VIDEOS, folder) ? GS_MISS : GS_OFF;
		t = db_titles_find(&titles, g->file);
		d->st[DLK_NAMES] = t && *t ? GS_HAVE : GS_MISS;
		if (!s || !s->cht_sys) d->st[DLK_CHEATS] = GS_OFF;
		else {
			int cs = cheats_game_state(folder, g->name);

			d->st[DLK_CHEATS] = cs > 0 ? GS_HAVE : cs < 0 ? GS_NONE : GS_MISS;
		}
		if (!s || !s->ach_sys) d->st[DLK_ACH] = GS_OFF;
		else if (listed(sets, nset, g->name, ".set")) d->st[DLK_ACH] = GS_HAVE;
		else if (listed(sets, nset, g->name, ".none")) d->st[DLK_ACH] = GS_NONE;
		else d->st[DLK_ACH] = GS_MISS;
	}
	db_titles_free(&titles);
	snaps_stems_free(mp4, nmp4);
	snaps_stems_free(png, npng);
	list_free(sets, nset);
}

/* What a game has and lacks, in two lines over the keys */
static void game_facts(const dl_game *g, char *l1, size_t n1, char *l2, size_t n2)
{
	static const str_id MISS[DLK_KINDS] = { S_DOWNLOADS_W_COVER, S_DOWNLOADS_W_VIDEO, S_DOWNLOADS_W_NAMES,
	                                        S_DOWNLOADS_W_ACHIEVEMENTS, S_DOWNLOADS_W_CHEATS };
	char have[192] = "", miss[160] = "";
	size_t o = 0, m = 0;
	int k;

	if (g->st[DLK_COVERS] == GS_HAVE) o += (size_t)snprintf(have + o, sizeof have - o, "%s", T(S_DOWNLOADS_HAS_COVER));
	if (g->st[DLK_VIDEOS] == GS_HAVE && o < sizeof have)
		o += (size_t)snprintf(have + o, sizeof have - o, "%s%s", o ? " \xc2\xb7 " : "", T(S_DOWNLOADS_HAS_VIDEO));
	if (g->st[DLK_NAMES] == GS_HAVE && o < sizeof have)
		o += (size_t)snprintf(have + o, sizeof have - o, "%s%s", o ? " \xc2\xb7 " : "", T(S_DOWNLOADS_HAS_NAMES));
	if (g->st[DLK_ACH] == GS_HAVE && o < sizeof have)
		o += (size_t)snprintf(have + o, sizeof have - o, "%s%s", o ? " \xc2\xb7 " : "", T(S_DOWNLOADS_HAS_ACHIEVEMENTS));
	if (g->st[DLK_CHEATS] == GS_HAVE && o < sizeof have)
		o += (size_t)snprintf(have + o, sizeof have - o, "%s%s", o ? " \xc2\xb7 " : "", T(S_DOWNLOADS_KIND_CHEATS));
	for (k = 0; k < DLK_KINDS; k++)
		if (g->st[k] == GS_MISS && m < sizeof miss)
			m += (size_t)snprintf(miss + m, sizeof miss - m, "%s%s", m ? ", " : "", T(MISS[k]));
	snprintf(l1, n1, "%s", o ? have : T(S_DOWNLOADS_NOTHING_YET));
	/* lists of facts: each part said drawn, for the shots' check */
	for (k = 0; k < DLK_KINDS; k++) if (g->st[k] == GS_MISS) ui_fit_drawn(T(MISS[k]));
	if (g->st[DLK_COVERS] == GS_HAVE) ui_fit_drawn(T(S_DOWNLOADS_HAS_COVER));
	if (g->st[DLK_NAMES] == GS_HAVE) ui_fit_drawn(T(S_DOWNLOADS_HAS_NAMES));
	if (g->st[DLK_ACH] == GS_HAVE) ui_fit_drawn(T(S_DOWNLOADS_HAS_ACHIEVEMENTS));
	if (m) {
		snprintf(l2, n2, T(S_DOWNLOADS_MISSING_LIST), miss);
		ui_fit_drawn(T(S_DOWNLOADS_MISSING_LIST));
	} else if (g->st[DLK_ACH] == GS_NONE) {
		snprintf(l2, n2, "%s", T(S_DOWNLOADS_NO_ACHIEVEMENTS));
	} else if (g->st[DLK_CHEATS] == GS_NONE) {
		snprintf(l2, n2, "%s", T(S_DOWNLOADS_NO_CHEATS));
	} else {
		snprintf(l2, n2, "%s", T(S_DOWNLOADS_EVERYTHING_HERE));
	}
}

static void sysgames_draw(dl_view *v, dl_frame *f, unsigned now)
{
	app *a = v->a;
	const ds_theme *t = theme();
	TTF_Font *fm = ui_font(UI_F_MENU), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	const dl_sys *s = sys_of(v, f->sys);
	int x0 = DS_INSET + 8, x1 = ZYPEROS_SCREEN_W - DS_INSET - 8, i, k, y0 = pf.body_y;
	char sub[48], nm[160], l1[192], l2[192];

	snprintf(sub, sizeof sub, TN(S_DOWNLOADS_N_GAMES, v->ngames), v->ngames);
	ds_page_head(a->r, &pf, DS_HEAD_SUB, s ? s->name : f->sys, sub, UI_GLYPH_COUNT, now);
	if (f->sel < f->win) f->win = f->sel;
	if (f->sel >= f->win + DL_GAME_ROWS) f->win = f->sel - DL_GAME_ROWS + 1;
	if (v->ngames) {
		float e = f->fill_from >= 0 ? ease_out((float)(now - f->fill_t0) / DS_MS_QUICK) : 1.0f;
		int fy = y0 + (f->sel - f->win) * DL_ROW;
		int yy = f->fill_from >= 0 ? (int)(f->fill_from + (fy - f->fill_from) * e) : fy;

		ui_round_rect(a->r, &(SDL_Rect){ x0, yy, x1 - x0, DL_ROW }, DS_RADIUS_ROW, ui_col(t->accent, 255));
		if (e >= 1.0f) f->fill_from = -1;
	}
	for (i = f->win; i < v->ngames && i < f->win + DL_GAME_ROWS; i++) {
		const dl_game *g = &v->games[i];
		bool on = i == f->sel;
		int cy = y0 + (i - f->win) * DL_ROW + DL_ROW / 2, mx = x1 - DL_ROW_PAD - DLK_KINDS * 44 + 17;
		SDL_Color ink = on ? ui_col(t->on_accent, 255) : UI_TEXT_SOFT;

		ui_fit_text(fm, g->title, nm, sizeof nm, mx - 24 - x0 - DL_ROW_PAD);
		ui_text(a->r, fm, nm, x0 + DL_ROW_PAD, ui_cap_y(fm, cy), -1, ink);
		for (k = 0; k < DLK_KINDS; k++) {
			int st = g->st[k], x = mx + k * 44;
			Uint8 al = st == GS_HAVE ? 255 : st == GS_OFF ? 30 : 82;

			ui_glyph_draw(a->r, KIND_MARK[k], x, cy, 34, ui_fade(ink, al / 255.0f));
			if (st == GS_NONE) {
				/* struck: the source has none */
				SDL_SetRenderDrawColor(a->r, ink.r, ink.g, ink.b, 200);
				SDL_RenderDrawLine(a->r, x - 18, cy + 12, x + 18, cy - 12);
				SDL_RenderDrawLine(a->r, x - 18, cy + 13, x + 18, cy - 11);
			}
		}
	}
	if (f->win > 0) ds_more(a->r, ZYPEROS_SCREEN_W / 2.0f, (float)(y0 - DS_MORE_H - 4), true, UI_TEXT_DIM);
	if (f->win + DL_GAME_ROWS < v->ngames)
		ds_more(a->r, ZYPEROS_SCREEN_W / 2.0f, (float)(y0 + DL_GAME_ROWS * DL_ROW + 4), false, UI_TEXT_DIM);
	if (v->ngames) {
		game_facts(&v->games[f->sel], l1, sizeof l1, l2, sizeof l2);
		ui_fit_text(fs, l1, nm, sizeof nm, pf.x1 - pf.x0);
		ui_text(a->r, fs, nm, ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, pf.line_y - DS_LINE_META), 0, UI_TEXT_DIM);
		ui_fit_text(fs, l2, nm, sizeof nm, pf.x1 - pf.x0);
		ui_text(a->r, fs, nm, ZYPEROS_SCREEN_W / 2, ui_cap_y(fs, pf.line_y), 0, UI_TEXT_DIM);
	}
	{
		const char *K[2] = { "A", "B" }, *W[2] = { T(S_DOWNLOADS_KEY_OPEN), T(S_DOWNLOADS_KEY_BACK) };

		foot(v, &pf, K, W, 2);
	}
}

/* ---- the run: one stream for every lane ------------------------------------------- */

/* ---- what came: the run's arrivals, kept while the page is closed ----------------------
 * The stream (the newest landing big, the six before it) and done's wall
 * were the page's own and went with it, so a run left to go on with B came
 * back with the pictures that had come gone and only what arrived after it
 * opened again (Tarik on 1.1.0-rc.3). In the lab the run is the queue's, not
 * the page's, and what landed stays with it. So they are kept here, for the
 * session: newest first, DL_RING at most, the oldest dropped; taken after
 * each step of a lane (dlq_on_step: downloads_stepped), the page open or
 * not, and kept through a game; the pictures the page made stay made when it
 * closes (the renderer is the same one until a game in the fallback takes
 * it, downloads_video_gone). The pictures of what came while it was closed are
 * read once it draws, by the paths kept, off the frame: nothing is looked for
 * on the card as it opens. A new run of the queue starts the list again, as
 * the lab's run does; once a run's end has been seen, the page closing lets
 * it go. Not over a power-off: POWER stops the run, and the next starts from
 * what is missing. */
static struct {
	dl_arrival it[DL_RING];
	int n;
	unsigned run;                 /* the queue's run it is of (dlq_runs) */
	unsigned key;                 /* the newest arrival's */
	unsigned seq[DLQ_LANES];      /* each lane's newest arrival taken */
	unsigned lane_run[DLQ_LANES]; /* ... in which of its runs (dlq_ends) */
	unsigned nc_out;              /* the arrival its card is being painted for, 0 none */
} g_rec;

static void rec_free(void)
{
	int i;

	for (i = 0; i < g_rec.n; i++) if (g_rec.it[i].tex) SDL_DestroyTexture(g_rec.it[i].tex);
	g_rec.n = 0;
	g_rec.nc_out = 0;
}

/* A new run of the queue: the list starts again */
static void rec_sync(void)
{
	if (g_rec.run == dlq_runs()) return;
	rec_free();
	g_rec.run = dlq_runs();
}

static void rec_add(int kind, const char *path, const char *game, const char *folder, int count)
{
	dl_arrival *it;

	if (g_rec.n == DL_RING) {
		if (g_rec.it[DL_RING - 1].tex) SDL_DestroyTexture(g_rec.it[DL_RING - 1].tex);
		g_rec.n--;
	}
	memmove(&g_rec.it[1], &g_rec.it[0], (size_t)g_rec.n * sizeof g_rec.it[0]);
	g_rec.n++;
	it = &g_rec.it[0];
	memset(it, 0, sizeof *it);
	it->kind = kind;
	it->count = count;
	it->key = ++g_rec.key;
	snprintf(it->game, sizeof it->game, "%s", game ? game : "");
	snprintf(it->folder, sizeof it->folder, "%s", folder ? folder : "");
	snprintf(it->path, sizeof it->path, "%s", path ? path : "");
	it->card = !it->path[0];
}

/* The page opens again: its threads are new, nothing is asked of them yet,
 * and a card painted in the other theme (Night Mode came on meanwhile) is
 * painted again */
static void rec_reopen(void)
{
	int i;

	for (i = 0; i < g_rec.n; i++) {
		dl_arrival *it = &g_rec.it[i];

		it->asked = false;
		if (it->tex && !cardgen_kept(ui_card_theme(it->tex), theme_index())) {
			SDL_DestroyTexture(it->tex);
			it->tex = NULL;
		}
	}
	g_rec.nc_out = 0;
}

void downloads_stepped(dlq_lane l)
{
	char path[LIB_PATH * 3] = "";

	if ((int)l < 0 || l >= DLQ_LANES) return;
	rec_sync();
	/* a lane's new run counts its arrivals from none */
	if (g_rec.lane_run[l] != dlq_ends(l)) {
		g_rec.lane_run[l] = dlq_ends(l);
		g_rec.seq[l] = 0;
	}
	if (l == DLQ_ART) {
		art_progress p;

		art_lane_status(&p);
		if (p.found_seq == g_rec.seq[l]) return;
		g_rec.seq[l] = p.found_seq;
		rec_add(p.found_video ? DLK_VIDEOS : p.found_names ? DLK_NAMES : DLK_COVERS, p.found_path, p.found_game,
		        p.found_folder, 0);
	} else if (l == DLQ_CHT) {
		cheats_progress p;

		cheats_status(&p);
		if (p.seq == g_rec.seq[l]) return;
		g_rec.seq[l] = p.seq;
		if (has_box_art(p.got_folder, p.got_game)) box_art_path(p.got_folder, p.got_game, path, sizeof path);
		rec_add(DLK_CHEATS, path, p.got_game, p.got_folder, p.got_count);
	} else {
		radl_progress p;
		char name[LIB_NAME];
		const char *b;
		size_t ln;

		radl_status(&p);
		if (p.set_seq == g_rec.seq[l]) return;
		g_rec.seq[l] = p.set_seq;
		b = strrchr(p.set_path, '/');
		snprintf(name, sizeof name, "%s", b ? b + 1 : p.set_path);
		ln = strlen(name);
		if (ln > 4 && !strcmp(name + ln - 4, ".set")) name[ln - 4] = '\0';
		if (has_box_art(p.folder_now, name)) box_art_path(p.folder_now, name, path, sizeof path);
		rec_add(DLK_ACH, path, p.set_title[0] ? p.set_title : name, p.folder_now, p.set_count);
	}
}

void downloads_video_gone(void)
{
	int i;

	for (i = 0; i < g_rec.n; i++) {
		if (g_rec.it[i].tex) SDL_DestroyTexture(g_rec.it[i].tex);
		g_rec.it[i].tex = NULL;
		g_rec.it[i].asked = false;
	}
}

/* ---- a name card for an arrival with no picture ---------------------------------------
 * ZyperOS's own card for a game with no art (src/cardart.h): its system's
 * ground and its name in the one treatment, as the shelf has it. A card is
 * tens of ms to paint on the Pixel 2, so it is painted here on a thread of
 * its own (ui_card_paint's workers' faces) and taken as it is ready, one at
 * a time, the newest first (pictures_ask). */
static SDL_Thread *g_nc_thread;
static SDL_mutex *g_nc_lock;
static SDL_cond *g_nc_wake;
static struct {
	bool want, quit, has;
	unsigned key, done_key;
	ui_card_order o;
	SDL_Surface *done;
} g_nc;

static int nc_worker(void *arg)
{
	(void)arg;
	SDL_LockMutex(g_nc_lock);
	while (!g_nc.quit) {
		ui_card_order o;
		unsigned key;
		SDL_Surface *sf;

		if (!g_nc.want) { SDL_CondWait(g_nc_wake, g_nc_lock); continue; }
		o = g_nc.o;
		key = g_nc.key;
		g_nc.want = false;
		SDL_UnlockMutex(g_nc_lock);
		sf = ui_card_paint(&o, true);
		SDL_LockMutex(g_nc_lock);
		if (g_nc.done) ui_card_free(g_nc.done);   /* an older one nobody took */
		g_nc.done = sf;
		g_nc.done_key = key;
		g_nc.has = true;                          /* NULL too: none could be painted */
	}
	SDL_UnlockMutex(g_nc_lock);
	return 0;
}

static void nc_start(void)
{
	if (g_nc_thread) return;
	if (!g_nc_lock && !(g_nc_lock = SDL_CreateMutex())) return;
	if (!g_nc_wake && !(g_nc_wake = SDL_CreateCond())) return;
	g_nc.quit = false;
	g_nc.want = false;
	g_nc_thread = SDL_CreateThread(nc_worker, "zyperos-dl-card", NULL);
}

static void nc_stop(void)
{
	if (!g_nc_thread) return;
	SDL_LockMutex(g_nc_lock);
	g_nc.quit = true;
	SDL_CondSignal(g_nc_wake);
	SDL_UnlockMutex(g_nc_lock);
	SDL_WaitThread(g_nc_thread, NULL);
	g_nc_thread = NULL;
	if (g_nc.done) ui_card_free(g_nc.done);
	g_nc.done = NULL;
	g_nc.has = false;
}

/* The order for an arrival's card: its name, its system's colour, era and art */
static void nc_order(dl_view *v, const dl_arrival *it, ui_card_order *o)
{
	const system_cfg *sc = NULL;
	char art[UI_CARD_PATH] = "";
	int i;

	for (i = 0; i < v->a->sys.count; i++) if (!strcmp(v->a->sys.systems[i].folder, it->folder)) sc = &v->a->sys.systems[i];
	ui_card_order_set(o, it->game, sc ? sc->accent : CFG_NO_COLOR, false);
	if (sc && home_art_key(sc->tag)[0])
		snprintf(art, sizeof art, "%s/system-icons/%s-still.png", v->fixture ? "res" : P_ROOT, home_art_key(sc->tag));
	ui_card_order_game(o, sc ? sc->tag : "", art, NULL);
}

/* Its card asked of the card's thread (a shot's painted now): false when
 * there is none to ask */
static bool nc_ask(dl_view *v, dl_arrival *it)
{
	ui_card_order o;

	nc_order(v, it, &o);
	if (v->fixture) {
		/* a shot draws one frame: the card now, on this thread */
		SDL_Surface *sf = ui_card_paint(&o, false);

		if (sf) {
			it->tex = ui_card_upload(v->a->r, sf, o.theme_ix);
			it->w = sf->w;
			it->h = sf->h;
			ui_card_free(sf);
		}
		return true;
	}
	if (!g_nc_thread) return false;
	SDL_LockMutex(g_nc_lock);
	g_nc.o = o;
	g_nc.key = it->key;
	g_nc.want = true;
	SDL_CondSignal(g_nc_wake);
	SDL_UnlockMutex(g_nc_lock);
	g_rec.nc_out = it->key;
	return true;
}

static bool nc_take(dl_view *v)
{
	SDL_Surface *sf = NULL;
	unsigned key = 0;
	bool has = false;
	int i;

	if (!g_nc_thread) return false;
	SDL_LockMutex(g_nc_lock);
	if (g_nc.has) { sf = g_nc.done; key = g_nc.done_key; g_nc.done = NULL; g_nc.has = false; has = true; }
	SDL_UnlockMutex(g_nc_lock);
	if (!has) return false;
	if (key == g_rec.nc_out) g_rec.nc_out = 0;
	if (!sf) return false;          /* none painted: its tile stays */
	for (i = 0; i < g_rec.n; i++) {
		dl_arrival *it = &g_rec.it[i];

		if (it->key != key || it->tex) continue;
		it->tex = ui_card_upload(v->a->r, sf, theme_index());
		it->w = sf->w;
		it->h = sf->h;
		if (i == 0 && it->key > v->open_key) v->land_at = anim_now();
		break;
	}
	ui_card_free(sf);
	return i < g_rec.n;
}

/* The pictures the list lacks, the newest first: a file's on the picture
 * thread (mediatex.h), a card on the card's, one at a time */
static void pictures_ask(dl_view *v)
{
	int i;

	for (i = 0; i < g_rec.n; i++) {
		dl_arrival *it = &g_rec.it[i];

		if (it->tex || it->asked) continue;
		if (!it->card && !v->fixture) {
			mt_ask((int)it->key, it->path, DL_BIG);
			it->asked = true;
		} else if (!g_rec.nc_out) {
			it->card = true;
			it->asked = nc_ask(v, it);
		}
	}
}

static bool take_pictures(dl_view *v)
{
	SDL_Surface *s;
	int key, i;
	bool any = false;

	while (mt_take(&key, &s)) {
		for (i = 0; i < g_rec.n; i++) {
			dl_arrival *it = &g_rec.it[i];

			if (it->key != (unsigned)key || it->tex) continue;
			if (!s) {                   /* no picture after all: its card */
				it->card = true;
				it->asked = false;
				break;
			}
			it->tex = SDL_CreateTextureFromSurface(v->a->r, s);
			it->w = s->w;
			it->h = s->h;
			if (i == 0 && it->key > v->open_key) v->land_at = anim_now();
			any = true;
		}
		if (s) SDL_FreeSurface(s);
	}
	if (nc_take(v)) any = true;
	pictures_ask(v);
	return any;
}

/* What came since the last look: the lanes' progress, and the newest of
 * what is kept (downloads_stepped took it) landing */
static bool take_arrivals(dl_view *v)
{
	if (v->art && dlq_running(DLQ_ART)) art_lane_status(&v->ap);
	if (v->ra && dlq_running(DLQ_RA)) radl_status(&v->rp);
	if (v->cht && dlq_running(DLQ_CHT)) cheats_status(&v->cp);
	if (g_rec.key == v->rec_key) return false;
	v->rec_key = g_rec.key;
	/* with no file to read it lands now, its card asked; with one, as its
	 * picture comes (take_pictures) */
	if (g_rec.n && g_rec.it[0].card) v->land_at = anim_now();
	pictures_ask(v);
	return true;
}

static void run_counts(const dl_view *v, int got[DLK_KINDS])
{
	got[DLK_COVERS] = v->ap.found;
	got[DLK_VIDEOS] = v->ap.snaps;
	got[DLK_NAMES] = v->ap.named;
	got[DLK_ACH] = v->rp.fetched + v->rp.updated;
	got[DLK_CHEATS] = v->cp.fetched + v->cp.updated;
}

static void run_shown(const dl_view *v, bool shown[DLK_KINDS])
{
	int k;

	for (k = 0; k < DLK_KINDS; k++)
		shown[k] = v->run_kind < 0 ? (k == DLK_ACH ? v->ra : k == DLK_CHEATS ? v->cht : v->art) : v->run_kind == k;
	if (v->run_kind < 0 && v->art) {
		shown[DLK_COVERS] = dl_kind_in(DLK_COVERS);
		shown[DLK_VIDEOS] = dl_kind_in(DLK_VIDEOS) && v->c && v->c->ss_in;
		shown[DLK_NAMES] = dl_kind_in(DLK_NAMES) && v->c && v->c->ss_in;
	}
}

/* Done and to do, across the lanes */
static void run_progress(const dl_view *v, int *done, int *total)
{
	int ad = 0, at = 0, rd = 0, rt = 0, cd = 0, ct = 0;

	if (v->art) {
		ad = v->ap.looked + v->ap.dlooked;
		at = v->art_total + (v->ap.details ? v->ap.dtotal : v->art_looks);
		if (at < ad) at = ad;
	}
	if (v->ra) {
		rd = v->rp.games_done;
		rt = v->rp.games > rd ? v->rp.games : rd;
	}
	if (v->cht) {
		cd = v->cp.games_done;
		ct = v->cp.games > cd ? v->cp.games : cd;
	}
	*done = ad + rd + cd;
	*total = at + rt + ct;
}

static double run_eta(const dl_view *v, unsigned now)
{
	int done, total;
	double art = 0, ra = 0, cht = 0, el = (now - v->t0) / 1000.0;

	run_progress(v, &done, &total);
	if (v->art) {
		int left = v->art_total + (v->ap.details ? v->ap.dtotal : v->art_looks) - v->ap.looked - v->ap.dlooked;

		art = (left > 0 ? left : 0) * (v->c && v->c->ss_in ? DL_S_SS : DL_S_LR);
	}
	if (v->ra) {
		int left = v->rp.games - v->rp.games_done;

		ra = (left > 0 ? left : 0) * DL_S_SET;
	}
	if (v->cht) {
		int left = v->cp.games - v->cp.games_done;

		cht = (left > 0 ? left : 0) * DL_CHT_HITS * DL_S_CHT;
	}
	/* the run's own pace once it has one */
	if (done - v->done0 >= 6 && el >= 15 && total > done)
		return el / (done - v->done0) * (total - done);
	if (cht > art) art = cht;
	return art > ra ? art : ra;
}

static void run_draw(dl_view *v, unsigned now)
{
	app *a = v->a;
	TTF_Font *ft = ui_font(UI_F_TITLE), *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	int hx = pf.head_x, m, done, total, cell0 = 0, sq, nc;
	float e = g_rec.n ? ease_out((float)(now - v->land_at) / DL_LAND) : 1.0f;
	char fit[160], cnt[48], eta[48];
	static ds_flex F;
	SDL_FRect cell[DL_CELLS], big;
	const char *title, *folder;
	ui_glyph km = UI_GLYPH_COVER, src = UI_GLYPH_CLOUD;
	const dl_sys *s;
	const char *K[2] = { "B", "Y" }, *W[2] = { T(S_UPDATE_BACK_GOES_ON), T(S_SCRAPER_KEY_STOP) };
	bool art_now = v->art && lane_on(DLQ_ART) && v->ap.now[0];

	/* The game being asked about: the art lane's, the longer, else the
	 * achievements lane's; its system, its kind and its source */
	if (art_now) {
		title = v->ap.now;
		folder = v->ap.folder_now;
		km = v->ap.details ? (dl_kind_in(DLK_NAMES) ? UI_GLYPH_BOOK : UI_GLYPH_FILM) : UI_GLYPH_COVER;
		src = v->ap.src == ART_SRC_SG ? UI_GLYPH_GRID : v->ap.src == ART_SRC_LR ? UI_GLYPH_SHELF : UI_GLYPH_CLOUD;
	} else if (v->ra && lane_on(DLQ_RA)) {
		title = v->rp.now[0] ? v->rp.now : v->rp.folder_now;
		folder = v->rp.folder_now;
		km = UI_GLYPH_TROPHY;
		src = UI_GLYPH_TROPHY;
	} else {
		title = v->cp.now[0] ? v->cp.now : v->cp.folder_now;
		folder = v->cp.folder_now;
		km = UI_GLYPH_WAND;
		src = UI_GLYPH_SHELF;
	}
	s = sys_of(v, folder);
	ui_fit_text(ft, title, fit, sizeof fit, pf.x1 - hx);
	ui_text(a->r, ft, fit, hx, ui_cap_y(ft, DS_PAGE_TITLE_Y), -1, UI_TEXT);
	if (s) icon_draw(v, s->key, hx + 26, DS_PAGE_SUB_Y, 52, 255);
	ui_glyph_draw(a->r, km, hx + 84, DS_PAGE_SUB_Y, 36, UI_TEXT_SOFT);
	ui_glyph_draw(a->r, src, hx + 132, DS_PAGE_SUB_Y, 36, UI_TEXT_SOFT);
	run_progress(v, &done, &total);
	snprintf(cnt, sizeof cnt, "%d / %d", done, total);
	{
		int bx = hx + 168, w = pf.x1 - bx - ui_text_width(fs, cnt) - 24;

		ds_bar(a->r, bx, DS_PAGE_SUB_Y - UI_BAR_H / 2, w, total > 0 ? (int)((long long)w * (done > total ? total : done) / total) : 0, 255);
	}
	ui_text(a->r, fs, cnt, pf.x1, ui_cap_y(fs, DS_PAGE_SUB_Y), 1, UI_TEXT_DIM);
	/* The body: the newest's square at the left, the ones before it in two
	 * rows in what is left, as many a row as it holds: three on a 4:3
	 * canvas, more on a wider one (the Scraper's run) */
	{
		const int root = ds_flex_begin(&F, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H,
		                               &(ds_box){ .dir = DS_ROW, .gap = DL_CELL_GAP,
		                                          .pad = { pf.body.x, pf.body.y, ZYPEROS_SCREEN_W - pf.body.x - pf.body.w,
		                                                   ZYPEROS_SCREEN_H - pf.body.y - pf.body.h } });
		const int grid_w = pf.body.w - DL_BIG - DL_CELL_GAP;
		const int cols = canvas_fit_cols(grid_w, DL_CELL, DL_CELL_MIN_GAP, DL_CELLS / 2);
		int grid, k;

		nc = 2 * cols;
		sq = ds_flex_add(&F, root, &(ds_box){ .w = DL_BIG, .h = DL_BIG });
		grid = ds_flex_add(&F, root, &(ds_box){ .dir = DS_ROW, .wrap = true, .w = grid_w,
		                                        .gap = cols > 1 ? (grid_w - cols * DL_CELL) / (cols - 1) : 0,
		                                        .line_gap = DL_CELL_GAP });
		for (k = 0; k < nc; k++) {
			int id = ds_flex_add(&F, grid, &(ds_box){ .w = DL_CELL, .h = DL_CELL });

			if (!k) cell0 = id;
		}
		ds_flex_solve(&F);
		for (k = 0; k < nc; k++) {
			SDL_Rect q = ds_flex_rect(&F, cell0 + k);

			cell[k] = (SDL_FRect){ (float)q.x, (float)q.y, (float)q.w, (float)q.h };
		}
		{
			SDL_Rect q = ds_flex_rect(&F, sq);

			big = (SDL_FRect){ (float)(q.x + 8), (float)(q.y + 8), (float)(q.w - 16), (float)(q.h - 16) };
		}
	}
	if (!g_rec.n) {
		const ds_theme *t = theme();
		int k;

		ui_round_rect(a->r, &(SDL_Rect){ (int)big.x, (int)big.y, (int)big.w, (int)big.h }, DS_RADIUS_ROW, ui_col(t->raised, 255));
		for (k = 0; k < nc; k++)
			ui_round_rect(a->r, &(SDL_Rect){ (int)cell[k].x, (int)cell[k].y, (int)cell[k].w, (int)cell[k].h }, DS_RADIUS_ROW,
			              ui_col(t->raised, 160));
	}
	for (m = nc; m >= 0; m--) {
		int k = m + 1;
		SDL_FRect to = cell[m < nc ? m : nc - 1], from = m == 0 ? big : cell[m - 1];

		if (k >= g_rec.n) continue;
		if (m == nc) arrival_draw(v, &g_rec.it[k], from, (Uint8)(255 * (1.0f - e)), false);
		else arrival_draw(v, &g_rec.it[k], (SDL_FRect){ from.x + (to.x - from.x) * e, from.y + (to.y - from.y) * e,
		                                               from.w + (to.w - from.w) * e, from.h + (to.h - from.h) * e },
		                  255, false);
	}
	if (g_rec.n) {
		SDL_FRect b = { big.x + big.w * 0.06f * (1 - e), big.y + 40 * (1 - e) + big.h * 0.06f * (1 - e),
		                big.w * (0.88f + 0.12f * e), big.h * (0.88f + 0.12f * e) };

		arrival_draw(v, &g_rec.it[0], b, (Uint8)(255 * e), true);
	}
	/* Over the keys: why it waits, or the counts per kind and the time left */
	if (!online() || (v->ra && lane_on(DLQ_RA) && v->rp.state == RADL_NO_NETWORK) ||
	    (v->cht && lane_on(DLQ_CHT) && v->cp.state == CHT_NO_NETWORK)) {
		ds_line_item it = { UI_GLYPH_WIFI, T(S_RADL_WAITING_WIFI), DS_TONE_WARN };

		ds_page_line(a->r, &pf, &it, 1);
	} else if (v->ra && lane_on(DLQ_RA) && (v->rp.state == RADL_SLOWED || v->rp.state == RADL_RETRYING)) {
		unsigned pn = plat_now_ms(), left = v->rp.resume_at > pn ? v->rp.resume_at - pn : 0, sec = (left + 999) / 1000;
		char w[24], line[160];
		ds_line_item it;

		snprintf(w, sizeof w, "%u:%02u", sec / 60, sec % 60);
		snprintf(line, sizeof line, v->rp.state == RADL_SLOWED ? T(S_RADL_PAUSED) : T(S_RADL_RETRYING), w);
		it = (ds_line_item){ UI_GLYPH_CLOCK, line, DS_TONE_WARN };
		ds_page_line(a->r, &pf, &it, 1);
	} else if (v->cht && lane_on(DLQ_CHT) && v->cp.state == CHT_RETRYING) {
		unsigned pn = plat_now_ms(), left = v->cp.resume_at > pn ? v->cp.resume_at - pn : 0, sec = (left + 999) / 1000;
		char w[24], line[160];
		ds_line_item it;

		snprintf(w, sizeof w, "%u:%02u", sec / 60, sec % 60);
		snprintf(line, sizeof line, T(S_RADL_RETRYING), w);
		it = (ds_line_item){ UI_GLYPH_CLOCK, line, DS_TONE_WARN };
		ds_page_line(a->r, &pf, &it, 1);
	} else {
		int got[DLK_KINDS];
		bool shown[DLK_KINDS];

		run_counts(v, got);
		run_shown(v, shown);
		time_left(run_eta(v, now), eta, sizeof eta);
		stats_draw(v, ZYPEROS_SCREEN_W / 2, pf.line_y, got, shown, eta);
	}
	foot(v, &pf, K, W, 2);
}

/* Done: what came as a wall, the counts per kind, what is left */
static void done_draw(dl_view *v, unsigned now)
{
	app *a = v->a;
	TTF_Font *fs = ui_font(UI_F_META);
	ds_page_frame pf = page_top(a, 1.0f);
	const char *head = v->end == END_SIGN_IN ? T(S_RADL_SIGN_IN_AGAIN) : v->end == END_STOPPED ? T(S_RADL_STOPPED)
	                 : T(S_SCRAPER_ALL_DONE);
	int m, n = g_rec.n > 14 ? 14 : g_rec.n, got[DLK_KINDS];
	float gap = (pf.body.w - 7.0f * DL_WALL) / 6;
	bool shown[DLK_KINDS];
	const char *K[2] = { "A", "B" };
	const char *W[2] = { v->end == END_SIGN_IN ? T(S_RADL_KEY_SIGN_IN) : T(S_SCRAPER_KEY_AGAIN), T(S_SCRAPER_KEY_CLOSE) };

	ds_page_head(a->r, &pf, DS_HEAD_SUB, head, v->why[0] ? v->why : NULL, UI_GLYPH_COUNT, now);
	for (m = 0; m < n; m++) {
		int k = n - 1 - m;     /* oldest first */

		arrival_draw(v, &g_rec.it[k], (SDL_FRect){ pf.body.x + (m % 7) * (DL_WALL + gap),
		                                         (float)(pf.body_y + (m / 7) * (DL_WALL + 16)), DL_WALL, DL_WALL },
		             255, false);
	}
	run_counts(v, got);
	run_shown(v, shown);
	stats_draw(v, ZYPEROS_SCREEN_W / 2, pf.body_y + 2 * DL_WALL + 16 + 48, got, shown, NULL);
	{
		ds_line_item it;

		/* offline now only when nothing was passed over (a source's day,
		 * a system it could not ask) */
		if (v->end == END_DONE && !v->why[0]) it = (ds_line_item){ UI_GLYPH_CHECK, T(S_DOWNLOADS_OFFLINE_NOW), DS_TONE_INFO };
		else it = (ds_line_item){ UI_GLYPH_COUNT, T(S_DOWNLOADS_NEXT_FROM_MISSING), DS_TONE_INFO };
		ds_page_line(a->r, &pf, &it, 1);
	}
	(void)fs;
	foot(v, &pf, K, W, 2);
}

static void dl_draw(dl_view *v, unsigned now)
{
	dl_frame *f = top_of(v);

	switch (f->v) {
	case V_KIND:    kind_draw(v, f, now); break;
	case V_SYSTEMS: systems_draw(v, f, now); break;
	case V_GAMES:   sysgames_draw(v, f, now); break;
	case V_RUN:     run_draw(v, now); break;
	case V_DONE:    done_draw(v, now); break;
	default:        hub_draw(v, f, now); break;
	}
}

/* The page under a panel opened from it (g_base_page) */
static void dl_base(app *a, void *ctx)
{
	(void)a;
	dl_draw(ctx, anim_now());
}

/* ---- starting and ending a run ------------------------------------------------------- */

/* Updates join Download (my pick in the lab): the achievements lane asks
 * RetroAchievements for each system's published counts and fetches again
 * every set that changed (radl.c), so Download runs it at least once a day
 * while achievements are in, even with nothing missing. dl.checked.ra is
 * the day's last check. */
#define DL_CHECK_S (24 * 60 * 60)

static bool ra_check_due(const dl_counts *c)
{
	char v[24];
	long long last = 0, now = (long long)time(NULL);

	if (!c || !c->ra_in || !dl_kind_in(DLK_ACH)) return false;
	if (db_get_str(db_dev(), "dl.checked.ra", v, sizeof v, NULL)) last = atoll(v);
	return now - last >= DL_CHECK_S || now < last;
}

static void ra_checked(void)
{
	char v[24];

	snprintf(v, sizeof v, "%lld", (long long)time(NULL));
	db_set_str(db_dev(), "dl.checked.ra", v);
}

/* The same for cheats: libretro rebuilds its list every day; a run asks its
 * last 64 KB, and fetches only the games whose files changed */
static bool cht_check_due(const dl_counts *c)
{
	char v[24];
	long long last = 0, now = (long long)time(NULL);
	int i;
	bool any = false;

	if (!c || !dl_kind_in(DLK_CHEATS)) return false;
	for (i = 0; i < c->n; i++) if (c->s[i].cht_sys && c->s[i].cht_have) any = true;
	if (!any) return false;
	if (db_get_str(db_dev(), "dl.checked.cht", v, sizeof v, NULL)) last = atoll(v);
	return now - last >= DL_CHECK_S || now < last;
}

static void cht_checked(void)
{
	char v[24];

	snprintf(v, sizeof v, "%lld", (long long)time(NULL));
	db_set_str(db_dev(), "dl.checked.cht", v);
}

/* The names part's systems: each one's Names switch (Systems' cell) */
static bool names_rule(const char *folder)
{
	return dl_sys_on(DLK_NAMES, folder);
}

/* Download: each lane with what it has to do, side by side (src/dlq.h).
 * `only` one kind, or -1 for every kind that is in. */
static bool run_start(dl_view *v, int only)
{
	const dl_counts *c = v->c;
	dl_plan pl;
	systems_cfg art_cfg, ra_cfg;
	scraper_kinds k;
	bool art = false, ra = false, cht = false, due, cdue;
	int i, j, covers = 0;

	if (!c) return false;
	if (!online()) {
		result_message(v->a, T(S_DOWNLOADS_HEADING), MSG_NEEDS_WIFI);
		return false;
	}
	dl_plan_of(c, only, &pl);
	due = (only < 0 || only == DLK_ACH) && ra_check_due(c);
	cdue = (only < 0 || only == DLK_CHEATS) && cht_check_due(c);
	if (!pl.n && !due && !cdue) return false;
	if (!pl.fits) {
		result_message(v->a, T(S_DOWNLOADS_HEADING), T(S_SCRAPER_NO_ROOM));
		return false;
	}
	k.covers = (only < 0 || only == DLK_COVERS) && dl_kind_in(DLK_COVERS);
	k.videos = (only < 0 || only == DLK_VIDEOS) && dl_kind_in(DLK_VIDEOS) && c->ss_in;
	k.names = (only < 0 || only == DLK_NAMES) && dl_kind_in(DLK_NAMES) && c->ss_in;
	if (only >= 0 && only != DLK_ACH) {
		/* a kind's own page: that kind, whatever Download has */
		k.covers = only == DLK_COVERS;
		k.videos = only == DLK_VIDEOS && c->ss_in;
		k.names = only == DLK_NAMES && c->ss_in;
	}
	memset(&art_cfg, 0, sizeof art_cfg);
	memset(&ra_cfg, 0, sizeof ra_cfg);
	memset(&g_cht_sys, 0, sizeof g_cht_sys);
	g_cht_games = 0;
	for (i = 0; i < c->n; i++) {
		const dl_sys *s = &c->s[i];
		const system_cfg *sc = NULL;

		for (j = 0; j < v->a->sys.count; j++)
			if (!strcmp(v->a->sys.systems[j].folder, s->folder)) sc = &v->a->sys.systems[j];
		if (!sc) continue;
		if ((k.covers && dl_sys_on(DLK_COVERS, s->folder)) || (k.names && dl_sys_on(DLK_NAMES, s->folder) && s->names_ok) ||
		    (k.videos && dl_sys_on(DLK_VIDEOS, s->folder))) {
			if (art_cfg.count < CFG_MAX_SYSTEMS) art_cfg.systems[art_cfg.count++] = *sc;
			if (k.covers && dl_sys_on(DLK_COVERS, s->folder)) covers += s->cov_miss;
		}
		if (s->ach_sys && dl_sys_on(DLK_ACH, s->folder) && ra_cfg.count < CFG_MAX_SYSTEMS)
			ra_cfg.systems[ra_cfg.count++] = *sc;
		if (s->cht_sys && dl_sys_on(DLK_CHEATS, s->folder) && g_cht_sys.count < CFG_MAX_SYSTEMS) {
			g_cht_sys.systems[g_cht_sys.count++] = *sc;
			g_cht_games += s->games;
		}
	}
	if (pl.covers + pl.videos + pl.looks > 0 && art_cfg.count)
		art = scraper_start_run(v->a, k, &art_cfg, v->only[0] ? v->only : NULL, covers, names_rule);
	if ((only < 0 || only == DLK_ACH) && (pl.ach > 0 || due) && ra_cfg.count &&
	    (ra = radl_start_run(v->a, &ra_cfg, v->only[0] ? v->only : NULL)))
		ra_checked();
	if ((only < 0 || only == DLK_CHEATS) && (pl.cheats > 0 || cdue) && g_cht_sys.count && !dlq_running(DLQ_CHT)) {
		snprintf(g_cht_only, sizeof g_cht_only, "%s", v->only);
		CHT_LANE.card = P_CARD;
		if ((cht = dlq_start(DLQ_CHT, &CHT_LANE, cht_lane_begin))) cht_checked();
	}
	if (!art && !ra && !cht) return false;
	/* a new run: what came starts again (unless it joined one going on) */
	rec_sync();
	v->rec_key = g_rec.key;
	memset(&v->ap, 0, sizeof v->ap);
	memset(&v->rp, 0, sizeof v->rp);
	memset(&v->cp, 0, sizeof v->cp);
	v->art = art;
	v->ra = ra;
	v->cht = cht;
	v->art_total = covers;
	v->art_looks = pl.looks;
	v->run_kind = only;
	v->t0 = anim_now();
	v->done0 = 0;
	v->why[0] = '\0';
	return true;
}

/* Back on a run that went on behind the screens: what came stays as it is
 * (g_rec), what came meanwhile with it */
static void run_rejoin(dl_view *v)
{
	v->art = dlq_running(DLQ_ART);
	v->ra = dlq_running(DLQ_RA);
	v->cht = dlq_running(DLQ_CHT);
	if (v->art) art_lane_status(&v->ap);
	if (v->ra) radl_status(&v->rp);
	if (v->cht) cheats_status(&v->cp);
	v->rec_key = g_rec.key;
	v->art_total = g_fake.on ? g_fake.n : scraper_run_total();
	v->art_looks = v->ap.dtotal;
	v->run_kind = -1;
	v->t0 = anim_now();
	{
		int d, t;

		run_progress(v, &d, &t);
		v->done0 = d;
	}
}

/* The run is over, here or behind the screens: how it ended, from what the
 * lanes kept as they ended */
static void run_finish(dl_view *v)
{
	dlq_end ea = v->art ? dlq_last(DLQ_ART) : DLQ_NONE, er = v->ra ? dlq_last(DLQ_RA) : DLQ_NONE;
	dlq_end ec = v->cht ? dlq_last(DLQ_CHT) : DLQ_NONE;

	if (v->art) art_lane_last(&v->ap);
	if (v->ra) radl_last(&v->rp);
	if (v->cht) v->cp = g_cht_last;
	v->end = END_DONE;
	if (v->ra && v->rp.problem == RADL_SIGN_IN) {
		v->end = END_SIGN_IN;
		snprintf(v->why, sizeof v->why, "%s", T(S_RADL_REFUSED));
	} else if (ea == DLQ_FULL || er == DLQ_FULL || ec == DLQ_FULL || (v->ra && v->rp.problem == RADL_NO_ROOM)) {
		v->end = END_STOPPED;
		snprintf(v->why, sizeof v->why, "%s", T(S_SCRAPER_NO_ROOM));
	} else if (v->cht && v->cp.problem != CHT_OK && !v->why[0]) {
		/* libretro's list would not come: the rest still did */
		v->end = END_STOPPED;
		snprintf(v->why, sizeof v->why, "%s", T(S_DOWNLOADS_LIBRETRO_NO_ANSWER));
	} else if (ea == DLQ_STOPPED || er == DLQ_STOPPED || ec == DLQ_STOPPED) {
		v->end = END_STOPPED;
		if (!v->why[0]) snprintf(v->why, sizeof v->why, "%s", T(S_DOWNLOADS_YOU_STOPPED));
	} else if (v->ra && v->rp.problem == RADL_NO_ANSWER) {
		v->end = END_STOPPED;
		snprintf(v->why, sizeof v->why, "%s", T(S_RADL_NO_ANSWER));
	} else if (v->art && v->ap.problem[0]) {
		snprintf(v->why, sizeof v->why, "%s", v->ap.problem);
	}
	g_shown_art = dlq_ends(DLQ_ART);
	g_shown_ra = dlq_ends(DLQ_RA);
	g_shown_cht = dlq_ends(DLQ_CHT);
	dl_count_stale();
	dl_count_again(v->a, v->only);
}

/* ---- panels over the page ---------------------------------------------------------------- */

/* Videos For: which games' video snaps a download gets */
typedef struct { const dl_counts *c; char val[SNAPS_FOR_COUNT][64]; } vfor_ctx;

static int vfor_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static const str_id FOR[SNAPS_FOR_COUNT] = { S_SCRAPER_ALL_GAMES, S_LIBRARY_FAVORITES, S_SCRAPER_PLAYED };
	vfor_ctx *x = ctx;
	const dl_counts *c = x->c;
	int w, i;

	(void)max;
	*heading = T(S_SCRAPER_VIDEOS_FOR);
	for (w = 0; w < SNAPS_FOR_COUNT; w++) {
		int n = 0;
		char sz[24];

		for (i = 0; c && i < c->n; i++) if (dl_sys_on(DLK_VIDEOS, c->s[i].folder)) n += c->s[i].vid_need[w];
		bytes_text(n * (c ? c->mb_video : 2.5) * 1e6, sz, sizeof sz);
		snprintf(x->val[w], sizeof x->val[w], "%d \xc2\xb7 %s", n, sz);
		rows[w] = (menu_row){ T(FOR[w]), x->val[w], true };
	}
	return SNAPS_FOR_COUNT;
}

static menu_result vfor_key(app *a, void *ctx, in_button key, int sel)
{
	(void)a;
	(void)ctx;
	if (key != IN_ACCEPT || sel < 0 || sel >= SNAPS_FOR_COUNT) return MENU_STAY;
	snaps_set_for(sel);
	return MENU_DONE;
}

static void videos_for(dl_view *v)
{
	static vfor_ctx x;

	memset(&x, 0, sizeof x);
	x.c = v->c;
	menu_run(v->a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(v->a), .start_sel = snaps_for() },
	         vfor_build, vfor_key, &x);
}

/* RetroAchievements' account: who is signed in, a sign-in again, and a
 * sign-out that asks first (there was none before, anywhere) */
enum { RA_WHO, RA_AGAIN, RA_OUT, RA_ROWS };
typedef struct { char who[96]; bool done; } ra_ctx;

static int ra_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	ra_ctx *x = ctx;

	(void)max;
	*heading = "RetroAchievements";
	if (!g_fx_user && !ra_signed_in()) {
		rows[0] = (menu_row){ T(S_SYSMENU_SIGN_IN), NULL, true };
		return 1;
	}
	snprintf(x->who, sizeof x->who, T(S_DOWNLOADS_SIGNED_IN_AS), g_fx_user ? g_fx_user : ra_user());
	rows[RA_WHO] = (menu_row){ x->who, NULL, false };
	rows[RA_AGAIN] = (menu_row){ T(S_DOWNLOADS_SIGN_IN_AGAIN), NULL, true };
	rows[RA_OUT] = (menu_row){ T(S_DOWNLOADS_SIGN_OUT), NULL, true };
	return RA_ROWS;
}

static menu_result ra_key(app *a, void *ctx, in_button key, int sel)
{
	(void)ctx;
	if (key != IN_ACCEPT) return MENU_STAY;
	if (!ra_signed_in()) {
		ra_signin_screen(a);
		return MENU_CHANGED;
	}
	if (sel == RA_AGAIN) {
		ra_signin_screen(a);
		return MENU_CHANGED;
	}
	if (sel == RA_OUT) {
		char q[512];

		cq_text(CQ_RA_SIGN_OUT, q, sizeof q);
		if (!confirm_ask(a, CQ_RA_SIGN_OUT, q)) return MENU_STAY;
		ra_sign_out();
		return MENU_CHANGED;
	}
	return MENU_STAY;
}

static void ra_account(dl_view *v)
{
	static ra_ctx x;

	memset(&x, 0, sizeof x);
	menu_run(v->a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(v->a), .start_sel = 1 },
	         ra_build, ra_key, &x);
}

/* On the Card: each kind's room, and a delete that asks first for the kinds
 * a download can get again */
typedef struct { const dl_counts *c; char val[DLK_KINDS][32]; } room_ctx;

static int room_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	room_ctx *x = ctx;
	const dl_counts *c = x->c;
	int k;

	(void)max;
	*heading = T(S_DOWNLOADS_TILE_ON_CARD);
	for (k = 0; k < DLK_KINDS; k++) {
		dl_sum s = { 0 };

		if (c) dl_sum_of(c, (dl_kind)k, &s);
		if (k == DLK_NAMES || (k == DLK_CHEATS && s.bytes && s.bytes < 1000000ULL))
			snprintf(x->val[k], sizeof x->val[k], "%s", T(S_DOWNLOADS_UNDER_1MB));
		else bytes_text((double)s.bytes, x->val[k], sizeof x->val[k]);
		rows[k] = (menu_row){ kind_name((dl_kind)k), x->val[k], true };
	}
	/* the note under the five kinds, no rule: a rule would push it out of the
	 * panel at the menus' row height */
	rows[DLK_KINDS] = (menu_row){ T(S_DOWNLOADS_STORAGE_TOO), MENU_NOTE_MARK, false };
	return DLK_KINDS + 1;
}

static void delete_sets(const dl_counts *c)
{
	char dir[LIB_PATH * 2], p[LIB_PATH * 3];
	int i, j, n;
	char **v;

	for (i = 0; c && i < c->n; i++) {
		if (!c->s[i].ach_sys) continue;
		snprintf(dir, sizeof dir, "%s/%s/.cheevos", P_ROMS, c->s[i].folder);
		v = list_dir(dir, &n);
		for (j = 0; j < n; j++) {
			size_t l = strlen(v[j]);

			/* the sets and the nones; the hashes stay, so a download does
			 * not hash the card again */
			if ((l > 4 && !strcmp(v[j] + l - 4, ".set")) || (l > 5 && !strcmp(v[j] + l - 5, ".none"))) {
				snprintf(p, sizeof p, "%s/%s", dir, v[j]);
				remove(p);
			}
		}
		list_free(v, n);
	}
}

static menu_result room_key(app *a, void *ctx, in_button key, int sel)
{
	room_ctx *x = ctx;
	const dl_counts *c = x->c;
	char q[512], sz[24];
	dl_sum s = { 0 };

	if (key != IN_ACCEPT || sel < 0 || sel >= DLK_KINDS) return MENU_STAY;
	if (sel == DLK_COVERS || sel == DLK_NAMES) {
		result_message(a, kind_name((dl_kind)sel), sel == DLK_COVERS ? T(S_DOWNLOADS_COVER_WHERE) : T(S_DOWNLOADS_NAMES_STAY));
		return MENU_STAY;
	}
	if (c) dl_sum_of(c, (dl_kind)sel, &s);
	if (!s.have && !(sel == DLK_CHEATS && s.none)) return MENU_FAILED;
	bytes_text((double)s.bytes, sz, sizeof sz);
	if (sel == DLK_VIDEOS) {
		cq_text(CQ_DL_DELETE_VIDEOS, q, sizeof q, sz);
		if (confirm_ask(a, CQ_DL_DELETE_VIDEOS, q)) {
			for (int i = 0; i < c->n; i++) snaps_delete(P_ROMS, c->s[i].folder);
			dl_count_stale();
			dl_count_again(a, c->only);
			return MENU_CHANGED;
		}
	} else if (sel == DLK_CHEATS) {
		cq_text(CQ_DL_DELETE_CHEATS, q, sizeof q);
		if (confirm_ask(a, CQ_DL_DELETE_CHEATS, q)) {
			cheats_delete_all(&a->sys);
			dl_count_stale();
			dl_count_again(a, c->only);
			return MENU_CHANGED;
		}
	} else {
		cq_text(CQ_DL_DELETE_SETS, q, sizeof q, sz);
		if (confirm_ask(a, CQ_DL_DELETE_SETS, q)) {
			delete_sets(c);
			dl_count_stale();
			dl_count_again(a, c->only);
			return MENU_CHANGED;
		}
	}
	return MENU_STAY;
}

static void on_the_card(dl_view *v)
{
	static room_ctx x;

	memset(&x, 0, sizeof x);
	x.c = v->c;
	menu_run(v->a, &(menu_style){ .accent = MENU_ZYPEROS }, room_build, room_key, &x);
}

/* Settings (START): the three accounts, Video Snaps, Names and Details, and
 * On the Card */
/* Six rows and no rule between the accounts and the rest: at the menus' row
 * height a rule is the room the sixth row needs, and On the Card would
 * scroll out of sight (the lab's rows are shorter) */
enum { SET_SS, SET_RA, SET_SG, SET_SNAPS, SET_NAMES, SET_ROOM, SET_ROWS };
typedef struct {
	dl_view *v;
	char ss[64], ra[64], sg[32], room[32];
	unsigned char sound[SET_ROWS];
	menu_choice choice[SET_ROWS];
	const char *onoff[2];
} set_ctx;

static int set_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	set_ctx *s = ctx;
	const dl_counts *c = s->v ? s->v->c : NULL;
	unsigned long long room = 0;
	int k;

	(void)max;
	*heading = T(S_HOME_SETTINGS);
	if (g_fx_user) {
		/* a shot's accounts, linked */
		snprintf(s->ss, sizeof s->ss, "%s", g_fx_user);
		snprintf(s->ra, sizeof s->ra, "%s", g_fx_user);
		snprintf(s->sg, sizeof s->sg, "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2" "1a2b");
	} else {
		if (!ss_have_dev()) snprintf(s->ss, sizeof s->ss, "%s", T(S_SYSMENU_NOT_IN_BUILD));
		else if (ss_signed_in()) snprintf(s->ss, sizeof s->ss, "%s", ss_user());
		else snprintf(s->ss, sizeof s->ss, "%s", T(S_SYSMENU_SC_LINK));
		if (ra_signed_in()) snprintf(s->ra, sizeof s->ra, "%s", ra_user());
		else snprintf(s->ra, sizeof s->ra, "%s", T(S_SYSMENU_SIGN_IN));
		if (!sgdb_have_key()) snprintf(s->sg, sizeof s->sg, "%s", T(S_SYSMENU_SC_LINK));
		else if (sgdb_key_state() == SGDB_REFUSED) snprintf(s->sg, sizeof s->sg, "%s", T(S_SCRAPER_KEY_REFUSED));
		else sgdb_key_masked(s->sg, sizeof s->sg);
	}
	for (k = 0; c && k < DLK_KINDS; k++) {
		dl_sum sm;

		dl_sum_of(c, (dl_kind)k, &sm);
		room += sm.bytes;
	}
	if (c) bytes_text((double)room, s->room, sizeof s->room);
	else snprintf(s->room, sizeof s->room, "%s", T(S_DOWNLOADS_COUNTING));
	rows[SET_SS] = (menu_row){ "ScreenScraper", s->ss, true };
	rows[SET_RA] = (menu_row){ "RetroAchievements", s->ra, true };
	rows[SET_SG] = (menu_row){ "SteamGridDB", s->sg, true };
	rows[SET_SNAPS] = (menu_row){ T(S_SNAPS_HEADING), NULL, true };
	s->onoff[0] = T(S_SCRAPER_ON);
	s->onoff[1] = T(S_SCRAPER_OFF);
	rows[SET_NAMES] = (menu_row){ T(S_SCRAPER_NAMES_DETAILS), scraper_names_on() ? s->onoff[0] : s->onoff[1], true };
	rows[SET_ROOM] = (menu_row){ T(S_DOWNLOADS_TILE_ON_CARD), s->room, true };
	memset(s->sound, 0, sizeof s->sound);
	memset(s->choice, 0, sizeof s->choice);
	s->sound[SET_NAMES] = MENU_SND_RING;
	s->choice[SET_NAMES] = (menu_choice){ .n = 2, .cur = scraper_names_on() ? 0 : 1, .labels = s->onoff };
	return SET_ROWS;
}

static menu_result set_key(app *a, void *ctx, in_button key, int sel)
{
	set_ctx *s = ctx;

	if (sel == SET_NAMES) {
		if (key != IN_LEFT && key != IN_RIGHT) return MENU_STAY;
		scraper_names_set(!scraper_names_on());
		return MENU_CHANGED;
	}
	if (key != IN_ACCEPT) return MENU_STAY;
	switch (sel) {
	case SET_SS:    ss_page(a); break;
	case SET_RA:    ra_account(s->v); break;
	case SET_SG:    sgdb_screen(a); break;
	case SET_SNAPS: snaps_screen(a); break;
	case SET_ROOM:  on_the_card(s->v); break;
	default:        return MENU_STAY;
	}
	return MENU_CHANGED;
}

static void settings(dl_view *v)
{
	static set_ctx s;

	memset(&s, 0, sizeof s);
	s.v = v;
	menu_run(v->a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(v->a), .sound = s.sound,
	                               .choices = s.choice },
	         set_build, set_key, &s);
}

/* A game's panel: what it has of each kind. A on its cover or its video
 * opens its Cover and Video page (the picker, every source); A on its names
 * or its achievements gets them, and Get What's Missing gets all it lacks,
 * one after another, in the panel: each value says Getting and turns as its
 * file lands. Its own requests, beside no lane (the lanes ask the same
 * servers): with a download going on it waits for that. */
/* Five kinds and Get What's Missing, no rule: as Settings, six rows are what
 * a panel holds at the menus' row height */
enum { GP_ROWS = DLK_KINDS + 1, GP_GET = DLK_KINDS };
enum { ONE_IDLE, ONE_SS, ONE_HASH, ONE_SET, ONE_CHT };

typedef struct {
	dl_view *v;
	dl_game *g;
	const system_cfg *sc;
	char val[DLK_KINDS][64];
	int queue[DLK_KINDS], nq, at;
	int phase;
	char set[RADL_SET_PATH], hash[40];
	bool video_asked;
} game_ctx;

static game_ctx *g_one;

/* Asked by the ScreenScraper run for this game alone */
static bool one_wants_video(const char *folder, const char *file)
{
	return g_one && g_one->g->st[DLK_VIDEOS] != GS_HAVE && !strcmp(folder, g_one->sc->folder) &&
	       !strcmp(file, g_one->g->file);
}

/* Whether the game's video is on the card: its own snap with something in
 * it, else EmulationStation's (es_art_video), as the shelf plays them. Not
 * the snap's path alone, which is always there to be had: taken for the
 * snap, every game's Videos said On card once its panel looked again (a
 * fetch, the Cover and Video page), and Get What's Missing asked for no
 * video after that (one_wants_video). */
static bool video_there(const system_cfg *sc, const dl_game *g)
{
	char dir[LIB_PATH * 2], p[LIB_PATH * 3];
	struct stat st;

	snprintf(dir, sizeof dir, "%s/%s", P_ROMS, sc->folder);
	return (snaps_path(P_ROMS, sc->folder, g->name, p, sizeof p) && stat(p, &st) == 0 && st.st_size > 0) ||
	       es_art_video(dir, g->file, g->name);
}

/* What the card has for the game now, after a file landed or its Cover and
 * Video page closed. Its video as the list reads it (sysgames_load): on the
 * card, else missing, or off for a system that gets none; one deleted on
 * the Cover and Video page is missing again. */
static void one_look(game_ctx *x)
{
	dl_game *g = x->g;
	char dir[LIB_PATH * 2], p[LIB_PATH * 3], t[GAME_TITLE_MAX], np[RADL_SET_PATH + 16];

	snprintf(dir, sizeof dir, "%s/%s", P_ROMS, x->sc->folder);
	if (has_box_art(x->sc->folder, g->name) || es_art_cover(dir, g->file, g->name)) g->st[DLK_COVERS] = GS_HAVE;
	g->st[DLK_VIDEOS] = video_there(x->sc, g) ? GS_HAVE : dl_sys_on(DLK_VIDEOS, x->sc->folder) ? GS_MISS : GS_OFF;
	if (db_game_title(db_lib(), x->sc->folder, g->file, t, sizeof t) && t[0]) g->st[DLK_NAMES] = GS_HAVE;
	chv_path(P_ROMS, x->sc->folder, g->name, p, sizeof p);
	chv_none_path(p, np, sizeof np);
	if (access(p, F_OK) == 0) g->st[DLK_ACH] = GS_HAVE;
	else if (access(np, F_OK) == 0) g->st[DLK_ACH] = GS_NONE;
	if (g->st[DLK_CHEATS] != GS_OFF) {
		int cs = cheats_game_state(x->sc->folder, g->name);

		g->st[DLK_CHEATS] = cs > 0 ? GS_HAVE : cs < 0 ? GS_NONE : GS_MISS;
	}
}

static void one_next(game_ctx *x);

static void one_done(game_ctx *x)
{
	if (x->video_asked) ss_run_videos_default();
	x->video_asked = false;
	x->phase = ONE_IDLE;
	one_look(x);
	x->at++;
	one_next(x);
}

/* The next kind in the queue, begun; none left: done, and the sound of it */
static void one_next(game_ctx *x)
{
	char dir[LIB_PATH * 2], rom[LIB_PATH * 3];

	snprintf(dir, sizeof dir, "%s/%s", P_ROMS, x->sc->folder);
	while (x->at < x->nq) {
		int k = x->queue[x->at];

		if (x->g->st[k] == GS_HAVE || x->g->st[k] == GS_NONE) { x->at++; continue; }
		if (k == DLK_CHEATS) {
			if (cheats_dl_one_begin(x->sc->folder, x->g->file)) { x->phase = ONE_CHT; return; }
		} else if (k == DLK_ACH) {
			snprintf(rom, sizeof rom, "%s/%s", dir, x->g->file);
			chv_path(P_ROMS, x->sc->folder, x->g->name, x->set, sizeof x->set);
			if (rhj_begin(rom, x->sc->tag, x->set)) { x->phase = ONE_HASH; return; }
		} else {
			bool ok;

			/* covers and videos come with the names, one request */
			if (k == DLK_VIDEOS || x->g->st[DLK_VIDEOS] == GS_MISS) {
				ss_run_videos(one_wants_video);
				x->video_asked = true;
			}
			ok = k == DLK_NAMES && x->g->st[DLK_COVERS] == GS_HAVE
			   ? ss_run_begin_metadata(x->sc->folder, x->g->file, x->g->name, dir, x->sc->exts)
			   : ss_run_begin(x->sc->folder, x->g->file, x->g->name, dir, x->sc->exts);
			if (ok) { x->phase = ONE_SS; return; }
			if (x->video_asked) ss_run_videos_default();
			x->video_asked = false;
		}
		x->at++;
	}
	x->nq = x->at = 0;
	x->phase = ONE_IDLE;
}

/* A frame of the game's own fetch */
static void one_step(game_ctx *x)
{
	char h[40], set[RADL_SET_PATH];
	int r;

	switch (x->phase) {
	case ONE_SS:
		if ((r = ss_run_step()) == 1) return;
		ss_run_cancel();
		one_done(x);
		return;
	case ONE_HASH:
		if ((r = rhj_poll(h, set, sizeof set)) == 0) return;
		if (r == 1) {
			snprintf(x->hash, sizeof x->hash, "%s", h);
			ra_fetch_begin(x->hash, x->set);
			x->phase = ONE_SET;
			return;
		}
		one_done(x);
		return;
	case ONE_SET:
		if ((r = ra_fetch_step()) == 0) return;
		if (r == RA_FETCH_NOTHING) chv_none_mark(x->set, true);
		one_done(x);
		return;
	case ONE_CHT:
		if (cheats_dl_one_step() == 1) return;
		one_done(x);
		return;
	default:
		return;
	}
}

static bool one_busy(const game_ctx *x, int k)
{
	return x->phase != ONE_IDLE && x->at < x->nq && x->queue[x->at] == k;
}

static int game_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	game_ctx *x = ctx;
	const dl_game *g = x->g;
	static const str_id ROW[DLK_KINDS] = { S_DOWNLOADS_HAS_COVER, S_DOWNLOADS_HAS_VIDEO, S_SCRAPER_NAMES_DETAILS,
	                                       S_SYSMENU_CHEEVOS, S_DOWNLOADS_KIND_CHEATS };
	bool any = false;
	int k;

	(void)max;
	one_step(x);
	*heading = g->title;
	for (k = 0; k < DLK_KINDS; k++) {
		if (one_busy(x, k) || (k == DLK_VIDEOS && one_busy(x, DLK_COVERS) && g->st[k] == GS_MISS)) {
			snprintf(x->val[k], sizeof x->val[k], "%s", T(S_DOWNLOADS_GETTING));
		} else switch (g->st[k]) {
		case GS_HAVE: snprintf(x->val[k], sizeof x->val[k], "%s", k == DLK_ACH || k == DLK_CHEATS ? T(S_DOWNLOADS_READY) : T(S_DOWNLOADS_ON_CARD_SHORT)); break;
		case GS_NONE: snprintf(x->val[k], sizeof x->val[k], "%s", T(S_DOWNLOADS_NONE)); break;
		/* a video not on the card is None whether or not its system's
		 * switch is on: A opens its Cover and Video page, where it can come */
		case GS_OFF:  snprintf(x->val[k], sizeof x->val[k], "%s", k == DLK_VIDEOS ? T(S_DOWNLOADS_NONE) : T(S_SCRAPER_OFF)); break;
		default:      snprintf(x->val[k], sizeof x->val[k], "%s", k == DLK_ACH || k == DLK_CHEATS ? T(S_DOWNLOADS_NOT_CHECKED)
		                                                          : k == DLK_VIDEOS ? T(S_DOWNLOADS_NONE) : T(S_DOWNLOADS_MISSING)); break;
		}
		if (g->st[k] == GS_MISS) any = true;
		rows[k] = (menu_row){ T(ROW[k]), x->val[k], true };
	}
	rows[GP_GET] = (menu_row){ T(S_DOWNLOADS_GET_MISSING), NULL, any && x->phase == ONE_IDLE };
	return GP_ROWS;
}

/* What the game's own fetch can get now, or why not */
static bool one_can(app *a, game_ctx *x, int k)
{
	if (!online()) {
		result_message(a, x->g->title, MSG_NEEDS_WIFI);
		return false;
	}
	if (dl_live() || x->phase != ONE_IDLE) return false;
	if (k == DLK_CHEATS) return x->v->c && sys_of(x->v, x->sc->folder) && sys_of(x->v, x->sc->folder)->cht_sys &&
	                            !cheats_running();
	if (k == DLK_ACH) return ra_signed_in() && x->v->c && sys_of(x->v, x->sc->folder) &&
	                         sys_of(x->v, x->sc->folder)->ach_sys;
	return ss_signed_in();
}

static menu_result game_key(app *a, void *ctx, in_button key, int sel)
{
	game_ctx *x = ctx;
	int si, k;

	if (key != IN_ACCEPT) return MENU_STAY;
	if (sel == DLK_COVERS || sel == DLK_VIDEOS) {
		if (x->phase != ONE_IDLE) return MENU_FAILED;
		for (si = 0; si < a->sys.count; si++) {
			const game_list *gl;

			if (strcmp(a->sys.systems[si].folder, x->sc->folder)) continue;
			gl = sys_games(&a->view[si]);
			if (x->g->gi < gl->count) media_screen(a, si, &gl->items[x->g->gi]);
			mt_start();               /* the Cover and Video page stopped it */
			one_look(x);
			return MENU_CHANGED;
		}
		return MENU_STAY;
	}
	if (sel == DLK_NAMES || sel == DLK_ACH || sel == DLK_CHEATS) {
		if (x->g->st[sel] != GS_MISS || !one_can(a, x, sel)) return MENU_FAILED;
		x->queue[0] = sel;
		x->nq = 1;
		x->at = 0;
		one_next(x);
		return MENU_CHANGED;
	}
	if (sel == GP_GET) {
		x->nq = x->at = 0;
		for (k = 0; k < DLK_KINDS; k++)
			if (x->g->st[k] == GS_MISS && one_can(a, x, k) && !(k == DLK_VIDEOS && x->g->st[DLK_COVERS] == GS_MISS))
				x->queue[x->nq++] = k;
		if (!x->nq) return MENU_FAILED;
		one_next(x);
		return MENU_CHANGED;
	}
	return MENU_STAY;
}

static void game_panel(dl_view *v, dl_game *g, const system_cfg *sc)
{
	static game_ctx x;

	memset(&x, 0, sizeof x);
	x.v = v;
	x.g = g;
	x.sc = sc;
	g_one = &x;
	menu_run(v->a, &(menu_style){ .accent = MENU_ZYPEROS, .fixed_w = menu_std_width(v->a) }, game_build, game_key, &x);
	/* closed with a fetch going: let it go */
	if (x.phase == ONE_SS) ss_run_cancel();
	if (x.phase == ONE_CHT) cheats_cancel();
	if (x.video_asked) ss_run_videos_default();
	g_one = NULL;
}

/* ---- keys ---------------------------------------------------------------------------- */

/* Up and Down on a list of `n` rows: round on a press, stopping at the ends
 * while held */
static bool move_list(app *a, int *sel, int n, bool rep_up, bool rep_dn)
{
	int to = *sel;

	if (rep_up) to = to > 0 ? to - 1 : a->in.pressed[IN_UP] ? n - 1 : to;
	if (rep_dn) to = to < n - 1 ? to + 1 : a->in.pressed[IN_DOWN] ? 0 : to;
	if (to == *sel) {
		if (rep_up || rep_dn) uisound_play(DS_SOUND_EDGE);
		return false;
	}
	*sel = to;
	uisound_play(DS_SOUND_MOVE);
	return true;
}

static int hub_row_y(int sel)
{
	return DS_PAGE_BODY_Y + sel * DL_ROW + (sel > 0 ? DL_RULE_H : 0);
}

static void open_run(dl_view *v)
{
	push(v, V_RUN);
	dlq_page(DLQ_ART, true);
	dlq_page(DLQ_RA, true);
	dlq_page(DLQ_CHT, true);
}

static void close_run_view(void)
{
	dlq_page(DLQ_ART, false);
	dlq_page(DLQ_RA, false);
	dlq_page(DLQ_CHT, false);
}

/* A: Download, or the run that goes on */
static void download(dl_view *v, int only)
{
	if (dl_live()) {
		uisound_play(DS_SOUND_ACCEPT);
		/* a run this page does not follow yet (started elsewhere, or again) */
		if ((dlq_running(DLQ_ART) && !v->art) || (dlq_running(DLQ_RA) && !v->ra) ||
		    (dlq_running(DLQ_CHT) && !v->cht) || (!v->art && !v->ra && !v->cht))
			run_rejoin(v);
		open_run(v);
		return;
	}
	if (!v->c) {
		/* still counting: it starts as the count ends */
		uisound_play(DS_SOUND_ACCEPT);
		v->pending = only;
		return;
	}
	if (run_start(v, only)) {
		uisound_play(DS_SOUND_ACCEPT);
		open_run(v);
	} else {
		uisound_play(DS_SOUND_DENY);
	}
}

static void hub_keys(dl_view *v, dl_frame *f, unsigned now)
{
	app *a = v->a;
	bool up = in_repeat(&a->in, IN_UP), dn = in_repeat(&a->in, IN_DOWN);
	int from = f->sel;

	if ((up || dn) && move_list(a, &f->sel, 1 + DLK_KINDS, up, dn)) glide(f, hub_row_y(from), now);
	if ((a->in.pressed[IN_LEFT] || a->in.pressed[IN_RIGHT]) && f->sel > 0) {
		dl_kind k = (dl_kind)(f->sel - 1);

		if (!v->c || dl_kind_blocked(v->c, k)) {
			uisound_play(DS_SOUND_DENY);
		} else {
			bool in = !dl_kind_in(k);
			dl_sum s;

			dl_kind_set(k, in);
			/* In, with no system's switch on: every system on */
			dl_sum_of(v->c, k, &s);
			if (in && !s.on)
				for (int i = 0; i < v->c->n; i++) dl_sys_set(k, v->c->s[i].folder, true);
			uisound_play(DS_SOUND_MOVE);
		}
	} else if (a->in.pressed[IN_LEFT] || a->in.pressed[IN_RIGHT]) {
		uisound_play(DS_SOUND_EDGE);
	}
	if (a->in.pressed[IN_ACCEPT]) {
		if (f->sel == 0) download(v, -1);
		else if (f->sel - 1 == DLK_ACH) {
			uisound_play(DS_SOUND_ACCEPT);
			radl_screen_from_downloads(a);
			dl_count_again(a, v->only);
		} else {
			uisound_play(DS_SOUND_ACCEPT);
			push(v, V_KIND);
			top_of(v)->kind = f->sel - 1;
		}
	} else if (a->in.pressed[IN_Y]) {
		int col = f->sel > 0 ? f->sel - 1 : 0;

		uisound_play(DS_SOUND_ACCEPT);
		push(v, V_SYSTEMS);
		top_of(v)->col = col;
	} else if (a->in.pressed[IN_START]) {
		uisound_play(DS_SOUND_ACCEPT);
		settings(v);
		dl_count_again(a, v->only);
	}
}

static void kind_keys(dl_view *v, dl_frame *f)
{
	app *a = v->a;

	if (a->in.pressed[IN_ACCEPT]) download(v, f->kind);
	else if (a->in.pressed[IN_X] && f->kind == DLK_VIDEOS) {
		uisound_play(DS_SOUND_ACCEPT);
		videos_for(v);
		dl_count_again(a, v->only);
	} else if (a->in.pressed[IN_Y]) {
		int col = f->kind;

		uisound_play(DS_SOUND_ACCEPT);
		push(v, V_SYSTEMS);
		top_of(v)->col = col;
	}
}

static void systems_keys(dl_view *v, dl_frame *f)
{
	app *a = v->a;
	int n = v->c ? v->c->n : 0;
	bool up = in_repeat(&a->in, IN_UP), dn = in_repeat(&a->in, IN_DOWN);

	if (!n) return;
	if (up || dn) move_list(a, &f->row, n, up, dn);
	if (in_repeat(&a->in, IN_LEFT)) {
		if (f->col > 0) { f->col--; uisound_play(DS_SOUND_MOVE); } else uisound_play(DS_SOUND_EDGE);
	}
	if (in_repeat(&a->in, IN_RIGHT)) {
		if (f->col < DLK_KINDS - 1) { f->col++; uisound_play(DS_SOUND_MOVE); } else uisound_play(DS_SOUND_EDGE);
	}
	if (a->in.pressed[IN_ACCEPT]) {
		dl_kind k = (dl_kind)f->col;
		const dl_sys *s = &v->c->s[f->row];

		if (dl_kind_blocked(v->c, k) || (k == DLK_ACH && !s->ach_sys) || (k == DLK_NAMES && !s->names_ok) ||
		    (k == DLK_CHEATS && !s->cht_sys)) {
			uisound_play(DS_SOUND_DENY);
		} else if (!dl_kind_in(k)) {
			/* the kind was out: in, for this system alone */
			int i;

			dl_kind_set(k, true);
			for (i = 0; i < n; i++) dl_sys_set(k, v->c->s[i].folder, i == f->row);
			uisound_play(DS_SOUND_MOVE);
		} else {
			dl_sys_set(k, s->folder, !dl_sys_on(k, s->folder));
			uisound_play(DS_SOUND_MOVE);
		}
	} else if (a->in.pressed[IN_X]) {
		const dl_sys *s = &v->c->s[f->row];
		char q[512], sz[24];

		if (f->col == DLK_VIDEOS && s->vid_have) {
			uisound_play(DS_SOUND_ACCEPT);
			bytes_text((double)s->vid_bytes, sz, sizeof sz);
			cq_text(CQ_DL_SYS_VIDEOS, q, sizeof q, s->name, sz);
			if (confirm_ask(a, CQ_DL_SYS_VIDEOS, q)) {
				snaps_delete(P_ROMS, s->folder);
				dl_count_stale();
				dl_count_again(a, v->only);
			}
		} else if (f->col == DLK_ACH && s->ach_ready) {
			dl_counts one;

			uisound_play(DS_SOUND_ACCEPT);
			bytes_text(s->ach_ready * DL_MB_SET * 1e6, sz, sizeof sz);
			cq_text(CQ_DL_SYS_SETS, q, sizeof q, s->name, sz);
			if (confirm_ask(a, CQ_DL_SYS_SETS, q)) {
				memset(&one, 0, sizeof one);
				one.s[0] = *s;
				one.n = 1;
				delete_sets(&one);
				dl_count_stale();
				dl_count_again(a, v->only);
			}
		} else if (f->col == DLK_CHEATS && s->cht_have + s->cht_none) {
			uisound_play(DS_SOUND_ACCEPT);
			cq_text(CQ_DL_SYS_CHEATS, q, sizeof q, s->name);
			if (confirm_ask(a, CQ_DL_SYS_CHEATS, q)) {
				cheats_delete_sys(s->folder);
				dl_count_stale();
				dl_count_again(a, v->only);
			}
		} else {
			uisound_play(DS_SOUND_DENY);
		}
	} else if (a->in.pressed[IN_Y]) {
		char folder[CFG_STR];

		snprintf(folder, sizeof folder, "%s", v->c->s[f->row].folder);
		uisound_play(DS_SOUND_ACCEPT);
		push(v, V_GAMES);
		snprintf(top_of(v)->sys, sizeof top_of(v)->sys, "%s", folder);
		sysgames_load(v, folder);
	}
}

static void sysgames_keys(dl_view *v, dl_frame *f, unsigned now)
{
	app *a = v->a;
	bool up = in_repeat(&a->in, IN_UP), dn = in_repeat(&a->in, IN_DOWN);
	int from = f->sel - f->win;

	if (!v->ngames) return;
	if ((up || dn) && move_list(a, &f->sel, v->ngames, up, dn)) {
		if (f->sel >= f->win && f->sel < f->win + DL_GAME_ROWS) glide(f, DS_PAGE_BODY_Y + from * DL_ROW, now);
	}
	if (a->in.pressed[IN_ACCEPT]) {
		const system_cfg *sc = NULL;
		int i;

		for (i = 0; i < a->sys.count; i++) if (!strcmp(a->sys.systems[i].folder, f->sys)) sc = &a->sys.systems[i];
		if (!sc) return;
		uisound_play(DS_SOUND_ACCEPT);
		game_panel(v, &v->games[f->sel], sc);
		dl_count_again(a, v->only);
	}
}

/* ---- a test of the page on a host -------------------------------------------------------
 * ZYPEROS_DOWNLOADS_TEST: the launcher opens the page at once and plays a
 * script, commas between its steps: a button (A B X Y START UP DOWN LEFT
 * RIGHT), wait=MS, count (until the count ends), run (until the run ends),
 * shot=PATH (the page as drawn now), fake=N (a stand-in run of N arrivals
 * on the art lane, g_fake), hold=K (until it holds after its Kth), pics
 * (until every arrival kept has its picture), list=NAME (what the page
 * shows came, a line each, with the page's pass since it opened: 0 is its
 * first, before it has read any picture). Once the page has closed (B on a
 * run, B on the hub), the steps go on behind it (downloads_test): away=K,
 * the stand-in going on and the lanes stepped as the shelf steps them until
 * K more have come or the run ends; game=MS, a game running, the lanes
 * waiting; video-gone, the renderer about to go as a game in the fallback
 * takes it; open, the page again, its first step on its first pass. The
 * waits are for what they name, each at most DL_TEST_WAIT_MS, never a
 * guess at how long a loaded machine takes.
 * look=FOLDER|NAME: a game's panel looking at the card (one_look), the
 * game as its system's list read it the first time its folder is looked at,
 * each kind's mark said before and after.
 * Each step said on stderr with the page's state; the launcher quits at the
 * script's end. The real loop and the real card: what the shots' stand-in
 * counts cannot show. */
static const char *g_dl_test;
static unsigned g_dl_until, g_dl_wait_end;
static bool g_dl_count_wait, g_dl_run_wait, g_dl_hold_wait, g_dl_pics_wait;
#define DL_TEST_WAIT_MS 30000

/* An arrival kept with no picture yet */
static bool test_pics_missing(void)
{
	int i;

	for (i = 0; i < g_rec.n; i++) if (!g_rec.it[i].tex) return true;
	return false;
}

/* fake=N: the stand-in run started as Download starts one (run_start) */
static void fake_start(dl_view *v, int n)
{
	int s, k;

	g_fake.ngames = 0;
	for (s = 0; s < v->a->sys.count && g_fake.ngames < DL_FAKE_MAX; s++) {
		const game_list *l = sys_games_c(&v->a->view[s]);

		for (k = 0; k < l->count && g_fake.ngames < DL_FAKE_MAX; k++) {
			snprintf(g_fake.g[g_fake.ngames].folder, sizeof g_fake.g[0].folder, "%s", v->a->sys.systems[s].folder);
			snprintf(g_fake.g[g_fake.ngames].name, sizeof g_fake.g[0].name, "%s", l->items[k].name);
			g_fake.ngames++;
		}
	}
	g_fake.n = n;
	if (!g_fake.ngames || n <= 0 || dl_live() || !dlq_start(DLQ_ART, &FAKE_LANE, fake_begin)) {
		fprintf(stderr, "downloads-test: the stand-in run did not start\n");
		return;
	}
	for (k = 0; k < n; k++)
		fprintf(stderr, "downloads-test: stand-in %d game=%s|folder=%s\n", k, g_fake.g[k % g_fake.ngames].name,
		        g_fake.g[k % g_fake.ngames].folder);
	rec_sync();
	v->rec_key = g_rec.key;
	memset(&v->ap, 0, sizeof v->ap);
	memset(&v->rp, 0, sizeof v->rp);
	memset(&v->cp, 0, sizeof v->cp);
	v->art = true;
	v->ra = v->cht = false;
	v->art_total = n;
	v->art_looks = 0;
	v->run_kind = -1;
	v->t0 = anim_now();
	v->done0 = 0;
	v->why[0] = '\0';
	open_run(v);
}

/* look=FOLDER|NAME (see dl_test) */
static void test_look(dl_view *v, const char *what)
{
	static const char *const ST[] = { "have", "miss", "none", "off" };
	static char loaded[CFG_STR];
	const char *bar = strchr(what, '|');
	const system_cfg *sc = NULL;
	char folder[CFG_STR];
	int i, k;

	if (!bar || (size_t)(bar - what) >= sizeof folder) return;
	snprintf(folder, sizeof folder, "%.*s", (int)(bar - what), what);
	for (i = 0; i < v->a->sys.count; i++)
		if (!strcmp(v->a->sys.systems[i].folder, folder)) sc = &v->a->sys.systems[i];
	if (sc && (strcmp(loaded, folder) || !v->games)) {
		sysgames_load(v, folder);
		snprintf(loaded, sizeof loaded, "%s", folder);
	}
	for (i = 0; sc && i < v->ngames; i++) {
		game_ctx x = { .v = v, .g = &v->games[i], .sc = sc };
		char was[96] = "", now[96] = "";

		if (strcmp(v->games[i].name, bar + 1)) continue;
		for (k = 0; k < DLK_KINDS; k++)
			snprintf(was + strlen(was), sizeof was - strlen(was), " %d=%s", k, ST[v->games[i].st[k] & 3]);
		one_look(&x);
		for (k = 0; k < DLK_KINDS; k++)
			snprintf(now + strlen(now), sizeof now - strlen(now), " %d=%s", k, ST[v->games[i].st[k] & 3]);
		fprintf(stderr, "downloads-test: look %s videos=%d was%s now%s\n", what, DLK_VIDEOS, was, now);
		return;
	}
	fprintf(stderr, "downloads-test: look %s not found\n", what);
}

/* list=NAME: what the page shows came, newest first, and each one's picture */
static void test_list(dl_view *v, const char *name)
{
	int i;

	fprintf(stderr, "downloads-test: came %s view=%d n=%d arrived=%d pass=%u\n", name, top_of(v)->v, g_rec.n, g_fake.i,
	        v->test_pass);
	for (i = 0; i < g_rec.n; i++) {
		const dl_arrival *it = &g_rec.it[i];

		fprintf(stderr, "downloads-test: came %s %d key=%u kind=%d tex=%s %dx%d card=%d game=%s|folder=%s\n", name, i,
		        it->key, it->kind, it->tex ? "yes" : "no", it->tex ? it->w : 0, it->tex ? it->h : 0, it->card, it->game,
		        it->folder);
	}
}

static void dl_test(dl_view *v, app *a)
{
	static const struct { const char *name; in_button b; } BTN[] = {
		{ "A", IN_ACCEPT }, { "B", IN_BACK }, { "X", IN_X }, { "Y", IN_Y }, { "START", IN_START },
		{ "UP", IN_UP }, { "DOWN", IN_DOWN }, { "LEFT", IN_LEFT }, { "RIGHT", IN_RIGHT },
	};
	char tok[512];
	unsigned now = plat_now_ms();
	size_t n, i;

	if (!g_dl_test) return;
	if (now < g_dl_until) return;
	if (g_dl_count_wait && dl_counting()) return;
	g_dl_count_wait = false;
	if (g_dl_run_wait && dl_live()) return;
	g_dl_run_wait = false;
	if (g_dl_hold_wait && g_fake.on && !g_fake.held && now < g_dl_wait_end) return;
	g_dl_hold_wait = false;
	if (g_dl_pics_wait && test_pics_missing() && now < g_dl_wait_end) return;
	g_dl_pics_wait = false;
	if (!*g_dl_test) { want_quit = 1; return; }
	n = strcspn(g_dl_test, ",");
	snprintf(tok, sizeof tok, "%.*s", (int)n, g_dl_test);
	g_dl_test += n + (g_dl_test[n] == ',');
	g_dl_until = now + 300;
	for (i = 0; i < sizeof BTN / sizeof BTN[0]; i++)
		if (!strcmp(tok, BTN[i].name)) a->in.pressed[BTN[i].b] = true;
	if (!strncmp(tok, "wait=", 5)) g_dl_until = now + (unsigned)atoi(tok + 5);
	if (!strcmp(tok, "count")) g_dl_count_wait = true;
	if (!strcmp(tok, "run")) g_dl_run_wait = true;
	if (!strncmp(tok, "hold=", 5)) {
		g_fake.hold_at = atoi(tok + 5);
		g_fake.held = g_fake.on && g_fake.i >= g_fake.hold_at;
		g_dl_hold_wait = true;
		g_dl_wait_end = now + DL_TEST_WAIT_MS;
	}
	if (!strcmp(tok, "pics")) {
		g_dl_pics_wait = true;
		g_dl_wait_end = now + DL_TEST_WAIT_MS;
	}
	if (!strncmp(tok, "fake=", 5)) fake_start(v, atoi(tok + 5));
	if (!strncmp(tok, "list=", 5)) test_list(v, tok + 5);
	if (!strncmp(tok, "look=", 5)) test_look(v, tok + 5);
	if (!strncmp(tok, "shot=", 5)) {
		SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, ZYPEROS_SCREEN_W, ZYPEROS_SCREEN_H, 32,
		                                                  SDL_PIXELFORMAT_RGBA32);

		dl_draw(v, anim_now());
		if (out) {
			SDL_RenderReadPixels(a->r, NULL, SDL_PIXELFORMAT_RGBA32, out->pixels, out->pitch);
			IMG_SavePNG(out, tok + 5);
			SDL_FreeSurface(out);
		}
	}
	fprintf(stderr, "downloads-test: %s view=%d depth=%d counted=%s systems=%d counting=%d live=%d\n", tok,
	        top_of(v)->v, v->depth, v->c ? "yes" : "no", v->c ? v->c->n : 0, dl_counting(), dl_live());
}

/* ---- the page ------------------------------------------------------------------------- */

static void dl_page(app *a, const char *only)
{
	dl_view *v = calloc(1, sizeof *v);
	int base;
	unsigned beat = 0;
	bool dirty = true;

	if (!v) return;
	v->a = a;
	v->pending = -2;
	snprintf(v->only, sizeof v->only, "%s", only ? only : "");
	/* The last count shows at once; a new one goes behind it */
	v->c = dl_count_shown(v->only);
	dl_count_again(a, v->only);
	v->gen = dl_count_gen();
	push(v, V_HUB);
	/* What came is as it was left, with what came meanwhile (g_rec) */
	rec_sync();
	rec_reopen();
	v->rec_key = v->open_key = g_rec.key;
	v->land_at = anim_now() - DL_LAND;
	/* A run behind the screens is shown where it is; one that ended while
	 * the page was closed, as done */
	if (dl_live()) {
		run_rejoin(v);
		open_run(v);
	} else if (dlq_ends(DLQ_ART) != g_shown_art || dlq_ends(DLQ_RA) != g_shown_ra ||
	           dlq_ends(DLQ_CHT) != g_shown_cht) {
		v->art = dlq_ends(DLQ_ART) != g_shown_art;
		v->ra = dlq_ends(DLQ_RA) != g_shown_ra;
		v->cht = dlq_ends(DLQ_CHT) != g_shown_cht;
		v->run_kind = -1;
		run_finish(v);
		push(v, V_DONE);
	}
	mt_start();
	nc_start();
	base = panel_base();
	g_base_page = dl_base;
	g_base_ctx = v;
	while (!want_quit && a->running) {
		unsigned now = anim_now();
		dl_frame *f = top_of(v);
		bool anim;

		plat_input_poll(&a->in);
		dl_test(v, a);
		v->test_pass++;
		mp4_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			power_off(a);              /* the run with everything else */
			break;
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);
		for (int b = 0; b < IN_COUNT; b++) if (a->in.pressed[b] || a->in.down[b]) dirty = true;
		if (a->in.pressed[IN_MENU]) {
			menu_leaving(a);           /* a run goes on behind the screens */
			break;
		}
		/* The count goes on a system at a time, a frame's share */
		if (dl_counting()) {
			dl_count_step(a, DL_COUNT_MS);
			dirty = true;
		}
		if (dl_count_gen() != v->gen) {
			v->gen = dl_count_gen();
			v->c = dl_count_shown(v->only);
			if (f->v == V_SYSTEMS && v->c && f->row >= v->c->n) f->row = v->c->n ? v->c->n - 1 : 0;
			if (v->pending != -2 && v->c) {
				int p = v->pending;

				v->pending = -2;
				if (f->v == V_DONE) v->depth--;
				if (run_start(v, p)) open_run(v);
				else uisound_play(DS_SOUND_DENY);
				f = top_of(v);
			}
			dirty = true;
		}
		if (f->v == V_RUN) {
			dlq_tick();
			if (take_arrivals(v)) dirty = true;
			if (take_pictures(v)) dirty = true;
			if (!dl_live()) {
				run_finish(v);
				close_run_view();
				f->v = V_DONE;
				dirty = true;
			} else if (a->in.pressed[IN_BACK]) {
				/* Continue in background: the page closes, the run goes on,
				 * the status corner's ring showing it */
				uisound_play(DS_SOUND_BACK);
				close_run_view();
				break;
			} else if (a->in.pressed[IN_Y]) {
				uisound_play(DS_SOUND_BACK);
				snprintf(v->why, sizeof v->why, "%s", T(S_DOWNLOADS_YOU_STOPPED));
				dlq_stop(DLQ_ART);
				dlq_stop(DLQ_RA);
				dlq_stop(DLQ_CHT);
				run_finish(v);
				close_run_view();
				f->v = V_DONE;
				dirty = true;
			}
		} else if (f->v == V_DONE) {
			/* the wall's pictures: opened on a run that ended behind it */
			if (take_pictures(v)) dirty = true;
			if (a->in.pressed[IN_BACK]) {
				uisound_play(DS_SOUND_BACK);
				if (v->depth <= 1) break;
				v->depth--;
			} else if (a->in.pressed[IN_ACCEPT]) {
				uisound_play(DS_SOUND_ACCEPT);
				if (v->end == END_SIGN_IN) {
					ra_signin_screen(a);
					dl_count_again(a, v->only);
					v->depth--;
				} else {
					/* Again: from what is missing, once the card is counted */
					v->pending = v->run_kind;
					if (!dl_counting()) dl_count_again(a, v->only);
				}
			}
		} else if (a->in.pressed[IN_BACK]) {
			uisound_play(DS_SOUND_BACK);
			if (f->v == V_GAMES) sysgames_free(v);
			if (v->depth <= 1) break;
			v->depth--;
		} else {
			switch (f->v) {
			case V_KIND:    kind_keys(v, f); break;
			case V_SYSTEMS: systems_keys(v, f); break;
			case V_GAMES:   sysgames_keys(v, f, now); break;
			default:        hub_keys(v, f, now); break;
			}
		}
		if (g_menu_closing) break;
		f = top_of(v);
		anim = (f->v == V_RUN && now - v->land_at < DL_LAND + 40) || f->fill_from >= 0 ||
		       (f->v == V_RUN && v->rp.state != RADL_WORKING);
		if (now - beat >= 1000) { beat = now; dirty = true; }
		if (!dirty && !anim) {
			plat_input_wait(f->v == V_RUN || dl_counting() ? 16 : 80);
			continue;
		}
		dirty = false;
		tick_tint(a);
		tick_theme(a);
		dl_draw(v, now);
		panel_leave_draw(a, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	close_run_view();
	nc_stop();
	mt_stop();
	mm_close(base, anim_now(), 0, g_menu_closing);
	/* What came stays for the next opening while its run goes on or its end
	 * is still to be seen; seen, it goes */
	if (!dlq_busy() && dlq_ends(DLQ_ART) == g_shown_art && dlq_ends(DLQ_RA) == g_shown_ra &&
	    dlq_ends(DLQ_CHT) == g_shown_cht)
		rec_free();
	icons_free(v);
	sysgames_free(v);
	g_base_page = NULL;
	g_base_ctx = NULL;
	free(v);
}

/* The test (dl_test): the page, and the steps behind it once it closes */
void downloads_test(app *a)
{
	char tok[512];

	g_dl_test = getenv("ZYPEROS_DOWNLOADS_TEST");
	dl_page(a, NULL);
	while (g_dl_test && *g_dl_test && a->running && !want_quit) {
		size_t n = strcspn(g_dl_test, ",");

		snprintf(tok, sizeof tok, "%.*s", (int)n, g_dl_test);
		g_dl_test += n + (g_dl_test[n] == ',');
		if (!strcmp(tok, "open")) {
			fprintf(stderr, "downloads-test: open live=%d\n", dl_live());
			g_dl_until = 0;
			dl_page(a, NULL);
			continue;
		}
		if (!strncmp(tok, "away=", 5)) {
			unsigned t0 = plat_now_ms();
			int until = g_fake.i + atoi(tok + 5);

			g_fake.held = false;
			g_fake.hold_at = 0;
			while (dl_live() && g_fake.i < until && plat_now_ms() - t0 < DL_TEST_WAIT_MS) {
				dlq_tick();
				SDL_Delay(4);
			}
		}
		if (!strncmp(tok, "game=", 5)) {
			unsigned t0 = plat_now_ms(), ms = (unsigned)atoi(tok + 5);

			dlq_game(true);
			while (plat_now_ms() - t0 < ms) {
				dlq_tick();
				SDL_Delay(4);
			}
			dlq_game(false);
		}
		if (!strcmp(tok, "video-gone")) downloads_video_gone();
		fprintf(stderr, "downloads-test: %s (behind the page) live=%d arrived=%d kept=%d\n", tok, dl_live(), g_fake.i,
		        g_rec.n);
	}
}

menu_result downloads_screen(app *a)
{
	dl_page(a, NULL);
	return MENU_CHANGED;
}

void downloads_page(app *a, const char *only)
{
	dl_page(a, only);
}

bool downloads_kind_in(const char *kind)
{
	if (!strcmp(kind, "covers")) return dl_kind_in(DLK_COVERS);
	if (!strcmp(kind, "videos")) return dl_kind_in(DLK_VIDEOS);
	if (!strcmp(kind, "ach")) return dl_kind_in(DLK_ACH);
	return dl_kind_in(DLK_NAMES);
}

/* MENU's row: how far a run behind the screens has got, else what the last
 * count left to fetch; NULL before any count */
const char *downloads_menu_value(const char *only, char *out, size_t n)
{
	const dl_counts *c = dl_count_shown(only);
	dl_plan pl;

	if (dl_live()) { snprintf(out, n, T(S_DOWNLOADS_RUNNING), run_pct()); return out; }
	if (!c) return NULL;
	if (!online()) { snprintf(out, n, "%s", T(S_MENU_NEEDS_WIFI)); return out; }
	dl_plan_of(c, -1, &pl);
	if (pl.n) snprintf(out, n, TN(S_DOWNLOADS_TO_FETCH_N, pl.n), pl.n);
	else snprintf(out, n, "%s", T(S_SCRAPER_COMPLETE));
	return out;
}

/* The news on the shelf when a run ended behind the screens: false when
 * there is none to say */
bool downloads_news(char *l1, size_t n1, char *l2, size_t n2)
{
	static bool was;
	art_progress ap;
	radl_progress rp;
	bool art, ra, cht, busy = dlq_busy();
	char parts[2][48];
	int np = 0;

	if (busy) { was = true; return false; }
	art = dlq_ends(DLQ_ART) != g_shown_art;
	ra = dlq_ends(DLQ_RA) != g_shown_ra;
	cht = dlq_ends(DLQ_CHT) != g_shown_cht;
	if (!was || (!art && !ra && !cht)) return false;
	was = false;
	if ((art && dlq_last(DLQ_ART) != DLQ_ENDED) || (ra && dlq_last(DLQ_RA) != DLQ_ENDED) ||
	    (cht && dlq_last(DLQ_CHT) != DLQ_ENDED))
		return false;
	scraper_last(&ap);
	radl_last(&rp);
	if (art && ap.found && np < 2) snprintf(parts[np++], sizeof parts[0], TN(S_DOWNLOADS_N_COVERS, ap.found), ap.found);
	if (ra && rp.fetched + rp.updated && np < 2)
		snprintf(parts[np++], sizeof parts[0], TN(S_DOWNLOADS_N_SETS, rp.fetched + rp.updated), rp.fetched + rp.updated);
	if (art && ap.snaps && np < 2) snprintf(parts[np++], sizeof parts[0], TN(S_DOWNLOADS_N_VIDEOS, ap.snaps), ap.snaps);
	if (art && ap.named && np < 2) snprintf(parts[np++], sizeof parts[0], TN(S_DOWNLOADS_N_NAMES, ap.named), ap.named);
	if (cht && g_cht_last.fetched + g_cht_last.updated && np < 2)
		snprintf(parts[np++], sizeof parts[0], TN(S_DOWNLOADS_N_CHEATS, g_cht_last.fetched + g_cht_last.updated),
		         g_cht_last.fetched + g_cht_last.updated);
	snprintf(l1, n1, "%s", T(S_DOWNLOADS_DONE_NEWS));
	if (np == 2) snprintf(l2, n2, "%s \xc2\xb7 %s", parts[0], parts[1]);
	else if (np == 1) snprintf(l2, n2, "%s", parts[0]);
	else snprintf(l2, n2, "%s", T(S_DOWNLOADS_OFFLINE_NOW));
	return true;
}

/* ---- --rows-report: the panels' rows ---------------------------------------------------- */

void downloads_rows_report(app *a)
{
	static set_ctx st;
	menu_row rows[SET_ROWS];
	const char *h;
	int n;

	memset(&st, 0, sizeof st);
	(void)a;
	n = set_build(&st, rows, SET_ROWS, &h);
	menu_rows_print("downloads-settings", rows, n, st.sound, st.choice);
}

/* ---- a shot (--downloads STATE) ----------------------------------------------------------
 * The page on a stand-in library, nothing read (the lab's: 640 games on 9
 * systems):
 *   0 the hub          1 a run going on (the hub)  2 every kind done
 *   3 no network (signed in and linked, as the lab) 4 Settings
 *   5 counting, nothing counted yet                 6 the hub on Covers
 *   7 Covers out (Left)                             8 Covers' page
 *   9 Videos' page    10 Names and Details' page   11 Systems
 *  12 a system's games 13 a game's panel           14 the run
 *  15 the run waiting for Wi-Fi                     16 the run paused
 *  17 done            18 stopped                   19 On the Card
 *  20 RetroAchievements' account                    21 a console's page
 *  22 Videos For      23 Cheats' page          24 RetroAchievements' account, signed out
 *  25 the hub on Names and Details              26 Covers' page, nothing missing
 *  27 no network, nobody signed in or linked */
int shot_downloads = -1;

static void fixture(dl_counts *c, int st)
{
	static const struct {
		const char *folder, *name, *tag;
		int games, cov, names, vneed, vhave, ach_todo, ach_ready, ach_none;
		bool vid;
	} SYS[9] = {
		{ "NES", "NES", "NES", 96, 3, 30, 29, 0, 20, 68, 8, false },
		{ "SNES", "SNES", "SFC", 88, 5, 18, 22, 70, 18, 64, 6, true },
		{ "Game Boy", "Game Boy", "GB", 64, 2, 25, 19, 0, 12, 46, 6, false },
		{ "Game Boy Color", "Game Boy Color", "GBC", 52, 4, 12, 16, 0, 10, 38, 4, false },
		{ "Game Boy Advance", "Game Boy Advance", "GBA", 110, 9, 40, 25, 85, 30, 72, 8, true },
		{ "Genesis", "Genesis", "MD", 72, 6, 20, 16, 55, 15, 52, 5, true },
		{ "PlayStation", "PlayStation", "PS1", 58, 7, 24, 17, 0, 20, 30, 8, false },
		{ "Nintendo 64", "Nintendo 64", "N64", 40, 1, 10, 12, 0, 10, 26, 4, false },
		{ "PC Engine", "PC Engine", "PCE", 60, 4, 20, 18, 0, 15, 34, 11, false },
	};
	int i, k, w;
	bool done = st == 2 || st == 26;

	memset(c, 0, sizeof *c);
	c->free = 11200000000ULL;
	c->mb_cover = 0.25;
	c->mb_video = 2.5;
	c->ss_in = st != 27;
	c->ra_in = st != 27;
	for (k = 0; k < DLK_KINDS; k++) c->fx_in[k] = true;
	if (st == 7) c->fx_in[DLK_COVERS] = false;
	for (i = 0; i < 9; i++) {
		dl_sys *s = &c->s[c->n++];

		snprintf(s->folder, sizeof s->folder, "%s", SYS[i].folder);
		snprintf(s->name, sizeof s->name, "%s", SYS[i].name);
		snprintf(s->tag, sizeof s->tag, "%s", SYS[i].tag);
		snprintf(s->key, sizeof s->key, "%s", home_art_key(SYS[i].tag));
		s->games = SYS[i].games;
		s->cov_miss = done ? 0 : SYS[i].cov;
		for (w = 0; w < SNAPS_FOR_COUNT; w++)
			s->vid_need[w] = done ? 0 : w == SNAPS_FOR_ALL ? SYS[i].games - SYS[i].vhave
			               : w == SNAPS_FOR_PLAYED ? SYS[i].vneed * 19 / 10 : SYS[i].vneed;
		s->vid_have = SYS[i].vhave;
		s->vid_bytes = (unsigned long long)(SYS[i].vhave * 2.5e6);
		s->names_todo = done ? 0 : SYS[i].names;
		s->names_have = SYS[i].games - SYS[i].names - SYS[i].cov;
		s->names_ok = true;
		s->ach_sys = true;
		s->ach_todo = done ? 0 : SYS[i].ach_todo;
		s->ach_ready = SYS[i].ach_ready + (done ? SYS[i].ach_todo : 0);
		s->ach_none = SYS[i].ach_none;
		s->ach_games = s->ach_todo + s->ach_ready + s->ach_none;
		s->cht_sys = true;
		s->cht_have = done ? SYS[i].games * 7 / 10 : 0;
		s->cht_none = done ? SYS[i].games - s->cht_have : 0;
		s->cht_todo = done ? 0 : SYS[i].games;
		s->fx_on[DLK_COVERS] = s->fx_on[DLK_NAMES] = s->fx_on[DLK_ACH] = s->fx_on[DLK_CHEATS] = true;
		s->fx_on[DLK_VIDEOS] = SYS[i].vid;
	}
	if (st == 21) {
		/* a console's page: SNES alone */
		c->s[0] = c->s[1];
		c->n = 1;
	}
}

static const char *const SHOT_SYS[9] = { "NES", "SNES", "Game Boy", "Game Boy Color", "Game Boy Advance", "Genesis",
                                          "PlayStation", "Nintendo 64", "PC Engine" };
static const char *const SHOT_GAMES[] = { "Kingdom Run", "Pocket Quest", "Night Drive", "Moon Base", "Sky Fort",
                                          "Turbo Tails", "Crystal Bay", "Iron Comet", "Lucky Lantern",
                                          "Pixel Knights", "Star Miner", "Frog Harbor", "Neon Alley", "Rocket Pals" };

void downloads_shot(app *a)
{
	static dl_counts c;
	static dl_view v;
	int st = shot_downloads, i;
	unsigned now = 100000;

	fixture(&c, st);
	dl_count_fixture(st == 5 ? NULL : &c);
	g_fx_net = st != 3 && st != 27;
	g_fx_art = g_fx_ra = g_fx_cht = st == 1 || (st >= 14 && st <= 16);
	g_fx_pct = st == 1 ? 41 : -1;
	g_fx_user = st == 27 || st == 24 ? NULL : "tarik";
	memset(&v, 0, sizeof v);
	v.a = a;
	v.fixture = true;
	v.c = st == 5 ? NULL : &c;
	v.pending = -2;
	push(&v, V_HUB);
	if (st == 6 || st == 7) top_of(&v)->sel = 1;
	if (st == 25) top_of(&v)->sel = 1 + DLK_NAMES;
	if (st == 21) snprintf(v.only, sizeof v.only, "SNES");
	if (st == 8 || st == 9 || st == 10 || st == 23 || st == 26) {
		push(&v, V_KIND);
		top_of(&v)->kind = st == 9 ? DLK_VIDEOS : st == 10 ? DLK_NAMES : st == 23 ? DLK_CHEATS : DLK_COVERS;
	}
	if (st == 11) {
		push(&v, V_SYSTEMS);
		top_of(&v)->col = DLK_NAMES;
	}
	if (st == 12 || st == 13) {
		push(&v, V_SYSTEMS);
		push(&v, V_GAMES);
		snprintf(top_of(&v)->sys, sizeof top_of(&v)->sys, "NES");
		v.games = calloc(14, sizeof *v.games);
		for (i = 0; v.games && i < 14; i++) {
			dl_game *g = &v.games[v.ngames++];

			snprintf(g->title, sizeof g->title, "%s", SHOT_GAMES[i]);
			g->st[DLK_COVERS] = i < 3 ? GS_MISS : GS_HAVE;
			g->st[DLK_VIDEOS] = GS_OFF;
			g->st[DLK_NAMES] = i % 3 ? GS_HAVE : GS_MISS;
			g->st[DLK_ACH] = i % 7 == 6 ? GS_NONE : i < 4 ? GS_MISS : GS_HAVE;
			g->st[DLK_CHEATS] = i % 5 == 4 ? GS_NONE : i < 3 ? GS_MISS : GS_HAVE;
		}
	}
	if (st == 1 || (st >= 14 && st <= 18)) {
		v.art = v.ra = v.cht = true;
		v.cp.games = 640;
		v.cp.games_done = 300;
		v.cp.fetched = 174;
		v.art_total = 41;
		v.art_looks = 262;
		v.ap.looked = 33;
		v.ap.found = 16;
		v.ap.named = 12;
		snprintf(v.ap.now, sizeof v.ap.now, "Retro Racer X");
		snprintf(v.ap.folder_now, sizeof v.ap.folder_now, "Game Boy Advance");
		v.ap.src = ART_SRC_SS;
		v.rp.games = 150;
		v.rp.games_done = 22;
		v.rp.fetched = 22;
		v.rp.state = st == 15 ? RADL_NO_NETWORK : st == 16 ? RADL_SLOWED : RADL_WORKING;
		v.rp.resume_at = plat_now_ms() + 42000;
		v.run_kind = -1;
		v.t0 = now;
		/* Eleven: what a 16:9 run's grid shows; a 4:3 one shows seven */
		for (i = 10; i >= 0; i--)
			rec_add(i == 0 ? DLK_CHEATS : i % 3 == 0 ? DLK_ACH : i % 3 == 1 ? DLK_COVERS : DLK_NAMES, NULL, SHOT_GAMES[i],
			        SHOT_SYS[i % 9], i == 0 ? 129 : i % 3 ? 0 : 40 + i * 13);
		pictures_ask(&v);
		v.land_at = now - DL_LAND;
	}
	if (st >= 14 && st <= 16) push(&v, V_RUN);
	if (st == 17 || st == 18) {
		for (i = 13; i >= 7; i--)
			rec_add(i % 4 == 0 ? DLK_VIDEOS : i % 3 == 0 ? DLK_ACH : i % 5 == 0 ? DLK_CHEATS : DLK_COVERS, NULL,
			        SHOT_GAMES[i], SHOT_SYS[i % 9], 0);
		pictures_ask(&v);
		v.end = st == 17 ? END_DONE : END_STOPPED;
		if (st == 18) snprintf(v.why, sizeof v.why, "%s", T(S_DOWNLOADS_YOU_STOPPED));
		v.ap.found = 41;
		v.ap.snaps = 63;
		v.ap.named = 199;
		v.rp.fetched = 137;
		v.cp.fetched = 428;
		push(&v, V_DONE);
	}
	g_base_page = dl_base;
	g_base_ctx = &v;
	if (st == 4 || st == 19 || st == 20 || st == 22 || st == 13 || st == 24) {
		menu_row rows[16];
		const char *h;
		int n = 0, sel = 0;

		dl_draw(&v, now);
		if (st == 4) {
			static set_ctx s;

			memset(&s, 0, sizeof s);
			s.v = &v;
			n = set_build(&s, rows, SET_ROWS, &h);
		} else if (st == 19) {
			static room_ctx r;

			memset(&r, 0, sizeof r);
			r.c = v.c;
			n = room_build(&r, rows, 16, &h);
		} else if (st == 20 || st == 24) {
			static ra_ctx r;

			memset(&r, 0, sizeof r);
			n = ra_build(&r, rows, 16, &h);
			sel = n > 1 ? 1 : 0;
		} else if (st == 22) {
			static vfor_ctx r;

			memset(&r, 0, sizeof r);
			r.c = v.c;
			n = vfor_build(&r, rows, 16, &h);
			sel = 1;
		} else {
			static game_ctx g;
			static system_cfg sc;

			memset(&g, 0, sizeof g);
			snprintf(sc.folder, sizeof sc.folder, "NES");
			g.v = &v;
			g.g = &v.games[3];
			g.sc = &sc;
			n = game_build(&g, rows, 16, &h);
		}
		ui_scrim(a->r, 0);
		menu_draw(a, h, rows, n, sel, st == 19 ? 0 : menu_std_width(a), MENU_ZYPEROS);
	} else {
		dl_draw(&v, now);
	}
	g_base_page = NULL;
	g_base_ctx = NULL;
	dl_count_fixture(NULL);
	g_fx_net = g_fx_art = g_fx_ra = g_fx_cht = g_fx_pct = -1;
	g_fx_user = NULL;
	rec_free();
	icons_free(&v);
	sysgames_free(&v);
}
