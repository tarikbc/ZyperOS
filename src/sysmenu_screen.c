/* SPDX-License-Identifier: GPL-3.0-only */
#include "sysmenu_screen.h"
#include "hotkeys_screen.h"
#include "dmodepreview.h"
#include "ds/preview.h"
#include "ds/colorref.h"
#include "ds/scale.h"

#include "aout_route.h"
#include "eq.h"
#include "mp4lib.h"
#include "mp4c.h"
#include "audioout.h"
#include "dslang.h"
#include "pacing.h"
#include "sgbmode.h"
#include "border_pic.h"
#include "handframe.h"
#include "app.h"
#include "bgm.h"
#include "bgm_menu.h"
#include "bgm_screen.h"
#include "bt.h"
#include "bt_screen.h"
#include "clamp.h"
#include "clock.h"
#include "clock_screen.h"
#include "collect_screen.h"
#include "coreopts_page.h"
#include "devmode.h"
#include "esmedia.h"
#include "folderart.h"
#include "homesys_screen.h"
#include "hotspot.h"
#include "linebreak.h"
#include "look.h"
#include "ds/focus.h"
#include "menu_panel.h"
#include "menu_pick.h"
#include "nowplaying_looks.h"
#include "nowplaying_screen.h"
#include "osdbar.h"
#include "panel.h"
#include "ds/hover.h"
#include "hover_sweep.h"
#include "nightkeep.h"
#include "panelgamma.h"
#include "powerhold.h"
#include "powerscreens.h"
#include "radl_screen.h"
#include "dl_screen.h"
#include "dlq.h"
#include "rafetch.h"
#include "saver.h"
#include "screencard.h"
#include "search_screen.h"
#include "sgdb.h"
#include "shelf_draw.h"
#include "shelfbuild.h"
#include "sort.h"
#include "stats_screen.h"
#include "storage_screen.h"
#include "strength.h"
#include "tour_screen.h"
#include "ui.h"
#include "uisound.h"
#include "update_screen.h"
#include "usblink.h"
#include "shaderdemo.h"
#include "rumble.h"

#include <math.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>


/* Developer (SSH) is offered on a development image (dropbear at boot) and
 * on a release image once the player has unlocked Developer Mode (devmode.h) */
static bool dev_offered(void)
{
	return usblink_dev_image() || devmode_unlocked(db_dev());
}

/* MENU's Developer Mode row: on a development image the link-always-bound
 * setting (usblink.h); on a release image, where the row is there only once
 * Developer Mode is unlocked, that it is on (the row opens its page) */
static bool dev_row_on(void)
{
	return usblink_dev_image() ? usblink_developer(db_dev()) : devmode_unlocked(db_dev());
}

/* MENU's USB rows: on a device where usbrole decides (or a shot that
 * says so), and USB Device while a plug waits */
static bool usb_power_offered(void)
{
	usb_power p;

	return shot_usb_menu || plat_usb_power(&p);
}

static bool usb_wait_row(void) { return shot_usb_menu || g_usb_wait != 0; }

/* Automatic's message, as it is chosen: what it does and its one risk, on
 * up to four balanced lines, a line more than result_message's, since the
 * risk is said whole rather than cut */
int usb_auto_lines(app *a, char lines[4][LB_LINE], menu_row rows[4])
{
	int w = menu_std_width(a), i;
	int n = ui_balance_lines(ui_font(UI_F_MENU), T(S_SYSMENU_USB_AUTO_SAID), w, 4, lines);

	ui_fit_drawn(T(S_SYSMENU_USB_AUTO_SAID));
	for (i = 0; i < n; i++) rows[i] = (menu_row){ lines[i], NULL, false };
	return n;
}

/* --cheevos-unchecked, for a shot: the Achievements rows as a game never
 * played with a network has them, whatever the card holds */
bool g_shot_unchecked;
/* --cheevos-no-hash file|system, the same for a game the launcher has no
 * hash for: its file's kind, or every kind its system takes (a PSP game) */
ra_hash_kind g_shot_no_hash;

/* A on an Achievements row that says "not checked yet": how a game gets its
 * set - one play with a network - and that it then works offline */
void cheevos_unchecked_message(app *a)
{
	char lines[4][LB_LINE];
	menu_row rows[4];
	int w = menu_std_width(a), i;
	int n = ui_balance_lines(ui_font(UI_F_MENU), T(S_CHEEVOS_UNCHECKED_SAID), w, 4, lines);

	ui_fit_drawn(T(S_CHEEVOS_UNCHECKED_SAID));
	for (i = 0; i < n; i++) rows[i] = (menu_row){ lines[i], NULL, false };
	timed_message(a, T(S_SYSMENU_CHEEVOS), rows, n, w, MENU_ZYPEROS);
}

static void usb_auto_message(app *a)
{
	char lines[4][LB_LINE];
	menu_row rows[4];
	int n = usb_auto_lines(a, lines, rows);

	timed_message(a, T(S_SYSMENU_USB_ACCESSORIES), rows, n, menu_std_width(a), MENU_ZYPEROS);
}

static const char *look_accent_name(void);   /* with the Theme page */
static const char *screen_colors_value(void); /* with the Screen Colors page */
/* Night Mode and its page (below Screen Colors): Night Warmth 1 to 10 */
#define NM_MODES NIGHT_MODES
#define NM_WARMTH_MIN 1
#define NM_WARMTH_MAX PANELGAMMA_NIGHT_MAX
static int nm_mode(void);
static const char *nm_mode_name(int m);
static void nm_keep(void);
static void nm_warmth_focus(void *app, int k);
static eq_out eq_out_now(void);

/* ---------- option rows' choices (menu.h's menu_choice, DESIGN.md 9) ------ */

/* Each option row of more than two choices in MENU, a system's menu and the
 * MP4 shelf, and the MP4 shelf's Sort By (two A did not turn), as their
 * key handlers step them: in the order Left and Right go, so the runner
 * applies a choice by sending them. The words live here until the next
 * build. */
#define CH_MAX 48   /* a MENU row's most: Night Hours' 48 half hours */
typedef struct {
	const char *lab[CH_MAX];
	char buf[CH_MAX][48];
} ch_pool;

static menu_choice g_pm_choice[PM_ROWS], g_sm_choice[SM_ROWS], g_smm_choice[SM_MP4_ROWS];
/* Recents' Card Picture (DESIGN.md 11.48) */
static menu_choice g_pic_choice;

static menu_choice ch_make(const ch_pool *p, int n, int cur, bool none)
{
	if (n > CH_MAX) n = CH_MAX;
	return (menu_choice){ .n = (unsigned char)n, .cur = (unsigned char)(cur >= 0 && cur < n ? cur : 0),
	                      .none = none, .labels = p->lab };
}

/* The shaders this device runs for the system tagged `tag`, in
 * shader_step's order, `cur` the place of shader `on` */
static int ch_shaders(ch_pool *p, int on, int *cur, const char *tag)
{
	int k, n = 0;

	*cur = 0;
	for (k = 0; k < SHADER_COUNT && n < CH_MAX; k++) {
		if (SHADERS[k].gpu && !plat_gpu_shaders()) continue;
		if (!shader_for(k, tag)) continue;
		if (k == on) *cur = n;
		p->lab[n++] = shader_label(k);
	}
	return n;
}

/* Strength's rungs, 10% to 100% */
static int ch_strengths(ch_pool *p, int on, int *cur)
{
	int v, n = 0;

	*cur = 0;
	for (v = STRENGTH_MIN; v <= STRENGTH_MAX && n < CH_MAX; v += STRENGTH_STEP) {
		if (v == on) *cur = n;
		snprintf(p->buf[n], sizeof p->buf[n], "%d%%", v);
		p->lab[n] = p->buf[n];
		n++;
	}
	return n;
}
/* The shelf's Display list's picture (menu_choice's preview): the game
 * under the cursor's last frame, its Auto picture, as mode `k` would draw
 * it, with the shape its system's games last said (dmode_note_shape); a
 * game never played has no frame, and the stand-in shows the mode */
static void sm_dmode_preview(void *app_, int k, int x, int y, int w, int h)
{
	static SDL_Texture *t;
	static char made[LIB_PATH * 2];
	static int tw, th;
	app *a = app_;
	sysview *v = &a->view[a->sys_cursor];
	char path[LIB_PATH * 2] = "";
	double aspect = 0.0;
	bool known = false;
	int fw, fh, o;

	if (v->cursor >= 0 && v->cursor < v->list.count) {
		o = shelf_owner(a, a->sys_cursor, v->cursor);
		preview_path(a, o, &v->list.items[v->cursor], path, sizeof path);
		aspect = dmode_shape_of(a->sys.systems[o].tag, &known);
	}
	if (strcmp(path, made)) {
		if (t) SDL_DestroyTexture(t);
		t = path[0] ? load_image(a->r, path, &tw, &th, NULL) : NULL;
		snprintf(made, sizeof made, "%s", path);
	}
	/* Kept shape unknown: a console's */
	if (!known) aspect = 4.0 / 3.0;
	dmode_surface(&fw, &fh);
	dmode_preview_draw(a, t, t ? tw : 0, t ? th : 0, aspect, fw, fh, k, x, y, w, h);
}

/* Border's list's picture: the game under the cursor's last frame, as
 * Display's list has it, where the system's mode puts it, each kind the row
 * offers around it (dmode_border_preview) */
static border_kind g_sm_border_kinds[BORDER_KINDS];

static void sm_border_preview(void *app_, int k, int x, int y, int w, int h)
{
	static SDL_Texture *t;
	static char made[LIB_PATH * 2];
	static int tw, th;
	app *a = app_;
	sysview *v = &a->view[a->sys_cursor];
	const system_cfg *s = &a->sys.systems[a->sys_cursor];
	char path[LIB_PATH * 2] = "", own[CFG_STR * 3] = "";
	double aspect = 0.0;
	bool known = false;
	int sw = 0, sh = 0;
	border_kind kind = k >= 0 && k < BORDER_KINDS ? g_sm_border_kinds[k] : BORDER_OFF;
	SDL_Surface *pic = NULL;

	if (v->cursor >= 0 && v->cursor < v->list.count)
		preview_path(a, a->sys_cursor, &v->list.items[v->cursor], path, sizeof path);
	if (strcmp(path, made)) {
		if (t) SDL_DestroyTexture(t);
		t = path[0] ? load_image(a->r, path, &tw, &th, NULL) : NULL;
		snprintf(made, sizeof made, "%s", path);
	}
	aspect = dmode_shape_of(s->tag, &known);
	if (!dmode_source_of(s->tag, &sw, &sh) && t) { sw = tw; sh = th; }
	if (kind == BORDER_OWN) pic = border_own_cached(s, own, sizeof own);
	if (kind == BORDER_HANDHELD || kind == BORDER_WHOLE) {
		SDL_Rect at;

		pic = border_frame_cached(a, a->sys_cursor, kind, -1, own, sizeof own, &at);
		dmode_border_preview(a, t, sw, sh, known ? aspect : 0.0, v->dmode, (int)kind, pic, own,
		                     pic ? &at : NULL, x, y, w, h);
		return;
	}
	dmode_border_preview(a, t, sw, sh, known ? aspect : 0.0, v->dmode, (int)kind, pic, own, NULL, x, y, w, h);
}

/* Handheld Color's list's picture: the frame in color k, whole where it is
 * offered (border_color_kind), the game under the cursor's last picture in
 * its window */
static void sm_color_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	sysview *v = &a->view[a->sys_cursor];
	border_kind kind = border_color_kind(a, a->sys_cursor);
	char key[96];
	SDL_Rect at;
	SDL_Surface *f = border_frame_cached(a, a->sys_cursor, kind, k, key, sizeof key, &at);

	dmode_border_preview(a, NULL, 0, 0, 0.0, v->dmode, (int)kind, f, key, f ? &at : NULL, x, y, w, h);
}

/* Card Picture's list's picture (DESIGN.md 11.48): the Recents game under
 * the cursor as each choice shows it, its cover (k 0) or its Last Screen
 * card (k 1), made once a game and kept while the list is open; a game with
 * no resume point shows its cover for both, as its card would */
static void sm_pic_preview(void *app_, int k, int x, int y, int w, int h)
{
	static SDL_Texture *t[2];
	static char made[2][LIB_PATH * 2];
	static int tw[2], th[2];
	app *a = app_;
	sysview *v = &a->view[a->sys_cursor];
	const SDL_Rect screen = { x, y, w, h };
	char frame[LIB_PATH * 2] = "", cover[LIB_PATH * 3] = "", st[LIB_PATH * 3], key[LIB_PATH * 2];
	SDL_Rect in, to;
	struct stat sb;
	int o, which = k ? 1 : 0;

	if (v->cursor >= 0 && v->cursor < v->list.count) {
		const game_entry *g = &v->list.items[v->cursor];

		o = shelf_owner(a, a->sys_cursor, v->cursor);
		box_art_path(a->sys.systems[o].folder, g->name, cover, sizeof cover);
		if ((stat(cover, &sb) != 0 || sb.st_size <= 0) &&
		    !es_media(P_ROMS, a->sys.systems[o].folder, g->file, g->name, ES_COVER, true,
		              cover, sizeof cover))
			cover[0] = '\0';
		if (which) {
			state_path(a, o, g, st, sizeof st);
			preview_path(a, o, g, frame, sizeof frame);
			if (stat(st, &sb) != 0 || stat(frame, &sb) != 0) frame[0] = '\0';
		}
	}
	snprintf(key, sizeof key, "%s|%s", frame, cover);
	if (strcmp(key, made[which])) {
		if (t[which]) SDL_DestroyTexture(t[which]);
		t[which] = NULL;
		ui_card_order card;
		int cw, ch;

		ui_card_order_set(&card, v->list.items[v->cursor].title,
		                  a->sys.systems[shelf_owner(a, a->sys_cursor, v->cursor)].accent, false);
		game_card_order(a, shelf_owner(a, a->sys_cursor, v->cursor), &v->list.items[v->cursor], &card);
		if (frame[0]) {
			SDL_Surface *sf = screen_card_make(frame, cover, &card, false);

			if (sf) {
				t[which] = SDL_CreateTextureFromSurface(a->r, sf);
				tw[which] = sf->w;
				th[which] = sf->h;
				SDL_FreeSurface(sf);
			}
		}
		if (!t[which] && cover[0]) t[which] = load_image(a->r, cover, &tw[which], &th[which], NULL);
		/* No cover file: the generated card, as the shelf shows it */
		if (!t[which] && (t[which] = ui_make_card_for(a->r, &card, &cw, &ch, NULL))) {
			tw[which] = cw;
			th[which] = ch;
		}
		snprintf(made[which], sizeof made[which], "%s", key);
	}
	in = ds_preview_screen_begin(a->r, &screen);
	if (t[which] && tw[which] > 0 && th[which] > 0) {
		/* Whole in the small screen, its shape kept */
		to.w = in.w;
		to.h = th[which] * in.w / tw[which];
		if (to.h > in.h) { to.h = in.h; to.w = tw[which] * in.h / th[which]; }
		to.x = in.x + (in.w - to.w) / 2;
		to.y = in.y + (in.h - to.h) / 2;
		SDL_RenderCopy(a->r, t[which], NULL, &to);
	}
	ds_preview_screen_end(a->r, &screen);
}

/* Timing's list's picture (menu_choice's preview, DESIGN.md 11.39): the two
 * ways, slowed down. A ball crosses a small screen a step a game frame, a
 * refresh every PACE_DEMO_MS, and where it was at each of the refreshes
 * before is behind it. Smooth shows the next frame each refresh: the steps
 * even. Exact keeps the game's own rate, so now and then a refresh shows the
 * frame before again: the ball stops for a refresh, its two places one,
 * ringed in the accent. Slowed about ten times, and the repeat made about
 * forty times as common as on the Pixel 2 (one refresh in about 170), so
 * the hitch can be seen at all. In the dark theme's colors in either, as a
 * game's screen is. */
#define PACE_DEMO_MS 160      /* one refresh, slowed */
#define PACE_DEMO_EVERY 4     /* Exact: one refresh in this many shows the frame before */
#define PACE_DEMO_STEPS 8     /* game frames to cross */
#define PACE_DEMO_TRAIL 5     /* the refreshes before, behind the ball */

/* The game frame refresh `r` shows */
static long pace_frame(int k, long r)
{
	return k == PACING_EXACT ? r - r / PACE_DEMO_EVERY : r;
}

static void pace_disc(SDL_Renderer *r, int cx, int cy, int d, ds_rgb c)
{
	ui_round_rect(r, &(SDL_Rect){ cx - d / 2, cy - d / 2, d, d }, d / 2, ui_col(c, 255));
}

