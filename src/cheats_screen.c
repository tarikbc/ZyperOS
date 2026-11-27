/* SPDX-License-Identifier: GPL-3.0-only */
/* See cheats_screen.h. */
#include "cheats_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cheats.h"
#include "cheevos.h"
#include "db.h"
#include "game_menu.h"
#include "gamedel_screen.h"
#include "i18n.h"
#include "ingame_screen.h"
#include "keyhints.h"
#include "menu_panel.h"
#include "menu_pick.h"
#include "menu_runner.h"
#include "panel.h"
#include "platform.h"
#include "rafetch.h"
#include "storage_screen.h"
#include "ui.h"
#include "uisound.h"

/* The running game: who it is, how many of its cheats are on, and whether
 * one has been on since it started. Set at its launch, so the in-game
 * menu's row is a read of these and never of the card. */
static struct {
	bool on;
	char folder[CFG_STR], tag[8], file[LIB_PATH];
	int  count;
	bool paused;
	/* Started Without Cheats (the crash question): none on in the game,
	 * and what is kept for it stays until a switch is turned here */
	bool without;
} g_cg;

/* The list while its page is open. Static: a thousand cheats is bigger
 * than a stack frame should be, and one page is open at a time. */
static cheat_list    g_L;
static char          g_state[CHEATS_MAX * 9 + 1];
static SDL_Texture  *g_bg;
static bool          g_on_first;
static char          g_query[64];

/* What each row of the page is: a cheat by its place in g_L, or one of
 * these; and each row's value color and sound */
#define ROW_NONE    (-1)
#define ROW_ALL_OFF (-2)
static int           g_map[MENU_RUN_ROWS];
static unsigned      g_vcol[MENU_RUN_ROWS];
static unsigned char g_sound[MENU_RUN_ROWS];
static char          g_heading[200];
/* The sentences of a game with none, wrapped to the panel a line a row */
#define EMPTY_LINES 12
static char          g_lines[EMPTY_LINES][CHV_WRAP_COLS];
static app          *g_app;
/* The list's quiet top line, that achievements wait while a cheat is on:
 * signed in, for a game with a set, before a cheat is on and while one is,
 * as the mock and the lab show it beside "2 On". Asked when the page opens
 * and at each switch, not every frame */
static bool          g_note;

static void note_now(void)
{
	g_note = ra_signed_in() && chv_count() > 0;
}

/* The top line, under the heading's rule in the quiet line's face, as the
 * Achievements tab's counts: a band of its own, not a row of the list, so
 * it costs the list no row (the mock's small line over the cheats) */
#define NOTE_HEADER_H (3 * DS_GRID + 32)

static void note_header(app *a, const SDL_Rect *q)
{
	TTF_Font *fm = ui_font(UI_F_META);
	const char *s = T(S_CHEATS_ACH_NOTE);
	int cy = q->y + q->h / 2 - DS_GRID;

	if (ui_text_width(fm, s) <= q->w)
		ui_text(a->r, fm, s, q->x, ui_cap_y(fm, cy), -1, UI_TEXT_SOFT);
	else
		ui_text_marquee(a->r, fm, s, q->x, ui_cap_y(fm, cy), q->w, SDL_GetTicks(), UI_TEXT_SOFT);
}

static menu_style g_st;

/* The header band while the top line is said, and none after */
static void note_band(void)
{
	g_st.header = g_note ? note_header : NULL;
	g_st.header_h = g_note ? NOTE_HEADER_H : 0;
}

/* ---- the running game -------------------------------------------------- */

static void keep_game(const system_cfg *s, const char *file)
{
	memset(&g_cg, 0, sizeof g_cg);
	g_cg.on = true;
	snprintf(g_cg.folder, sizeof g_cg.folder, "%s", s->folder);
	snprintf(g_cg.tag, sizeof g_cg.tag, "%s", s->tag);
	snprintf(g_cg.file, sizeof g_cg.file, "%s", file);
}

/* The game's list, with what is kept for it on (none in a game started
 * Without Cheats, until a switch is turned). A cheat its file gives no name
 * is named by its place. */
