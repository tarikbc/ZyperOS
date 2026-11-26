/* SPDX-License-Identifier: GPL-3.0-only */
#include "menu_runner.h"
#include "menu_pick.h"

#include "aout_route.h"
#include "app.h"
#include "cheevos.h"
#include "ctl_channel.h"
#include "ctltab.h"
#include "menu_panel.h"
#include "message.h"
#include "pace.h"
#include "panel.h"
#include "saver.h"
#include "shelf_draw.h"
#include "uisound.h"
#include "usbdrive_screen.h"
#include "usblink.h"
#include "wifi_menu.h"
#include "zyperdraw.h"
#include "zyperrig.h"
#include "levels.h"
#include "wdog.h"
#ifdef ZYPEROS_DESK
#include "device/desk.h"
#endif

#include <SDL.h>
#include <SDL_image.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

/* ---- the menu runner -------------------------------------------------- */

/* Every ZyperOS menu row has to fit through the runner too, or the bottom of
 * the menu is cut off with no sign of it. */
_Static_assert(MENU_RUN_ROWS >= MENU_MAX_ROWS, "MENU_RUN_ROWS < MENU_MAX_ROWS");
/* ...and the in-game menu's Controls tab's (src/ctltab.h) */
_Static_assert(MENU_RUN_ROWS >= CT_MAX_ROWS, "MENU_RUN_ROWS < CT_MAX_ROWS");

/* How a screen ended, for the callers that have to act differently. The
 * in-game menu is the one that does: B and MENU mean Continue there, and it
 * has to send RESUME - but the runner owns those two buttons and flushes the
 * input on the way out, so asking a->in afterwards would always say no. */
/* menu_exit, in src/panel.h */

/* Fill `rows`, name the screen, and return how many rows. MUST NOT touch the
 * renderer or the event loop, and the part that actually decides what the rows
 * SAY must be a separate function in a file the check can link - src/wifi_menu.c
 * and src/sys_menu.c are those files. The design has failed if that stops
 * being true.
 *
 * Reading the device from here is allowed and unavoidable: a row that reports
 * the network has to ask every frame. What makes it checkable is that the
 * asking is confined to this half - wifi.c is stubbed on a host build, and the
 * system menu's asking lives in menu_build rather than in sys_menu_build.
 *
 * Called every frame, so it is also where live state is reflected - there is no
 * separate "refresh".
 *
 * The heading comes from here because it is a function of the same state the
 * rows are: the system menu is titled with the system whose rows it is showing.
 * Passing it to the runner separately meant keeping two things in step that are
 * really one thing. */
/* menu_build_fn, in src/panel.h */

/* A result can be reached through MP4 over a paused game. Keep that
 * menu's backdrop and power rule through nested menus, until it returns.
 * A stop inside a result must unwind even if its caller returns FAILED or
 * CHANGED, rather than DONE. */
static const menu_style *g_message_over;
static void *g_message_ctx;
bool g_message_stopped;

/* Read every frame through the caller's pointer, so a screen that keeps its
 * style inside its own context can change it between frames. The info screen
 * does: its accent belongs to the system that OWNS the game, and toggling a
 * favorite on the Favorites shelf can hand it a different one. */

/* A key the runner did not consume, and the row it happened on. Free to open a
 * confirm, a keyboard or a wait panel: each runs its own loop and returns.
 * `sel` indexes the rows the last build produced. */
/* menu_key_fn, in src/panel.h */

/* Zyper's answer to how a press went, in the menu he is in (`z`, NULL
 * elsewhere): a hop and a pant for something changed, the cocked head for a
 * failure, nothing for the rest. menu_send answers with it and so does a
 * --zyper shot, so the rule the menu keeps is the one a shot shows. */
void menu_zyper_answer(zr_rig *z, unsigned now, menu_result r)
{
	if (!z) return;
	if (r == MENU_CHANGED) zr_works(z, now);
	else if (r == MENU_FAILED) zr_fails(z, now);
}

/* A key to the screen, and Zyper's answer to it: any press drops what he had
 * booked, and the answer comes from the moment the handler returns - which,
 * for a sign-in, is when its screen has gone and he can be seen again. */
static menu_result menu_send(app *a, zr_rig *z, menu_key_fn on_key, void *ctx,
                             in_button key, int sel)
{
	menu_result r;

	if (z) zr_press(z, plat_now_ms());
	r = on_key(a, ctx, key, sel);
	menu_zyper_answer(z, plat_now_ms(), r);
	return r;
}

/* Left or right, given to the screen, and answered (DESIGN.md 9). A row whose
 * value steps round always moves, so it is heard on the press, before the
 * screen's handler, which may take a while (MENU_SND_RING); a row that plays
 * its own is left to it (MENU_SND_OWN). Any other - a value that stops at its
 * ends, or a row with nothing to step - can only be heard after: move when
 * its value changed, edge when it did not, the rows built again to see, as
 * the next frame would build them. Each goes through menu_send, so Zyper
 * answers a side key as he answers any other. */
