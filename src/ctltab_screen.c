/* SPDX-License-Identifier: GPL-3.0-only */
/* See ctltab_screen.h. */
#include "ctltab_screen.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "clamp.h"
#include "controls.h"
#include "design.h"
#include "game_menu.h"
#include "home.h"
#include "i18n.h"
#include "platform.h"
#include "remapstore.h"
#include "tourdevice.h"
#include "tween.h"
#include "ui.h"

/* ---- the handheld's place and motion (the mock, 2026-11-13) ----------------
 *
 * Centered 99 units in from the content's left edge, so its left edge is on
 * the heading rule's left end, at y 458 of the 1024 x 768 screen, 3.15
 * units a millimetre, turned 10 degrees (the tutorial's front pose): its
 * halos and rings end at x 302 of the 335 where the focus fill starts. The
 * panel is as tall as the screen allows on every tab and page that has it
 * (menu_style's side), so it stands at the same place on all of them. */
#define DEV_DX    99.0f
#define DEV_CY    458.0f
#define DEV_SCALE 3.15f
#define DEV_DEG   10.0f
/* Turned this far as the tab comes in, to DEV_DEG in **turn** */
#define DEV_DEG_IN 40.0f
/* How far a tab's move takes it: in with the rows, 64 units the press's way;
 * out with the old tab's name, 96 the press's way (menu_tabs_draw's) */
#define DEV_IN_DX  64.0f
#define DEV_OUT_DX 96.0f
/* The kept texture's box about the handheld's middle: what a lit shoulder's
 * halo, the d-pad's ring and the faces' halos reach in every pose here */
#define KEEP_L 128
#define KEEP_T 164
#define KEEP_W 256
#define KEEP_H 300

/* Which screen sets the handheld's lights: the tab, or a button's choices */
typedef enum { CS_TAB, CS_PICK } cs_mode;

static struct {
	/* The card as the tab shows it, read again when it changed */
	bool        stale;
	char        key[CFG_STR + LIB_PATH + 8];   /* tag, file and scope it was read for */
	ct_model    m;
	char        ext[16], label[64];
	rm_choice   ch[32];
	int         nch;
	/* The rows last built, and whether since the tab was turned to; the
	 * lines of a quiet row too long for the column */
	ct_bufs     b;
	ct_row      ids[CT_MAX_ROWS];
	int         n;
	bool        fresh;
	char        lines[16][96];
	/* The row the cursor came onto (-1 none yet) and the table then: steps
	 * work from it (rm_set), and what differs from it lights */
	int         base_row;
	rm_table    base;
	/* Each key's light, and its press: from where it was, and when */
	tween       on[TD_KEYS];
	bool        snap;
	float       press_from[TD_KEYS];
	unsigned    press_t0[TD_KEYS];
	cs_mode     mode;
	int         pick_button;
	/* A button's choices open or close over the tab: the handheld holds
	 * still over the panels' motion (MENU_SIDE_OVER) until a panel with it
	 * is at rest */
	bool        steady;
	td_keep     keep;
} g_cs = { .stale = true, .base_row = -1, .snap = true };

/* ---- the card ---------------------------------------------------------------- */

/* The rows' column: the in-game menu's content, less the handheld's */
static int column_w(const ctlscr_game *g)
{
	return (g->over && g->over->fixed_w > 0 ? g->over->fixed_w : 860) - CTLSCR_SIDE_W;
}

/* Applies To's value for every game of the system: its name, or its home
 * label where the name does not fit beside Applies To in the rows' column
 * (measured with a gap wider than the panel's own, so it is never tight) */
static void label_of(const ctlscr_game *g, char *out, size_t n)
{
	TTF_Font *f = ui_font(UI_F_MENU);
	const int width = column_w(g);

	if (ui_text_width(f, T(S_GAMEMENU_APPLIES_TO)) + ui_font_line(UI_F_MENU) +
	    ui_text_width(f, g->s->name) <= width)
		snprintf(out, n, "%s", g->s->name);
	else
		snprintf(out, n, "%s", home_label(g->s->tag, g->s->name));
}

