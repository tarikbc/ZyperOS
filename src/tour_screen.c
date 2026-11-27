/* SPDX-License-Identifier: GPL-3.0-only */
#include "tour_screen.h"
#include "menukeys_screen.h"

#include "aout_route.h"
#include "app.h"
#include "bgm.h"
#include "panel.h"
#include "shelf_draw.h"
#include "tour.h"
#include "tourdraw.h"
#include "uisound.h"
#include "zyperdraw.h"
#include "zypermark.h"
#include "levels.h"

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

/* ---------- the tutorial -------------------------------------------------- */

/* DESIGN.md 11.10: the first thing a new handheld shows, and again from MENU >
 * Show Tutorial. The steps are src/tour.c's and the screens src/tourdraw.c's;
 * this is the loop that reads the buttons, plays the sounds, turns the real
 * volume and brightness, and lends the drawing Zyper. */

/* Zyper as the tutorial poses him, by the menu's draw_zyper_pose: the high
 * brow following his tilt (zyper_brow_lift) as the menu's does, and on Beard
 * his shadow first, where he stands, so a hop or a breath lifts him off it. */
void tour_zyper_draw(void *ctx, float cx, float cy, float h, const tour_zyper *z)
{
	app *a = ctx;

	SDL_SetRenderDrawBlendMode(a->r, SDL_BLENDMODE_BLEND);
	if (z->shadow > 0.0f)
		zyper_shadow_draw(a->r, cx, cy, h, z->pose.tilt, z->shadow * (1.0f - z->dim));
	draw_zyper_pose(a->r, cx, cy, h, &z->pose, zyper_brow_lift(z->pose.tilt), z->dim,
	                z->alpha);
}

/* The power step's stage plays the power-off's own frames, small. */
void tour_poweroff_draw(void *ctx, unsigned t, float cx, float cy, float h,
                        float from_x, Uint8 alpha)
{
	poweroff_zyper((app *)ctx, t, cx, cy, h, from_x, alpha);
}

/* A sound the tutorial asks for (DESIGN.md 9): accept for the welcome's and
 * the finish's A, move or edge for the first move, reward for each lesson,
 * deny for a wrong press and back for START; TOUR_QUIET for none. */
static void tour_sound(int s)
{
	if (s != TOUR_QUIET) uisound_play((ds_sound)s);
}

/* "Show Tutorial is in MENU." over the shelf, after a skip at the first
 * start: for hold-short, in in move and out in fade-out, as a one-line
 * message is (8.2, 11.8). Any press but the levels' puts it away at once,
 * from where it is, and is taken by it: nothing waits on a message. */
static void tour_skip_message(app *a)
{
	const char *s = T(S_TOURRUN_SKIP_MESSAGE);
	const uint32_t at = plat_now_ms();
	uint32_t out = at + DS_MS_HOLD_SHORT, now;

	while (a->running && !want_quit &&
	       (int32_t)((now = plat_now_ms()) - out) < DS_MS_FADE_OUT) {
		float op;
		int b;

		plat_input_poll(&a->in);
		/* As tour_run follows them, and menu_run_body */
		mp4_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		levels_keys(&a->in, BOOST_HIDE);
		for (b = 0; b < IN_COUNT; b++)
			if (a->in.pressed[b] && b != IN_VOLUP && b != IN_VOLDN &&
			    b != IN_BRIGHTUP && b != IN_BRIGHTDN && (int32_t)(now - out) < 0) {
				uisound_play(DS_SOUND_BACK);
				out = now;
			}
		op = tour_ease(TC_OUT, now, at, DS_MS_MOVE)
		   * (1.0f - fminf(1.0f, fmaxf(0.0f, (float)(int32_t)(now - out) / DS_MS_FADE_OUT)));
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		tour_message(a->r, s, op);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(6);
	}
	redraw_now();
}

/* The tutorial, from its welcome until it is finished or skipped, or POWER
 * turns the handheld off in the middle of it, which leaves it to run again
 * at the next start. `first`: the first start's, which after a skip says
 * where to find it again; from the menu, the player knows. */
