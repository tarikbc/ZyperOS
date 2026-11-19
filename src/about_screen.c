/* SPDX-License-Identifier: GPL-3.0-only */
#include "about_screen.h"
#include "ds/mark.h"   /* ds_bar_in */

#include "about.h"
#include "aout_route.h"
#include "app.h"
#include "devmode.h"
#include "ingame_screen.h"
#include "menu_panel.h"
#include "panel.h"
#include "uisound.h"
#include "usblink.h"
#include "zyperdraw.h"

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

/* ---------- About --------------------------------------------------------- */

/* Zyper's photo on The name's page, at the body's right, the story wrapped
 * in the column left of it (2026-11-06: "can we put a photo of him in
 * that section?"). res/zyper.png, beside the launcher on the card: 300 x 400
 * with radius.card's corners already cut, and no camera data. */
#define ABOUT_PHOTO_W 300
#define ABOUT_PHOTO_H 400
#define ABOUT_PHOTO_GAP 40
/* The README's Credits, one row each: who, and what of theirs ZyperOS runs
 * on, short enough that no row has to scroll; then the people who help with
 * the other handhelds, and the note that every license itself is in
 * Licenses/ on the card. A name is the same in every language and is written
 * here; a word is an ID, its name NULL, read through T() as About opens
 * (about_pages). */
typedef struct {
	const char *who;
	str_id      who_s;
	const char *what;
	str_id      what_s;
} about_thanks;
static const about_thanks ABOUT_THANKS[] = {
	{ "Eric Reinsmidt", 0, "TortOS, Diatom", 0 },
	{ "plastron", 0, NULL, S_ABOUT_THANKS_PLASTRON },
	{ NULL, 0, NULL, S_ABOUT_THANKS_KERNEL },   /* the handheld's (about_pages) */
	{ "Buildroot", 0, NULL, S_ABOUT_THANKS_BUILDROOT },
	{ "libretro", 0, NULL, S_ABOUT_THANKS_LIBRETRO },
	{ "PortMaster", 0, NULL, S_ABOUT_THANKS_PORTMASTER },
	{ "plorpOS", 0, "DSperate, FAKE-08", 0 },
	{ "OpenBOR Team", 0, "OpenBOR", 0 },
	{ NULL, S_ABOUT_THANKS_MENU_MUSIC, "OpenGameArt", 0 },
	{ "Lexend", 0, NULL, S_ABOUT_THANKS_LEXEND },
	{ "SDL, Mesa", 0, NULL, S_ABOUT_THANKS_SDL },
	{ "FFmpeg, SQLite", 0, NULL, S_ABOUT_THANKS_FFMPEG },
	{ "minimp3, stb_vorbis", 0, NULL, S_ABOUT_THANKS_MINIMP3 },
	{ "Linux, U-Boot", 0, NULL, S_ABOUT_THANKS_LINUX },
	{ "Temurin, DejaVu", 0, NULL, S_ABOUT_THANKS_TEMURIN },
};

/* The people who help with the other handhelds ZyperOS runs on: a name and
 * the handheld, both written as they are in every language. One row a
 * person, in the order they are thanked, above the { NULL, NULL } that ends
 * it, as
 *
 *	{ "Their name", "TrimUI Brick" },
 *
 * Under its own heading, Help with other handhelds, which is there only
 * while this has a row. */
static const struct { const char *who, *handheld; } ABOUT_HANDHELD_THANKS[] = {
	{ "autobiogphnation", "TrimUI Brick" },   /* kit 2's first tester, all 12 steps */
	{ NULL, NULL }
};

#define ARRAY_N(x) ((int)(sizeof (x) / sizeof (x)[0]))

/* The story laid out for the panel, and the thanks with their note; once,
 * when About opens: neither changes while it is open */