static bool scope_all(const ctlscr_game *g)
{
	return g->scope_all && *g->scope_all && !g->fixed_game;
}

/* The model for `g`, read from the card when it changed */
static void refresh(const ctlscr_game *g)
{
	ct_model *m = &g_cs.m;
	const char *file = g->file ? g->file : "";
	const char *dot = strrchr(file, '.');
	char key[sizeof g_cs.key];
	rm_table own;

	snprintf(key, sizeof key, "%s\t%s\t%d\t%d", g->s->tag, file, scope_all(g), g->fixed_game);
	if (!g_cs.stale && !strcmp(key, g_cs.key)) return;
	if (strcmp(key, g_cs.key)) g_cs.base_row = -1;
	snprintf(g_cs.key, sizeof g_cs.key, "%s", key);
	memset(m, 0, sizeof *m);
	snprintf(g_cs.ext, sizeof g_cs.ext, "%s", dot ? dot + 1 : "");
	label_of(g, g_cs.label, sizeof g_cs.label);
	m->tag = g->s->tag;
	m->ext = g_cs.ext;
	m->sys_label = g_cs.label;
	m->sys_name = g->s->name;
	m->map = g->s->map;
	m->scope_all = scope_all(g);
	m->fixed_game = g->fixed_game;
	/* A key of the game's that the system's table has come to equal is no
	 * buttons of its own any more: it goes, as a change that left it so
	 * would have taken it (rms_put) */
	if (file[0] && rms_game_tidy(g->s, file))
		fprintf(stderr, "controls: map.%s.%s equals the system's, deleted\n", g->s->tag, file);
	m->game_has_own = rms_game(g->s, file, &own);
	/* This Game's default is the system's table; All <System>'s ZyperOS's */
	if (m->scope_all) {
		rms_system(g->s, &m->cur);
		rms_default(g->s, &m->def);
	} else {
		m->cur = own;
		rms_system(g->s, &m->def);
	}
	rms_default(g->s, &m->zdef);
	m->held = ct_held_of(g->s->tag, g->s->stick);
	m->stick_offered = rms_stick_offered(g->s);
	m->stick_on = rms_stick_get(g->s, file);
	m->stick_default = rms_stick_default(g->s);
	m->pak = g->pak;
	m->face_offered = ctl_letter_layout(g->s->tag, &m->letters);
	/* The N64's SELECT layer is its C buttons; the Dreamcast's stick rule
	 * (mode dpad) takes SELECT for its own d-pad while it is on */
	m->select_held_c = ct_select_held_c(&m->cur);
	m->select_held_dpad = !m->select_held_c && m->stick_on && !strcmp(g->s->stick, "dpad");
	g_cs.nch = ctl_remap_choices(g->s->tag, g_cs.ext, g->s->map, g_cs.ch, 32);
	if (g_cs.base_row < 0) g_cs.base = m->cur;
	m->base = g_cs.base;
	g_cs.stale = false;
}

/* The cursor on row `id`: a row it came onto starts from the table as it is */
static void track(int id)
{
	if (id == g_cs.base_row) return;
	g_cs.base_row = id;
	g_cs.base = g_cs.m.cur;
	g_cs.m.base = g_cs.base;
}

/* What the game is sent: the whole table, at once, while it is paused; it
 * plays with it from Continue */
static void send_map(const ctlscr_game *g)
{
	rm_table sent;
	char map[320];

	if (!g->running) return;
	rms_sent(g->s, g->file, rms_stick_get(g->s, g->file), &sent);
	rm_format(&sent, map, sizeof map);
	fprintf(stderr, "controls: map %s %s\n", g->s->tag, map);
	plat_resident_line("SETMAP\tmap=%s", map);
}