static bool load_list(void)
{
	char key[CFG_STR + LIB_PATH + 16];
	int i;

	cheats_free(&g_L);
	if (!g_cg.on) return false;
	cheats_load(&g_L, P_CARD, g_cg.folder, g_cg.tag, g_cg.file);
	for (i = 0; i < g_L.n; i++)
		if (!g_L.c[i].desc[0]) {
			char name[48];

			snprintf(name, sizeof name, T(S_CHEATS_UNTITLED), i + 1);
			free(g_L.c[i].desc);
			g_L.c[i].desc = strdup(name);
			if (!g_L.c[i].desc) g_L.c[i].desc = strdup("");
		}
	if (g_cg.without) return g_L.n > 0;
	cheats_key(g_cg.tag, g_cg.file, key, sizeof key);
	db_get_str(db_lib(), key, g_state, sizeof g_state, "");
	cheats_state_apply(&g_L, g_state);
	return g_L.n > 0;
}

int cheats_game_begin(const system_cfg *s, const char *file, bool without,
                      char *keys, size_t kn)
{
	int n;

	keep_game(s, file);
	g_cg.without = without;
	/* A game with no file under Cheats/ is a few stats and nothing more */
	if (!load_list() || without) {
		cheats_free(&g_L);
		return 0;
	}
	n = cheats_on_count(&g_L);
	if (n > 0 && cheats_write_codes(&g_L, CHEATS_RUN_FILE) == n &&
	    strlen(keys) + strlen(CHEATS_RUN_FILE) + 9 < kn) {
		snprintf(keys + strlen(keys), kn - strlen(keys), "%scheats=%s",
		         keys[0] ? "\t" : "", CHEATS_RUN_FILE);
		g_cg.count = n;
		/* Diatom stops the set from the first frame (its cheats.h) */
		g_cg.paused = true;
	}
	cheats_free(&g_L);
	return g_cg.count;
}

void cheats_game_started(void)
{
	char body[64];

	if (g_cg.count <= 0) return;
	snprintf(body, sizeof body, TN(S_CHEATS_POPUP, g_cg.count), g_cg.count);
	popup_say(T(S_CHEATS_ROW), body);
}

static void crash_key(const char *tag, const char *file, char *out, size_t n)
{
	snprintf(out, n, "cheats-crash.%s.%s", tag, file);
}

void cheats_game_end(bool crashed)
{
	char key[CFG_STR + LIB_PATH + 24];

	if (!g_cg.on) return;
	crash_key(g_cg.tag, g_cg.file, key, sizeof key);
	if (crashed && g_cg.count > 0) db_set_int(db_lib(), key, 1);
	else if (db_has(db_lib(), key)) db_del(db_lib(), key);
	g_cg.on = false;
	g_cg.count = 0;
	g_cg.paused = false;
}

bool cheats_crash_asks(const system_cfg *s, const char *file)
{
	char key[CFG_STR + LIB_PATH + 24], st[16];

	crash_key(s->tag, file, key, sizeof key);
	if (!db_has(db_lib(), key)) return false;
	/* Only while some are still kept on: turned off since, nothing to ask */
	cheats_key(s->tag, file, key, sizeof key);
	return db_get_str(db_lib(), key, st, sizeof st, "") && st[0];
}

bool g_shot_cheats_crashed;

int cheats_shelf_line(const system_cfg *s, const char *file, char *out, size_t n, bool *crashed)
{
	static cheat_list L;
	static char st[CHEATS_MAX * 9 + 1];
	char key[CFG_STR + LIB_PATH + 24];
	int c;

	if (n) out[0] = '\0';
	if (crashed) *crashed = false;
	if (!s || !file || !n) return 0;
	cheats_key(s->tag, file, key, sizeof key);
	if (!db_get_str(db_lib(), key, st, sizeof st, "") || !st[0]) return 0;
	/* Counted from the list, not the kept ids: an update may have taken a
	 * cheat that was on out of its file */
	cheats_load(&L, P_CARD, s->folder, s->tag, file);
	cheats_state_apply(&L, st);
	c = cheats_on_count(&L);
	cheats_free(&L);
	if (c <= 0) return 0;
	snprintf(out, n, TN(S_CHEATS_POPUP, c), c);
	crash_key(s->tag, file, key, sizeof key);
	if (crashed) *crashed = db_has(db_lib(), key) || g_shot_cheats_crashed;
	return c;
}

int  cheats_game_on(void)     { return g_cg.on ? g_cg.count : 0; }
bool cheats_game_paused(void) { return g_cg.on && g_cg.paused; }