void about_pages(app *a, about_view *v)
{
	char path[CFG_STR * 2];
	int i, n = 0, wrap = menu_std_width(a);

	/* No photo (a host shot's card, say) is the story at the full width */
	snprintf(path, sizeof path, "%s/zyper.png", P_ROOT);
	if (!v->photo && (v->photo = IMG_LoadTexture(a->r, path)))
		SDL_SetTextureBlendMode(v->photo, SDL_BLENDMODE_BLEND);
	if (v->photo) wrap -= ABOUT_PHOTO_W + ABOUT_PHOTO_GAP;
	/* Laid out once, not twice: it holds still and is walked by hand */
	v->nstory = syn_layout(T(S_ABOUT_STORY), wrap, v->story_lines, v->story, &v->story_loop);
	if (v->story_loop > 0) v->nstory = v->story_loop - SYN_LOOP_GAP;
	v->story_loop = 0;
	for (i = 0; i < ARRAY_N(ABOUT_THANKS); i++) {
		const about_thanks *t = &ABOUT_THANKS[i];

		/* Its kernel is the handheld's: ROCKNIX's for the Pixel 2,
		 * Allwinner's for the Brick (the profile's `kernel`) */
		if (t->what_s == S_ABOUT_THANKS_KERNEL) {
			const device_profile *d = plat_profile();

			snprintf(v->kernel, sizeof v->kernel, T(S_ABOUT_THANKS_KERNEL), d->kernel.of);
			v->thanks[n++] = (menu_row){ d->kernel.by, v->kernel, false };
			continue;
		}
		v->thanks[n++] = (menu_row){ t->who ? t->who : T(t->who_s),
		                             t->what ? t->what : T(t->what_s), false };
	}
	/* The people who help with the other handhelds, under a heading of
	 * their own (2026-11-16: "I'll thank those who help me with other
	 * devices in that page") */
	if (ABOUT_HANDHELD_THANKS[0].who) {
		v->thanks[n++] = MENU_HR;
		v->thanks[n++] = MENU_NOTE(T(S_ABOUT_THANKS_HANDHELDS));
		for (i = 0; ABOUT_HANDHELD_THANKS[i].who && n < ABOUT_THANKS_ROWS - 2; i++)
			v->thanks[n++] = (menu_row){ ABOUT_HANDHELD_THANKS[i].who,
			                             ABOUT_HANDHELD_THANKS[i].handheld, false };
	}
	/* Under the rule, held at the foot while the rest is walked */
	v->thanks[n++] = MENU_RULE;
	v->thanks[n++] = MENU_NOTE(T(S_ABOUT_LICENSES));
	v->nthanks = n;
}

/* One width for every page, grown only if a value needs it */
int about_width(app *a, const about_view *v)
{
	int w = menu_std_width(a), m;

	if ((m = menu_measure(v->details, ABOUT_DETAIL_ROWS, T(S_ABOUT_HEADING))) > w) w = m;
	if ((m = menu_measure(v->system, v->nsystem, T(S_ABOUT_HEADING))) > w) w = m;
	if ((m = menu_measure(v->thanks, v->nthanks, T(S_ABOUT_THANKS))) > w) w = m;
	return w;
}

/* The first page's fifth row, only when there is something to say: Developer
 * Mode is on, or its hold is under way (devmode.h) */
void about_refresh(about_view *v, unsigned now)
{
	int pct;
	bool charging;

	if (!v->primed || now - v->refreshed >= 2000) {
		char ssid[WIFI_SSID_MAX], ip[64];
		wifi_state ws = wifi_status(ssid, sizeof ssid, ip, sizeof ip);
		v->primed = true;
		v->refreshed = now;
		snprintf(v->addr, sizeof v->addr, "%s", ws == WIFI_CONNECTED
		         ? (ip[0] ? ip : T(S_ABOUT_NO_ADDRESS_YET)) : T(S_ABOUT_NOT_CONNECTED));
		about_read(&v->info, P_CARD, plat_battery_info_path());
		about_detail_rows(&v->info, v->details, &v->values);
	}
	/* Use the shared cable cache, so About follows charger changes as the
	 * status corner does instead of waiting for the storage refresh. */
	if (!battery_now(&pct, &charging))
		snprintf(v->batt, sizeof v->batt, "%s", T(S_ABOUT_UNKNOWN));
	else if (charging)
		snprintf(v->batt, sizeof v->batt, T(S_ABOUT_BATTERY_CHARGING), pct);
	else
		snprintf(v->batt, sizeof v->batt, "%d%%", pct);
	snprintf(v->ver, sizeof v->ver, "%s", ZYPEROS_VERSION);
	snprintf(v->up, sizeof v->up, T(S_ABOUT_UPTIME), now / 3600000, (now / 60000) % 60);
	v->system[0] = (menu_row){ T(S_ABOUT_VERSION), v->ver, false };
	v->system[1] = (menu_row){ T(S_ABOUT_ADDRESS), v->addr, false };
	v->system[2] = (menu_row){ T(S_ABOUT_BATTERY), v->batt, false };
	v->system[3] = (menu_row){ T(S_ABOUT_AWAKE_FOR), v->up, false };
	v->nsystem = 4;
	if (devmode_unlocked(db_dev())) {
		v->system[v->nsystem++] = (menu_row){ T(S_SYSMENU_DEVELOPER), T(S_MENU_ON_TITLE), false };
	} else if (devmode_hold_permille(&v->hold, now) > 0) {
		/* The row comes with the hold, its bar drawn over its value (about_draw) */
		v->system[v->nsystem++] = (menu_row){ T(S_SYSMENU_DEVELOPER), NULL, false };
	}
}