/* The stick as RUN would start it (rms_run_stick), at once; Diatom's STICK
 * answer is not waited for, and shows no pop-up (the row says it already:
 * plat_resident_set_stick) */
static void send_stick(const ctlscr_game *g, bool on)
{
	char run[24];

	if (!g->running) return;
	rms_run_stick(g->s, on, run, sizeof run);
	fprintf(stderr, "controls: stick %s %s\n", g->s->tag, run[0] ? run : "off");
	plat_resident_set_stick(!strncmp(run, "stick=", 6) ? run + 6 : "off");
}

/* A table for the scope shown, kept and sent */
static void put(const ctlscr_game *g, const rm_table *t)
{
	rms_put(g->s, g_cs.m.scope_all ? NULL : g->file, t);
	send_map(g);
	g_cs.stale = true;
}

/* ---- the lights and presses ------------------------------------------------------ */

/* A key pressing: in over **press**, out over the next (DESIGN.md 8), from
 * wherever it was when a new press came */
static float press_at(int k, unsigned now)
{
	const unsigned el = now - g_cs.press_t0[k];

	if (!g_cs.press_t0[k]) return 0.0f;
	if (el < DS_MS_PRESS)
		return g_cs.press_from[k] + (1.0f - g_cs.press_from[k]) * ds_out((float)el / DS_MS_PRESS);
	if (el < 2 * DS_MS_PRESS) return 1.0f - ds_out((float)(el - DS_MS_PRESS) / DS_MS_PRESS);
	return 0.0f;
}

static void press(int k)
{
	const unsigned now = anim_now();

	if (k < 0 || k >= TD_KEYS) return;
	g_cs.press_from[k] = press_at(k, now);
	g_cs.press_t0[k] = now ? now : 1;
}

/* The keys lit move to `target`, each in **quick** from where it is, the
 * old going out as the new come in; or there at once, the first time */
static void lights_to(unsigned target, unsigned now)
{
	int k;

	for (k = 0; k < TD_KEYS; k++) {
		const float to = target >> k & 1u ? 1.0f : 0.0f;

		if (g_cs.snap) tween_snap(&g_cs.on[k], to);
		else tween_to(&g_cs.on[k], to, DS_MS_QUICK, now);
	}
	g_cs.snap = false;
}

/* The keys the live screen's rows are about, false when they say nothing
 * now (another tab, or the tab before its rows were built) */
static bool lights_of(const menu_side *at, unsigned *target)
{
	switch (g_cs.mode) {
	case CS_PICK:
		*target = ct_lit(&g_cs.m, (ct_row)(CT_BUTTON + g_cs.pick_button));
		return true;
	default:
		if (at->tab_at >= 0 && at->tab_at != GT_CONTROLS) return false;
		if (!g_cs.fresh) return false;
		if (at->sel < 0 || at->sel >= g_cs.n) {
			*target = 0;
			return true;
		}
		track(g_cs.ids[at->sel]);
		*target = ct_lit(&g_cs.m, g_cs.ids[at->sel]);
		return true;
	}
}

/* The handheld's pose for a frame of `at`, false for none (another tab, or
 * gone); whether it moves, in *moving */