static void sm_pace_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	const ds_theme *dk = theme_at(DS_DARK), *t = theme();
	const SDL_Rect screen = { x, y, w, h };
	/* A shot's moment: a repeat behind Exact's ball, neither wrapped */
	const Uint32 now = shot_path ? 7u * PACE_DEMO_MS + PACE_DEMO_MS / 2 : plat_now_ms();
	const long r = (long)(now / PACE_DEMO_MS);
	const SDL_Rect in = ds_preview_screen_begin(a->r, &screen);
	const int d = in.h / 5 > 8 ? in.h / 5 : 8, cy = in.y + in.h / 2;
	const int x0 = in.x + d, span = in.w - 2 * d;
	const long f_now = pace_frame(k, r) % PACE_DEMO_STEPS;
	int i;

	SDL_SetRenderDrawColor(a->r, dk->edge.r, dk->edge.g, dk->edge.b, 255);
	SDL_RenderFillRect(a->r, &(SDL_Rect){ x0, cy + d / 2 + DS_GRID, span, 2 });
	for (i = PACE_DEMO_TRAIL; i >= 0; i--) {
		const long ri = r - i;
		long f;
		int fx;

		if (ri < 0) continue;
		f = pace_frame(k, ri) % PACE_DEMO_STEPS;
		if (f > f_now) continue;   /* before the ball came round */
		fx = x0 + (int)(f * span / (PACE_DEMO_STEPS - 1));
		/* A refresh that showed the frame before again: ringed */
		if (ri > 0 && pace_frame(k, ri) == pace_frame(k, ri - 1)) {
			pace_disc(a->r, fx, cy, d + d / 2, t->accent);
			pace_disc(a->r, fx, cy, d + d / 2 - 6, dk->ink);
		}
		if (i == 0) pace_disc(a->r, fx, cy, d, t->accent);
		else pace_disc(a->r, fx, cy, d / 3, dk->text_dim);
	}
	ds_preview_screen_end(a->r, &screen);
	if (!shot_path) redraw_at(now + PACE_DEMO_MS - now % PACE_DEMO_MS);
}

/* Super Game Boy's list's picture (src/dmodepreview.h): each choice at the
 * shelf's own display mode, as its games would be drawn */
static void sm_sgb_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;

	dmode_sgb_preview(a, a->view[a->sys_cursor].dmode, k, x, y, w, h);
}

/* The Shader and Strength lists' picture (src/shaderdemo.h): a system's
 * menu with its own shader and strength, as the lists were made */
static int g_sm_demo_shader, g_sm_demo_strength;
static const char *g_sm_demo_tag;

static void sm_shader_preview(void *app_, int k, int x, int y, int w, int h)
{
	shader_demo_draw(app_, shader_at(k, g_sm_demo_tag), g_sm_demo_strength, x, y, w, h);
}

static void sm_strength_preview(void *app_, int k, int x, int y, int w, int h)
{
	shader_demo_draw(app_, g_sm_demo_shader, STRENGTH_MIN + k * STRENGTH_STEP, x, y, w, h);
}

/* MENU's Shader and Strength lists: the screens' own (src/screenfx.h), so
 * the cursor shows its choice on the whole screen, the list and the menu
 * under it too, as Theme's lists do (src/ds/hover.h): nothing saved until
 * A, the kept one back as the list closes. No picture: the screen is it.
 * g_pm_screen_at maps the list's places to SHADERS. */
static int g_pm_screen_at[SHADER_COUNT];

static void pm_shader_show(void *app_, int k)
{
	(void)app_;
	g_screen_shown = k >= 0 && k < SHADER_COUNT ? g_pm_screen_at[k] : -1;
	screen_shader_apply();
}

static void pm_strength_show(void *app_, int k)
{
	(void)app_;
	g_screen_shown_strength = STRENGTH_MIN + k * STRENGTH_STEP;
	screen_shader_apply();
}

static void pm_screen_undo(void *app_, int kept)
{
	(void)app_; (void)kept;
	g_screen_shown = g_screen_shown_strength = -1;
	screen_shader_apply();
}

static const ds_hover PM_SHADER_HOVER = { .show = pm_shader_show, .undo = pm_screen_undo };
static const ds_hover PM_STRENGTH_HOVER = { .show = pm_strength_show, .undo = pm_screen_undo };

/* MP4's Player Look (src/mp4look.h, DESIGN.md 11.64): each look's name
 * and what it is, and the list's picture, the look itself playing in the
 * small screen */
static const char *look_label(mp4l_id id)
{
	switch (id) {
	case MP4L_CASSETTE: return T(S_SYSMENU_LOOK_CASSETTE);
	case MP4L_VINYL:    return T(S_SYSMENU_LOOK_VINYL);
	case MP4L_CLASSIC:  return T(S_SYSMENU_LOOK_CLASSIC);
	case MP4L_SPECTRUM: return T(S_SYSMENU_LOOK_SPECTRUM);
	default:            return T(S_SYSMENU_LOOK_COVER);
	}
}

static const char *look_note(mp4l_id id)
{
	switch (id) {
	case MP4L_CASSETTE: return T(S_SYSMENU_LOOK_CASSETTE_NOTE);
	case MP4L_VINYL:    return T(S_SYSMENU_LOOK_VINYL_NOTE);
	case MP4L_CLASSIC:  return T(S_SYSMENU_LOOK_CLASSIC_NOTE);
	case MP4L_SPECTRUM: return T(S_SYSMENU_LOOK_SPECTRUM_NOTE);
	default:            return T(S_SYSMENU_LOOK_COVER_NOTE);
	}
}

static void smm_look_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	const SDL_Rect screen = { x, y, w, h };
	const SDL_Rect in = ds_preview_screen_begin(a->r, &screen);

	npl_preview(a, (mp4l_id)k, in.x, in.y, in.w, in.h);
	ds_preview_screen_end(a->r, &screen);
}

/* MP4's Repeat, videos' own mode (src/sys_menu.h): its values in the row's
 * order, Off first, which is how videos played before it */
static const muq_mode REPEAT_MODE[3] = { MUQ_IN_ORDER, MUQ_REPEAT_ONE, MUQ_REPEAT_ALL };
static const str_id REPEAT_TEXT[3] = { S_MENU_OFF_TITLE, S_SYSMENU_MP4_REPEAT_ONE,
                                       S_SYSMENU_MP4_REPEAT_ALL };

static int repeat_at(void)
{
	int i;

	for (i = 0; i < 3; i++)
		if (REPEAT_MODE[i] == mp4c_video_mode()) return i;
	return 0;
}

/* Rumble's list: the level under the cursor buzzes once as the cursor
 * comes onto it, so each is felt before it is chosen (the lab's) */
static void pm_rumble_focus(void *app_, int k)
{
	static int last = -1;

	(void)app_;
	if (k < 0 || k >= RUMBLE_LEVELS) { last = -1; return; }
	if (k == last) return;
	/* The list opening is no step: its first pass only notes where it is */
	if (last >= 0)
		plat_rumble_buzz((float)rumble_level_pct((rumble_level)k) / 100.0f, RUMBLE_BUZZ_MS);
	last = k;
}

/* The games menu's lists, each its own slot in its pool, so no two that one
 * pass builds share words: Recents' Card Picture and Border once both took
 * the thirteenth, and Card Picture's list said Border's Off and Blur
 * (1.1.0's test run). A new list takes a new name here. */
enum {
	PS_SHOW, PS_MSORT, PS_MVIEW, PS_LOOK, PS_REPEAT,   /* MP4's shelf */
	PS_PICTURE,                     /* Recents' Card Picture */
	PS_CORE, PS_DSLANG, PS_SGB, PS_PACE, PS_SORT, PS_CSORT, PS_DISPLAY, PS_BORDER, PS_BORDER_COLOR,
	PS_SHADER, PS_STRENGTH, PS_VIEW,
	PS_SLOTS
};

static void menu_choices(app *a, int sys, bool games)
{
	/* A system's menu and MENU each their own words: the shelf under MENU
	 * builds a system's rows too, and must not write over MENU's */
	static ch_pool PS[PS_SLOTS], PM[14];
	ch_pool *P = games ? PS : PM;
	int i, n, cur;

	memset(g_pm_choice, 0, sizeof g_pm_choice);
	memset(g_sm_choice, 0, sizeof g_sm_choice);
	memset(g_smm_choice, 0, sizeof g_smm_choice);
	memset(&g_pic_choice, 0, sizeof g_pic_choice);
	if (games && sys == shelf_recents_at(a)) {
		/* Two, so A would turn it over: its list opens all the same, the
		 * picture and a note telling them apart, as Timing's */
		static const char *notes[2];

		P[PS_PICTURE].lab[0] = T(S_SYSMENU_CARD_PICTURE_COVER);
		P[PS_PICTURE].lab[1] = T(S_SYSMENU_CARD_PICTURE_SCREEN);
		notes[0] = T(S_SYSMENU_CARD_PICTURE_COVER_NOTE);
		notes[1] = T(S_SYSMENU_CARD_PICTURE_SCREEN_NOTE);
		g_pic_choice = ch_make(&P[PS_PICTURE], 2, g_recents_screen ? 1 : 0, false);
		g_pic_choice.notes = notes;
		g_pic_choice.preview = sm_pic_preview;
		g_pic_choice.list = true;
	}
	if (games) {
		const system_cfg *sc = &a->sys.systems[sys];
		sysview *v = &a->view[sys];

		if (is_mp4(sc)) {
			/* Show: the kinds the card has, then All, round (ML_ALL is
			 * the number after the three) */
			static const int show_text[ML_ALL + 1] = { S_SYSMENU_MP4_MUSIC, S_SYSMENU_MP4_AUDIOBOOKS,
			                                           S_SYSMENU_MP4_VIDEOS, S_SYSMENU_MP4_ALL };

			for (i = 0, n = 0, cur = 0; i <= ML_ALL; i++) {
				if (!ml_count_kind(&g_mp4, (ml_kind)i)) continue;
				if (i == (int)mp4_kind_shown()) cur = n;
				P[PS_SHOW].lab[n++] = T(show_text[i]);
			}
			g_smm_choice[SMM_SHOW] = ch_make(&P[PS_SHOW], n, cur, false);
			for (i = 0; i < ML_ORDERS; i++) P[PS_MSORT].lab[i] = ml_order_label_kind((ml_order)i, mp4_kind_shown());
			g_smm_choice[SMM_SORT] = ch_make(&P[PS_MSORT], ML_ORDERS, v->sort, false);
			/* View: Covers, Grid, List, in START's order (view_turn); an
			 * album has no snap for a Video view */
			P[PS_MVIEW].lab[0] = T(S_SYSMENU_VIEW_COVERS);
			P[PS_MVIEW].lab[1] = T(S_SYSMENU_VIEW_GRID);
			P[PS_MVIEW].lab[2] = T(S_SYSMENU_VIEW_LIST);
			g_smm_choice[SMM_VIEW] = ch_make(&P[PS_MVIEW], 3, v->listed ? 2 : v->grid ? 1 : 0, false);
			{
				static const char *notes[MP4L_COUNT];

				for (i = 0; i < MP4L_COUNT; i++) {
					P[PS_LOOK].lab[i] = look_label((mp4l_id)i);
					notes[i] = look_note((mp4l_id)i);
				}
				g_smm_choice[SMM_LOOK] = ch_make(&P[PS_LOOK], MP4L_COUNT, np_look(), false);
				g_smm_choice[SMM_LOOK].notes = notes;
				g_smm_choice[SMM_LOOK].preview = smm_look_preview;
				g_smm_choice[SMM_LOOK].list = true;
			}
			for (i = 0; i < 3; i++) P[PS_REPEAT].lab[i] = T(REPEAT_TEXT[i]);
			g_smm_choice[SMM_REPEAT] = ch_make(&P[PS_REPEAT], 3, repeat_at(), false);
			return;
		}
		n = core_offered_count(sc);
		for (i = 0, cur = 0; i < n && i < CH_MAX; i++) {
			char core[CFG_STR];

			if (!core_offered_at(sc, i, core, sizeof core)) core[0] = '\0';
			if (!strcmp(core, sc->core)) cur = i;
			core_label(core, P[PS_CORE].buf[i], sizeof P[PS_CORE].buf[i]);
			P[PS_CORE].lab[i] = P[PS_CORE].buf[i];
		}
		if (n > 1) g_sm_choice[SM_CORE] = ch_make(&P[PS_CORE], n, cur, false);
		for (i = 0; i < DSL_CHOICES; i++)
			P[PS_DSLANG].lab[i] = dslang_label((dsl_choice)i, P[PS_DSLANG].buf[i], sizeof P[PS_DSLANG].buf[i]);
		g_sm_choice[SM_DSLANG] = ch_make(&P[PS_DSLANG], DSL_CHOICES, dslang_load(), false);
		{
			/* Super Game Boy's two, a list with a picture of each and a
			 * note telling them apart, as Timing's (src/sgbmode.h) */
			static const char *notes[SGBM_CHOICES];

			for (i = 0; i < SGBM_CHOICES; i++) {
				P[PS_SGB].lab[i] = T(sgbm_label_id((sgbm_choice)i));
				notes[i] = T(sgbm_note_id((sgbm_choice)i));
			}
			g_sm_choice[SM_SGB] = ch_make(&P[PS_SGB], SGBM_CHOICES, sgbm_load(sc), false);
			g_sm_choice[SM_SGB].notes = notes;
			g_sm_choice[SM_SGB].preview = sm_sgb_preview;
			g_sm_choice[SM_SGB].list = true;
		}
		for (i = 0; i < PACING_CHOICES; i++) P[PS_PACE].lab[i] = T(pacing_label_id((pacing_choice)i));
		{
			/* Two, so A would turn it over: its list opens all the same,
			 * the picture and a note telling them apart (DESIGN.md 9) */
			static const char *notes[PACING_CHOICES];

			notes[PACING_SMOOTH] = T(S_SYSMENU_PACE_SMOOTH_NOTE);
			notes[PACING_EXACT] = T(S_SYSMENU_PACE_EXACT_NOTE);
			g_sm_choice[SM_PACE] = ch_make(&P[PS_PACE], PACING_CHOICES, pacing_load(sc), false);
			g_sm_choice[SM_PACE].notes = notes;
			g_sm_choice[SM_PACE].preview = sm_pace_preview;
			g_sm_choice[SM_PACE].list = true;
		}
		{
			/* Collections has its own four, Added first (src/sort.h), in a
			 * pool of their own: the menu's width is measured over every
			 * shelf's rows, and the others' would be written over them */
			bool coll = sys == shelf_coll_at(a);
			ch_pool *sp = coll ? &P[PS_CSORT] : &P[PS_SORT];

			for (i = 0; i < sort_choices(coll); i++) sp->lab[i] = sort_label(sort_choice(coll, i));
			g_sm_choice[SM_SORT] = ch_make(sp, sort_choices(coll), sort_choice_of(coll, v->sort), false);
		}
		{
			static const char *notes[CH_MAX];

			for (i = 0; i < DMODE_COUNT && i < CH_MAX; i++) {
				P[PS_DISPLAY].lab[i] = dmode_label(i);
				notes[i] = dmode_note(i);
			}
			g_sm_choice[SM_DISPLAY] = ch_make(&P[PS_DISPLAY], DMODE_COUNT, v->dmode, false);
			g_sm_choice[SM_DISPLAY].notes = notes;
			g_sm_choice[SM_DISPLAY].preview = sm_dmode_preview;
		}
		if (border_row_value(a, sys)) {
			/* Border's offered kinds, each with its picture and note:
			 * only where the row is (no Favorites, Recents or Collections,
			 * whose games are many systems') */
			static const char *notes[BORDER_KINDS];
			border_kind kept = border_load(a, sys);

			n = border_offered(border_offer_for(a, sys), g_sm_border_kinds);
			for (i = 0, cur = 0; i < n; i++) {
				P[PS_BORDER].lab[i] = T(border_label_id(g_sm_border_kinds[i]));
				notes[i] = T(border_note_id(g_sm_border_kinds[i]));
				if (g_sm_border_kinds[i] == kept) cur = i;
			}
			g_sm_choice[SM_BORDER] = ch_make(&P[PS_BORDER], n, cur, false);
			g_sm_choice[SM_BORDER].notes = notes;
			g_sm_choice[SM_BORDER].preview = sm_border_preview;
			g_sm_choice[SM_BORDER].list = true;
		}
		{
			/* Handheld Color's colors, each with the frame in it */
			const hf_model *m = hf_model_for(sc->tag);

			if (m && m->ncolors > 1) {
				for (i = 0; i < m->ncolors && i < CH_MAX; i++)
					P[PS_BORDER_COLOR].lab[i] = border_color_label(m->color_name[i]);
				g_sm_choice[SM_BORDER_COLOR] = ch_make(&P[PS_BORDER_COLOR], m->ncolors, border_color_load(a, sys),
				                                       false);
				g_sm_choice[SM_BORDER_COLOR].preview = sm_color_preview;
			}
		}
		{
			static const char *snotes[CH_MAX], *tnotes[CH_MAX];

			g_sm_demo_shader = v->shader;
			g_sm_demo_strength = strength_clamp(v->strength);
			g_sm_demo_tag = a->sys.systems[a->sys_cursor].tag;
			n = ch_shaders(&P[PS_SHADER], v->shader, &cur, g_sm_demo_tag);
			g_sm_choice[SM_SHADER] = ch_make(&P[PS_SHADER], n, cur, false);
			shader_demo_notes(snotes, n, g_sm_demo_tag);
			g_sm_choice[SM_SHADER].notes = snotes;
			g_sm_choice[SM_SHADER].preview = sm_shader_preview;
			n = ch_strengths(&P[PS_STRENGTH], strength_clamp(v->strength), &cur);
			g_sm_choice[SM_STRENGTH] = ch_make(&P[PS_STRENGTH], n, cur, false);
			shader_demo_strength_notes(tnotes, n);
			g_sm_choice[SM_STRENGTH].notes = tnotes;
			g_sm_choice[SM_STRENGTH].preview = sm_strength_preview;
		}
		/* View: in the order Right steps it (view_turn) */
		P[PS_VIEW].lab[0] = T(S_SYSMENU_VIEW_COVERS);
		P[PS_VIEW].lab[1] = T(S_SYSMENU_VIEW_GRID);
		P[PS_VIEW].lab[2] = T(S_SYSMENU_VIEW_LIST);
		P[PS_VIEW].lab[3] = T(S_SYSMENU_VIEW_VIDEO);
		g_sm_choice[SM_VIEW] = ch_make(&P[PS_VIEW], VIEW_COUNT, view_index(v), false);
		return;
	}
	/* MENU */
	/* Light, Dark and Auto (back in 1.1.0), so A opens their list, each
	 * with what it does under it: Auto's says it is Night Mode's hours,
	 * which that page sets */
	{
		static const char *anotes[APPEARANCES];

		for (i = 0; i < APPEARANCES; i++) P[0].lab[i] = appearance_name((appearance)i);
		anotes[APPEAR_LIGHT] = T(S_THEME_LIGHT_NOTE);
		anotes[APPEAR_DARK] = T(S_THEME_DARK_NOTE);
		anotes[APPEAR_AUTO] = T(S_THEME_AUTO_NOTE);
		g_pm_choice[PM_APPEARANCE] = ch_make(&P[0], APPEARANCES, (int)theme_appearance(), false);
		g_pm_choice[PM_APPEARANCE].notes = anotes;
	}
	P[1].lab[BATT_BARS] = T(S_PANEL_BATTERY_BARS);
	P[1].lab[BATT_PCT] = T(S_PANEL_BATTERY_PERCENTAGE);
	P[1].lab[BATT_TIME] = T(S_PANEL_BATTERY_TIME);
	g_pm_choice[PM_BATTERY] = ch_make(&P[1], BATT_MODES, g_batt_mode, false);
	for (i = 0, cur = 0; i < CHARGE_LIMITS_COUNT; i++) {
		if (CHARGE_LIMITS[i] == g_charge_limit) cur = i;
		if (CHARGE_LIMITS[i] > 0) snprintf(P[2].buf[i], sizeof P[2].buf[i], "%d%%", CHARGE_LIMITS[i]);
		P[2].lab[i] = CHARGE_LIMITS[i] > 0 ? P[2].buf[i] : T(S_MENU_OFF_TITLE);
	}
	g_pm_choice[PM_CHARGE] = ch_make(&P[2], CHARGE_LIMITS_COUNT, cur, false);
	{
		/* The screens' shaders, in screen_shader_step's order, the note
		 * the same under each: what the row is for */
		static const char *snotes[CH_MAX], *tnotes[CH_MAX];
		int k;

		for (k = 0, n = 0, cur = 0; k < SHADER_COUNT && n < CH_MAX; k++) {
			if (!screen_shader_offered(k)) continue;
			if (k == g_screen_shader) cur = n;
			g_pm_screen_at[n] = k;
			snotes[n] = T(S_GAMEMENU_SHADER_SCREENS_NOTE);
			P[3].lab[n++] = shader_label(k);
		}
		g_pm_choice[PM_SHADER] = ch_make(&P[3], n, cur, false);
		g_pm_choice[PM_SHADER].notes = snotes;
		g_pm_choice[PM_SHADER].hover = &PM_SHADER_HOVER;
		n = ch_strengths(&P[4], g_screen_strength, &cur);
		g_pm_choice[PM_STRENGTH] = ch_make(&P[4], n, cur, false);
		shader_demo_strength_notes(tnotes, n);
		g_pm_choice[PM_STRENGTH].notes = tnotes;
		g_pm_choice[PM_STRENGTH].hover = &PM_STRENGTH_HOVER;
	}
	{
		aout_state ao = aout_now();
		aout_dest dest = aout_actual(&ao);

		for (i = 0; i < AOUT_CHANNELS_COUNT && i < CH_MAX; i++) {
			aout_channels_label((aout_channels)i, aout_channels_for((aout_channels)i, dest, plat_speaker_mono()),
			                    P[5].buf[i], sizeof P[5].buf[i]);
			P[5].lab[i] = P[5].buf[i];
		}
		g_pm_choice[PM_CHANNELS] = ch_make(&P[5], AOUT_CHANNELS_COUNT, (int)g_channels, false);
	}
	{
		/* The voices round from the first, as uisound_setting_step goes */
		int s0 = 0, s = s0, on = uisound_setting();

		for (n = 0, cur = 0; n < CH_MAX; ) {
			if (s == on) cur = n;
			P[6].lab[n++] = uisound_setting_name(s);
			s = uisound_setting_step(s, 1);
			if (s == s0) break;
		}
		g_pm_choice[PM_SOUNDS] = ch_make(&P[6], n, cur, false);
	}
	for (i = 0, cur = 0; i < AUTO_OFF_COUNT; i++) {
		if (AUTO_OFF[i] == a->auto_off) cur = i;
		sys_menu_auto_off_label(AUTO_OFF[i], P[7].buf[i], sizeof P[7].buf[i]);
		P[7].lab[i] = P[7].buf[i];
	}
	g_pm_choice[PM_SLEEP] = ch_make(&P[7], AUTO_OFF_COUNT, cur, false);
	{
		/* The screensaver's rungs, from its lowest up, as saver_step goes */
		int s = g_saver_s, t;

		while ((t = saver_step(s, -1)) != s) s = t;
		for (n = 0, cur = 0; n < CH_MAX; ) {
			if (s == g_saver_s) cur = n;
			sys_menu_saver_label(s, P[8].buf[n], sizeof P[8].buf[n]);
			P[8].lab[n] = P[8].buf[n];
			n++;
			if ((t = saver_step(s, 1)) == s) break;
			s = t;
		}
		g_pm_choice[PM_SAVER] = ch_make(&P[8], n, cur, false);
	}
	/* Home Screen: in the order Right steps it */
	for (i = 0; i < HOME_LAYOUTS; i++) P[9].lab[i] = home_name((home_layout)i);
	g_pm_choice[PM_HOME] = ch_make(&P[9], HOME_LAYOUTS, (int)a->home_layout, false);
	{
		/* Off to Full, each with what it does; felt as the cursor reaches
		 * it (pm_rumble_focus) */
		static const char *rnotes[RUMBLE_LEVELS];
		static char rbuf[RUMBLE_LEVELS][160];

		for (i = 0; i < RUMBLE_LEVELS; i++) {
			P[13].lab[i] = rumble_level_name((rumble_level)i);
			rumble_level_note((rumble_level)i, rbuf[i], sizeof rbuf[i]);
			rnotes[i] = rbuf[i];
		}
		g_pm_choice[PM_RUMBLE] = ch_make(&P[13], RUMBLE_LEVELS, (int)rumble_menu(), false);
		g_pm_choice[PM_RUMBLE].notes = rnotes;
		g_pm_choice[PM_RUMBLE].focus = pm_rumble_focus;
	}
	g_pm_choice[PM_LANGUAGE] = (menu_choice){ .n = LANG_COUNT, .cur = (unsigned char)i18n_lang(), .own = true };
}