void about_draw(app *a, const about_view *v, int page)
{
	const menu_row *rows = v->system;
	const char *heading = T(S_ABOUT_HEADING);
	char count[16];
	int n = v->nsystem;

	int loop_at = 0;

	switch (page) {
	case 1: rows = v->details; n = ABOUT_DETAIL_ROWS; break;
	case 2:
		rows = v->story; n = v->nstory; loop_at = v->story_loop;
		heading = T(S_ABOUT_THE_NAME);
		break;
	case 3: rows = v->thanks; n = v->nthanks; heading = T(S_ABOUT_THANKS); break;
	}
	/* The panel draws posed into a pass of its own while it opens; the
	 * photo comes once it is at rest, rather than unscaled over it */
	bool rest = g_pframe.panel.scale == 1.0f && g_pframe.panel.alpha == 1.0f;

	snprintf(count, sizeof count, "%d / %d", page + 1, ABOUT_PAGES);
	/* The story and the thanks hold still where Up and Down have put them */
	g_menu_scroll_hold = page == 2 ? v->story_scroll : page == 3 ? v->thanks_scroll : -1;
	if (page != 2 && page != 3) g_menu_scroll_span = 0;
	menu_draw_caption(a, heading, rows, n, -1, v->width, MENU_ZYPEROS, NULL, false, loop_at,
	                  count);
	g_menu_scroll_hold = -1;
	/* Developer Mode's hold, as the one bar every hold in the system is
	 * (UI_BAR_H), in the last row's value column once the panel is at rest */
	{
		int pm = page == 0 ? devmode_hold_permille(&v->hold, plat_now_ms()) : 0;

		if (pm > 0 && rest && g_menu_body.h > 0 && v->nsystem > 4) {
			int pitch = g_menu_body.h / v->nsystem;
			int x0 = g_menu_body.x + g_menu_body.w * 62 / 100;   /* clear of the longest label */
			SDL_Rect bar = { x0, g_menu_body.y + g_menu_body.h - pitch / 2 - UI_BAR_H / 2,
			                 g_menu_body.x + g_menu_body.w - x0, UI_BAR_H };

			/* in accent-ink, as every other hold fills (it filled in text
			 * until 1.0.6) */
			ds_bar_in(a->r, &bar, bar.w * pm / 1000, ui_col(theme()->track, 255),
			          ui_col(theme()->accent_ink, 255));
		}
	}
	if (page == 2 && v->photo && rest && g_menu_body.h > 0) {
		/* At the body's right, middle of its height, smaller only if the
		 * body is shorter than the photo */
		int h = g_menu_body.h < ABOUT_PHOTO_H ? g_menu_body.h : ABOUT_PHOTO_H;
		int w = h * ABOUT_PHOTO_W / ABOUT_PHOTO_H;
		SDL_Rect r = { g_menu_body.x + g_menu_body.w - w,
		               g_menu_body.y + (g_menu_body.h - h) / 2, w, h };

		SDL_RenderCopy(a->r, v->photo, NULL, &r);
	}
}