void cheats_shot_game(const system_cfg *s, const char *file, bool paused)
{
	keep_game(s, file);
	if (load_list()) g_cg.count = cheats_on_count(&g_L);
	g_cg.paused = paused || g_cg.count > 0;
	cheats_free(&g_L);
}

/* ---- a switch ----------------------------------------------------------- */

/* What the list says now, kept for the game and handed to the game: the
 * codes that are on in a file for SETCHEATS, or none */
static void keep_and_send(void)
{
	char key[CFG_STR + LIB_PATH + 16];
	int n = cheats_on_count(&g_L);

	cheats_state_str(&g_L, g_state, sizeof g_state);
	cheats_key(g_cg.tag, g_cg.file, key, sizeof key);
	if (g_state[0]) db_set_str(db_lib(), key, g_state);
	else if (db_has(db_lib(), key)) db_del(db_lib(), key);
	g_cg.without = false;

	if (n > 0 && cheats_write_codes(&g_L, CHEATS_RUN_FILE) == n)
		plat_resident_line("SETCHEATS\tpath=%s", CHEATS_RUN_FILE);
	else
		plat_resident_line("SETCHEATS\tpath=");
	g_cg.count = n;
	if (n > 0) g_cg.paused = true;
	note_now();
	note_band();
}

/* The first cheat of a game asks first while its achievements would stop
 * for it: signed in, a set for the game, none on yet since it started, and
 * not asked for this game before (DESIGN.md 11.66) */
static bool first_asks(void)
{
	char key[CFG_STR + LIB_PATH + 24];

	if (g_cg.paused || !ra_signed_in() || chv_count() <= 0) return false;
	cheats_asked_key(g_cg.tag, g_cg.file, key, sizeof key);
	return !db_has(db_lib(), key);
}

static bool ask_first(app *a)
{
	char key[CFG_STR + LIB_PATH + 24];

	if (!first_asks()) return true;
	char q[512];

	cq_text(CQ_CHEATS_FIRST, q, sizeof q);
	if (!confirm_ask_over(a, CQ_CHEATS_FIRST, q, &g_st, NULL)) return false;
	cheats_asked_key(g_cg.tag, g_cg.file, key, sizeof key);
	db_set_int(db_lib(), key, 1);
	return true;
}

/* ---- the page ----------------------------------------------------------- */

/* A seam's name: libretro's file's code type, or libretro. The player's own
 * files are one group, always first, so never under a seam. */
static const char *group_label(const cheat_group *g)
{
	return g->label[0] ? g->label : T(S_CHEATS_LIBRETRO);
}

static void heading_now(void)
{
	int on = cheats_on_count(&g_L);
	char part[96];

	if (g_query[0]) {
		snprintf(part, sizeof part, T(S_CHEATS_FOUND), g_query);
		snprintf(g_heading, sizeof g_heading, "%s · %s", T(S_CHEATS_ROW), part);
		/* joined in the heading, so each said to the host's check apart */
		ui_fit_drawn(part);
	} else if (on > 0) {
		snprintf(part, sizeof part, TN(S_CHEATS_ROW_ON, on), on);
		snprintf(g_heading, sizeof g_heading, "%s · %s", T(S_CHEATS_ROW), part);
	} else {
		snprintf(g_heading, sizeof g_heading, "%s", T(S_CHEATS_ROW));
	}
}

/* `text` wrapped to the panel into g_lines from `at`, a body row a line */
static int wrapped(const char *text, int at, menu_row *rows, int n)
{
	int w = g_app ? gm_width(g_app) : menu_most_w();
	int got = wrap_text(ui_font(UI_F_MENU), text, w, g_lines + at, EMPTY_LINES - at), i;

	ui_fit_drawn(text);    /* drawn in lines, so said whole to the host's check */
	for (i = 0; i < got; i++) rows[n++] = MENU_BODY(g_lines[at + i]);
	return n;
}

/* A game with no cheats: why, and how a file goes on by hand, in two
 * paragraphs wrapped to the panel */