static bool pose_of(const menu_side *at, td_pose *p, bool *moving)
{
	float dx = 0.0f, op = 1.0f, deg = DEV_DEG;
	int k;

	*moving = false;
	if (g_cs.mode == CS_TAB && at->tab_at >= 0) {
		const unsigned el = at->now - at->tab_t0;
		const float mv = at->tab_t0 && el < DS_MS_MOVE ? ds_out((float)el / DS_MS_MOVE) : 1.0f;

		if (at->tab_at == GT_CONTROLS) {
			/* In with the rows, faded up, and turning to face front */
			dx = at->tab_dir * DEV_IN_DX * (1.0f - mv);
			op = mv;
			if (at->tab_t0 && at->tab_from != GT_CONTROLS && el < DS_MS_TURN) {
				deg = DEV_DEG_IN + (DEV_DEG - DEV_DEG_IN) * ds_turn((float)el / DS_MS_TURN);
				*moving = true;
			}
		} else if (at->tab_from == GT_CONTROLS && mv < 1.0f) {
			/* Out with the old tab's name, gone in two fifths of the move */
			dx = -at->tab_dir * DEV_OUT_DX * mv;
			op = fmaxf(0.0f, 1.0f - 2.5f * mv);
		} else {
			return false;
		}
		if (mv < 1.0f) *moving = true;
	}
	if (op <= 0.004f) return false;
	/* Cleared whole, padding too, so two poses compare as bytes */
	memset(p, 0, sizeof *p);
	p->cx = (float)at->content_x + DEV_DX + dx;
	p->cy = DEV_CY;
	p->scale = DEV_SCALE;
	p->deg = deg;
	p->op = op;
	for (k = 0; k < TD_KEYS; k++) {
		p->on[k] = tween_at(&g_cs.on[k], at->now);
		p->press[k] = press_at(k, at->now);
		if (tween_moving(&g_cs.on[k], at->now) || p->press[k] > 0.0f ||
		    (g_cs.press_t0[k] && at->now - g_cs.press_t0[k] < 2 * DS_MS_PRESS))
			*moving = true;
	}
	return true;
}

void ctlscr_side(app *a, const menu_side *at)
{
	const bool draw = at->stage == MENU_SIDE_REST ||
	                  (at->stage == MENU_SIDE_OVER) == g_cs.steady;
	unsigned target;
	bool moving;
	td_pose p;

	/* The live screen sets the lights, once a frame */
	if (at->live && at->stage != MENU_SIDE_OVER && lights_of(at, &target))
		lights_to(target, at->now);
	/* ...and a panel with the handheld at rest ends a page's hold */
	if (at->live && at->stage == MENU_SIDE_REST) g_cs.steady = false;
	if (!draw || !pose_of(at, &p, &moving)) return;
	if (moving) redraw_now();
	g_cs.keep.box = (SDL_Rect){ (int)floorf(p.cx) - KEEP_L, (int)p.cy - KEEP_T, KEEP_W, KEEP_H };
	/* Into its texture first, where the renderer keeps one, so the copy is
	 * clipped to the panel: a slide in from the left would show past it */
	td_make(a->r, &g_cs.keep, &p);
	SDL_RenderSetClipRect(a->r, &(SDL_Rect){ at->panel.x + UI_PANEL_BORDER, at->panel.y,
	                                         at->panel.w - 2 * UI_PANEL_BORDER, at->panel.h });
	td_draw(a->r, &g_cs.keep, &p);
	SDL_RenderSetClipRect(a->r, NULL);
}

/* ---- the tab's rows and keys ------------------------------------------------------ */

void ctlscr_open(void)
{
	int k;

	g_cs.stale = true;
	g_cs.snap = true;
	g_cs.fresh = false;
	g_cs.base_row = -1;
	g_cs.mode = CS_TAB;
	g_cs.steady = false;
	for (k = 0; k < TD_KEYS; k++) g_cs.press_t0[k] = 0;
}

void ctlscr_enter(void)
{
	g_cs.stale = true;
	g_cs.snap = true;
	g_cs.fresh = false;
}

void ctlscr_close(void)
{
	td_keep_free(&g_cs.keep);
}

void ctlscr_changed(void)
{
	g_cs.stale = true;
}

/* `src` on lines no wider than `w` in the menu's font, broken at a space,
 * or between two characters where a word is wider than the line (Chinese
 * and Japanese have no spaces); what is left after `max` - 1 on the last.
 * The count. */