int menu_build(app *a, screen_id screen, int sys,
               menu_row *out, menu_bufs *b, const char **heading)
{
	char ss[WIFI_SSID_MAX];
	sys_ui u = { 0 };

	/* Asked once. The cache is why - see menu_wifi - and three rows used to
	 * ask separately, which is how it got expensive in the first place. */
	u.wifi  = menu_wifi(ss, sizeof ss);
	u.ssid  = ss;
	u.games = screen == SCREEN_GAMES;
	u.no_wifi = !plat_has_wifi();
	u.no_bt   = !plat_has_bluetooth();
	/* USB File Transfer over the cable, or over a phone's tethering */
	u.cable_xfer = xfer_link_up();
	/* A way out to the internet with no radio: a phone's tethering */
	u.online = net_internet(NULL);
	u.no_developer = !dev_offered();
	u.developer  = dev_row_on();
	u.hotspot = hotspot_available();     /* two stats, no fork */
	/* File Transfer behind the shelf: its row says so, under its name */
	u.xfer = xs_row_value();
	u.xfer_cable = xs_row_cable();
	u.no_lights = !plat_battery_lights();
	u.no_rumble = !plat_has_rumble();
	u.no_screen_colors = !plat_screen_colors_here();
	u.no_charge_limit = !plat_charge_limit_supported();
	u.charge_limit = g_charge_limit;
	u.no_usb_power = !usb_power_offered();
	u.usb_auto = g_usb_auto;
	u.no_usb_wait = !usb_wait_row();
	u.tab = g_tabs.at;

	if (u.games) {
		const system_cfg *sc = &a->sys.systems[sys];

		static char corebuf[CFG_STR];

		u.sys_name   = sc->name;
		u.sys_core   = core_label(sc->core, corebuf, sizeof corebuf);
		u.core_choice = core_offered_count(sc) > 1;
		{
			static char dslbuf[64];

			u.dslang = dslang_row_shown(a, sys) ? dslang_label(dslang_load(), dslbuf, sizeof dslbuf) : NULL;
			u.pace = pacing_row_shown(a, sys) ? T(pacing_label_id(pacing_load(sc))) : NULL;
			u.sgb = sgb_row_shown(a, sys) ? T(sgbm_label_id(sgbm_load(sc))) : NULL;
			u.border = border_row_value(a, sys);
			u.border_color = u.border ? border_color_value(a, sys) : NULL;
		}
		/* By what it HAS, not by its name or its position: a shelf with no
		 * core and no folder is the built one, and a check can hand that
		 * over without inventing a flag day. */
		u.mp4       = is_mp4(sc);
		u.coll       = sys == shelf_coll_at(a);
		u.recents    = sys == shelf_recents_at(a);
		u.recents_screen = u.recents && g_recents_screen;
		u.fav        = !u.mp4 && !u.coll && sc->core[0] == '\0' && sc->folder[0] == '\0';
		if (u.coll) {
			const sysview *cv = &a->view[sys];

			u.coll_inside = cv->dir[0] != '\0';
			u.coll_name = u.coll_inside ? cv->dir
			            : cv->cursor >= 0 && cv->cursor < cv->list.count ? cv->list.items[cv->cursor].name : "";
		}
		/* Every game of the system, in its folders too: the shelf's own
		 * entries would count a folder as one. Not Splore's entry, nor
		 * PortMaster's while it is not on the card: no game */
		u.game_count = sys_games(&a->view[sys])->count - pm_shelf_offers(a, sys);
		{
			static char dlv[48];

			u.sys_downloads = downloads_menu_value(sc->folder, dlv, sizeof dlv);
		}
		u.dmode      = dmode_label(a->view[sys].dmode);
		u.shader     = shader_label(a->view[sys].shader);
		u.strength   = strength_row_label(a, sys);
		u.no_strength = !u.strength;
		u.listed     = a->view[sys].listed;
		u.grid       = a->view[sys].grid;
		u.video      = a->view[sys].video;
		u.mp4_books = u.mp4 && mp4_books_shown();
		u.mp4_videos = u.mp4 && mp4_kind_shown() == ML_VIDEOS;
		u.mp4_all   = u.mp4 && mp4_kind_shown() == ML_ALL;
		u.mp4_songs = u.mp4 && ml_count_kind(&g_mp4, ML_MUSIC) > 0;
		u.mp4_both  = u.mp4 && mp4_kinds() > 1;
		u.mp4_playing = u.mp4 && lock_allowed();
		u.mp4_look  = u.mp4 ? look_label(np_look()) : NULL;
		u.mp4_repeat = u.mp4 ? T(REPEAT_TEXT[repeat_at()]) : NULL;
		u.eq         = eq_preset_name(eq_preset_get(eq_out_now()));
		u.sort       = u.mp4 ? ml_order_label_kind((ml_order)a->view[sys].sort, mp4_kind_shown())
		                      : sort_label(a->view[sys].sort);
	} else {
		aout_state ao = aout_now();

		u.ra_in     = ra_signed_in();
		u.ra_name   = u.ra_in ? ra_user() : NULL;
		/* A check the page started may have ended after it closed */
		sgdb_try_poll();
		/* Downloads: how far a run behind the screens has got, else what
		 * the Scraper's page counted as it closed */
		{
			static char dlv[48];

			u.scraper = downloads_menu_value(NULL, dlv, sizeof dlv);
		}
		{
			static char upv[48];
			u.update = up_have() ? su_menu_value(up_view(false), upv, sizeof upv) : NULL;
		}
		u.storage = storage_menu_value();
		u.appearance = appearance_name(theme_appearance());
		u.shader_screens = shader_label(g_screen_shader);
		u.strength_screens = screen_strength_label();
		u.no_strength = !screen_strength_shown();
		u.no_shader = !plat_gpu_shaders();
		{
			u.no_night = !plat_night_here();
			u.night = nm_mode_name(nm_mode());
			/* A table load a key's repeat held back (platform.h), made
			 * once its gap is over: this build runs every pass */
			plat_screen_colors_tick(false);
		}
		u.theme = look_accent_name();
		u.screen_colors = screen_colors_value();
		u.home = home_name(a->home_layout);
		u.order = g_order_release ? T(S_PANEL_ORDER_RELEASE) : T(S_PANEL_ORDER_AZ);
		{
			static char hs[24];

			u.homesys = homesys_value(hs, sizeof hs);
		}
		u.battery = g_batt_mode == BATT_TIME ? T(S_PANEL_BATTERY_TIME)
		          : g_batt_mode == BATT_PCT ? T(S_PANEL_BATTERY_PERCENTAGE) : T(S_PANEL_BATTERY_BARS);
		u.ui_sounds  = uisound_setting_name(uisound_setting());
		u.menu_music = bgm_menu_value(bgm_on(), bgm_count());
		u.rumble     = rumble_level_name(rumble_menu());
		u.eq         = eq_preset_name(eq_preset_get(eq_out_now()));
		u.auto_off  = a->auto_off;
		u.power_hold = powerhold_mode();
		u.saver     = g_saver_s;
		{
			static char lv[16];
			u.lights = u.no_lights ? NULL : side_lights_value(plat_side_lights(), lv, sizeof lv);
		}
		{
			static char clk[32];
			time_t t = time(NULL);

			/* In a shot, --status's time or none, as the corner's */
			if (shot_time(&t)) clock_label(t, g_clock24, clk, sizeof clk);
			else               clk[0] = '\0';
			u.clock = clk[0] ? clk : NULL;
		}
		u.clock_24h = g_clock24;
		u.audio_policy = ao.policy;
		u.audio_dest   = aout_actual(&ao);
		u.audio_channels = g_channels;
		u.audio_mix    = aout_channels_for(g_channels, u.audio_dest, plat_speaker_mono());

		/* The connected headset's name.
		 *
		 * Cached for two seconds, the same way the Wi-Fi row is and for the
		 * same reason: this is rebuilt every menu frame and the answer costs
		 * a fork. The first version read /tmp/zyperos_btsink to avoid that,
		 * which is what made this row say "not connected" while the Bluetooth
		 * screen said the opposite - launch.sh writes that file on a
		 * twenty-second poll. One source for both now. */
		{
			static char btname[BT_NAME_MAX];
			static unsigned next_bt;
			unsigned now = plat_now_ms();

			if (next_bt == 0 || now >= next_bt) {
				bt_device bd[BT_MAX];
				int nb = bt_bonded(bd, BT_MAX), i;

				next_bt = now + 2000;
				btname[0] = '\0';
				if (nb > 0 && bt_status() == BT_READY) {
					bt_mark_connected_now(bd, nb);
					for (i = 0; i < nb; i++)
						if (bd[i].connected) {
							snprintf(btname, sizeof btname, "%s", bd[i].name);
							break;
						}
				}
			}
			u.bt_name = btname[0] ? btname : NULL;
		}
	}
	menu_choices(a, sys, u.games);
	u.pm_choice = g_pm_choice;
	u.sm_choice = g_sm_choice;
	u.smm_choice = g_smm_choice;
	u.pic_choice = &g_pic_choice;
	return sys_menu_build(&u, out, b, heading);
}

/* ---------- MENU > Display: Theme (DESIGN.md 3.5, 11.42) ------------------ */

/* The volume and brightness bar the launcher draws (src/osdbar.c) in the
 * look's colors, or the tokens' for the look as it ships */
void look_bar_colors(void)
{
	int t;

	for (t = 0; t < DS_THEMES; t++)
		osdbar_colors(t, look_is_default(look_now()) ? NULL : theme_at(t));
}

/* The look and Auto's hours as the database keeps them (src/look.h,
 * src/theme.h): read at start before theme_init, and written as a row
 * changes. Words for the look; Auto's hours are the Night Hours
 * (night.from, night.until: src/nightkeep.h), which Night Mode's page
 * changes. */
void look_load(void)
{
	char ac[16], li[16], dk[16], co[16], zy[8], fo[16], hl[16];
	look l;

	db_get_str(db_dev(), "look.accent", ac, sizeof ac, DS_ACCENT_ID[0]);
	db_get_str(db_dev(), "look.light", li, sizeof li, DS_TONE_ID[DS_LIGHT][0]);
	db_get_str(db_dev(), "look.dark", dk, sizeof dk, DS_TONE_ID[DS_DARK][0]);
	db_get_str(db_dev(), "look.corners", co, sizeof co, "rounded");
	db_get_str(db_dev(), "look.zyper", zy, sizeof zy, "on");
	db_get_str(db_dev(), "look.folders", fo, sizeof fo, "system");
	db_get_str(db_dev(), "look.focus", hl, sizeof hl, "bar");
	l = look_parse(ac, li, dk, co, zy);
	l.folder = look_folder_parse(fo);
	l.focus = look_focus_parse(hl);
	look_set(&l);
	look_bar_colors();
	{
		const night_cfg n = night_load();

		night_theme_hours(&n);
	}
}