/* The tutorial's choices (DESIGN.md 11.10): the settings as they are, as
 * tour_choice_name's indexes, for its steps to start on. */
void tour_settings(const app *a, int at[TOUR_CHOICES])
{
	at[0] = theme_index() == DS_DARK ? 1 : 0;
	at[1] = (int)a->home_layout;          /* Spotlight, Gallery, Grid: home_layout's order */
	at[2] = uisound_setting();
	at[3] = bgm_on() ? 0 : 1;
}

/* Choice `c`'s `v` applied, to be seen and heard as it is looked at, and
 * with `save` written to the key MENU's row writes, as it is kept. The home
 * screen's layout is only previewed until it is kept. */
static void tour_apply(app *a, int c, int v, bool save)
{
	if (c == 0) {
		const appearance k = v ? APPEAR_DARK : APPEAR_LIGHT;
		const int was = theme_index();

		theme_init(k);
		if (save) {
			db_set_str(db_dev(), "appearance", appearance_id(k));
			boot_theme_changed();
		}
		if (theme_index() != was) theme_changed(a);
	} else if (c == 1 && save) {
		a->home_layout = v == 2 ? HOME_GRID : v ? HOME_GALLERY : HOME_SPOTLIGHT;
		db_set_str(db_dev(), "home", home_id(a->home_layout));
		home_reset(&a->home, a->sys_cursor, a->sys.count, plat_now_ms());
	} else if (c == 2) {
		uisound_set(v);
		if (save) db_set_str(db_dev(), "uisounds", uisound_setting_id(v));
	} else if (c == 3) {
		/* Heard while On is looked at: at the first start the music waits
		 * for the shelf, which comes after the tutorial, so it is let go */
		bgm_set_on(v == 0);
		if (v == 0) bgm_hold(BGM_HOLD_START, false);
		if (save) db_set_str(db_dev(), "bgm", bgm_on_id(v == 0));
	}
}