static menu_result menu_side_key(app *a, zr_rig *z, menu_build_fn build,
                                 menu_key_fn on_key, void *ctx,
                                 const menu_row *rows, int sel,
                                 unsigned how, in_button b)
{
	static menu_row now[MENU_RUN_ROWS];
	const char *heading;
	char was[128];
	menu_result r;
	int n;

	if (how & MENU_SND_OWN) return menu_send(a, z, on_key, ctx, b, sel);
	if (how & MENU_SND_RING) {
		moved_sound(a, b);
		return menu_send(a, z, on_key, ctx, b, sel);
	}
	snprintf(was, sizeof was, "%s",
	         rows[sel].value && !ROW_IS_NOTE(rows[sel]) ? rows[sel].value : "");
	r = menu_send(a, z, on_key, ctx, b, sel);
	if (r == MENU_DONE || !a->running || want_quit || g_message_stopped) return r;
	n = build(ctx, now, MENU_RUN_ROWS, &heading);
	if (n > MENU_RUN_ROWS) n = MENU_RUN_ROWS;
	uisound_cue(uisound_step(&g_steps,
	                         sel >= n || strcmp(was, now[sel].value && !ROW_IS_NOTE(now[sel])
	                                                 ? now[sel].value : ""), !a->in.pressed[b]));
	return r;
}

/* An option row's A (menu.h's menu_choice, DESIGN.md 9): more than two
 * choices and their list opens (menu_pick, src/menu_panel.c); two and the
 * other one. The choice is applied by sending the row's own Left or Right
 * as many times as it is away, so the screen's handler does what it always
 * did and a screen needs no code of its own to apply one; only the last
 * step goes through menu_send, so Zyper answers once. */
static menu_result menu_choose(app *a, zr_rig *z, const menu_style *st, menu_key_fn on_key,
                               void *ctx, const char *heading, const menu_choice *c, int sel)
{
	menu_result r = MENU_STAY;
	bool right;
	int to, steps;

	uisound_play(DS_SOUND_ACCEPT);
	if (c->n > 2 || c->list) {
		to = c->preview && c->hover
		     ? menu_pick_preview_hover(a, st, ctx, heading, c->labels, c->notes, c->preview, c->hover,
		                               c->n, c->none ? -1 : c->cur, c->cur)
		     : c->preview
		     ? menu_pick_preview(a, st, ctx, heading, c->labels, c->notes, c->preview, c->n,
		                         c->none ? -1 : c->cur, c->cur)
		     : c->focus
		     ? menu_pick_focus(a, st, ctx, heading, c->labels, c->notes, c->focus, c->n,
		                       c->none ? -1 : c->cur, c->cur)
		     : c->hover
		     ? menu_pick_hover(a, st, ctx, heading, c->labels, c->notes, c->hover, c->n,
		                       c->none ? -1 : c->cur, c->cur)
		     : menu_pick(a, st, ctx, heading, c->labels, c->notes, c->n, c->none ? -1 : c->cur, c->cur);
		if (to == MENU_PICK_POWER) return MENU_DONE;   /* as POWER on the menu itself */
		if (to < 0 || !a->running || want_quit || g_message_stopped || g_menu_closing) return MENU_STAY;
	} else {
		to = c->cur == 0 ? 1 : 0;
	}
	steps = menu_choice_steps(c, to, &right);
	/* None on as a whole and that one chosen: Right and back, which sets
	 * it for all (menu_choice_steps) */
	if (to == c->cur && steps == 2) {
		if (c->cur + 1 >= c->n) right = false;
		r = on_key(a, ctx, right ? IN_RIGHT : IN_LEFT, sel);
		return r == MENU_DONE ? r : menu_send(a, z, on_key, ctx, right ? IN_LEFT : IN_RIGHT, sel);
	}
	while (steps > 0 && r != MENU_DONE && a->running) {
		in_button b = right ? IN_RIGHT : IN_LEFT;

		r = --steps ? on_key(a, ctx, b, sel) : menu_send(a, z, on_key, ctx, b, sel);
	}
	return r;
}

/* The loop itself. Called only through menu_run below, which owns the flush on
 * either side of it. */
/* A panel screen's picture, for pace_due (src/pace.h): its heading and
 * rows, the cursor, and what the frame around them shows - the scrim's
 * strength, the tint, the theme, the replug message */
unsigned pace_rows(const char *heading, const menu_row *rows, int n, int sel, float k,
                   unsigned tint)
{
	unsigned h = pace_menu(5381u, heading, rows, n, sel);

	h = pace_int(h, (int)(k * 1000.0f));
	h = pace_int(h, (int)tint);
	h = pace_int(h, theme_index());
	h = pace_int(h, (int)replug_shown());
	return h;
}