void about_screen(app *a)
{
	about_view v = { 0 };
	int page = 0, drawn = -1;   /* the page the scroll's span was last measured on */
	bool done = false;
	int lv = panel_open();
	float k;

	while (!done && !want_quit && a->running) {
		unsigned now = plat_now_ms();
		bool refresh = !v.primed || now - v.refreshed >= 2000;
		int rows_was = v.nsystem;
		about_refresh(&v, now);
		if (!v.nstory) about_pages(a, &v);
		/* Every page keeps one width. Grow only if their values need it,
		 * as the shared menu renderer does, instead of clipping a value.
		 * A row that comes (Developer Mode) is measured too. */
		if (refresh || v.nsystem != rows_was) v.width = about_width(a, &v);
		plat_input_poll(&a->in);
		muse_poll();
		aout_apply(false);
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);
		if (in_repeat(&a->in, IN_LEFT)) {
			moved_sound(a, IN_LEFT);
			page = (page + ABOUT_PAGES - 1) % ABOUT_PAGES;
		}
		if (in_repeat(&a->in, IN_RIGHT)) {
			moved_sound(a, IN_RIGHT);
			page = (page + 1) % ABOUT_PAGES;
		}
		/* Developer Mode's unlock (devmode.h): L2 and R2 held together for
		 * five seconds, on the first page. Another page, or letting go of
		 * either, starts again; once it is on there is nothing left to hold.
		 * Held as taps on a device whose L2 and R2 are taps, as buttons on
		 * one whose are buttons (dev_l2r2_held). */
		if (page != 0 || devmode_unlocked(db_dev())) {
			devmode_hold_reset(&v.hold);
		} else if (devmode_hold_step(&v.hold,
		                             dev_l2r2_held(&plat_profile()->chords,
		                                           plat_tap_held(IN_L2), plat_tap_held(IN_R2),
		                                           a->in.down[IN_L2], a->in.down[IN_R2]),
		                             plat_now_ms())) {
			menu_row said[2] = { { T(S_DEVMODE_IS_ON), NULL, false },
			                     { T(S_DEVMODE_WHERE), NULL, false } };

			devmode_set(db_dev(), true);
			usblink_release_dev(true);
			fprintf(stderr, "devmode: on\n");
			uisound_play(DS_SOUND_ACCEPT);
			timed_message(a, T(S_SYSMENU_DEVELOPER), said, 2, 0, MENU_ZYPEROS);
			v.refreshed = 0;                  /* the row, at once */
		}
		/* The story and the thanks: Up and Down walk them a line at a time,
		 * held they repeat, and they stop at the top and at the bottom. They
		 * never move by themselves. The span is the page's own, from its
		 * last draw, so a page just turned to is walked from its next. */
		if ((page == 2 || page == 3) && drawn == page && g_menu_scroll_line > 0) {
			int *sc = page == 2 ? &v.story_scroll : &v.thanks_scroll;
			int was = *sc, lim = g_menu_scroll_span;

			if (in_repeat(&a->in, IN_UP)) {
				*sc = was > g_menu_scroll_line ? was - g_menu_scroll_line : 0;
				step_sound(a, IN_UP, was, *sc);
			}
			if (in_repeat(&a->in, IN_DOWN)) {
				was = *sc;
				*sc = was + g_menu_scroll_line < lim ? was + g_menu_scroll_line : lim;
				step_sound(a, IN_DOWN, was, *sc);
			}
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		about_draw(a, &v, page);
		drawn = page;
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);
	if (v.photo) SDL_DestroyTexture(v.photo);
}

/* MENU > Controls: what every button does, a page per place. Read-only like
 * About, and paged with left and right the way Play Time changes its window -
 * the whole list at once is thirty rows nobody reads. */
void controls_screen(app *a)
{
	ctl_page page = CTL_SHELF;
	bool done = false;
	int lv = panel_open();
	float k;

	ctl_set_chords(&plat_profile()->chords);

	while (!done && !want_quit && a->running) {
		menu_row rows[CTL_MAX_ROWS];
		char head[80], count[16], keys[CTL_MAX_ROWS][CTL_KEYS_MAX];
		int n = ctl_rows(page, rows);

		/* the button column in the handheld's own glyphs (DESIGN.md 11.9) */
		ctl_glyph_rows(rows, n, keys);

		snprintf(head, sizeof head, T(S_PANEL_CONTROLS), ctl_page_name(page));
		/* which page of how many, at the heading's right (DESIGN.md 11.3) */
		snprintf(count, sizeof count, "%d / %d", (int)page + 1, CTL_PAGES);

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; return; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return; }
		if (menu_leaving(a)) done = true;
		/* SELECT opens Muse here as it does on every other menu screen -
		 * which is one of the things this page exists to tell you. */
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		if (in_repeat(&a->in, IN_RIGHT)) {
			step_sound(a, IN_RIGHT, page, (page + 1) % CTL_PAGES);
			page = (ctl_page)((page + 1) % CTL_PAGES);
		}
		if (in_repeat(&a->in, IN_LEFT)) {
			step_sound(a, IN_LEFT, page, (page + CTL_PAGES - 1) % CTL_PAGES);
			page = (ctl_page)((page + CTL_PAGES - 1) % CTL_PAGES);
		}
		if (in_repeat(&a->in, IN_VOLUP))    plat_volume_nudge(+1);
		if (in_repeat(&a->in, IN_VOLDN))    plat_volume_nudge(-1);
		if (in_repeat(&a->in, IN_BRIGHTUP)) plat_brightness_nudge(+1);
		if (in_repeat(&a->in, IN_BRIGHTDN)) plat_brightness_nudge(-1);

		if (!panel_frame(a, lv, &k)) continue;
		tick_tint(a);
		tick_theme(a);
		draw_shelf(a);
		ui_scrim_k(a->r, 0, k);
		menu_draw_caption(a, head, rows, n, -1, menu_std_width(a), MENU_ZYPEROS, NULL,
		                  false, 0, count);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);
}