static int wrap_lines(const char *src, int w, char (*out)[96], int max)
{
	TTF_Font *f = ui_font(UI_F_MENU);
	int n = 0;

	while (*src && n < max) {
		char buf[96];
		int i, take = 0, fit = 0;

		while (*src == ' ') src++;
		if (!*src) break;
		for (i = 1; src[i - 1] && i < (int)sizeof buf; i++) {
			if (((unsigned char)src[i] & 0xC0) == 0x80) continue;
			memcpy(buf, src, (size_t)i);
			buf[i] = '\0';
			if (ui_text_width(f, buf) > w) break;
			fit = i;
			if (src[i] == ' ' || src[i] == '\0') take = i;
		}
		if (!take) take = fit;
		if (n == max - 1 || !take) take = (int)strlen(src);
		if (take >= (int)sizeof out[n]) take = (int)sizeof out[n] - 1;
		memcpy(out[n], src, (size_t)take);
		out[n][take] = '\0';
		n++;
		src += take;
	}
	return n;
}

int ctlscr_rows(app *a, const ctlscr_game *g, menu_row *rows, unsigned *vcol,
                unsigned char *sound, int *ids)
{
	menu_row made[CT_MAX_ROWS];
	ct_row id0[CT_MAX_ROWS];
	unsigned vc0[CT_MAX_ROWS];
	TTF_Font *f = ui_font(UI_F_MENU);
	const int col = column_w(g);
	int n0, n = 0, used = 0, i, k;

	(void)a;
	refresh(g);
	n0 = ct_rows(&g_cs.m, id0, made, vc0, &g_cs.b);
	/* A quiet row too long for the column - the note, a warning - on lines
	 * of its own, left as a paragraph's are, rather than sliding */
	for (i = 0; i < n0; i++) {
		const bool quiet = id0[i] == CT_OWN_NOTE || (id0[i] >= CT_WARN && id0[i] < CT_RESET);

		if (quiet && used < 15 && n0 - i + n + 2 <= CT_MAX_ROWS &&
		    ui_text_width(f, made[i].label) > col) {
			const int lines = wrap_lines(made[i].label, col, &g_cs.lines[used],
			                             16 - used < 3 ? 16 - used : 3);

			for (k = 0; k < lines; k++, n++) {
				rows[n] = MENU_BODY(g_cs.lines[used + k]);
				g_cs.ids[n] = id0[i];
				vcol[n] = 0;
			}
			used += lines;
			continue;
		}
		rows[n] = made[i];
		g_cs.ids[n] = id0[i];
		vcol[n] = vc0[i];
		n++;
	}
	for (i = 0; i < n; i++) {
		const ct_row id = g_cs.ids[i];

		/* Left and right step a value round on every row that has one */
		if (sound) sound[i] = rows[i].live && (id == CT_SCOPE || id == CT_STICK || id == CT_PAK ||
		                                       id == CT_FACE || ct_button_of(id) >= 0)
		                      ? MENU_SND_RING : 0;
		if (ids) ids[i] = id;
	}
	g_cs.n = n;
	g_cs.fresh = true;
	return n;
}

/* The pages this tab opens are drawn over the in-game menu's backdrop with
 * its power rule: their own context carries the menu's */
typedef struct {
	const ctlscr_game *g;
	int                button;
	ct_pick_bufs       b;
} cs_ctx;

static void cs_backdrop(app *a, void *ctx, float k)
{
	const ctlscr_game *g = ((cs_ctx *)ctx)->g;

	if (g->over && g->over->backdrop) g->over->backdrop(a, g->ctx, k);
}

static menu_result cs_power(app *a, void *ctx)
{
	const ctlscr_game *g = ((cs_ctx *)ctx)->g;

	return g->over && g->over->on_power ? g->over->on_power(a, g->ctx) : MENU_DONE;
}