/* Something on screen whose every frame counts, whatever the screen: the
 * panel opening or closing, the volume bar, a pop-up, art coming in, or a
 * button down */
bool pace_busy(const app *a)
{
	int b;

	for (b = 0; b < IN_COUNT; b++)
		if (a->in.pressed[b] || a->in.down[b]) return true;
	return g_pframe.moving || plat_osd_until() != REDRAW_NEVER || texload_ready() ||
	       g_songpop.on || g_heatpop.on;
}

/* The memory's slower clock, said once a change, for whichever screen is up:
 * the shelf's loop and the panels' share it (plat_ram_idle) */
bool g_ram_idle;

void ram_idle_set(bool idle)
{
	if (idle == g_ram_idle) return;
	g_ram_idle = idle;
	plat_ram_idle(idle);
}

/* Zyper's pose beside the heading, as a frame shows it: a quarter unit, a
 * quarter degree, a fiftieth of a blink or of his tongue. A breath moves
 * him 2 units in 1.2 s, so a still menu draws a few frames a second for
 * it; a hop or a blink changes it every frame, and draws every frame. */
static unsigned zyper_key(const zr_rig *z, unsigned t)
{
	zr_pose p = zr_pose_at(z, t);
	unsigned h = 977u;

	h = pace_int(h, (int)lroundf(p.tilt * 4.0f));
	h = pace_int(h, (int)lroundf(p.tongue * 50.0f));
	h = pace_int(h, (int)lroundf(p.ears * 4.0f));
	h = pace_int(h, (int)lroundf(p.blink * 50.0f));
	h = pace_int(h, (int)lroundf(p.dy * 4.0f));
	h = pace_int(h, (int)lroundf(p.squash * 200.0f));
	return pace_int(h, (int)lroundf(p.mouth * 50.0f));
}