/* A new look: on, kept, and every screen told. The bar's other programs read
 * it from the theme file theme_init writes (themefile.h), so that is written
 * again too; cards and marks drawn in the old colors are drawn again. */
static void look_change(app *a, const look *l)
{
	look_set(l);
	look_bar_colors();
	db_set_str(db_dev(), "look.accent", DS_ACCENT_ID[l->accent]);
	db_set_str(db_dev(), "look.light", DS_TONE_ID[DS_LIGHT][l->tone[DS_LIGHT]]);
	db_set_str(db_dev(), "look.dark", DS_TONE_ID[DS_DARK][l->tone[DS_DARK]]);
	db_set_str(db_dev(), "look.corners", l->square ? "square" : "rounded");
	db_set_str(db_dev(), "look.zyper", l->zyper_off ? "off" : "on");
	db_set_str(db_dev(), "look.folders", look_folder_word(l->folder));
	db_set_str(db_dev(), "look.focus", look_focus_word(l->focus));
	theme_init(theme_appearance());
	theme_changed(a);
}

/* A time of day, `m` minutes in, as MENU > Clock writes one: "7:00 AM",
 * or "07:00" in 24-hour */
static void time_of_day(int m, char *out, size_t n)
{
	int h = m / 60, mi = m % 60;

	if (g_clock24) snprintf(out, n, "%02d:%02d", h, mi);
	else snprintf(out, n, T(S_LOOK_TIME_12H), h % 12 ? h % 12 : 12, mi,
	              T(h < 12 ? S_CLOCK_AM : S_CLOCK_PM));
}

/* ---- Night Mode (src/nightmode.h, DESIGN.md 11.42) ----------------------
 * Warmer colors for the night, over the player's Screen Colors: Off, On or
 * By Hours (MENU > Display > Night Mode), how much (Night Warmth, 1 to 10)
 * and when (Night Hours: Night From and Until, by the half hour), where the
 * display has a table (plat_night_here). night_keep saves a change and puts
 * it on the display at once, with one frame's blink as every table change;
 * Night Warmth's list shows the warmth under its cursor on the whole screen,
 * as the Look list does, and B puts back what was set. Tarik's move,
 * approved on the sheet 2026-11-16. */
#define NM_STEP NIGHT_HOURS_STEP

/* The settings, read once, as Screen Colors' are (90e438b): MENU's build
 * asks for them every pass of its loop, and only these rows change them
 * while the launcher runs */
static night_cfg g_nm;
static bool g_nm_have;

static night_cfg *nm_cfg(void)
{
	if (!g_nm_have) {
		g_nm = night_load();   /* put right (night_cfg_clamp) */
		g_nm_have = true;
	}
	return &g_nm;
}

static int nm_mode(void)
{
	return (int)nm_cfg()->mode;
}

/* A row's change: kept, and on the display at once */
static void nm_keep(void)
{
	night_keep(nm_cfg());
}

/* Night Warmth's list: the warmth under the cursor on the display alone,
 * loaded once a move (a list's build runs every pass), and the load a
 * key's repeat held back made once its gap is over; the list closed, what
 * is kept back on it */
static int g_nm_shown;

static void nm_warmth_focus(void *app_, int k)
{
	const int w = k < 0 ? 0 : NM_WARMTH_MIN + k;

	(void)app_;
	if (w != g_nm_shown) {
		g_nm_shown = w;
		plat_night_preview(w);
	}
	plat_screen_colors_tick(false);
}

/* Night Mode's words for a mode: Off, On or By Hours */
static const char *nm_mode_name(int m)
{
	return m == NIGHT_ON ? T(S_MENU_ON_TITLE) : m == NIGHT_HOURS ? T(S_SYSMENU_NIGHT_BY_HOURS)
	     : T(S_MENU_OFF_TITLE);
}

/* MENU > Display's Night Mode row as MENU's width measures it (main.c's
 * menu_shelf_width), into out[0]: at its longest mode, so the panel holds
 * its width as the mode changes. 0 where there is no Night Mode (the
 * Brick). */
int night_row_widest(menu_row *out)
{
	TTF_Font *f = ui_font(UI_F_MENU);
	int i, w, best = -1, at = 0;

	if (!plat_night_here()) return 0;
	for (i = 0; i < NM_MODES; i++)
		if ((w = ui_text_width(f, nm_mode_name(i))) > best) best = w, at = i;
	out[0] = (menu_row){ T(S_SYSMENU_NIGHT_MODE), nm_mode_name(at), true };
	return 1;
}

/* MENU > Display > Night Mode's page (DESIGN.md 11.42; Tarik's for 1.0.6,
 * "the night mode settings all in the night mode row"): Night Mode, Night
 * Warmth, Night From and Until on one page, as Screen Colors has its own.
 * Each is an option row (DESIGN.md 9): A opens its list, Left and Right
 * step it, and a change is kept and on the display at once. Night Warmth's
 * list shows the warmth under its cursor on the whole screen. A row the
 * mode does not use is quiet, its value still read - Night Warmth and the
 * hours while Off, the hours while On - so the page holds its shape and
 * says what On or By Hours would use. The hours are Appearance Auto's too
 * (src/theme.h): with Appearance on Auto they are never quiet, whatever the
 * mode, since the theme turns by them. */
static const unsigned char NIGHT_SOUND[NP_ROWS] = {
	[NP_MODE] = MENU_SND_RING,
	/* Stops at 1 and 10, as Strength */
	[NP_WARMTH] = MENU_SND_NO_A,
	[NP_FROM] = MENU_SND_RING, [NP_UNTIL] = MENU_SND_RING,
};

/* The rows' choices (menu.h's menu_choice), as night_page_key steps them,
 * so A opens their list: the modes, the warmths, the day's half hours */
static menu_choice g_np_choice[NP_ROWS];

int night_page_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static const char *mode[NM_MODES], *warm_lab[NM_WARMTH_MAX], *hour_lab[24 * 60 / NM_STEP];
	static char warm_buf[NM_WARMTH_MAX][4], hour_buf[24 * 60 / NM_STEP][24];
	_Static_assert(24 * 60 / NM_STEP <= MENU_CHOICES_MAX, "Night From's list");
	night_view *v = ctx;
	const night_cfg *c = nm_cfg();
	const int m = nm_mode(), hours = 24 * 60 / NM_STEP;
	/* By Hours uses them, and Appearance's Auto whatever the mode */
	const bool timed = m == NIGHT_HOURS || theme_appearance() == APPEAR_AUTO;
	int i;

	(void)max;
	plat_screen_colors_tick(false);   /* a held-back load (platform.h): this runs every pass */
	for (i = 0; i < NM_MODES; i++) mode[i] = nm_mode_name(i);
	for (i = 0; i < NM_WARMTH_MAX; i++) {
		snprintf(warm_buf[i], sizeof warm_buf[i], "%d", NM_WARMTH_MIN + i);
		warm_lab[i] = warm_buf[i];
	}
	for (i = 0; i < hours; i++) {
		time_of_day(i * NM_STEP, hour_buf[i], sizeof hour_buf[i]);
		hour_lab[i] = hour_buf[i];
	}
	snprintf(v->warmth, sizeof v->warmth, "%d", c->warmth);
	time_of_day(c->from, v->from, sizeof v->from);
	time_of_day(c->until, v->until, sizeof v->until);
	*heading = T(S_SYSMENU_NIGHT_MODE);
	rows[NP_MODE]   = (menu_row){ T(S_SYSMENU_NIGHT_MODE), mode[m], true };
	rows[NP_WARMTH] = (menu_row){ T(S_SYSMENU_NIGHT_WARMTH), v->warmth, m != NIGHT_OFF };
	rows[NP_FROM]   = (menu_row){ T(S_LOOK_NIGHT_FROM), v->from, timed };
	rows[NP_UNTIL]  = (menu_row){ T(S_LOOK_NIGHT_UNTIL), v->until, timed };
	g_np_choice[NP_MODE] = (menu_choice){ .n = NM_MODES, .cur = (unsigned char)m, .labels = mode };
	g_np_choice[NP_WARMTH] = (menu_choice){ .n = NM_WARMTH_MAX, .cur = (unsigned char)(c->warmth - NM_WARMTH_MIN),
	                                        .labels = warm_lab, .focus = nm_warmth_focus };
	g_np_choice[NP_FROM] = (menu_choice){ .n = (unsigned char)hours, .cur = (unsigned char)(c->from / NM_STEP),
	                                      .labels = hour_lab };
	g_np_choice[NP_UNTIL] = (menu_choice){ .n = (unsigned char)hours, .cur = (unsigned char)(c->until / NM_STEP),
	                                       .labels = hour_lab };
	return NP_ROWS;
}

const menu_choice *night_page_choices(void)
{
	return g_np_choice;
}

/* Night Mode round Off, On and By Hours; Night Warmth on Strength's idiom,
 * a step past either end changing nothing; the hours round the day by the
 * half hour. Kept, and on the display at once, so a mode or an hour that
 * turns the warmth on or off shows; an hour that turns Appearance's Auto
 * turns the theme at once too, and is the boot screen's (its words carry
 * Auto's hours, src/boottheme.h). */
static menu_result night_page_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;
	night_cfg *c = nm_cfg();
	const int day = 24 * 60;

	(void)ctx;
	if (sel == NP_MODE && (d || key == IN_ACCEPT)) {
		c->mode = (night_mode)((nm_mode() + (d ? d : 1) + NM_MODES) % NM_MODES);
	} else if (sel == NP_WARMTH && d) {
		int k = clampi(c->warmth + d, NM_WARMTH_MIN, NM_WARMTH_MAX);

		if (k == c->warmth) return MENU_STAY;
		c->warmth = k;
	} else if ((sel == NP_FROM || sel == NP_UNTIL) && (d || key == IN_ACCEPT)) {
		int *t = sel == NP_FROM ? &c->from : &c->until;

		*t = ((*t + (d ? d : 1) * NM_STEP) % day + day) % day;
		nm_keep();   /* and Auto's hours (night_keep) */
		boot_theme_changed();
		if (theme_refresh()) theme_changed(a);
		return MENU_CHANGED;
	} else {
		return MENU_STAY;
	}
	nm_keep();
	return MENU_CHANGED;
}

static void night_page(app *a)
{
	night_view v = { .a = a };
	menu_style st = { .fixed_w = menu_std_width(a), .accent = MENU_ZYPEROS,
	                  .sound = NIGHT_SOUND, .choices = g_np_choice };

	menu_run(a, &st, night_page_build, night_page_key, &v);
}

/* ---- Screen Colors (src/panelgamma.h) -----------------------------------
 * The whole screen's colors, in the display's own table and its BCSH block
 * (Saturation): five rows, each SCREEN_COLORS_RANGE either way of 0, the
 * frame as drawn, and the look, a ready-made set of all five (Custom once a
 * row is moved by hand). A row's step is applied and kept at once, so the
 * page itself shows it: one frame's blink, as every table change, and at
 * most one every SCREEN_COLORS_GAP_MS while a key repeats (platform.h). The
 * Look's list shows the look under its cursor on the whole screen as the
 * cursor moves, A keeps it and B puts back what was set. Screenshots never
 * show it, being taken before the table. Kept in the device's database (the
 * five "screen_" settings), which plat_settings_init reads at every start. */
enum { SC_LOOK, SC_TEMP, SC_TINT, SC_CONTRAST, SC_SAT, SC_GAMMA, SC_ROWS };

/* The looks, by their place in the list and on Left and Right; the values in
 * platform.h's order: temp, tint, contrast, saturation, gamma (the ranges
 * sheet Tarik approved, 2026-11-16) */
typedef struct { int name, note; screen_colors c; } sc_look_def;
static const sc_look_def SC_LOOKS[] = {
	{ S_SCREEN_STANDARD, S_SCREEN_STANDARD_NOTE, {  0, 0,  0,  0,  0 } },
	{ S_SCREEN_VIVID,    S_SCREEN_VIVID_NOTE,    {  0, 0,  3,  3, -2 } },
	{ S_SCREEN_INTENSE,  S_SCREEN_INTENSE_NOTE,  {  0, 0,  6,  6, -3 } },
	{ S_SCREEN_WARM,     S_SCREEN_WARM_NOTE,     {  3, 0,  0,  1,  0 } },
	{ S_SCREEN_COOL,     S_SCREEN_COOL_NOTE,     { -3, 0,  1,  0,  0 } },
	{ S_SCREEN_MUTED,    S_SCREEN_MUTED_NOTE,    {  0, 0, -2, -4,  1 } },
};
#define SC_LOOKS_N ((int)(sizeof SC_LOOKS / sizeof SC_LOOKS[0]))
#define SC_CUSTOM SC_LOOKS_N   /* not a look: what the rows make that none is */

/* The kept colors, read from the database once: the page's build asks for
 * them every pass of its loop, and MENU > Display's row every pass of
 * MENU's, and only sc_keep changes them while the launcher runs */
static screen_colors g_sc_kept;
static bool g_sc_have;

static screen_colors sc_load(void)
{
	const int r = SCREEN_COLORS_RANGE;

	if (!g_sc_have) {
		g_sc_kept.temp = clampi(db_get_int(db_dev(), "screen_temp", 0), -r, r);
		g_sc_kept.tint = clampi(db_get_int(db_dev(), "screen_tint", 0), -r, r);
		g_sc_kept.contrast = clampi(db_get_int(db_dev(), "screen_contrast", 0), -r, r);
		g_sc_kept.saturation = clampi(db_get_int(db_dev(), "screen_saturation", 0), -r, r);
		g_sc_kept.gamma = clampi(db_get_int(db_dev(), "screen_gamma", 0), -r, r);
		g_sc_have = true;
	}
	return g_sc_kept;
}

static int sc_look_of(const screen_colors *c)
{
	int k;

	for (k = 0; k < SC_LOOKS_N; k++)
		if (!memcmp(&SC_LOOKS[k].c, c, sizeof *c)) return k;
	return SC_CUSTOM;
}

/* MENU > Display's value for the row: the look */
static const char *screen_colors_value(void)
{
	screen_colors c = sc_load();
	int k = sc_look_of(&c);

	return T(k == SC_CUSTOM ? S_SCREEN_CUSTOM : SC_LOOKS[k].name);
}

/* The Look list's cursor on the display: its look, not the kept one, until
 * the list is left (sc_side puts the kept one back) or one is kept */
static bool g_sc_previewing;
static int g_sc_preview_k = -1;

static void sc_keep(const screen_colors *c)
{
	db_set_int(db_dev(), "screen_temp", c->temp);
	db_set_int(db_dev(), "screen_tint", c->tint);
	db_set_int(db_dev(), "screen_contrast", c->contrast);
	db_set_int(db_dev(), "screen_saturation", c->saturation);
	db_set_int(db_dev(), "screen_gamma", c->gamma);
	g_sc_kept = *c;
	g_sc_have = true;
	g_sc_previewing = false;
	g_sc_preview_k = -1;
	plat_screen_colors(c, true);
}

/* The kept colors back on the display after a preview */
static void sc_preview_end(void)
{
	screen_colors c;

	if (!g_sc_previewing) return;
	c = sc_load();
	g_sc_previewing = false;
	g_sc_preview_k = -1;
	plat_screen_colors(&c, false);
}

/* The Look list's picture: the color chart in a screen (src/ds/colorref.h),
 * and on the whole screen the look under the cursor, once a move (the list
 * draws this every frame), so the chart shows it as the panel makes it */
static void sc_look_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	const SDL_Rect screen = { x, y, w, h };
	SDL_Rect in;

	if (k != g_sc_preview_k) {
		screen_colors c = k >= 0 && k < SC_LOOKS_N ? SC_LOOKS[k].c : sc_load();

		g_sc_preview_k = k;
		g_sc_previewing = true;
		plat_screen_colors(&c, false);
	}
	plat_screen_colors_tick(false);
	in = ds_preview_screen_begin(a->r, &screen);
	ds_color_ref_draw(a->r, &(SDL_Rect){ in.x + DS_GRID, in.y + DS_GRID, in.w - 2 * DS_GRID, in.h - 2 * DS_GRID });
	ds_preview_screen_end(a->r, &screen);
}

/* The Look's list as the page opens it and the shots draw it: every look,
 * Custom last while it is the one on, with what each does */
void sc_look_choice(menu_choice *out)
{
	static const char *labels[SC_LOOKS_N + 1], *notes[SC_LOOKS_N + 1];
	screen_colors c = sc_load();
	int cur = sc_look_of(&c), k;

	for (k = 0; k < SC_LOOKS_N; k++) {
		labels[k] = T(SC_LOOKS[k].name);
		notes[k] = T(SC_LOOKS[k].note);
	}
	labels[SC_CUSTOM] = T(S_SCREEN_CUSTOM);
	notes[SC_CUSTOM] = T(S_SCREEN_CUSTOM_NOTE);
	*out = (menu_choice){ .n = (unsigned char)(SC_LOOKS_N + (cur == SC_CUSTOM)), .cur = (unsigned char)cur,
	                      .labels = labels, .notes = notes, .preview = sc_look_preview };
}

/* The rows by place: Saturation only where the kernel has its knob */
static unsigned char g_sc_row[SC_ROWS];
static int g_sc_n;
static const unsigned char SC_SOUND[SC_ROWS] = {
	MENU_SND_RING, MENU_SND_NO_A, MENU_SND_NO_A, MENU_SND_NO_A, MENU_SND_NO_A, MENU_SND_NO_A,
};
static menu_choice g_sc_choice[SC_ROWS];
static menu_style g_sc_style;