/* The in-game menu's look for a page this tab opens, with the handheld */
static menu_style cs_style(const ctlscr_game *g, bool side)
{
	menu_style st;

	memset(&st, 0, sizeof st);
	if (g->over) {
		st.fixed_w = g->over->fixed_w;
		st.accent = g->over->accent;
		st.follow_tint = g->over->follow_tint;
	}
	st.backdrop = cs_backdrop;
	st.on_power = cs_power;
	if (side) {
		st.side = ctlscr_side;
		st.side_w = CTLSCR_SIDE_W;
	}
	return st;
}

/* After a button's choices: the handheld holds still while they leave,
 * unless MENU is closing every screen */
static void back_to_tab(void)
{
	g_cs.mode = CS_TAB;
	g_cs.steady = !menu_closing();
	g_cs.stale = true;
}

static int pick_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	cs_ctx *c = ctx;
	int sel, n;

	refresh(c->g);
	n = ct_pick_rows(&g_cs.m, c->button, g_cs.ch, g_cs.nch, rows, &c->b, &sel);
	*heading = c->b.head;
	return n < max ? n : max;
}

/* A on a choice takes it and goes back: one swap, from the table as it was
 * when the cursor came onto the button's row */
static menu_result pick_key(app *a, void *ctx, in_button key, int sel)
{
	cs_ctx *c = ctx;
	rm_table t;

	(void)a;
	if (key != IN_ACCEPT || sel < 0 || sel >= g_cs.nch) return MENU_STAY;
	ct_pick(&g_cs.m, c->button, g_cs.ch[sel]);
	t = g_cs.m.cur;
	put(c->g, &t);
	return MENU_DONE;
}

static void pick(app *a, const ctlscr_game *g, int button)
{
	cs_ctx c;
	menu_row rows[32];
	menu_style st = cs_style(g, true);

	memset(&c, 0, sizeof c);
	c.g = g;
	c.button = button;
	ct_pick_rows(&g_cs.m, button, g_cs.ch, g_cs.nch, rows, &c.b, &st.start_sel);
	g_cs.mode = CS_PICK;
	g_cs.pick_button = button;
	g_cs.steady = true;
	menu_run(a, &st, pick_build, pick_key, &c);
	back_to_tab();
}

/* The Hotkeys row's pages: the read-only Controls pages, at their own
 * width and with no handheld, less the system's buttons the tab shows */
static void pages(app *a, const ctlscr_game *g)
{
	cs_ctx c;
	menu_style st = cs_style(g, false);

	memset(&c, 0, sizeof c);
	c.g = g;
	game_controls_pages(a, &st, &c);
	g_cs.stale = true;
}

void ctlscr_question(const ctlscr_game *g, char *q, size_t n)
{
	if (scope_all(g)) snprintf(q, n, T(S_GAMEMENU_RESET_BUTTONS_ALL), g->s->name);
	else snprintf(q, n, T(S_GAMEMENU_RESET_BUTTONS_GAME), g->s->name, g->title ? g->title : "");
}

/* Reset Buttons: asked first, on Cancel. This Game goes back to the
 * system's buttons and its stick to the system's default; All <System> to
 * ZyperOS's. */
static void reset(app *a, const ctlscr_game *g)
{
	cs_ctx c;
	menu_style st = cs_style(g, false);
	char q[LIB_NAME + 256];

	memset(&c, 0, sizeof c);
	c.g = g;
	ctlscr_question(g, q, sizeof q);
	if (!ask_panel(a, &st, &c, T(S_GAMEMENU_RESET_BUTTONS), q, T(S_GAMEMENU_RESET_BUTTONS_YES))) return;
	rms_reset(g->s, scope_all(g) ? NULL : g->file);
	if (!scope_all(g) && rms_stick_offered(g->s) && rms_stick_get(g->s, g->file) != rms_stick_default(g->s)) {
		rms_stick_put(g->s, g->file, rms_stick_default(g->s));
		send_stick(g, rms_stick_default(g->s));
	}
	send_map(g);
	g_cs.stale = true;
	g_cs.base_row = -1;
}