static menu_exit menu_run_body(app *a, const menu_style *st,
                               menu_build_fn build, menu_key_fn on_key,
                               void *ctx, int lv)
{
	menu_row rows[MENU_RUN_ROWS];
	const char *heading = NULL;
	int sel = st->start_sel, n = 0, b;
	float k;
	/* A menu over the shelf draws when its picture changes (src/pace.h).
	 * One over a game's paused frame or Now Playing (st->backdrop) draws
	 * every pass, as before: its backdrop is the screen's own. */
	pace pc;

	pace_init(&pc, plat_now_ms());
	/* Zyper's moves, in the menu that has him, on this loop's clock: it
	 * draws every pass, so the breath and the blinks need nothing more */
	zr_rig zy, *z = st->zyper ? &zy : NULL;

	if (z) {
		unsigned t = plat_now_ms();

		zr_init(z, t, t);
		zr_open(z, t);
	}
	while (a->running && !want_quit && !g_message_stopped) {
		if (z) zr_tick(z, plat_now_ms());
		n = build(ctx, rows, MENU_RUN_ROWS, &heading);
		if (n > MENU_RUN_ROWS) n = MENU_RUN_ROWS;
		if (n <= 0) return MENU_LEFT_GONE;

		/* The list is rebuilt every frame and can shrink under the cursor -
		 * a rescan finding fewer networks, a row going away. Clamp first,
		 * then land on something live. */
		if (sel >= n) sel = n - 1;
		if (sel < 0) sel = 0;
		if (!rows[sel].live && !st->visits_all) {
			sel = menu_step_sel(rows, n, sel, +1);
			if (!rows[sel].live) sel = menu_step_sel(rows, n, sel, -1);
		}
		if (st->sel_out) *st->sel_out = sel;

		plat_input_poll(&a->in);
		/* Where the sound goes, followed in the menus too: a headset connected
		 * from the Bluetooth screen, or a cable plugged in, only took the sound
		 * once the menus were left - seen 2026-09-26. It sends only on a change.
		 * And MP4 is read here as well: its answers - where it now is, and the
		 * end of a track, which is when the next is sent - waited for the menus
		 * to be left, so the row read stale and an album stopped at the end of a
		 * song while a menu was open. #45. */
		mp4_poll();
		aout_apply(false);
		/* Auto's clock, in every screen this runs - the menus, and the
		 * in-game menu over a paused game too. A Night Hours edge arriving while
		 * one is open is drawn from the next frame, which this loop draws
		 * every pass, and the file Diatom and portkeys read follows. */
		tick_theme(a);
		if (a->in.quit_requested) { a->running = false; return MENU_LEFT_GONE; }
		/* px2-open's quit over the in-game menu: its game is stopping */
		if (g_ctl_close_menu) return MENU_LEFT_GONE;
		if (a->in.pressed[IN_POWER] || idle_due(a)) {
			if (!st->on_power) { power_off(a); return MENU_LEFT_GONE; }
			if (st->on_power(a, ctx) == MENU_DONE) return MENU_LEFT_GONE;
		}
		if (menu_leaving(a))
			return MENU_LEFT_BACK;
		/* A computer plugged in while a menu is open: the menus close back
		 * to the shelf, whose loop asks Computer Connected at once
		 * (2026-11-12: the question waited, unseen, until the player closed
		 * MENU by hand). Not over a game's or a port's menu: a game is
		 * never interrupted, and is asked about back on the shelf. */
		if (!g_in_game_menu && !g_port_menu && usblink_wants_ask(db_dev(), plat_now_ms())) {
			fprintf(stderr, "usb: a computer on the cable with a menu open: back to the shelf to ask\n");
			g_menu_closing = true;
			return MENU_LEFT_BACK;
		}
		/* The in-game menu too: its game is paused underneath, and MP4
		 * borrows its backdrop and its power rule for as long as it is up.
		 * Rebuilt on the way back, because anything may have changed while
		 * MP4 covered it - a Bluetooth headset connecting, for one. */
		/* Over a port's only when MP4 is kept playing over it (your
		 * PICO-8, port_mp4_ready): launch.sh stops MP4 for every other */
		if (a->in.pressed[IN_SELECT] && (!g_port_menu || port_mp4_ready(a))) {
			if (mp4_open(a, st, ctx)) return MENU_LEFT_GONE;
			continue;
		}

		/* A move is a press too: it drops what Zyper had booked */
		/* Every row a stop where the screen says so (visits_all): stepped
		 * as a plain list, round on a press */
		if (in_repeat(&a->in, IN_UP)) {
			sel = st->visits_all ? list_step_heard(a, IN_UP, n, sel)
			                     : menu_step_heard(a, rows, n, sel, -1, IN_UP);
			if (z) zr_press(z, plat_now_ms());
		}
		if (in_repeat(&a->in, IN_DOWN)) {
			sel = st->visits_all ? list_step_heard(a, IN_DOWN, n, sel)
			                     : menu_step_heard(a, rows, n, sel, +1, IN_DOWN);
			if (z) zr_press(z, plat_now_ms());
		}

		/* Volume and brightness keep working here, as they do everywhere.
		 *
		 * They were missed when this runner was written, so the Wi-Fi screen
		 * spent a day as the one screen in the launcher where the volume keys
		 * did nothing. That is the exact failure this runner exists to make
		 * impossible, arriving by the one route it could still come from: the
		 * runner forgetting, rather than a screen forgetting. Every other loop
		 * in this file carries these four lines by hand. */
		levels_keys(&a->in, BOOST_HIDE);

		/* Left and right reach the screen on REPEAT, not just on press, so a
		 * row whose value cycles can be held down. Everything else is edge
		 * triggered: holding A on a row that opens a screen should open it
		 * once. Sent before the press loop below, which skips them. */
		if (on_key && in_repeat(&a->in, IN_LEFT))
			if (menu_side_key(a, z, build, on_key, ctx, rows, sel,
			                  menu_sound_at(st->sound, n, sel), IN_LEFT) == MENU_DONE)
				return MENU_LEFT_SCREEN;
		if (on_key && in_repeat(&a->in, IN_RIGHT))
			if (menu_side_key(a, z, build, on_key, ctx, rows, sel,
			                  menu_sound_at(st->sound, n, sel), IN_RIGHT) == MENU_DONE)
				return MENU_LEFT_SCREEN;
		if (!a->running || want_quit || g_message_stopped) return MENU_LEFT_GONE;
		/* A step can move the cursor too: a Controls row whose change put a
		 * warning above it stays the row under it (menu_cursor_to) */
		if (g_menu_sel_to >= 0) {
			sel = g_menu_sel_to < n ? g_menu_sel_to : n - 1;
			g_menu_sel_to = -1;
		}

		/* Everything the runner did not consume goes to the screen, so a
		 * screen-specific key needs no runner change to exist. */
		for (b = 0; b < IN_COUNT; b++) {
			if (!a->in.pressed[b]) continue;
			if (b == IN_LEFT || b == IN_RIGHT) continue;
			if (b == IN_UP || b == IN_DOWN || b == IN_BACK || b == IN_MENU
			    || b == IN_POWER || b == IN_VOLUP || b == IN_VOLDN
			    || b == IN_BRIGHTUP || b == IN_BRIGHTDN) continue;
			/* A on an option row: its list, or the other of two, applied
			 * through the row's Left and Right (menu_choose); its screen
			 * does not get this A */
			if (b == IN_ACCEPT && on_key && rows[sel].live) {
				const menu_choice *c = menu_choice_at(st->choices, n, sel);

				if (c && !c->own && c->n >= 2) {
					if (menu_choose(a, z, st, on_key, ctx, rows[sel].label, c, sel) == MENU_DONE)
						return MENU_LEFT_SCREEN;
					if (!a->running || want_quit || g_message_stopped) return MENU_LEFT_GONE;
					if (g_menu_closing) return MENU_LEFT_BACK;
					continue;
				}
			}
			/* A, answered before the screen has it, since what it opens
			 * runs its own loop: accept, or deny on a row that can do
			 * nothing, or nothing with A; a row that plays its own is
			 * left to its screen. */
			if (b == IN_ACCEPT && !(menu_sound_at(st->sound, n, sel) & MENU_SND_OWN))
				uisound_play(on_key && (rows[sel].live || st->visits_all) &&
				             !(menu_sound_at(st->sound, n, sel) & MENU_SND_NO_A)
				             ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
			if (on_key && menu_send(a, z, on_key, ctx, (in_button)b, sel) == MENU_DONE)
				return MENU_LEFT_SCREEN;
			/* The handler moved the cursor: MENU's tab changed under it */
			if (g_menu_sel_to >= 0) {
				sel = g_menu_sel_to;
				g_menu_sel_to = -1;
			}
			if (!a->running || want_quit || g_message_stopped) return MENU_LEFT_GONE;
			/* The screen it opened was left with MENU: out of this one too,
			 * without drawing it again first. */
			if (g_menu_closing) return MENU_LEFT_BACK;
			/* A handler may have run a modal and changed everything under
			 * us; rebuild before drawing rather than drawing stale rows. */
			break;
		}

		/* The cursor where it is for this frame: an option's list shows
		 * the choice under it before the frame is drawn */
		if (st->at) st->at(a, ctx, sel);
		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		if (!st->backdrop) {
			unsigned nowp = plat_now_ms();
			unsigned key;
			/* usbrole's state, followed every pass, as every frame's
			 * overlay followed it while frames came every pass */
			power_follow();
			key = pace_rows(heading, rows, n, sel, k, a->tint);
			/* Zyper: his pose now, and every frame while the next one's
			 * differs (a hop, a blink, a breath's step), through the
			 * redraw clock: his breath is no press, and leaves the
			 * memory's slow clock alone, as the home's sprite does;
			 * woken for his next blink or booked move */
			bool busy = pace_busy(a);
			unsigned cap = 80;

			if (z && zyper_key(z, nowp) != zyper_key(z, nowp + 17)) redraw_at(nowp);

			if (z) {
				key = pace_int(key, (int)zyper_key(z, nowp));
				if ((int)(z->next_blink - nowp) > 0 && z->next_blink - nowp < cap)
					cap = z->next_blink - nowp;
				if (z->booked && (int)(z->booked_at - nowp) > 0 && z->booked_at - nowp < cap)
					cap = z->booked_at - nowp;
			}
			if (!pace_due(&pc, key, nowp, g_redraw_at, busy)) {
				ram_idle_set(pace_ram_idle(&pc, nowp));
				plat_input_wait(pace_rest(&pc, nowp, g_redraw_at, cap));
				continue;
			}
			ram_idle_set(false);
			g_redraw_at = REDRAW_NEVER;
			pace_drawn(&pc, key, nowp);
			draw_shelf(a);
			ui_scrim_k(a->r, 0, k);
		} else {
			st->backdrop(a, ctx, k);
		}
		g_menu_zyper = z;
		g_menu_zyper_at = plat_now_ms();
		menu_draw_styled(a, st, heading, rows, n, sel);
		g_menu_zyper = NULL;
#ifdef ZYPEROS_DESK
		/* tools/desk's `state`: the panel as drawn, its heading ("" for none)
		 * and the row under the cursor, for a check that waits for a key's
		 * screen rather than for a time */
		desk_note_focus(heading ? heading : "", sel, sel >= 0 && sel < n && rows[sel].label ? rows[sel].label : "");
#endif
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	return MENU_LEFT_GONE;
}

/* One screen, one loop. Owns polling, power, idle, quit, back, volume,
 * brightness, the cursor and the whole frame, so that no screen can forget any
 * of them - which every menu defect this project has had came down to.
 *
 * The flush is here rather than in the body: the press that opens a screen must
 * not also close it, and the press that closes one must not arrive again at
 * whatever is underneath. This file does that by hand about twenty times, and
 * the body has nine ways out, so putting it at each of them would eventually
 * mean missing one. */
menu_exit menu_run(app *a, const menu_style *st,
                   menu_build_fn build, menu_key_fn on_key, void *ctx)
{
	menu_exit how;
	int lv;
	const menu_style *previous = g_message_over;
	void *previous_ctx = g_message_ctx;

	if (!previous) g_message_stopped = false;
	if (!previous || st->backdrop || st->on_power) {
		g_message_over = st;
		g_message_ctx = ctx;
	}

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	/* A cursor move is for the runner it was asked of: none left over from
	 * a screen that ended on the handler's press reaches this one, or the
	 * one under it */
	g_menu_sel_to = -1;

	/* And it opens and closes (panel_open): quit and power, and nothing left
	 * to show, leave nothing to see go. */
	lv = panel_open();
	how = menu_run_body(a, st, build, on_key, ctx, lv);
	g_menu_sel_to = -1;
	panel_close(a, lv, how == MENU_LEFT_GONE);

	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	g_message_over = previous;
	g_message_ctx = previous_ctx;
	return how;
}

/* A result, unlike wait_panel's single frame before a blocking operation.
 * Its hold starts after the panel arrives, on the same wall clock. Its
 * close completes here, so a result cannot hand queued keys to its parent.
 * The parent's own arrival remains the parent's to draw. */
bool timed_message(app *a, const char *heading, const menu_row *rows,
                   int n, int fixed_w, unsigned accent)
{
	message_time clock;
	message_phase phase;
	bool closed = false, power = a->in.pressed[IN_POWER];
	bool quit = a->in.quit_requested;
	int lv = panel_open();
	unsigned now = anim_now();
	mm_frame f = mm_at(lv, now);
	static const unsigned colors[1] = { VCOL_MESSAGE };

	if (!g_message_over) g_message_stopped = false;
	message_begin(&clock, now, n, f.moving
	              ? DS_MS_QUICK + (f.before.alpha > 0.0f ? DS_MS_EXIT : 0) : 0);
	/* The opening press is already consumed. Poll the queue rather than
	 * flushing it: POWER or quit during a blocking call must still act. */
	memset(&a->in, 0, sizeof a->in);
	snap_record(g_snap_next, PANEL_MENU, heading, rows, n, -1, fixed_w,
	            accent, n == 1 ? colors : NULL, false, 0, NULL);
	for (;;) {
		bool press = false, back;
		int b;
		float k;

		plat_input_poll(&a->in);
		mp4_poll();
		aout_apply(false);
		tick_theme(a);
		now = anim_now();
		quit = quit || a->in.quit_requested || want_quit || !a->running;
		power = power || a->in.pressed[IN_POWER] || idle_due(a);
		for (b = 0; b < IN_COUNT; b++)
			if (a->in.pressed[b]) { press = true; break; }
		back = menu_leaving(a);
		phase = message_step(&clock, now, press, back, power, quit);
		if (phase == MESSAGE_QUIT || phase == MESSAGE_POWER) {
			g_message_stopped = true;
			if (phase == MESSAGE_QUIT) a->running = false;
			else if (g_message_over && g_message_over->on_power)
				g_message_over->on_power(a, g_message_ctx);
			else power_off(a);
			if (!closed) panel_close(a, lv, true);
			mm_leaving_cut();
			break;
		}
		levels_keys(&a->in, BOOST_HIDE);
		if (phase != MESSAGE_SHOW && !closed) {
			if (back) panel_settle(lv);
			panel_close_at(a, lv, false, clock.close_at);
			closed = true;
		}
		/* MENU leaves every parent without another frame. The outermost
		 * owner draws this panel's exit once, as for every other submenu. */
		if (g_menu_closing) break;
		if (phase == MESSAGE_DONE) {
			/* A press during the exit ends it now. Do not leave a second
			 * copy of that exit for the parent to draw after returning. */
			if (press) { mm_press(mm_depth()); mm_leaving_cut(); }
			break;
		}
		if (closed) {
			f = mm_at(mm_depth(), now);
			if (!(f.before.alpha > 0.0f)) f = mm_leaving(now);
			k = f.scrim;
		} else {
			if (!panel_frame(a, lv, &k)) continue;
		}
		tick_tint(a);
		if (g_message_over && g_message_over->backdrop)
			g_message_over->backdrop(a, g_message_ctx, k);
		else { draw_shelf(a); ui_scrim_k(a->r, 0, k); }
		if (closed) snap_draw(a, f.before);
		else menu_draw_ex(a, heading, rows, n, -1, fixed_w, accent,
		                  n == 1 ? colors : NULL, false, 0);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	/* All queued events went through the poll above, including on the last
	 * frame. Clear held/repeat state too; do not blindly drain a new POWER. */
	memset(&a->in, 0, sizeof a->in);
	/* The owner may return immediately too (Box Art). The result's exit
	 * already finished; its saved panel must not leave a second time. */
	if (!g_menu_closing && g_snap->level == lv) g_snap->ok = false;
	return !g_message_stopped && !g_menu_closing;
}

/* A result's rows, for result_message and its shot: the text as one row
 * (`*w` 0, the row's own width), or, wider than the panel, onto lines,
 * balanced, rather than sliding where a word is cut ("neogeo is a BIOS other
 * games need. It stays.", "PortMaster runs every port. It stays." in
 * French), the panel at the shared width. Up to RESULT_LINES: three cut
 * Rename's refusal of a name in English, its last line ending "* ?..."
 * where the characters a name cannot have were the point. */
#define RESULT_LINES 5
static int result_rows(app *a, const char *text, char (*lines)[LB_LINE], menu_row *rows, int *w)
{
	int n, i;

	*w = menu_std_width(a);
	if (ui_text_width(ui_font(UI_F_MENU), text) > *w) {
		n = ui_balance_lines(ui_font(UI_F_MENU), text, *w, RESULT_LINES, lines);
		if (n > 1) {
			ui_fit_drawn(text);    /* drawn in lines, said whole to the host's check */
			for (i = 0; i < n; i++) rows[i] = (menu_row){ lines[i], NULL, false };
			return n;
		}
	}
	*w = 0;
	rows[0] = (menu_row){ text, NULL, false };
	return 1;
}

bool result_message(app *a, const char *heading, const char *text)
{
	char lines[RESULT_LINES][LB_LINE];
	menu_row rows[RESULT_LINES];
	int w, n = result_rows(a, text, lines, rows, &w);

	return timed_message(a, heading, rows, n, w, MENU_ZYPEROS);
}

/* One frame of it once it has arrived, over the shelf, for a shot: what
 * timed_message draws, as menu_message does for one row */
void result_preview(app *a, const char *heading, const char *text)
{
	static const unsigned msg[1] = { VCOL_MESSAGE };
	char lines[RESULT_LINES][LB_LINE];
	menu_row rows[RESULT_LINES];
	int w, n = result_rows(a, text, lines, rows, &w);

	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw_ex(a, heading, rows, n, -1, w, MENU_ZYPEROS, n == 1 ? msg : NULL, false, 0);
}

kb_pose keyboard_panel(void *ctx, kb_stage stage, bool pressed)
{
	keyboard_transition *tr = ctx;
	app *a = tr->a;
	const kb_pose gone = {1, 0, false};
	mm_frame f;
	unsigned now = anim_now();

	if (stage == KB_PANEL_OPEN) {
		tr->level = panel_open();
		return gone;
	}
	if (stage == KB_PANEL_LEAVE || stage == KB_PANEL_CUT) {
		if (!tr->closed) {
			int shown = stage == KB_PANEL_CUT ? 0 :
			            tr->shown ? tr->level : snap_shown();

			mm_close(tr->level, now, shown, false);
			g_snap_next->ok = false;
			tr->closed = true;
		}
		if (stage == KB_PANEL_CUT) {
			mm_press(mm_depth());
			mm_leaving_cut();
			tr->shown = false;
		}
		return gone;
	}
	if (stage == KB_PANEL_FINISH) {
		/* No keyboard text or image ever enters g_snap. Its private
		 * textures die in kb_prompt before this callback returns. */
		mm_leaving_cut();
		tr->shown = false;
		return gone;
	}
	if (tr->closed) {
		if (pressed) {
			mm_press(mm_depth());
			mm_leaving_cut();
			tr->shown = false;
		}
		f = mm_at(mm_depth(), now);
		if (!(f.before.alpha > 0.0f)) f = mm_leaving(now);
		if (!tr->shown || !(f.before.alpha > 0.0f)) return gone;
	} else {
		if (pressed) mm_press(tr->level);
		f = mm_at(tr->level, now);
	}

	ui_sounds_follow();
	tick_theme(a);
	draw_shelf(a);
	ui_scrim_k(a->r, 0, f.scrim);
	if (tr->closed)
		return (kb_pose){f.before.scale, f.before.alpha, true};
	if (f.before.alpha > 0.0f) snap_draw(a, f.before);
	if (f.panel.alpha > 0.0f) tr->shown = true;
	return (kb_pose){f.panel.scale, f.panel.alpha, f.moving};
}

static bool keyboard_idle(void *ctx)
{
	return idle_due(((keyboard_transition *)ctx)->a);
}

kb_result keyboard_prompt(app *a, const char *title, char *buf, int cap)
{
	keyboard_transition tr = { .a = a };
	kb_result result = kb_prompt(a->r, &a->in, title, buf, cap,
	                              keyboard_panel, keyboard_idle, &tr);
	if (a->in.quit_requested) a->running = false;
	memset(&a->in, 0, sizeof a->in);
	return result;
}

static menu_result wifi_key(app *a, void *ctx, in_button key, int sel)
{
	wifi_ui *w = ctx;
	char status[96], ssid[WIFI_SSID_MAX], ip[64];
	int k = sel - 1;                    /* row 0 is the switch */
	bool net_row = sel >= 1 && sel <= w->n;

	/* Y scans, with the radio on; X forgets, on a network that was joined.
	 * Anywhere else they can do nothing, and say so. */
	if (key == IN_Y) uisound_play(w->on ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
	if (key == IN_X)
		uisound_play(w->on && net_row && w->nets[k].known ? DS_SOUND_ACCEPT
		                                                  : DS_SOUND_DENY);
	if (key == IN_Y && w->on) { wifi_begin_scan(w); return MENU_STAY; }

	/* Forget a saved network. Only saved rows offer it - there is nothing to
	 * forget about one the device has never joined, and a button that silently
	 * does nothing on some rows is the same defect as a menu entry wired to
	 * nothing.
	 *
	 * Forgetting the CONNECTED network is allowed, deliberately. Refusing would
	 * be safer - it drops the device off the LAN, taking Wi-Fi File Transfer and ssh
	 * with it, and a handheld has no keyboard to climb back out with. But
	 * wanting to forget the network you are on is reasonable, and the honest
	 * place to say what it costs is the confirm. */
	if (key == IN_X && w->on && net_row && w->nets[k].known) {
		char cur[WIFI_SSID_MAX], msg[160];
		bool joined = wifi_status(cur, sizeof cur, NULL, 0) == WIFI_CONNECTED
		              && !strcmp(cur, w->nets[k].ssid);

		cq_text(joined ? CQ_WIFI_FORGET_JOINED : CQ_WIFI_FORGET, msg, sizeof msg, w->nets[k].ssid);
		if (confirm_ask(a, joined ? CQ_WIFI_FORGET_JOINED : CQ_WIFI_FORGET, msg)) {
			snprintf(status, sizeof status,
			         wifi_forget(w->nets[k].ssid) ? T(S_WIFI_FORGOT)
			                                      : T(S_WIFI_FORGET_FAILED),
			         w->nets[k].ssid);
			if (!result_message(a, T(S_MENU_WIFI), status)) return MENU_STAY;
			if (w->on) wifi_begin_scan(w);
		}
		return MENU_STAY;
	}

	if (key != IN_ACCEPT) return MENU_STAY;

	/* The switch. Saved on every change rather than on the way out: the way out
	 * of a handheld is often the power button. */
	if (sel == 0) {
		if (w->on) {
			/* Wi-Fi's own waits have no end of their own (wdog.h) */
			wdog_hold("Wi-Fi");
			wifi_down();
			wdog_release();
			wifi_pref_save(false);
			w->on = false;
			w->n = 0;
		} else {
			wait_panel(a, T(S_MENU_WIFI), T(S_WIFI_TURNING_ON));
			wdog_hold("Wi-Fi");
			w->on = wifi_up();
			wdog_release();
			wifi_pref_save(w->on);
			if (!w->on) {
				if (!result_message(a, T(S_MENU_WIFI), T(S_WIFI_DID_NOT_COME_UP)))
					return MENU_STAY;
			}
			if (w->on) wifi_begin_scan(w);
		}
		return MENU_STAY;
	}

	if (net_row) {
		char psk[80] = "";
		bool ok;

		/* A saved network already has its passphrase in wpa_supplicant.conf,
		 * so asking again would be asking the user to retype something the
		 * device is holding. An open network has none to ask for. */
		if (w->nets[k].secured && !w->nets[k].known) {
			kb_result kr = keyboard_prompt(a, w->nets[k].ssid, psk, (int)sizeof psk);
			if (kr == KB_POWER) { power_off(a); return MENU_DONE; }
			if (kr != KB_ACCEPT) return MENU_STAY;
		}

		wait_panel(a, T(S_MENU_WIFI), T(S_WIFI_CONNECTING));
		wdog_hold("Wi-Fi");
		ok = wifi_connect(w->nets[k].ssid, psk[0] ? psk : NULL);
		wdog_release();
		/* Wiped as soon as it has been handed over. It still exists in the
		 * supplicant's config, which is the point, but there is no reason for
		 * a copy to sit in the launcher's stack afterwards. */
		memset(psk, 0, sizeof psk);

		if (ok) {
			/* Connecting is turning it on, whatever the switch said. */
			wifi_pref_save(true);
			w->on = true;
			wifi_status(ssid, sizeof ssid, ip, sizeof ip);
			snprintf(status, sizeof status, T(S_WIFI_CONNECTED_TO), ssid);
		} else {
			snprintf(status, sizeof status, T(S_WIFI_CONNECT_FAILED), w->nets[k].ssid);
		}
		if (!result_message(a, T(S_MENU_WIFI), status)) return MENU_STAY;
		/* Out on success, because the job is done and the menu row behind this
		 * screen already names the network - staying in the list makes you back
		 * out by hand to see the result. On failure stay, because the next
		 * thing wanted is another try or another network, and both are here. */
		if (ok) return MENU_DONE;
		wifi_begin_scan(w);
	}
	return MENU_STAY;
}

void wifi_screen(app *a)
{
	wifi_ui w = { 0 };

	/* Entering does not switch the radio on. It used to, which made the screen
	 * impossible to leave in the off state: you opened it to turn wifi OFF and
	 * the act of opening it turned wifi on. */
	w.on = wifi_status(NULL, 0, NULL, 0) != WIFI_OFF;
	if (w.on) wifi_begin_scan(&w);

	menu_run(a, &(menu_style){ .accent = MENU_ZYPEROS,
	                           .fixed_w = menu_std_width(a) },
	         wifi_build, wifi_key, &w);
}