static int empty_rows(menu_row *rows)
{
	char name[LIB_PATH], *dot, why[400], hand[LIB_PATH + 400], a[200], b[LIB_PATH + 200];
	const char *slash = strrchr(g_cg.file, '/');
	int n = 0;

	snprintf(name, sizeof name, "%s", slash ? slash + 1 : g_cg.file);
	dot = strrchr(name, '.');
	if (dot && dot != name) *dot = '\0';
	if (g_L.where == CHEATS_NONE_LIBRETRO) {
		snprintf(why, sizeof why, "%s", T(S_CHEATS_NONE_LIBRETRO));
	} else {
		snprintf(a, sizeof a, T(S_CHEATS_GET_DOWNLOADS), T(S_DOWNLOADS_HEADING));
		snprintf(why, sizeof why, "%s %s", T(S_CHEATS_NOT_CHECKED), a);
	}
	snprintf(a, sizeof a, T(S_CHEATS_BY_HAND), g_cg.folder);
	snprintf(b, sizeof b, T(S_CHEATS_BY_HAND_NAME), name);
	snprintf(hand, sizeof hand, "%s %s", a, b);
	/* Each sentence said to the host's check apart, as well as the
	 * paragraph it is drawn in: joined, no one string is the text */
	if (g_L.where != CHEATS_NONE_LIBRETRO) {
		char d[200];

		snprintf(d, sizeof d, T(S_CHEATS_GET_DOWNLOADS), T(S_DOWNLOADS_HEADING));
		ui_fit_drawn(T(S_CHEATS_NOT_CHECKED));
		ui_fit_drawn(d);
	}
	ui_fit_drawn(a);
	ui_fit_drawn(b);
	n = wrapped(why, 0, rows, n);
	rows[n++] = MENU_HR;
	n = wrapped(hand, n - 1, rows, n);
	return n;
}

static bool matches(const cheat *c)
{
	return !g_query[0] || strcasestr(c->desc, g_query) != NULL;
}

static int build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static int order[CHEATS_MAX];
	key_hint h[2];
	int n = 0, k, no, last = -1, shown = 0;

	(void)ctx;
	heading_now();
	*heading = g_heading;
	if (max > MENU_RUN_ROWS) max = MENU_RUN_ROWS;
	for (k = 0; k < max; k++) { g_map[k] = ROW_NONE; g_vcol[k] = 0; g_sound[k] = 0; }

	if (g_L.n == 0) return empty_rows(rows);

	/* A quiet line first while the first cheat would stop achievements */

	no = cheats_order(&g_L, g_on_first, order);
	for (k = 0; k < no && n < max - 6; k++) {
		const cheat *c = &g_L.c[order[k]];

		if (!matches(c)) continue;
		/* Each file's group after the first under a seam with its name
		 * (the mock's "Game Genie"), while the list is in its own order:
		 * the first needs none, the list's top says whose it is */
		if (!g_on_first && !g_query[0] && last >= 0 && c->group != last)
			rows[n++] = MENU_NOTE(group_label(&g_L.g[c->group]));
		last = c->group;
		rows[n] = (menu_row){ c->desc, T(c->on ? S_MENU_ON_TITLE : S_MENU_OFF_TITLE), true };
		g_map[n] = order[k];
		g_vcol[n] = c->on ? VCOL_EARNED : 0u;
		g_sound[n] = MENU_SND_RING;
		n++;
		shown++;
	}
	if (!shown) rows[n++] = MENU_NOTE(T(S_CHEATS_NO_MATCH));

	/* Turn All Off, last: live while one is on */
	rows[n++] = MENU_HR;
	rows[n] = (menu_row){ T(S_CHEATS_ALL_OFF), NULL, cheats_on_count(&g_L) > 0 };
	g_map[n++] = ROW_ALL_OFF;

	/* Y searches, X puts the ones that are on first and back */
	h[0] = (key_hint){ "Y", T(S_CHEATS_SEARCH) };
	h[1] = (key_hint){ "X", T(g_on_first ? S_CHEATS_IN_ORDER : S_CHEATS_ON_FIRST) };
	rows[n++] = MENU_RULE;
	rows[n++] = MENU_NOTE(keyhints_legend(h, 2));
	return n;
}

static void search(app *a)
{
	char q[sizeof g_query];
	kb_result r;

	snprintf(q, sizeof q, "%s", g_query);
	r = keyboard_prompt(a, T(S_CHEATS_SEARCH), q, (int)sizeof q);
	if (r == KB_POWER) {
		plat_note_power_pressed();
		plat_resident_line("STOP");
		return;
	}
	if (r == KB_ACCEPT) snprintf(g_query, sizeof g_query, "%s", q);
	menu_cursor_to(0);
}