/* After a key on row `id`: the cursor stays on that row, found again by its
 * id in the rows as they are now. A change can put a warning row above it
 * or take one away, and the runner keeps its cursor by place. Not when the
 * screen is going (POWER, MENU in a screen it opened). */
static void keep_cursor(app *a, const ctlscr_game *g, int id)
{
	menu_row rows[CT_MAX_ROWS];
	unsigned vcol[CT_MAX_ROWS];
	int n, k;

	if (!a->running || menu_closing()) return;
	n = ctlscr_rows(a, g, rows, vcol, NULL, NULL);
	for (k = 0; k < n && (int)g_cs.ids[k] != id; k++) ;
	if (k < n) menu_cursor_to(k);
}

menu_result ctlscr_key(app *a, const ctlscr_game *g, in_button key, int id)
{
	const bool side = key == IN_LEFT || key == IN_RIGHT;
	ct_model *m = &g_cs.m;
	int bt;

	if (id < 0 || (!side && key != IN_ACCEPT)) return MENU_STAY;
	refresh(g);
	track(id);
	switch (id) {
	case CT_SCOPE:
		if (m->fixed_game || !g->scope_all) break;
		*g->scope_all = !*g->scope_all;
		g_cs.stale = true;
		break;
	case CT_STICK: {
		const bool on = !m->stick_on;

		rms_stick_put(g->s, g->file, on);
		send_stick(g, on);
		/* The Dreamcast's SELECT follows the stick (rms_sent) */
		send_map(g);
		g_cs.stale = true;
		break;
	}
	case CT_FACE: {
		rm_table t = m->cur;
		int i;

		ct_faces_set(m, &t, ct_faces_next(ct_faces(m, &m->cur)));
		put(g, &t);
		for (i = RM_A; i <= RM_Y; i++)
			if (t.b[i].to != m->cur.b[i].to || t.b[i].turbo != m->cur.b[i].turbo) press(ct_key_of(i));
		break;
	}
	case CT_RESET:
		if (key == IN_ACCEPT && ct_reset_live(m)) reset(a, g);
		break;
	case CT_HOTKEYS:
		if (key == IN_ACCEPT) pages(a, g);
		break;
	default:
		bt = ct_button_of((ct_row)id);
		if (bt < 0 || ct_dead(m, bt)) break;
		if (side) {
			rm_table t;

			if (ct_step(m, (ct_row)id, key == IN_LEFT ? -1 : 1)) {
				t = m->cur;
				put(g, &t);
				press(ct_key_of(bt));
			}
		} else {
			pick(a, g, bt);
		}
		break;
	}
	keep_cursor(a, g, id);
	redraw_now();
	return MENU_STAY;
}

void ctlscr_shot_pick(app *a, const ctlscr_game *g, int id)
{
	cs_ctx c;
	menu_row rows[32];
	menu_style st = cs_style(g, true);
	int bt = ct_button_of((ct_row)id), sel, n;

	memset(&c, 0, sizeof c);
	c.g = g;
	if (bt < 0) bt = RM_A;
	refresh(g);
	track(id);
	n = ct_pick_rows(&g_cs.m, bt, g_cs.ch, g_cs.nch, rows, &c.b, &sel);
	g_cs.mode = CS_PICK;
	g_cs.pick_button = bt;
	/* Over the backdrop the choices' runner draws them over (the screen's
	 * own; a shot of the tab has drawn its paused game already) */
	st.backdrop(a, &c, 1.0f);
	menu_draw_styled(a, &st, c.b.head, rows, n, sel);
	g_cs.mode = CS_TAB;
}

/* ---- the page of its own ------------------------------------------------------ */

/* What the page's runner carries: the game it is about, its rows' value
 * colors and sounds (read every frame through the style), and the style the
 * pages it opens take their width and accent from */
typedef struct {
	app          *a;
	ctlscr_game   g;
	menu_style    over;
	const char   *title;
	unsigned      vcol[CT_MAX_ROWS];
	unsigned char sound[CT_MAX_ROWS];
} cs_page;

