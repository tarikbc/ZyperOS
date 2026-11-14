/* SPDX-License-Identifier: GPL-3.0-only */
/* Runs main.c's real timed-message loop and panel/snapshot plumbing with
 * a fake clock and input queue. Only drawing and hardware are replaced.
 * messages-check.py extracts those functions, without a second loop here. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "message.h"
#include "menumotion.h"
#include "menusnap.h"
#include "design.h"
#include "message-input.inc"
typedef struct { in_state in; bool running; void *r; } app;
typedef enum { MENU_STAY, MENU_DONE, MENU_CHANGED, MENU_FAILED } menu_result;
typedef struct {
	void (*backdrop)(app *, void *, float);
	menu_result (*on_power)(app *, void *);
} menu_style;
static const menu_style *g_message_over;
static void *g_message_ctx;
static bool g_message_stopped, g_menu_closing, want_quit;
static menu_snap snaps[2], *g_snap = &snaps[0], *g_snap_next = &snaps[1];
static unsigned g_frame_no, g_menu_zyper_at;
static const zr_rig *g_menu_zyper;
/* MENU's tabs, a screen's row marks and header picture, which snap_record
 * keeps with a panel and snap_draw puts back (src/main.c): none here */
typedef struct { int x, y, w, h; } SDL_Rect;
struct menu_tabs { int at, from, dir; unsigned t0; int count; const char *(*name)(int i); };
typedef struct menu_tabs menu_tabs;
static menu_tabs g_tabs;
static const menu_tabs *g_menu_tabs;
static void (*g_row_mark)(app *a, const menu_row *row, int right, int cy, float on_fill);
static void (*g_menu_header)(app *a, const SDL_Rect *r);
static int g_menu_header_h;
/* ...and the picture at a panel's rows' left (menu_style's side), which
 * snap_draw tells it is drawing a copy: none here either */
typedef struct menu_side menu_side;
static void (*g_menu_side)(app *a, const menu_side *at);
static int g_menu_side_w;
static bool g_snap_drawing;
static mm_frame g_pframe;
#define PANEL_REST ((mm_frame){ {1,0}, {1,1}, 1, false })
enum { PANEL_MENU, PANEL_HOT, PANEL_TILES, PANEL_P2, PANEL_MOVE, PANEL_TILES_GRID, PANEL_LIST, VCOL_MESSAGE };
/* A tiles panel's marks (src/ds/glyph.h's ui_glyph): none here */
#define UI_GLYPH_COUNT 0
static const int *g_tile_marks;
static int g_tile_marks_n;
static unsigned now, step, event_at, polls, frames, services[3];
static int button, power_calls, game_stops, shelf_frames, game_frames, nudges[2];
static bool idle, queued_quit, sent, jump, extra_accept, ticking;
static unsigned anim_now(void) { return ticking ? now++ : now; }
static void redraw_now(void) { }
static void uisound_play(int sound) { (void)sound; }
static void plat_input_poll(in_state *in)
{
	assert(++polls < 1000); /* A loop stuck on MENU or an exit fails here. */
	memset(in->pressed, 0, sizeof in->pressed);
	if (!sent && now >= event_at) {
		sent = true;
		if (button >= 0) in->pressed[button] = in->down[button] = true;
		if (extra_accept) in->pressed[IN_ACCEPT] = in->down[IN_ACCEPT] = true;
		if (queued_quit) in->quit_requested = true;
	}
}
static bool in_repeat(in_state *in, in_button b) { return in->pressed[b]; }
static bool idle_due(app *a) { (void)a; return idle && now >= event_at; }
static void muse_poll(void) { services[0]++; }
static void aout_apply(bool force) { (void)force; services[1]++; }
static void tick_theme(app *a) { (void)a; services[2]++; }
static void tick_tint(app *a) { (void)a; }
static void power_off(app *a) { power_calls++; a->running = false; }
static void plat_volume_nudge(int d) { nudges[0] += d; }
static void plat_brightness_nudge(int d) { nudges[1] += d; }
static void draw_shelf(app *a) { (void)a; shelf_frames++; g_frame_no++; }
static void ui_scrim_k(void *r, int game, float k)
{ (void)r; (void)game; assert(k >= 0 && k <= 1); }
static void plat_draw_osd(void *r) { (void)r; }
static void plat_present(void) { frames++; }
static void SDL_Delay(unsigned delay)
{ assert(delay <= 8); now += jump ? 9000 : step; }
/* A frame's rest after its present (src/ui.h): the harness's frame step */
static void ui_frame_rest(unsigned ms) { SDL_Delay(ms); }
static void menu_panel(app *a, const char *heading, const menu_row *rows,
                       int n, int sel, int fixed_w, unsigned accent,
                       const unsigned *colors, bool visits_all, int loop_at,
                       const char *note, mm_pose pose)
{
	(void)a; (void)heading; (void)rows; (void)n; (void)sel; (void)fixed_w;
	(void)accent; (void)colors; (void)visits_all; (void)loop_at; (void)note;
	assert(pose.scale >= MM_SCALE_FROM && pose.scale <= 1);
	assert(pose.alpha >= 0 && pose.alpha <= 1);
}
static void hot_panel(app *a, const char *heading, const menu_row *rows, int n,
                      mm_pose pose)
{ (void)a; (void)heading; (void)rows; (void)n; (void)pose; assert(0); }
static void tiles_panel(app *a, int kind, const char *heading, const menu_row *tiles,
                        const int *marks, int n, int sel, mm_pose pose)
{ (void)a; (void)kind; (void)heading; (void)tiles; (void)marks; (void)n; (void)sel; (void)pose; assert(0); }
static void p2_panel(app *a, const char *heading, const menu_row *rows, int n,
                     unsigned accent, mm_pose pose)
{ (void)a; (void)heading; (void)rows; (void)n; (void)accent; (void)pose; assert(0); }
static void move_page(app *a, const char *heading, const menu_row *rows, int n, mm_pose pose)
{ (void)a; (void)heading; (void)rows; (void)n; (void)pose; assert(0); }
#include "message-loop.inc"