int sc_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static char val[SC_ROWS][DS_SCALE_VALUE];
	static const ds_scale_kind KIND[SC_ROWS] = { DS_SCALE_KINDS, DS_SCALE_TEMPERATURE, DS_SCALE_TINT,
	                                             DS_SCALE_CONTRAST, DS_SCALE_SATURATION, DS_SCALE_GAMMA };
	const int row_name[SC_ROWS] = { S_SCREEN_LOOK, S_SCREEN_TEMPERATURE, S_SCREEN_TINT,
	                                S_SCREEN_CONTRAST, S_SCREEN_SATURATION, S_SCREEN_GAMMA };
	screen_colors c = sc_load();
	const int v[SC_ROWS] = { 0, c.temp, c.tint, c.contrast, c.saturation, c.gamma };
	int look = sc_look_of(&c), id, n = 0;

	(void)ctx; (void)max;
	*heading = T(S_SCREEN_TITLE);
	for (id = 0; id < SC_ROWS; id++) {
		const char *value;

		if (id == SC_SAT && !plat_screen_saturation_here()) continue;
		if (id == SC_LOOK) {
			value = T(look == SC_CUSTOM ? S_SCREEN_CUSTOM : SC_LOOKS[look].name);
			/* A list of its own on A: the page opens it, with the whole
			 * screen's preview (sc_look_list) */
			g_sc_choice[n] = (menu_choice){ .n = (unsigned char)(SC_LOOKS_N + (look == SC_CUSTOM)),
			                                .cur = (unsigned char)look, .own = true };
		} else {
			/* A graded bar with its signed number (src/ds/scale.h) */
			value = ds_scale_value(val[id], KIND[id], v[id], SCREEN_COLORS_RANGE);
			g_sc_choice[n] = (menu_choice){ 0 };
		}
		g_sc_row[n] = (unsigned char)id;
		rows[n++] = (menu_row){ T(row_name[id]), value, true };
	}
	g_sc_n = n;
	return n;
}

/* A on the Look row: its list, the whole screen showing the look under the
 * cursor; A keeps it, B (or the menu closing) puts back what was set */
static menu_result sc_look_list(app *a)
{
	menu_choice ch;
	int k;

	sc_look_choice(&ch);
	g_sc_preview_k = ch.cur;   /* what the display shows already */
	k = menu_pick_preview(a, &g_sc_style, NULL, T(S_SCREEN_LOOK), ch.labels, ch.notes, ch.preview,
	                      ch.n, ch.cur, ch.cur);
	if (k >= 0 && k < SC_LOOKS_N) {
		sc_keep(&SC_LOOKS[k].c);
	} else {
		g_sc_previewing = true;
		sc_preview_end();
	}
	return k == MENU_PICK_POWER ? MENU_DONE : MENU_CHANGED;
}

static menu_result sc_key(app *a, void *ctx, in_button key, int sel)
{
	const int r = SCREEN_COLORS_RANGE;
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;
	int id = sel >= 0 && sel < g_sc_n ? g_sc_row[sel] : -1;
	screen_colors c = sc_load(), was = c;

	(void)ctx;
	if (id == SC_LOOK && key == IN_ACCEPT) return sc_look_list(a);
	if (!d || id < 0) return MENU_STAY;
	switch (id) {
	case SC_LOOK: {
		int k = sc_look_of(&c);

		/* Round the looks; from Custom, Right to the first and Left to the
		 * last */
		k = k == SC_CUSTOM ? (d > 0 ? 0 : SC_LOOKS_N - 1) : (k + d + SC_LOOKS_N) % SC_LOOKS_N;
		c = SC_LOOKS[k].c;
		break;
	}
	/* The rows stop at their ends */
	case SC_TEMP:     c.temp = clampi(c.temp + d, -r, r); break;
	case SC_TINT:     c.tint = clampi(c.tint + d, -r, r); break;
	case SC_CONTRAST: c.contrast = clampi(c.contrast + d, -r, r); break;
	case SC_SAT:      c.saturation = clampi(c.saturation + d, -r, r); break;
	case SC_GAMMA:    c.gamma = clampi(c.gamma + d, -r, r); break;
	}
	if (!memcmp(&c, &was, sizeof c)) return MENU_STAY;
	sc_keep(&c);
	return MENU_CHANGED;
}

/* The page's column: the color chart (src/ds/colorref.h) down to the panel's
 * foot. Every frame, so a table load held back for the gap is made here, and
 * a preview left by a list that closed is put back. */
void sc_side(app *a, const menu_side *at)
{
	const int h = at->panel.y + at->panel.h - menu_pad() - at->content_y;

	sc_preview_end();
	plat_screen_colors_tick(false);
	if (at->stage == MENU_SIDE_OVER) return;
	ds_color_ref_draw(a->r, &(SDL_Rect){ at->content_x, at->content_y, SC_SIDE_W - DS_GRID * 4, h });
}

static void screen_colors_screen(app *a)
{
	g_sc_style = (menu_style){ .fixed_w = menu_most_w(), .accent = MENU_ZYPEROS,
	                           .sound = SC_SOUND, .choices = g_sc_choice,
	                           .side = sc_side, .side_w = SC_SIDE_W };
	menu_run(a, &g_sc_style, sc_build, sc_key, NULL);
	/* Out of the page: a preview gone and a load held back made, so what
	 * the display shows is what is kept */
	sc_preview_end();
	plat_screen_colors_tick(true);
}

/* ---- MENU > Sound > Equalizer (src/eq.h, DESIGN.md 11.60) ----------------- */

/* The output whose preset the page shows: where the system's sound goes */
/* --eq-phones: a shot's headphones, for the computer has no jack */
bool g_shot_eq_phones;

static eq_out eq_out_now(void)
{
	aout_state s = aout_now();

	if (shot_path && g_shot_eq_phones) return EQ_OUT_PHONES;
	return eq_out_of(aout_resolve(&s));
}

/* The list's cursor, heard before it is chosen (-1: the list is shut) */
static int g_eq_pv_k = -1;

static void eq_preset_bands(eq_out o, int k, int8_t db[EQ_BANDS])
{
	if (k == EQ_CUSTOM) eq_custom_get(o, db);
	else                memcpy(db, EQ_CURVE[k >= 0 && k < EQ_PRESETS ? k : EQ_OFF], EQ_BANDS);
}

/* A preset's picture, the lab's: its five gains joined by a line in the
 * accent over the 0 dB line, a dot on each, the bands left to right as the
 * page lists them (their names are the page's rows: a name at the menu's
 * least size would not fit five across). And the preset under the cursor
 * heard at once, on the output in use. */
void eq_curve_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	const SDL_Rect screen = { x, y, w, h };
	const ds_theme *t = theme();
	const eq_out o = eq_out_now();
	int8_t db[EQ_BANDS];
	SDL_Rect in;
	int px[EQ_BANDS], py[EQ_BANDS], b, mid, span;

	eq_preset_bands(o, k, db);
	if (k != g_eq_pv_k) {
		g_eq_pv_k = k;
		eq_route_preview(o, db);
	}
	in = ds_preview_screen_begin(a->r, &screen);
	mid = in.y + in.h / 2;
	span = in.h / 2 - DS_GRID * 4;
	for (b = 0; b < EQ_BANDS; b++) {
		px[b] = in.x + in.w * (2 * b + 1) / (2 * EQ_BANDS);
		py[b] = mid - db[b] * span / EQ_DB_MAX;
	}
	SDL_SetRenderDrawColor(a->r, t->edge.r, t->edge.g, t->edge.b, 255);
	SDL_RenderFillRect(a->r, &(SDL_Rect){ in.x + DS_GRID * 2, mid - 1, in.w - DS_GRID * 4, 2 });
	SDL_SetRenderDrawColor(a->r, t->accent.r, t->accent.g, t->accent.b, 255);
	for (b = 0; b + 1 < EQ_BANDS; b++) {
		int d;

		/* Four units thick, the lab's stroke */
		for (d = -2; d < 2; d++)
			SDL_RenderDrawLine(a->r, px[b], py[b] + d, px[b + 1], py[b + 1] + d);
	}
	for (b = 0; b < EQ_BANDS; b++)
		ui_round_rect(a->r, &(SDL_Rect){ px[b] - 7, py[b] - 7, 14, 14 }, 7, ui_col(t->accent, 255));
	ds_preview_screen_end(a->r, &screen);
}

/* The page's rows by place: the preset, then the five bands. What plays
 * goes on behind the panel (MP4's Now Playing, from its menu); six rows
 * fill the panel, so there is no strip of its own under them. */
#define EQ_PAGE_ROWS (1 + EQ_BANDS)
static const unsigned char EQ_SOUND[EQ_PAGE_ROWS] = {
	MENU_SND_RING,
	/* The bands stop at -6 and +6, heard by whether they moved */
	MENU_SND_NO_A, MENU_SND_NO_A, MENU_SND_NO_A, MENU_SND_NO_A, MENU_SND_NO_A,
};
static menu_choice g_eq_choice[EQ_PAGE_ROWS];
static menu_style g_eq_style;

int eq_page_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static char val[EQ_BANDS][DS_SCALE_VALUE], name[EQ_BANDS][16];
	const eq_out o = eq_out_now();
	int8_t db[EQ_BANDS];
	int b, n = 0;

	(void)ctx;
	if (max < EQ_PAGE_ROWS) return 0;
	*heading = T(S_SYSMENU_EQUALIZER);
	eq_bands(o, db);
	/* The preset of the output in use: its own list on A, with each
	 * preset's picture (eq_preset_list) */
	g_eq_choice[n] = (menu_choice){ .n = EQ_PRESETS, .cur = (unsigned char)eq_preset_get(o), .own = true };
	rows[n++] = (menu_row){ eq_out_name(o), eq_preset_name(eq_preset_get(o)), true };
	for (b = 0; b < EQ_BANDS; b++) {
		eq_band_name(b, name[b], sizeof name[b]);
		g_eq_choice[n] = (menu_choice){ 0 };
		rows[n++] = (menu_row){ name[b], ds_scale_value(val[b], DS_SCALE_GAIN, db[b], EQ_DB_MAX), true };
	}
	return n;
}

/* The presets' list as the page opens it and the shots draw it */
void eq_preset_choice(menu_choice *out)
{
	static const char *labels[EQ_PRESETS], *notes[EQ_PRESETS];
	int k;

	for (k = 0; k < EQ_PRESETS; k++) {
		labels[k] = eq_preset_name((eq_preset)k);
		notes[k] = eq_preset_note((eq_preset)k);
	}
	*out = (menu_choice){ .n = EQ_PRESETS, .cur = (unsigned char)eq_preset_get(eq_out_now()),
	                      .labels = labels, .notes = notes, .preview = eq_curve_preview };
}

/* A on the preset row: the presets, each heard as the cursor reaches it
 * (eq_curve_preview); A keeps one, B puts back what was set */
static menu_result eq_preset_list(app *a)
{
	menu_choice ch;
	const eq_out o = eq_out_now();
	int k;

	eq_preset_choice(&ch);
	g_eq_pv_k = -1;
	k = menu_pick_preview(a, &g_eq_style, NULL, eq_out_name(o), ch.labels, ch.notes, ch.preview,
	                      ch.n, ch.cur, ch.cur);
	g_eq_pv_k = -1;
	eq_route_preview(o, NULL);
	if (k >= 0 && k < EQ_PRESETS) eq_preset_set(o, (eq_preset)k);
	eq_route_changed();
	return k == MENU_PICK_POWER ? MENU_DONE : MENU_CHANGED;
}

static menu_result eq_page_key(app *a, void *ctx, in_button key, int sel)
{
	const int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;
	const eq_out o = eq_out_now();

	(void)ctx;
	if (sel == 0 && key == IN_ACCEPT) return eq_preset_list(a);
	if (!d) return MENU_STAY;
	if (sel == 0) {
		/* The presets round, as Audio Channels' choices go */
		eq_preset_set(o, (eq_preset)((eq_preset_get(o) + d + EQ_PRESETS) % EQ_PRESETS));
		eq_route_changed();
		return MENU_CHANGED;
	}
	if (sel >= 1 && sel <= EQ_BANDS && eq_band_step(o, sel - 1, d)) {
		eq_route_changed();
		return MENU_CHANGED;
	}
	return MENU_STAY;
}

/* The page, from MENU and from MP4's menu: what plays goes on playing
 * behind it, MP4 keeping little queued so each step is heard at once */
void eq_screen(app *a)
{
	g_eq_style = (menu_style){ .fixed_w = menu_most_w(), .accent = MENU_ZYPEROS,
	                           .sound = EQ_SOUND, .choices = g_eq_choice };
	eq_route_live(true);
	aout_apply(false);
	menu_run(a, &g_eq_style, eq_page_build, eq_page_key, NULL);
	eq_route_live(false);
	eq_route_preview(eq_out_now(), NULL);
	aout_apply(false);
}

/* A theme's chip, 64 by 36: its ground in its edge, its accent on it, so
 * the row says what the theme is now (DESIGN.md 11.42) */
static void look_chip(app *a, const ds_theme *t, int x, int cy)
{
	SDL_Rect c = { x, cy - 18, 64, 36 };

	ui_round_rect(a->r, &c, DS_RADIUS_CHIP + 1, ui_col(t->edge, 255));
	ui_round_rect(a->r, &(SDL_Rect){ c.x + 3, c.y + 3, c.w - 6, c.h - 6 }, DS_RADIUS_CHIP - 2,
	              ui_col(t->ground, 255));
	ui_round_rect(a->r, &(SDL_Rect){ c.x + 12, cy - 6, 40, 12 }, 6, ui_col(t->accent, 255));
}

/* Three systems' colors for System Colors' pictures: the shelf the menu is
 * over first, then the next ones with a color of their own, each once; a
 * card with fewer has its colors again, and one with none the folders'
 * text-dim (CFG_NO_COLOR) */
static void look_system_rgbs(const app *a, unsigned out[3])
{
	int n = 0, i, k;

	for (i = 0; i < a->sys.count && n < 3; i++) {
		const unsigned c = a->sys.systems[(a->sys_cursor + i) % a->sys.count].accent;
		bool seen = c == CFG_NO_COLOR;

		for (k = 0; k < n && !seen; k++) seen = out[k] == c;
		if (!seen) out[n++] = c;
	}
	for (k = n; k < 3; k++) out[k] = n ? out[k % n] : CFG_NO_COLOR;
}

/* Folder Color's choice `k` as three folders' colors: System Colors' three
 * systems, or the one color three times */
static void look_folder_rgbs(const app *a, int k, unsigned out[3])
{
	if (k <= 0 || k >= LOOK_FOLDERS) {
		look_system_rgbs(a, out);
		return;
	}
	out[0] = out[1] = out[2] = ui_rgb(DS_ACCENT[k - 1]);
}

/* A folder, small, drawn from shapes as the chips are: its back with the
 * tab at 74% of the color, its front the color, its top edge lit, all in a
 * 2-unit edge at 42% as the pixel folder's (src/folderart.h), `w` wide with
 * its foot at `foot`, its left at `x` */
static void look_folder_shape(app *a, unsigned rgb, int x, int foot, int w)
{
	const int h = w * 3 / 4, top = foot - h, r = DS_RADIUS_CHIP - 2, e = 2;
	SDL_Color c, back, edge, lit;

	if (rgb == CFG_NO_COLOR) rgb = ui_rgb(theme()->text_dim);
	c = (SDL_Color){ (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255 };
	back = (SDL_Color){ (Uint8)(c.r * 74 / 100), (Uint8)(c.g * 74 / 100), (Uint8)(c.b * 74 / 100), 255 };
	edge = (SDL_Color){ (Uint8)(c.r * 42 / 100), (Uint8)(c.g * 42 / 100), (Uint8)(c.b * 42 / 100), 255 };
	lit = (SDL_Color){ (Uint8)(c.r + (255 - c.r) * 35 / 100), (Uint8)(c.g + (255 - c.g) * 35 / 100),
	                   (Uint8)(c.b + (255 - c.b) * 35 / 100), 255 };
	/* The edge: the tab and the body, each 2 units larger */
	ui_round_rect(a->r, &(SDL_Rect){ x - e, top - e, w * 9 / 20 + 2 * e, h / 3 + 2 * e }, r + e, edge);
	ui_round_rect(a->r, &(SDL_Rect){ x - e, top + h / 7 - e, w + 2 * e, h - h / 7 + 2 * e }, r + e, edge);
	ui_round_rect(a->r, &(SDL_Rect){ x, top, w * 9 / 20, h / 3 }, r, back);
	ui_round_rect(a->r, &(SDL_Rect){ x, top + h / 7, w, h - h / 7 }, r, back);
	ui_round_rect(a->r, &(SDL_Rect){ x, top + h / 3, w, h - h / 3 }, r, c);
	ui_round_rect(a->r, &(SDL_Rect){ x + r, top + h / 3, w - 2 * r, 2 }, 0, lit);
}

/* Folder Color's mark: one folder in the color every folder takes, or for
 * System Colors three of the card's systems' folders stacked, the shelf's
 * in front and the others standing up behind it */
static void look_folder_mark(app *a, int right, int cy)
{
	const int folder = look_now()->folder, w = 38, foot = cy + w * 3 / 8;
	unsigned rgb[3];
	int i;

	look_folder_rgbs(a, folder, rgb);
	if (folder > 0) {
		look_folder_shape(a, rgb[0], right - w, foot, w);
		return;
	}
	for (i = 2; i >= 0; i--)
		look_folder_shape(a, rgb[i], right - w - i * DS_GRID, foot - i * DS_GRID * 3 / 4, w);
}

/* Folder Color's list's picture: a shelf in small, the theme's ground and
 * three folders as the shelf draws them (src/folderart.h), the middle one in
 * focus and larger, in the colors choice `k` gives them */
static void look_folder_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	const SDL_Rect screen = { x, y, w, h };
	const ds_rgb g = theme()->ground;
	unsigned rgb[3];
	SDL_Rect in;
	int big = 3, gap = DS_GRID * 2, foot, cx, i;

	look_folder_rgbs(a, k, rgb);
	in = ds_preview_screen_begin(a->r, &screen);
	SDL_SetRenderDrawColor(a->r, g.r, g.g, g.b, 255);
	SDL_RenderFillRect(a->r, &in);
	/* The middle folder's cells three units, the others' two, or less
	 * where the column is narrow */
	while (big > 1 && (2 * (big - 1) + big) * FA_ICON_W + 2 * gap > in.w) big--;
	foot = in.y + (in.h + big * FA_ICON_H) / 2;
	cx = in.x + in.w / 2;
	for (i = 0; i < 3; i++) {
		const int s = i == 1 ? big : (big > 1 ? big - 1 : 1);
		const int fx = i == 1 ? cx - big * FA_ICON_W / 2
		             : i == 0 ? cx - big * FA_ICON_W / 2 - gap - s * FA_ICON_W
		                      : cx + big * FA_ICON_W / 2 + gap;
		int tw, th;
		SDL_Texture *t = ui_make_folder_icon(a->r, rgb[i == 1 ? 0 : i == 0 ? 1 : 2], &tw, &th);

		if (!t) continue;
		/* The ones beside the focus faded, as the shelf's are */
		if (i != 1) SDL_SetTextureAlphaMod(t, 150);
		SDL_RenderCopy(a->r, t, NULL, &(SDL_Rect){ fx, foot - s * th, s * tw, s * th });
		SDL_DestroyTexture(t);
	}
	ds_preview_screen_end(a->r, &screen);
}