/* What the page's own screens (a button's choices, Reset Buttons' question,
 * the Hotkeys pages) are drawn over and keep: the shelf under the scrim, as
 * the runner draws it under the page itself, which has no backdrop of its
 * own; and POWER powering off, as everywhere on the shelf, not only closing
 * the screen it was pressed on */
static void page_backdrop(app *a, void *ctx, float k)
{
	(void)ctx;
	draw_shelf(a);
	ui_scrim_k(a->r, 0, k);
}

static menu_result page_power(app *a, void *ctx)
{
	(void)ctx;
	power_off(a);
	return MENU_DONE;
}

static void page_game(cs_page *p, const system_cfg *owner, const char *file,
                      const char *title, unsigned accent)
{
	memset(p, 0, sizeof *p);
	p->title = title;
	p->over.fixed_w = CTLSCR_PAGE_W;
	p->over.accent = accent;
	p->over.backdrop = page_backdrop;
	p->over.on_power = page_power;
	p->g.s = owner;
	p->g.file = file;
	p->g.title = title;
	/* Applies To is fixed on This Game, and nothing is paused under it */
	p->g.fixed_game = true;
	p->g.running = false;
	p->g.over = &p->over;
}

static int page_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	cs_page *p = ctx;
	int n = ctlscr_rows(p->a, &p->g, rows, p->vcol, p->sound, NULL);

	*heading = p->title;
	return n < max ? n : max;
}

static menu_result page_key(app *a, void *ctx, in_button key, int sel)
{
	cs_page *p = ctx;

	/* X does nothing here: it quits from the in-game menu, and there is no
	 * game to quit */
	if (key == IN_X) return MENU_STAY;
	ctlscr_key(a, &p->g, key, sel >= 0 && sel < g_cs.n ? (int)g_cs.ids[sel] : -1);
	return MENU_STAY;
}

void ctlscr_page(app *a, const system_cfg *owner, const char *file,
                 const char *title, unsigned accent)
{
	cs_page p;
	menu_style st;

	page_game(&p, owner, file, title, accent);
	p.a = a;
	memset(&st, 0, sizeof st);
	st.fixed_w = CTLSCR_PAGE_W;
	st.accent = accent;
	st.sound = p.sound;
	st.vcolors = p.vcol;
	st.side = ctlscr_side;
	st.side_w = CTLSCR_SIDE_W;
	ctlscr_open();
	menu_run(a, &st, page_build, page_key, &p);
	ctlscr_close();
}

void ctlscr_page_shot_pick(app *a, const system_cfg *owner, const char *file,
                           const char *title, unsigned accent, int id)
{
	cs_page p;

	page_game(&p, owner, file, title, accent);
	p.a = a;
	ctlscr_open();
	ctlscr_shot_pick(a, &p.g, id);
}

void ctlscr_page_shot(app *a, const system_cfg *owner, const char *file,
                      const char *title, unsigned accent, int sel)
{
	static menu_row rows[CT_MAX_ROWS];
	cs_page p;
	menu_style st;
	int n;

	page_game(&p, owner, file, title, accent);
	p.a = a;
	memset(&st, 0, sizeof st);
	st.fixed_w = CTLSCR_PAGE_W;
	st.accent = accent;
	st.side = ctlscr_side;
	st.side_w = CTLSCR_SIDE_W;
	st.vcolors = p.vcol;
	ctlscr_open();
	n = ctlscr_rows(a, &p.g, rows, p.vcol, p.sound, NULL);
	if (sel >= n) sel = n - 1;
	if (sel < 0 || !rows[sel].live) {
		sel = menu_step_sel(rows, n, sel < 0 ? 0 : sel, +1);
		if (!rows[sel].live) sel = menu_step_sel(rows, n, sel, -1);
	}
	menu_draw_styled(a, &st, title, rows, n, sel);
}