static menu_result on_key(app *a, void *ctx, in_button key, int sel)
{
	int i;

	(void)ctx;
	if (g_L.n == 0) return MENU_STAY;
	if (key == IN_X) {
		uisound_play(DS_SOUND_ACCEPT);
		g_on_first = !g_on_first;
		menu_cursor_to(0);
		return MENU_STAY;
	}
	if (key == IN_Y) {
		uisound_play(DS_SOUND_ACCEPT);
		search(a);
		return plat_run_power_pressed() ? MENU_DONE : MENU_STAY;
	}
	if (sel < 0 || sel >= MENU_RUN_ROWS) return MENU_STAY;
	i = g_map[sel];
	if (i == ROW_ALL_OFF && key == IN_ACCEPT) {
		for (i = 0; i < g_L.n; i++) g_L.c[i].on = false;
		keep_and_send();
		return MENU_STAY;
	}
	if (i >= 0 && i < g_L.n && (key == IN_ACCEPT || key == IN_LEFT || key == IN_RIGHT)) {
		bool to = !g_L.c[i].on;

		if (to && !ask_first(a))
			return plat_run_power_pressed() ? MENU_DONE : MENU_STAY;
		g_L.c[i].on = to;
		keep_and_send();
	}
	return plat_run_power_pressed() ? MENU_DONE : MENU_STAY;
}

static void backdrop(app *a, void *ctx, float k)
{
	(void)ctx;
	paused_backdrop(a, g_bg, k);
}

static menu_result on_power(app *a, void *ctx)
{
	(void)a; (void)ctx;
	plat_note_power_pressed();
	plat_resident_line("STOP");
	return MENU_DONE;
}

static void style(app *a)
{
	memset(&g_st, 0, sizeof g_st);
	g_st.fixed_w = gm_width(a);
	g_st.follow_tint = true;
	g_st.backdrop = backdrop;
	g_st.on_power = on_power;
	g_st.sound = g_sound;
	g_st.vcolors = g_vcol;
	g_st.pin_foot = true;
	note_band();
}

void cheats_page(app *a, SDL_Texture *bg)
{
	g_app = a;
	g_bg = bg;
	g_query[0] = '\0';
	load_list();
	note_now();
	style(a);
	menu_run(a, &g_st, build, on_key, NULL);
	cheats_free(&g_L);
	g_bg = NULL;
}

void cheats_rows_report(app *a)
{
	static menu_row rows[MENU_RUN_ROWS];
	const char *h;
	int n;

	g_app = a;
	load_list();
	note_now();
	n = build(NULL, rows, MENU_RUN_ROWS, &h);
	menu_rows_print("cheats", rows, n, g_sound, NULL);
	cheats_free(&g_L);
}

void cheats_page_shot(app *a, SDL_Texture *bg, const cheats_shot *o)
{
	static menu_row rows[MENU_RUN_ROWS];
	const char *h;
	int n, sel = o->sel;
	bool ask = o->ask;

	g_app = a;
	g_bg = bg;
	load_list();
	note_now();
	g_note = g_note || o->note;
	g_on_first = o->on_first;
	snprintf(g_query, sizeof g_query, "%s", o->query ? o->query : "");
	style(a);
	n = build(NULL, rows, MENU_RUN_ROWS, &h);
	backdrop(a, NULL, 1.0f);
	if (sel < 0 || sel >= n || !rows[sel].live) {
		int s = menu_step_sel(rows, n, sel < 0 ? 0 : sel >= n ? n - 1 : sel, +1);

		sel = rows[s].live ? s : menu_step_sel(rows, n, s, -1);
	}
	menu_draw_styled(a, &g_st, h, rows, n, sel);
	if (ask) {
		/* The first cheat's question over the list, as confirm_ask draws it:
		 * the cursor on Cancel */
		char q[512];

		cq_text(CQ_CHEATS_FIRST, q, sizeof q);
		backdrop(a, NULL, 1.0f);
		confirm_preview(a, CQ_CHEATS_FIRST, q);
	}
	cheats_free(&g_L);
	g_bg = NULL;
	g_on_first = false;
	g_query[0] = '\0';
}

void cheats_crash_shot(app *a)
{
	char q[480];

	cq_text(CQ_CHEATS_CRASH, q, sizeof q);
	/* over the shelf's scrim, the cursor on the left answer, Without
	 * Cheats, as confirm_ask starts */
	ui_scrim(a->r, 0);
	confirm_preview(a, CQ_CHEATS_CRASH, q);
}