static void game_backdrop(app *a, void *ctx, float k)
{
	(void)a; assert(ctx == &game_stops); assert(k >= 0 && k <= 1);
	game_frames++; g_frame_no++;
}
static menu_result game_power(app *a, void *ctx)
{ (void)a; assert(ctx == &game_stops); game_stops++; return MENU_DONE; }
static app reset(bool parent, bool game)
{
	app a = { .running = true };
	static const menu_style st = { game_backdrop, game_power };
	menu_row row = { "parent", NULL, false };
	mm_reset(); mm_leaving_cut();
	memset(snaps, 0, sizeof snaps);
	g_pframe = PANEL_REST;
	g_message_over = game ? &st : NULL; g_message_ctx = &game_stops;
	g_message_stopped = g_menu_closing = want_quit = false;
	now = polls = frames = g_frame_no = 0; step = 8; event_at = 99999;
	memset(services, 0, sizeof services); memset(nudges, 0, sizeof nudges);
	button = -1; power_calls = game_stops = shelf_frames = game_frames = 0;
	idle = queued_quit = sent = jump = extra_accept = ticking = false;
	if (parent) {
		int lv = panel_open(); mm_press(lv);
		menu_draw_ex(&a, "parent", &row, 1, -1, 0, 0, NULL, false, 0);
	}
	return a;
}
static bool run(app *a, int rows)
{
	menu_row r[2] = { {"Result", NULL, false}, {"B: close", NULL, false} };
	bool ok = timed_message(a, "Result", r, rows, 0, 0);
	in_state empty = {0};
	assert(!memcmp(&a->in, &empty, sizeof empty)); /* No press/repeat leaks. */
	assert(services[0] == polls && services[1] == polls && services[2] == polls);
	return ok;
}
int main(void)
{
	app a;
	for (int parent = 0; parent <= 1; parent++) {
		for (int rows = 1; rows <= 2; rows++) {
			unsigned duration = (parent ? 260 : 120) + (rows == 1 ? 1600 : 2200) + 140;
			a = reset(parent, false);
			assert(run(&a, rows));
			assert(now >= duration && now < duration + 8);
			assert(mm_depth() == parent && !mm_leaving(now).moving);
			assert(mm_at(parent, now).before.alpha == 0);
			if (parent) {
				/* A result's owner can return immediately (Box Art). Its
				 * close must not replay the result's completed exit. */
				panel_close(&a, 1, false);
				assert(!mm_leaving(now).moving);
			}
		}
	}
	/* B while the parent is still leaving, while holding, and while closing. */
	for (unsigned at = 0; at <= 1950; at += 50) {
		a = reset(true, false); event_at = at; button = IN_BACK; extra_accept = true;
		assert(run(&a, 1)); assert(now < at + 156);
		assert(mm_depth() == 1 && mm_at(1, now).before.alpha == 0);
	}
	for (int game = 0; game <= 1; game++) {
		for (int kind = 0; kind < 4; kind++) {
			for (unsigned at = 0; at <= 1950; at += 50) {
				a = reset(true, game); event_at = at;
				button = kind == 0 ? IN_POWER : IN_ACCEPT;
				idle = kind == 1; queued_quit = kind == 2; want_quit = kind == 3;
				assert(!run(&a, 1)); assert(now <= at + 8);
				assert(game_stops == (game && kind < 2));
				assert(power_calls == (!game && kind < 2));
				if (game) assert(!shelf_frames);
				assert(!mm_leaving(now).moving);
			}
		}
	}
	/* POWER already read by the caller, and queued quit, survive entry. */
	a = reset(false, false); a.in.pressed[IN_POWER] = true;
	assert(!run(&a, 1) && power_calls == 1 && now == 0);
	a = reset(false, false); a.in.quit_requested = true;
	assert(!run(&a, 1) && !power_calls && now == 0);
	/* Other keys settle arrival but never answer a parent menu. */
	for (int b = IN_VOLUP; b <= IN_BRIGHTDN; b++) {
		a = reset(false, false); button = b; event_at = 40;
		assert(run(&a, 1)); assert(now >= 1780 && now < 1788);
		assert(nudges[b >= IN_BRIGHTUP] == ((b == IN_VOLUP || b == IN_BRIGHTUP) ? 1 : -1));
	}
	a = reset(true, false); event_at = 40; button = IN_MENU;
	assert(!run(&a, 1) && g_menu_closing && now == 40);
	/* MENU unwinds the parents without drawing them, then the outermost
	 * owner draws this result leaving once, as Task 2 requires. */
	panel_close(&a, 1, false);
	assert(mm_leaving(now).moving && g_snap->ok);
	assert(!mm_leaving(now + 140).moving);
	a = reset(true, false); jump = true;
	assert(run(&a, 1) && polls == 2 && !mm_leaving(now).moving);
	/* The clock advances between panel_open's internal reads. Querying its
	 * motion with an earlier timestamp must not skip the arrival duration. */
	a = reset(false, false); ticking = true;
	assert(run(&a, 1) && now >= 1860 && now < 1890);
	puts("ok: real message loop polls services and input, bounds transitions, preserves game context and consumes keys");
	return 0;
}