/* Highlight's picture (DESIGN.md 11.42): three of the home's systems, the
 * one in focus and one either side, on the theme's ground, the middle one
 * marked as the choice under the cursor marks it and the others a step
 * back - the home Grid's own tiles, small. The list shows the choice on the
 * whole screen too, but the screen under it is MENU: the picture is where
 * the mark is seen. */
static void look_focus_preview(void *app_, int k, int x, int y, int w, int h)
{
	app *a = app_;
	const SDL_Rect screen = { x, y, w, h };
	const ds_rgb g = theme()->ground;
	home_item all[CFG_MAX_SYSTEMS], three[3];
	const int n = a->sys.count, c = a->sys_cursor;
	SDL_Rect in;

	home_items(a, all);
	for (int i = 0; i < 3; i++)
		three[i] = n > 0 ? all[((c + i - 1) % n + n) % n] : (home_item){ "", "", 0 };
	in = ds_preview_screen_begin(a->r, &screen);
	SDL_SetRenderDrawColor(a->r, g.r, g.g, g.b, 255);
	SDL_RenderFillRect(a->r, &in);
	home_focus_preview(&a->home, a->r, three, in, k);
	ds_preview_screen_end(a->r, &screen);
}

/* The marks before Theme's values: the accent's dot on Accent, each theme's
 * chip on its background's row, the folders on Folder Color, and both
 * chips on MENU's Theme row */
void look_row_mark(app *a, const menu_row *row, int right, int cy, float on_fill)
{
	const char *l = row->label;

	(void)on_fill;
	if (l == T(S_LOOK_FOLDERS)) look_folder_mark(a, right, cy);
	else if (l == T(S_LOOK_BG_LIGHT)) look_chip(a, theme_at(DS_LIGHT), right - 64, cy);
	else if (l == T(S_LOOK_BG_DARK)) look_chip(a, theme_at(DS_DARK), right - 64, cy);
	else if (l == T(S_SYSMENU_THEME)) {
		look_chip(a, theme_at(DS_DARK), right - 64, cy);
		look_chip(a, theme_at(DS_LIGHT), right - 64 - DS_GRID - 64, cy);
	} else if (l == T(S_LOOK_ACCENT)) {
		const ds_rgb ac = DS_ACCENT[look_now()->accent];

		ui_round_rect(a->r, &(SDL_Rect){ right - 34, cy - 17, 34, 34 }, 17,
		              ui_col(theme()->edge, 255));
		ui_round_rect(a->r, &(SDL_Rect){ right - 32, cy - 15, 30, 30 }, 15,
		              ui_col(theme()->surface, 255));
		ui_round_rect(a->r, &(SDL_Rect){ right - 28, cy - 11, 22, 22 }, 11,
		              (SDL_Color){ ac.r, ac.g, ac.b, 255 });
	}
}

/* One theme, small: the home with a menu over it, drawn from shapes (text is
 * never under 37 units), in that theme as the look has it now. */
static void look_thumb(app *a, const ds_theme *t, const SDL_Rect *q)
{
	SDL_Renderer *r = a->r;
	const int rad = 16;
	int mx = q->x + q->w * 48 / 100, my = q->y + 12, mw = q->w * 48 / 100, mh = q->h - 24;
	int bx = q->x + 16, i;
#define BAR(x, y, w, h, c) ui_round_rect(r, &(SDL_Rect){ (x), (y), (w), (h) }, (h) / 2, ui_col((c), 255))

	ui_round_rect(r, q, rad, ui_col(t->edge, 255));
	ui_round_rect(r, &(SDL_Rect){ q->x + 2, q->y + 2, q->w - 4, q->h - 4 }, rad - 2,
	              ui_col(t->ground, 255));
	/* the home: the system's name and count, A, the art, the dock and its
	 * mark under the one in focus */
	BAR(bx, q->y + 22, q->w * 22 / 100, 13, t->text);
	BAR(bx, q->y + 44, q->w * 13 / 100, 8, t->text_dim);
	ui_round_rect(r, &(SDL_Rect){ bx, q->y + 60, 16, 16 }, 8, ui_col(t->accent, 255));
	BAR(bx + 22, q->y + 64, q->w * 11 / 100, 8, t->text_soft);
	ui_round_rect(r, &(SDL_Rect){ q->x + q->w * 27 / 100, q->y + 14, q->w * 16 / 100, q->w * 12 / 100 },
	              6, ui_col(t->raised, 255));
	/* the dock's mark as Highlight has it (src/ds/focus.h): Bar's bar,
	 * Border's ring, Lift's rise on a shadow, None's step back alone */
	{
		const int f = look_focus(), s = q->w / 14, fx = bx + 2 * (q->w / 10), fy = q->y + q->h - 46;

		for (i = 0; i < 4; i++) {
			SDL_Rect d = { bx + i * (q->w / 10), fy, s, s };

			if (i == 2 && f == LOOK_FOCUS_LIFT) {
				ui_round_rect(r, &(SDL_Rect){ d.x + 1, d.y + 2, s, s }, 4, ui_col(t->ink, 70));
				d.y -= 3;
			}
			ui_round_rect(r, &d, 4, ui_col(i == 2 ? t->text_soft : t->text_dim, i == 2 || f != LOOK_FOCUS_NONE ? 255 : 150));
		}
		if (f == LOOK_FOCUS_BAR) BAR(fx, q->y + q->h - 14, s, 4, t->accent_ink);
		if (f == LOOK_FOCUS_BORDER)
			ds_round_ring(r, (SDL_Rect){ fx - 5, fy - 5, s + 10, s + 10 }, (SDL_Rect){ fx - 2, fy - 2, s + 4, s + 4 },
			              9, 6, ui_col(t->accent_ink, 255));
	}
	/* a menu over it: its heading, rule and bar, three rows, one in focus */
	ui_round_rect(r, &(SDL_Rect){ mx, my, mw, mh }, 12, ui_col(t->edge, 255));
	ui_round_rect(r, &(SDL_Rect){ mx + 2, my + 2, mw - 4, mh - 4 }, 10, ui_col(t->surface, 255));
	BAR(mx + mw / 2 - mw / 6, my + 12, mw / 3, 8, t->text_soft);
	ui_round_rect(r, &(SDL_Rect){ mx + 10, my + 28, mw - 20, 2 }, 0, ui_col(t->edge, 255));
	BAR(mx + mw / 2 - mw / 6, my + 27, mw / 3, 4, t->accent_ink);
	BAR(mx + 14, my + 40, mw * 35 / 100, 8, t->text_soft);
	BAR(mx + mw - 14 - mw / 5, my + 40, mw / 5, 8, t->text_dim);
	ui_round_rect(r, &(SDL_Rect){ mx + 6, my + 56, mw - 12, 22 }, 6, ui_col(t->accent, 255));
	BAR(mx + 14, my + 63, mw * 40 / 100, 8, t->on_accent);
	BAR(mx + 14, my + mh - 18, mw * 30 / 100, 8, t->text_soft);
	BAR(mx + mw - 14 - mw / 4, my + mh - 18, mw / 4, 8, t->text_dim);
	/* Zyper in its corner, a dot at this size, while he is on */
	if (!look_zyper_off())
		ui_round_rect(r, &(SDL_Rect){ mx + mw - 22, my + 7, 12, 12 }, 6, ui_col(t->text_dim, 255));
#undef BAR
}

/* Theme's picture: Beard and Salt & Pepper as the look has them, side by
 * side, Light at the left whatever is on */
void look_header(app *a, const SDL_Rect *q)
{
	int gap = DS_GRID * 3, w = (q->w - gap) / 2;

	look_thumb(a, theme_at(DS_LIGHT), &(SDL_Rect){ q->x, q->y, w, q->h });
	look_thumb(a, theme_at(DS_DARK), &(SDL_Rect){ q->x + w + gap, q->y, w, q->h });
}

static const unsigned char LOOK_SOUND[LK_ROWS] = {
	MENU_SND_RING, MENU_SND_RING, MENU_SND_RING, MENU_SND_RING, MENU_SND_RING, MENU_SND_RING,
	MENU_SND_RING, 0 };
static const int ACCENT_NAME[DS_ACCENTS] = {
	S_LOOK_ACCENT_LIME, S_LOOK_ACCENT_GREEN, S_LOOK_ACCENT_TEAL, S_LOOK_ACCENT_BLUE,
	S_LOOK_ACCENT_PURPLE, S_LOOK_ACCENT_PINK, S_LOOK_ACCENT_RED, S_LOOK_ACCENT_ORANGE,
	S_LOOK_ACCENT_YELLOW, S_LOOK_ACCENT_SILVER };
static const int TONE_NAME[DS_THEMES][DS_TONES] = {
	{ S_LOOK_TONE_WARM, S_LOOK_TONE_NEUTRAL, S_LOOK_TONE_COOL },
	{ S_LOOK_TONE_BLACK, S_LOOK_TONE_DEEPBLUE, S_LOOK_TONE_DARKGREY } };

static const char *look_accent_name(void)
{
	return T(ACCENT_NAME[look_now()->accent]);
}

/* Folder Color's value: System Colors, or the accent's name */
static const char *look_folder_name(int folder)
{
	return folder > 0 && folder < LOOK_FOLDERS ? T(ACCENT_NAME[folder - 1]) : T(S_LOOK_FOLDERS_SYSTEM);
}

/* Theme's four lists (src/ds/hover.h): the accent, the ground or the
 * folders' color under the cursor on the whole screen, in memory (look_set,
 * no setting and no theme file); a ground of the theme that is not on shows
 * that theme while its list is open. The look as it was, back on undo. */
static look g_lk_kept;
static bool g_lk_showing;

static void look_show(app *a, int row, int k)
{
	look l;

	if (!g_lk_showing) {
		g_lk_kept = *look_now();
		g_lk_showing = true;
	}
	l = g_lk_kept;
	if (row == LK_ACCENT && k >= 0 && k < DS_ACCENTS) l.accent = k;
	if (row == LK_LIGHT && k >= 0 && k < DS_TONES) l.tone[DS_LIGHT] = k;
	if (row == LK_DARK && k >= 0 && k < DS_TONES) l.tone[DS_DARK] = k;
	if (row == LK_FOLDERS && k >= 0 && k < LOOK_FOLDERS) l.folder = k;
	if (row == LK_HIGHLIGHT && k >= 0 && k < LOOK_FOCUSES) l.focus = k;
	look_set(&l);
	theme_show(row == LK_LIGHT ? DS_LIGHT : row == LK_DARK ? DS_DARK : -1);
	theme_changed(a);
}

static void look_undo(void *app_, int kept)
{
	(void)kept;
	if (!g_lk_showing) return;
	g_lk_showing = false;
	look_set(&g_lk_kept);
	theme_show(-1);
	theme_changed(app_);
}

static void accent_show(void *app_, int k) { look_show(app_, LK_ACCENT, k); }
static void light_show(void *app_, int k) { look_show(app_, LK_LIGHT, k); }
static void dark_show(void *app_, int k) { look_show(app_, LK_DARK, k); }
static void folder_show(void *app_, int k) { look_show(app_, LK_FOLDERS, k); }
static void focus_show(void *app_, int k) { look_show(app_, LK_HIGHLIGHT, k); }

static const ds_hover LK_ACCENT_HOVER = { .show = accent_show, .undo = look_undo };
static const ds_hover LK_LIGHT_HOVER = { .show = light_show, .undo = look_undo };
static const ds_hover LK_DARK_HOVER = { .show = dark_show, .undo = look_undo };
static const ds_hover LK_FOLDER_HOVER = { .show = folder_show, .undo = look_undo };
static const ds_hover LK_FOCUS_HOVER = { .show = focus_show, .undo = look_undo };

/* Highlight's choices, their words and what each does (DESIGN.md 3.5) */
static const int FOCUS_NAME[LOOK_FOCUSES] = {
	S_LOOK_FOCUS_BAR, S_LOOK_FOCUS_BORDER, S_LOOK_FOCUS_LIFT, S_LOOK_FOCUS_NONE };
static const int FOCUS_NOTE[LOOK_FOCUSES] = {
	S_LOOK_FOCUS_BAR_NOTE, S_LOOK_FOCUS_BORDER_NOTE, S_LOOK_FOCUS_LIFT_NOTE, S_LOOK_FOCUS_NONE_NOTE };

/* Theme's rows' choices (menu.h's menu_choice): the accents, each theme's
 * grounds and the folders' colors, as look_key steps them, so A opens
 * their list; Folder Color's with its picture, since a folder is nowhere
 * on the list itself */
static menu_choice g_lk_choice[LK_ROWS];

const menu_choice *theme_choices(void)
{
	return g_lk_choice;
}

int look_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	static const char *acc[DS_ACCENTS], *tone[DS_THEMES][DS_TONES], *fold[LOOK_FOLDERS];
	static const char *foc[LOOK_FOCUSES], *focn[LOOK_FOCUSES];
	const look *l = look_now();
	int i, t;

	(void)ctx; (void)max;
	for (i = 0; i < DS_ACCENTS; i++) acc[i] = T(ACCENT_NAME[i]);
	for (i = 0; i < LOOK_FOLDERS; i++) fold[i] = look_folder_name(i);
	for (i = 0; i < LOOK_FOCUSES; i++) { foc[i] = T(FOCUS_NAME[i]); focn[i] = T(FOCUS_NOTE[i]); }
	for (t = 0; t < DS_THEMES; t++)
		for (i = 0; i < DS_TONES; i++) tone[t][i] = T(TONE_NAME[t][i]);
	memset(g_lk_choice, 0, sizeof g_lk_choice);
	g_lk_choice[LK_ACCENT] = (menu_choice){ .n = DS_ACCENTS, .cur = (unsigned char)l->accent, .labels = acc,
	                                        .hover = &LK_ACCENT_HOVER };
	g_lk_choice[LK_LIGHT] = (menu_choice){ .n = DS_TONES, .cur = (unsigned char)l->tone[DS_LIGHT],
	                                       .labels = tone[DS_LIGHT], .hover = &LK_LIGHT_HOVER };
	g_lk_choice[LK_DARK] = (menu_choice){ .n = DS_TONES, .cur = (unsigned char)l->tone[DS_DARK],
	                                      .labels = tone[DS_DARK], .hover = &LK_DARK_HOVER };
	g_lk_choice[LK_FOLDERS] = (menu_choice){ .n = LOOK_FOLDERS, .cur = (unsigned char)l->folder,
	                                         .labels = fold, .preview = look_folder_preview,
	                                         .hover = &LK_FOLDER_HOVER };
	g_lk_choice[LK_HIGHLIGHT] = (menu_choice){ .n = LOOK_FOCUSES, .cur = (unsigned char)l->focus,
	                                           .labels = foc, .notes = focn, .preview = look_focus_preview,
	                                           .hover = &LK_FOCUS_HOVER };
	*heading = T(S_SYSMENU_THEME);
	rows[LK_ACCENT]  = (menu_row){ T(S_LOOK_ACCENT), T(ACCENT_NAME[l->accent]), true };
	rows[LK_LIGHT]   = (menu_row){ T(S_LOOK_BG_LIGHT), T(TONE_NAME[DS_LIGHT][l->tone[DS_LIGHT]]), true };
	rows[LK_DARK]    = (menu_row){ T(S_LOOK_BG_DARK), T(TONE_NAME[DS_DARK][l->tone[DS_DARK]]), true };
	rows[LK_FOLDERS] = (menu_row){ T(S_LOOK_FOLDERS), look_folder_name(l->folder), true };
	rows[LK_CORNERS] = (menu_row){ T(S_LOOK_CORNERS), T(l->square ? S_LOOK_SQUARE : S_LOOK_ROUNDED), true };
	rows[LK_HIGHLIGHT] = (menu_row){ T(S_LOOK_HIGHLIGHT), T(FOCUS_NAME[l->focus]), true };
	rows[LK_ZYPER]   = (menu_row){ T(S_LOOK_ZYPER), T(l->zyper_off ? S_MENU_OFF_TITLE : S_MENU_ON_TITLE), true };
	rows[LK_RESET]   = (menu_row){ T(S_LOOK_RESET), NULL, true };
	return LK_ROWS;
}