void tour_run(app *a, bool first)
{
	const tour_hooks hk = { tour_zyper_draw, tour_poweroff_draw, a, P_ROOT };
	tour t;
	bool saved = false, shown = false, kept[TOUR_CHOICES] = { false };
	int snd[IN_COUNT], n, i, at[TOUR_CHOICES], looked[TOUR_CHOICES];
	tour_step was_step = TOUR_WELCOME;
	/* What Appearance was, Auto too, for a theme looked at and not kept */
	const appearance look_was = theme_appearance();

	bool lang_kept = false;

	/* MENU's chords as MENU > System > Hotkeys has them, for its lesson
	 * and the lock's, and how long SELECT and START open the lock */
	unsigned unlocks = mk_unlocks();
	{
		const mk_table *m = mk_now();
		char shot[16], lock[16];

		mk_chord(m, MK_SHOT, shot, sizeof shot);
		mk_chord(m, MK_LOCK, lock, sizeof lock);
		tour_menu_keys(mk_in_of(m->key[MK_SHOT]), mk_in_of(m->key[MK_LOCK]), shot, lock);
		tour_lock_hold(m->hold_ms);
	}
	tour_menu_word(dev_menu_word(plat_profile()));
	tour_start(&t, plat_now_ms(), &plat_profile()->chords,
	           dev_tutorial_model(plat_profile(), &plat_profile()->chords));
	tour_lang_from(&t, (int)i18n_lang());
	tour_settings(a, at);
	tour_choices_from(&t, at);
	memcpy(looked, at, sizeof looked);
	while (!t.over && a->running && !want_quit) {
		uint32_t now;

		plat_input_poll(&a->in);
		/* Where the sound goes, and MP4's answers, as menu_run_body follows
		 * them: a cable or a headset during the tour is heard at once - its
		 * sounds' route is set here before its first press, at boot too -
		 * and an album playing under MENU > Show Tutorial goes on to its
		 * next track (#45). */
		mp4_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; break; }
		now = plat_now_ms();
		if (idle_due(a)) { power_off(a); break; }
		/* POWER is the tutorial's in its own step, so it can be tried, and
		 * anywhere else turns the handheld off as on every screen. */
		if (a->in.pressed[IN_POWER]) {
			if (!tour_takes_power(&t)) { power_off(a); break; }
			tour_sound(tour_press(&t, TK_POWER, now));
		}
		/* The levels' keys work in every step, as on every screen, and
		 * answer their own. */
		levels_keys(&a->in, BOOST_HIDE);
		/* MENU and its button locked the handheld during that poll, as on
		 * any screen, and SELECT and START opened it: the lock lesson's
		 * press (and nothing in any other step) */
		if (mk_unlocks() != unlocks) {
			unlocks = mk_unlocks();
			tour_sound(tour_press(&t, TK_LOCK, now));
		}
		tour_set_music(&t, bgm_count() > 0);
		n = tour_buttons(&t, a->in.pressed, now, snd);
		/* A choice looked at is applied before its press is heard, so the
		 * sounds' step plays in the voice it lands on; a kept one is saved */
		{
			const int c = tour_choice_of(t.step);

			/* The menu music plays from the moment its step comes on, on
			 * the On it starts on (My note: "have the menu music play the
			 * music when it's on in onboarding") */
			if (t.step != was_step && t.step == TOUR_MUSIC) tour_apply(a, c, t.pick[c], false);
			was_step = t.step;
			if (c >= 0 && t.pick[c] != looked[c]) {
				tour_apply(a, c, t.pick[c], false);
				looked[c] = t.pick[c];
			}
			if (c >= 0 && t.done && !kept[c]) {
				tour_apply(a, c, t.kept[c], true);
				kept[c] = true;
			}
			/* Skipped with one looked at and not kept: what was saved */
			if (c >= 0 && t.over && !t.done && looked[c] != t.kept[c]) {
				if (c == 0) {
					const int was = theme_index();

					theme_init(look_was);
					if (theme_index() != was) theme_changed(a);
				} else {
					tour_apply(a, c, t.kept[c], false);
				}
				looked[c] = t.kept[c];
			}
		}
		for (i = 0; i < n; i++) tour_sound(snd[i]);
		{
			/* The Pixel 2's L2 and R2 come only as taps on release:
			 * held, the handheld has the platform's word for them */
			bool held[IN_COUNT];

			memcpy(held, a->in.down, sizeof held);
			held[IN_L2] = held[IN_L2] || plat_tap_held(IN_L2);
			held[IN_R2] = held[IN_R2] || plat_tap_held(IN_R2);
			tour_hands(&t, held, plat_menu_held(), now);
		}
		tour_tick(&t, now);
		/* The language step (DESIGN.md 11.30): the screen in the language
		 * under the cursor from the frame it lands on, going round or
		 * moved, and the one kept saved once A leaves the step or START
		 * skips the rest */
		if (t.step == TOUR_LANG && !t.over) {
			const lang f = (lang)tour_lang_focus(&t, now);

			if (f != i18n_lang()) language_apply(a, f, false);
		} else if (!lang_kept) {
			language_apply(a, (lang)tour_lang_kept(&t), true);
			lang_kept = true;
		}
		tick_theme(a);
		/* Seen once past the power step's press, so the second POWER in
		 * its reward, which turns the handheld off, leaves it seen */
		if (!saved && tour_seen(&t)) {
			tour_save(db_dev(), &t);
			saved = true;
		}
		if (t.over) break;
		/* A whole frame, which the menu it was opened from is not on: that
		 * panel is gone, not left for the shelf to draw leaving */
		g_frame_no++;
		tour_draw(a->r, &t, now, &hk);
		plat_draw_osd(a->r);
		plat_present();
		/* Said once it is on the screen, which check-tour-shots waits for */
		if (!shown) fprintf(stderr, "tour: %s, on the screen\n", first ? "the first start's" : "again");
		shown = true;
		ui_frame_rest(6);
	}
	tour_draw_free();
	if (!saved) tour_save(db_dev(), &t);
	if (t.over && t.skipped && first) tour_skip_message(a);
	redraw_now();
}