static menu_result look_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT || key == IN_ACCEPT ? 1 : key == IN_LEFT ? -1 : 0;
	look l = *look_now();

	(void)ctx;
	if (!d || sel < 0 || sel >= LK_ROWS) return MENU_STAY;
	switch (sel) {
	case LK_ACCENT:  l.accent = (l.accent + d + DS_ACCENTS) % DS_ACCENTS; break;
	case LK_LIGHT:   l.tone[DS_LIGHT] = (l.tone[DS_LIGHT] + d + DS_TONES) % DS_TONES; break;
	case LK_DARK:    l.tone[DS_DARK] = (l.tone[DS_DARK] + d + DS_TONES) % DS_TONES; break;
	case LK_FOLDERS: l.folder = (l.folder + d + LOOK_FOLDERS) % LOOK_FOLDERS; break;
	case LK_CORNERS: l.square = !l.square; break;
	case LK_HIGHLIGHT: l.focus = (l.focus + d + LOOK_FOCUSES) % LOOK_FOCUSES; break;
	case LK_ZYPER:   l.zyper_off = !l.zyper_off; break;
	case LK_RESET:
		if (key != IN_ACCEPT) return MENU_STAY;
		l = LOOK_DEFAULT;
		break;
	}
	look_change(a, &l);
	return MENU_CHANGED;
}

static void look_screen(app *a)
{
	menu_style st = { .fixed_w = menu_std_width(a), .accent = MENU_ZYPEROS,
	                  .sound = LOOK_SOUND, .choices = g_lk_choice, .row_mark = look_row_mark,
	                  .header = look_header, .header_h = 150 };

	menu_run(a, &st, look_build, look_key, NULL);
}

menu_result sysmenu_key(app *a, void *ctx, in_button key, int sel)
{
	int d = key == IN_RIGHT ? 1 : key == IN_LEFT ? -1 : 0;

	(void)ctx;

	if (a->screen == SCREEN_GAMES) {
		/* MP4's menu is its own rows - see SM_MP4_ROWS - and which is
		 * where depends on what its shelf shows, so the enum's row numbers
		 * below mean nothing on it: sys_menu_mp4_rows says. Sort By goes on
		 * to the Sort By below like any shelf's. Album Art is a dead row off
		 * the network, and the runner does not land on dead rows. */
		if (is_mp4(&a->sys.systems[a->sys_cursor])) {
			sm_mp4_row ids[SM_MP4_ROWS];
			int n = sys_menu_mp4_rows(mp4_rows_kind(), mp4_kinds() > 1,
			                           !plat_has_wifi() && !net_internet(NULL), ids);
			sm_mp4_row id = sel >= 0 && sel < n ? ids[sel] : SMM_COUNT;

			/* Quit, or X from any row (DESIGN.md 11.14): closes the menu,
			 * and MP4 after it, as the in-game menu's Quit and its X. A is
			 * heard from the runner; X, which it leaves alone, here. */
			if (key == IN_X || (id == SMM_QUIT && key == IN_ACCEPT)) {
				if (key == IN_X) uisound_play(DS_SOUND_ACCEPT);
				mp4_ask_quit();
				return MENU_DONE;
			}
			if (id == SMM_SORT) {
				sel = SM_SORT;
			} else if (id == SMM_VIEW) {
				/* Covers, Grid, List, round either way and on A as a
				 * system's View, kept (view_turn) */
				if (d || key == IN_ACCEPT) view_turn(a, a->sys_cursor, d < 0 ? -1 : 1);
				return MENU_STAY;
			} else if (id == SMM_EQ) {
				/* MENU's Equalizer: the presets round, A its page, over
				 * what plays */
				const eq_out o = eq_out_now();

				if (key == IN_ACCEPT) {
					eq_screen(a);
					return MENU_STAY;
				}
				if (!d) return MENU_STAY;
				eq_preset_set(o, (eq_preset)((eq_preset_get(o) + d + EQ_PRESETS) % EQ_PRESETS));
				eq_route_changed();
				return MENU_STAY;
			} else if (id == SMM_LOCK) {
				/* Closes the menu; the screen underneath locks. */
				if (key != IN_ACCEPT) return MENU_STAY;
				mp4_ask_lock();
				return MENU_DONE;
			} else if (id == SMM_REPEAT) {
				/* Off, One, All, round either way (A opens their list,
				 * menu_choose), applied to videos playing now and kept */
				if (d) {
					int i = (repeat_at() + d + 3) % 3;

					mp4c_set_video_mode(REPEAT_MODE[i]);
					db_set_str(db_dev(), "video.mode", MP4_MODES[REPEAT_MODE[i]].key);
				}
				return MENU_STAY;
			} else if (id == SMM_SHUFFLE) {
				/* Closes the menu; Now Playing opens on the first song. */
				if (key != IN_ACCEPT) return MENU_STAY;
				mp4_ask_shuffle();
				return MENU_DONE;
			} else if (id == SMM_LOOK) {
				/* Round the looks either way, kept at once; A opens their
				 * list, the picture the look under the cursor
				 * (menu_choose) */
				if (d) np_look_set((mp4l_id)((np_look() + d + MP4L_COUNT) % MP4L_COUNT));
				return MENU_STAY;
			} else if (id == SMM_SHOW) {
				/* All, then the kinds the card has, round in either
				 * direction (A opens their list, menu_choose). ML_ALL is the
				 * number after the three, so the round is All, Music,
				 * Audiobooks, Videos. */
				if (d || key == IN_ACCEPT) {
					int k = mp4_kind_shown(), step = d < 0 ? ML_ALL : 1, t;

					for (t = 0; t <= ML_ALL; t++) {
						k = (k + step) % (ML_ALL + 1);
						if (ml_count_kind(&g_mp4, (ml_kind)k)) break;
					}
					mp4_show(&a->view[a->sys_cursor], (ml_kind)k);
				}
				return MENU_STAY;
			} else {
				if (key != IN_ACCEPT) return MENU_STAY;
				if (id == SMM_ART) { album_art_screen(a); return MENU_STAY; }
				if (id != SMM_RESCAN) return MENU_STAY;
				wait_panel(a, "MP4", T(S_LIBRARY_SCANNING));
				rescan_all(a);
				return MENU_DONE;
			}
		}
		/* Favorites' menu is its own rows too, Games and Sort By - see
		 * SM_FAV_ROWS - so its Sort By is row 1 and not SM_SORT. Nothing here
		 * knew that from the day the menu was cut to two rows, and left and
		 * right on Favorites' Sort By did nothing at all. */
		/* Collections' rows are its own (SM_COLL_ROWS): its Sort By and View
		 * go on below as any shelf's; the three about a collection are
		 * src/collect_screen.c's, and close the menu onto the shelf as it
		 * is after them */
		if (a->sys_cursor == shelf_coll_at(a)) {
			sm_coll_row ids[SM_COLL_ROWS];
			int n = sys_menu_coll_rows(a->view[a->sys_cursor].dir[0] != '\0', ids);
			sm_coll_row id = sel >= 0 && sel < n ? ids[sel] : SMC_GAMES;

			if (id == SMC_RENAME || id == SMC_REMOVE || id == SMC_DELETE) {
				if (key != IN_ACCEPT) return MENU_STAY;
				if (id == SMC_RENAME) return collect_rename_screen(a) ? MENU_DONE : MENU_STAY;
				if (id == SMC_REMOVE) { collect_remove_game(a); return MENU_DONE; }
				return collect_delete_screen(a) ? MENU_DONE : MENU_STAY;
			}
			sel = id == SMC_RANDOM ? SM_RANDOM : id == SMC_SORT ? SM_SORT : id == SMC_VIEW ? SM_VIEW : SM_GAMES;
		} else if (a->sys_cursor == shelf_recents_at(a) && sel == SM_FAV_ROWS) {
			/* Recents' Card Picture: Left and Right turn it over (A opens
			 * its list, which sends them); the shelf under the menu turns
			 * with it */
			if (d) recents_picture_turn(a);
			return MENU_STAY;
		} else if (a->view[a->sys_cursor].owner)
			sel = sel == 1 ? SM_RANDOM : sel == 2 ? SM_SORT : sel == 3 ? SM_VIEW : SM_GAMES;
		/* And a console's rows lose Box Art on a device with no radio, which
		 * moves Rescan Folder up one: which row is which comes from the same
		 * list the build used. */
		else if (!is_mp4(&a->sys.systems[a->sys_cursor])) {
			sys_ui dev = { .no_wifi = !plat_has_wifi(), .online = net_internet(NULL),
			               .dslang = dslang_row_shown(a, a->sys_cursor) ? "" : NULL,
			               .pace = pacing_row_shown(a, a->sys_cursor) ? "" : NULL,
			               .sgb = sgb_row_shown(a, a->sys_cursor) ? "" : NULL,
			               .border = border_row_value(a, a->sys_cursor),
			               .border_color = border_color_value(a, a->sys_cursor),
			               .no_strength = !shader_has_strength(a->view[a->sys_cursor].shader) };
			sm_row ids[SM_ROWS];
			int n = sys_menu_system_rows(&dev, ids);

			sel = sel >= 0 && sel < n ? (int)ids[sel] : SM_ROWS;
		}
		/* The core, on a shelf that lists more than one: left, right or A
		 * steps to the next, saved at once like the display mode. The next
		 * game launched from here runs on it. */
		if ((d || key == IN_ACCEPT) && sel == SM_CORE &&
		    core_offered_count(&a->sys.systems[a->sys_cursor]) > 1) {
			core_step(a, a->sys_cursor, d ? d : 1);
			return MENU_STAY;
		}
		/* The Nintendo DS's game language, stepped round the eight the same
		 * way, saved at once: the next game started runs in it. */
		if ((d || key == IN_ACCEPT) && sel == SM_DSLANG) {
			dsl_choice c = dslang_step(dslang_load(), d ? d : 1);

			db_set_str(db_dev(), DSLANG_SETTING, dslang_id(c));
			return MENU_STAY;
		}
		/* Super Game Boy, this system's own, the same way: its next game
		 * starts so (src/sgbmode.h) */
		if ((d || key == IN_ACCEPT) && sel == SM_SGB) {
			const system_cfg *sc = &a->sys.systems[a->sys_cursor];
			char gkey[32];

			sgbm_key(sc->tag, gkey, sizeof gkey);
			db_set_str(db_dev(), gkey, sgbm_id(sgbm_step(sgbm_load(sc), d ? d : 1)));
			return MENU_STAY;
		}
		/* Timing, Smooth or Exact, this system's own, the same way: its
		 * next game is paced so (src/pacing.h) */
		if ((d || key == IN_ACCEPT) && sel == SM_PACE) {
			const system_cfg *sc = &a->sys.systems[a->sys_cursor];
			char pkey[32];

			pacing_key(sc->tag, pkey, sizeof pkey);
			db_set_str(db_dev(), pkey, pacing_id(pacing_step(pacing_load(sc), d ? d : 1)));
			return MENU_STAY;
		}
		/* The shader, stepped round on left, right and A and kept at once,
		 * for this system alone: its next game plays with it. MENU's row
		 * is ZyperOS's own screens'. */
		if ((d || key == IN_ACCEPT) && sel == SM_SHADER) {
			sysview *v = &a->view[a->sys_cursor];

			v->shader = shader_step(v->shader, d ? d : 1, a->sys.systems[a->sys_cursor].tag);
			shader_save(a, a->sys_cursor);
			return MENU_STAY;
		}
		/* Its strength, on left and right, stopping at the ends, kept at
		 * once for this system alone (src/strength.h) */
		if (d && sel == SM_STRENGTH) {
			sysview *v = &a->view[a->sys_cursor];

			v->strength = strength_step(v->strength, d);
			strength_write(db_dev(), a->sys.systems[a->sys_cursor].tag, v->strength);
			return MENU_STAY;
		}
		/* Display mode, saved the moment it changes because there is no
		 * confirm step to hang the write off. */
		/* View: the covers, the grid or a list, this shelf's (DESIGN.md
		 * 11.39, 11.47). Right steps it forward, Left back, round the
		 * three; A opens the three as a list (menu_choices' SM_VIEW, the
		 * runner's). The shelf under the menu turns with it, on the game it
		 * was on. */
		if ((d || key == IN_ACCEPT) && sel == SM_VIEW) {
			view_turn(a, a->sys_cursor, d < 0 ? -1 : 1);
			return MENU_STAY;
		}
		if (d && sel == SM_DISPLAY) {
			sysview *v = &a->view[a->sys_cursor];

			v->dmode = (v->dmode + d + DMODE_COUNT) % DMODE_COUNT;
			display_save(a);
			return MENU_STAY;
		}
		/* Handheld Color, the same way */
		if (d && sel == SM_BORDER_COLOR) {
			border_color_step(a, a->sys_cursor, d);
			return MENU_STAY;
		}
		/* Border, stepped among what the row offers, kept for this system
		 * at once: its next game has it (src/border.h). A opens its list,
		 * which comes back as steps. */
		if (d && sel == SM_BORDER) {
			/* A frame chosen turns Display to Sharp Pixels, its size */
			if (border_choose(a, a->sys_cursor, d, border_room_kept(a, a->sys_cursor))) display_save(a);
			return MENU_STAY;
		}
		/* Sort order, on the same left/right idiom.
		 *
		 * The cursor follows the GAME, not its index. Re-sorting under a
		 * cursor left where it sat means closing the menu onto a different
		 * game than the one that was selected when it opened - the shelf
		 * would appear to have jumped on its own. Which game you were
		 * looking at is the thing that survives a reorder; where it happened
		 * to sit in the old order is not. On MP4's shelf the same goes for
		 * the album, whose folder is its `file`. */
		if (d && sel == SM_SORT) {
			sysview *v = &a->view[a->sys_cursor];
			char keep[LIB_PATH];
			int i, keep_owner = -1;

			keep[0] = '\0';
			if (v->cursor >= 0 && v->cursor < v->list.count) {
				snprintf(keep, sizeof keep, "%s",
				         v->list.items[v->cursor].file);
				/* On Favorites a file name is not enough: the same one can
				 * be on the shelf twice, from two systems. */
				if (v->owner) keep_owner = v->owner[v->cursor];
			}

			v->sort = is_mp4(&a->sys.systems[a->sys_cursor])
			        ? (v->sort + d + ML_ORDERS) % ML_ORDERS
			        : sort_step_in(a->sys_cursor == shelf_coll_at(a), v->sort, d);
			sort_shelf(a, a->sys_cursor);
			sort_save(a);

			/* The textures are indexed by position, so they moved with
			 * nothing. Dropping them lets the next frame fetch each card's
			 * art for where it now sits. */
			free_view_textures(v);

			v->cursor = 0;
			for (i = 0; i < v->list.count; i++)
				if (!strcmp(v->list.items[i].file, keep) &&
				    (!v->owner || v->owner[i] == keep_owner)) {
					v->cursor = i;
					break;
				}
			cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
			return MENU_STAY;
		}
		if (key != IN_ACCEPT) return MENU_STAY;
		/* One of this shelf's games: the menu closes and it starts */
		if (sel == SM_RANDOM) {
			if (!random_pick(a, false, &g_pick.shelf, &g_pick.game)) return MENU_STAY;
			g_pick.hold_ms = 700;
			return MENU_DONE;
		}
		/* The system's own color, not the menu's. This acts on the shelf you
		 * are looking at, and menu_draw already follows that rule everywhere
		 * else. */
		if (sel == SM_COREOPTS) {
			const system_cfg *s = &a->sys.systems[a->sys_cursor];

			coreopts_page(a, s->tag, s->core, NULL, false);
		}
		if (sel == SM_BOXART)
			downloads_page(a, a->sys.systems[a->sys_cursor].folder);
		if (sel == SM_RESCAN) {
			wait_panel(a, a->sys.systems[a->sys_cursor].name, T(S_LIBRARY_SCANNING));
			rescan_all(a);
			/* Close, rather than redraw this menu over a shelf that may have
			 * just lost the system it was built for. Acting and closing is
			 * also what pressing it means. */
			return MENU_DONE;
		}
		return MENU_STAY;
	}

	/* The row's identity rather than its position: a device with no radio has
	 * fewer of them, and the same list the build used says which is which. */
	{
		sys_ui dev = { .no_wifi = !plat_has_wifi(), .no_bt = !plat_has_bluetooth(),
		               .cable_xfer = xfer_link_up(), .online = net_internet(NULL),
		               .no_developer = !dev_offered(),
		               .no_lights = !plat_battery_lights(),
		               .no_screen_colors = !plat_screen_colors_here(),
		               .no_charge_limit = !plat_charge_limit_supported(),
		               .no_usb_power = !usb_power_offered(),
		               .no_usb_wait = !usb_wait_row(),
		               /* the same rule as the build's (menu_build): a card with
		                * the update program has the row */
		               .update = up_have() ? "" : NULL,
		               .no_strength = !screen_strength_shown(),
		               .no_shader = !plat_gpu_shaders() };
		pm_row ids[PM_ROWS];
		int n = sys_menu_tab_rows(&dev, (pm_tab)g_tabs.at, ids);

		/* L1 and R1 go round the tabs (DESIGN.md 11.3): the row each tab
		 * is on is kept, and the cursor lands on the next tab's own */
		if (key == IN_L1 || key == IN_R1) {
			int dir = key == IN_R1 ? +1 : -1, k;

			if (sel >= 0 && sel < n) g_tab_row[g_tabs.at] = ids[sel];
			tabs_turn(&g_tabs, dir);
			n = sys_menu_tab_rows(&dev, (pm_tab)g_tabs.at, ids);
			for (k = 0; k < n && ids[k] != g_tab_row[g_tabs.at]; k++) ;
			g_menu_sel_to = k < n ? k : 0;
			moved_sound(a, key);
			redraw_now();
			return MENU_STAY;
		}
		sel = sel >= 0 && sel < n ? (int)ids[sel] : PM_ROWS;
	}

	/* Auto Off, on the left/right idiom Display mode uses. A step past either
	 * end changes nothing, and Zyper is told only of a change. */
	if (d && sel == PM_SLEEP) {
		int k, at = 0, was = a->auto_off;

		for (k = 0; k < AUTO_OFF_COUNT; k++)
			if (AUTO_OFF[k] == a->auto_off) { at = k; break; }
		at += d;
		if (at < 0) at = 0;
		if (at >= AUTO_OFF_COUNT) at = AUTO_OFF_COUNT - 1;
		a->auto_off = AUTO_OFF[at];
		auto_off_save(a->auto_off);
		/* From now, not from whenever the last countdown began: choosing 30s
		 * should not inherit two minutes of an old one already spent. */
		a->idle.since_ms = plat_now_ms();
		return a->auto_off != was ? MENU_CHANGED : MENU_STAY;
	}
	/* Power Button, Press or Hold (src/powerhold.h): two choices, so A
	 * switches it as Left and Right do. Saved as its word, which portkeys
	 * reads from the same database as a port starts. */
	if (sel == PM_POWER && (d || key == IN_ACCEPT)) {
		powerhold_set_mode(!powerhold_mode());
		db_set_str(db_dev(), POWERHOLD_KEY, powerhold_mode() ? "hold" : "press");
		redraw_now();
		return MENU_CHANGED;
	}
	/* Screensaver, on Auto Off's idiom: left and right step the delay, off
	 * at one end; a step past either end changes nothing. */
	if (d && sel == PM_SAVER) {
		int was = g_saver_s;

		g_saver_s = saver_step(g_saver_s, d);
		saver_save(g_saver_s);
		return g_saver_s != was ? MENU_CHANGED : MENU_STAY;
	}
	/* System Order: the shelves read again in the other order, the cursor
	 * kept on its system by tag (rescan_all), as File Transfer reads them
	 * when it closes. Once per press, never per frame. */
	if (sel == PM_ORDER && (d || key == IN_ACCEPT)) {
		g_order_release = !g_order_release;
		db_set_str(db_dev(), "systems.order", g_order_release ? "release" : "az");
		rescan_all(a);
		redraw_now();
		return MENU_CHANGED;
	}
	/* The home layout is independent of Appearance and the selected system. */
	if (sel == PM_HOME && (d || key == IN_ACCEPT)) {
		/* Spotlight, Gallery, Grid and round: Right on, Left back; A opens
		 * the three as a list (menu_choices' PM_HOME, the runner's). START
		 * on the home turns it on too (home_turn). */
		home_turn(a, d < 0 ? -1 : 1);
		return MENU_CHANGED;
	}
	/* Charge Limit, round Off, 80%, 85% and 90% as Side Lights goes round its
	 * four (A opens their list, menu_choose): saved, and the kernel told at once, so the
	 * charger stops or goes on by its next reading */
	if (sel == PM_CHARGE && (d || key == IN_ACCEPT)) {
		int k, at = 0;

		for (k = 0; k < CHARGE_LIMITS_COUNT; k++)
			if (CHARGE_LIMITS[k] == g_charge_limit) at = k;
		at = (at + (d ? d : 1) + CHARGE_LIMITS_COUNT) % CHARGE_LIMITS_COUNT;
		g_charge_limit = CHARGE_LIMITS[at];
		db_set_int(db_dev(), "chargelimit", g_charge_limit);
		charge_limit_apply();
		redraw_now();
		return MENU_CHANGED;
	}
	/* Light, Dark and Auto round (A opens their list, menu_choose), applied
	 * the moment it changes: saved as its word, and the theme file the bar's
	 * other programs read is rewritten by theme_init - so the next frame is
	 * drawn from the new theme(), and the next bar over a game or a port is
	 * drawn in it. Saved even when the theme it comes to is the one already
	 * on: Auto at noon is Beard and so is Light, and they are still two
	 * settings. */
	if (sel == PM_APPEARANCE && (d || key == IN_ACCEPT)) {
		appearance k = appearance_step(theme_appearance(), d ? d : 1);
		int was = theme_index();

		theme_init(k);
		db_set_str(db_dev(), "appearance", appearance_id(k));
		boot_theme_changed();
		if (theme_index() != was) theme_changed(a);
		return MENU_CHANGED;
	}

	/* The screens' shader (src/screenfx.h), stepped round (A opens the
	 * list), kept at once and on the next frame: no system's moves */
	if (sel == PM_SHADER && (d || key == IN_ACCEPT)) {
		screen_shader_set(screen_shader_step(g_screen_shader, d ? d : 1));
		return MENU_CHANGED;
	}
	/* Its strength, on Auto Off's idiom: left and right, a step past
	 * either end changing nothing (src/strength.h) */
	if (d && sel == PM_STRENGTH) {
		int was = g_screen_strength, k = strength_step(was, d);

		screen_strength_set(k);
		return k != was ? MENU_CHANGED : MENU_STAY;
	}

	/* On or off, so left, right and A all turn it over. Off lets the link
	 * go at once unless something holds it, and the next computer is asked
	 * (src/usblink.h); the card's file says so for the next boot too. */
	if (sel == PM_DEVELOPER && (d || key == IN_ACCEPT)) {
		if (usblink_dev_image()) {
			usblink_set_developer(db_dev(), P_USERDATA, !usblink_developer(db_dev()));
		} else {
			/* A release image: the row is there only while Developer Mode is
			 * on, and opens its page, where A turns it off. Unlocking again
			 * is the hold on About. */
			dev_screen(a);
		}
		return MENU_CHANGED;
	}

	/* Ask or Automatic, so left, right and A all turn it over; Automatic
	 * says what it does and its one risk as it is chosen */
	if (sel == PM_USB && (d || key == IN_ACCEPT)) {
		usb_auto_save(!g_usb_auto);
		if (g_usb_auto) usb_auto_message(a);
		return MENU_CHANGED;
	}
	/* A plug waiting: the question again, for that plug */
	if (sel == PM_USB_WAIT && key == IN_ACCEPT) {
		if (g_usb_wait) usb_power_ask(a, g_usb_wait);
		return MENU_CHANGED;
	}

	/* 12-hour or 24-hour, two positions, so left, right and A all turn it
	 * over. The shelf's corner and the row above it read g_clock24 as they
	 * are drawn, so the next frame is in the new one. */
	if (sel == PM_HOURS && (d || key == IN_ACCEPT)) {
		clock24_save(!g_clock24);
		return MENU_CHANGED;
	}
	/* Bars, Percentage or Time Left: three positions round, as Charge Limit
	 * goes, Right forward and Left back (A opens the list), saved as its word */
	if (sel == PM_BATTERY && (d || key == IN_ACCEPT)) {
		g_batt_mode = (g_batt_mode + (d < 0 ? BATT_MODES - 1 : 1)) % BATT_MODES;
		db_set_str(db_dev(), "battery_icon", BATT_IDS[g_batt_mode]);
		redraw_now();
		return MENU_CHANGED;
	}

	/* Two positions, so left and right and A all do the same thing: there is
	 * nothing to step through, only something to turn off and on. */
	if (sel == PM_AUDIO && (d || key == IN_ACCEPT)) {
		g_aout_policy = g_aout_policy == AOUT_AUTO ? AOUT_SPEAKER : AOUT_AUTO;
		aout_save(g_aout_policy);
		/* Forced: the resolved DEVICE may not have changed - pinning Speaker
		 * with a cable in is still the codec - but the setting did, and the
		 * next unplug must act on the new policy rather than on a cached
		 * device string that happens to match. */
		aout_apply(true);
		return MENU_CHANGED;
	}

	/* Five positions, Auto after mono right and before stereo, stepped either
	 * way like the card sets (A opens their list).
	 * Applied at once, to MP4 and to Diatom if they are there to be told, so
	 * a player trying them over music hears each as it is chosen. */
	if (sel == PM_CHANNELS && (d || key == IN_ACCEPT)) {
		g_channels = (aout_channels)((g_channels + (d ? d : 1) + AOUT_CHANNELS_COUNT)
		                             % AOUT_CHANNELS_COUNT);
		channels_save(g_channels);
		aout_apply(false);
		return MENU_CHANGED;
	}

	/* The output in use's preset, round either way as Audio Channels' choices
	 * go, heard at once; A opens the page */
	if (sel == PM_EQ && d) {
		const eq_out o = eq_out_now();

		eq_preset_set(o, (eq_preset)((eq_preset_get(o) + d + EQ_PRESETS) % EQ_PRESETS));
		eq_route_changed();
		return MENU_CHANGED;
	}
	if (sel == PM_EQ && key == IN_ACCEPT) {
		eq_screen(a);
		return MENU_CHANGED;
	}

	/* Wood, Glass, Bubble, Off, stepped either way like Audio Channels, A
	 * going forward, and heard as each is chosen: the row is MENU_SND_OWN, so
	 * the runner plays nothing for it and its press is played here, in the
	 * voice it comes to, before the setting is written. Off is silent. */
	if (sel == PM_SOUNDS && (d || key == IN_ACCEPT)) {
		int k = uisound_setting_step(uisound_setting(), d ? d : 1);

		uisound_set(k);
		if (d) moved_sound(a, key);
		else   uisound_play(DS_SOUND_ACCEPT);
		db_set_str(db_dev(), "uisounds", uisound_setting_id(k));
		return MENU_STAY;
	}

	/* Off, Low, Medium, Full, stopping at its ends (heard by the runner as
	 * Auto Off is, after, by whether it moved), and felt: the motor buzzes
	 * once at the level it comes to. A opens the list (its choices). */
	if (sel == PM_RUMBLE && d) {
		const rumble_level was = rumble_menu(), l = rumble_level_step(was, d);

		if (l == was) return MENU_STAY;
		rumble_menu_set(l);
		plat_rumble_buzz((float)rumble_level_pct(l) / 100.0f, RUMBLE_BUZZ_MS);
		return MENU_CHANGED;
	}

	if (key != IN_ACCEPT) return MENU_STAY;
	switch (sel) {
	case PM_WIFI:         wifi_screen(a); break;
	/* A game from every system: the menu closes and it starts */
	case PM_RANDOM:
		if (!random_pick(a, true, &g_pick.shelf, &g_pick.game)) break;
		g_pick.hold_ms = 700;
		return MENU_DONE;
	case PM_XFER:         xfer_screen(a); break;
	case PM_LIGHTS:       lights_screen(a); break;
	case PM_HOTKEYS:      hotkeys_screen(a); break;
	/* The shelves the home shows; the home built again as it closes */
	case PM_HOMESYS:      homesys_screen(a); return MENU_CHANGED;
	case PM_LANGUAGE:     language_screen(a); return MENU_CHANGED;
	/* These three say how they went, which is Zyper's to answer */
	/* Every download of game data (src/dl_screen.h): the Scraper's page,
	 * the achievements download's and the accounts are behind it */
	case PM_SCRAPER:      return downloads_screen(a);
	case PM_BT:           bt_screen(a); break;
	case PM_CLOCK:        clock_screen(a); break;
	case PM_MUSIC:        bgm_screen(a); break;
	case PM_STATS:        if (stats_screen(a)) return MENU_DONE; break;
	case PM_CONTROLS:     controls_screen(a); break;
	/* Closes the menu: the tutorial ends on the shelf, where its finish
	 * says to start playing. */
	case PM_TOUR:         tour_run(a, false); return MENU_DONE;
	case PM_STORAGE:      storage_screen(a); break;
	case PM_NIGHT:        night_page(a); return MENU_CHANGED;
	case PM_THEME:        look_screen(a); return MENU_CHANGED;
	case PM_SCREEN:       screen_colors_screen(a); return MENU_CHANGED;
	case PM_UPDATE:       update_screen(a); break;
	case PM_ABOUT:        about_screen(a); break;
	/* A game by its name: the menu closes once one is picked, and it starts */
	case PM_SEARCH:
		if (!search_screen(a, &g_pick.shelf, &g_pick.game)) break;
		g_pick.hold_ms = 0;
		return MENU_DONE;
	default: break;
	}
	return MENU_STAY;
}

void zyperos_menu_for(app *a, int screen)
{
	sysmenu_ctx c;

	uisound_play(DS_SOUND_ACCEPT);                /* a menu opening */
	memset(&c, 0, sizeof c);
	c.a = a;
	c.screen = screen;
	menu_style st = zyperos_menu_style(a, screen);
	/* Each row's sound class, which the build sets beside the rows */
	st.sound = c.bufs.sound;
	/* And its choices, so an option row's A opens its list (menu_choose) */
	st.choices = c.bufs.choice;

	if (st.tabs) {
		sys_ui dev = { .no_wifi = !plat_has_wifi(), .no_bt = !plat_has_bluetooth(),
		               .cable_xfer = xfer_link_up(), .online = net_internet(NULL),
		               .no_developer = !dev_offered(),
		               .no_lights = !plat_battery_lights(),
		               .no_screen_colors = !plat_screen_colors_here(),
		               .no_charge_limit = !plat_charge_limit_supported(),
		               .no_usb_power = !usb_power_offered(),
		               .no_usb_wait = !usb_wait_row(),
		               .update = up_have() ? "" : NULL,
		               .no_strength = !screen_strength_shown(),
		               .no_shader = !plat_gpu_shaders() };
		pm_row ids[PM_ROWS];
		int n, k;

		/* A plug waiting for its answer: Connections, on USB Device */
		if (!dev.no_usb_wait && !dev.no_usb_power) {
			g_tabs.at = PT_CONNECTIONS;
			g_tab_row[PT_CONNECTIONS] = PM_USB_WAIT;
		}
		g_tabs.from = g_tabs.at;
		g_tabs.t0 = 0;
		g_menu_epoch++;
		n = sys_menu_tab_rows(&dev, (pm_tab)g_tabs.at, ids);
		for (k = 0; k < n && ids[k] != g_tab_row[g_tabs.at]; k++) ;
		st.start_sel = k < n ? k : 0;
		c.seen = st.start_sel;
		st.sel_out = &c.seen;
		g_pick.shelf = -1;
		menu_run(a, &st, sysmenu_build, sysmenu_key, &c);
		/* where it was closed, for the next time it opens; Strength may
		 * have come or gone with a Shader step */
		dev.no_strength = !screen_strength_shown();
		n = sys_menu_tab_rows(&dev, (pm_tab)g_tabs.at, ids);
		if (c.seen >= 0 && c.seen < n) g_tab_row[g_tabs.at] = ids[c.seen];
	} else {
		g_pick.shelf = -1;
		menu_run(a, &st, sysmenu_build, sysmenu_key, &c);
	}
	/* A game Random Game or Search Games picked, once the menu has gone */
	if (g_pick.shelf >= 0 && a->running && !want_quit) pick_launch(a);
	g_pick.shelf = -1;
}

void zyperos_menu(app *a)
{
	zyperos_menu_for(a, a->screen);
}

/* Theme's lists through what their cursors show (src/hover_sweep.h) */
static int sweep_theme(app *a, void *ctx, menu_row *rows, int max, const menu_choice **choices)
{
	const char *h;

	(void)a; (void)ctx;
	*choices = g_lk_choice;
	return look_build(NULL, rows, max, &h);
}

void sysmenu_hover_sweep(app *a)
{
	hover_sweep_page(a, "theme", sweep_theme, NULL);
}

void sysmenu_rows_report(app *a)
{
	static menu_row rows[MENU_MAX_ROWS];
	static menu_bufs b;
	night_view nv = { .a = a };
	const char *h;
	int n, was = g_tabs.at;

	g_tabs.at = PT_TABS;   /* every one of MENU's rows (sys_menu_build) */
	n = menu_build(a, SCREEN_SYSTEMS, a->sys_cursor, rows, &b, &h);
	menu_rows_print("menu", rows, n, b.sound, b.choice);
	g_tabs.at = was;
	n = menu_build(a, SCREEN_GAMES, a->sys_cursor, rows, &b, &h);
	menu_rows_print("system", rows, n, b.sound, b.choice);
	n = look_build(NULL, rows, MENU_MAX_ROWS, &h);
	menu_rows_print("theme", rows, n, LOOK_SOUND, g_lk_choice);
	n = night_page_build(&nv, rows, MENU_MAX_ROWS, &h);
	menu_rows_print("night", rows, n, NIGHT_SOUND, g_np_choice);
}

/* MENU > Power Button as saved: "hold" is Hold, anything else Press, the
 * default (src/powerhold.h). Once, as the launcher starts. */
void power_button_load(void)
{
	char v[8];

	db_get_str(db_dev(), POWERHOLD_KEY, v, sizeof v, "press");
	powerhold_set_mode(!strcmp(v, "hold"));
}
