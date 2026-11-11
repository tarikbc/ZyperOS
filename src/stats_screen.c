/* SPDX-License-Identifier: GPL-3.0-only */
#include "stats_screen.h"
#include "keyhints.h"

#include "aout_route.h"
#include "design.h"
#include "menu.h"
#include "panel.h"
#include "stats.h"
#include "ui.h"
#include "uisound.h"
#include "zyperdraw.h"

#include <SDL.h>
#include <stdio.h>
#include <string.h>

/* Play time, most played first.
 *
 * Summarized once on entry rather than every frame: it is a fold over every
 * session ever recorded, which measured 6.3 ms cold on the card - nothing at
 * all once, and a waste sixty times a second. Nothing here writes.
 */
/* The most the list can ever be. The number actually shown is asked of
 * menu_list_fit at entry, because the answer moves with the panel's fonts
 * and the rows around the list. */
#define STATS_VISIBLE 8

/* Returns true if it sent the player to a shelf, so the menu above it closes
 * rather than redrawing over a screen that has moved on. */
bool stats_screen(app *a)
{
	char vals[STATS_VISIBLE + 1][32], labels[STATS_VISIBLE + 1][80];
	/* +3: the total row above the list, then a rule and two notes below it. */
	menu_row rows[STATS_VISIBLE + 4];
	/* The total row says more than a duration, so it gets its own buffer
	 * rather than borrowing a game's - which the device compiler caught as a
	 * truncation the host compiler let through. */
	char total[32], total_val[64], detail[192];
	stats_window win = STATS_ALL;
	bool by_sys = false;
	long now = (long)time(NULL);
	int ngames = stats_summarize(win, by_sys, now);
	int cursor = 0, top = 0;
	/* The total above the list, then a rule and two notes below it. What is
	 * left over is the list, and it is asked rather than assumed. */
	int vis = menu_list_fit(1, 1, 2);

	/* The buffers are fixed, so however much room the panel has, the list
	 * stops where they do. */
	if (vis > STATS_VISIBLE) vis = STATS_VISIBLE;
	bool done = false;
	int lv = panel_open();
	float k;

	stats_format(stats_total_seconds(), total, sizeof total);

	while (!done && !want_quit && a->running) {
		int shown = 0, i;

		/* The total is a row rather than a heading, so it lines up with the
		 * games under it and reads as one table. */
		/* The window's name IS the total's label. A row already sat here
		 * saying "Total", and a heading that also said which slice would
		 * have said the same thing twice in two type sizes. */
		snprintf(labels[0], sizeof labels[0], "%s", stats_window_name(win));
		/* "over" spelled out put "This Month  2h 16m over 143 launches" at
		 * 764px against 704 of content, and menu_draw answered by cutting
		 * the LABEL - so the row said "This Y...". The separator is the
		 * cheapest 60px on the screen. */
		snprintf(total_val, sizeof total_val, TN(S_PLAYTIME_TOTAL, stats_total_launches()),
		         total, stats_total_launches());
		rows[shown++] = (menu_row){ labels[0], total_val, false };

		for (i = top; i < ngames && shown <= vis; i++) {
			const char *tag, *file;
			long secs;
			int launches;
			const char *dot;
			int len;

			if (!stats_at(i, &tag, &file, &secs, &launches)) break;
			if (by_sys) {
				/* stats.c knows the tag and nothing else - the display name
				 * lives in systems.cfg, which is this side of the seam. A
				 * system no longer on the card keeps its tag rather than
				 * disappearing: the time was still spent. */
				int k, found = 0;

				for (k = 0; k < a->sys.count; k++)
					if (!strcmp(a->sys.systems[k].tag, tag)) {
						snprintf(labels[shown], sizeof labels[0], "%s",
						         a->sys.systems[k].name);
						found = 1;
						break;
					}
				if (!found)
					snprintf(labels[shown], sizeof labels[0], "%s", tag);
			} else {
			/* The extension is noise in a list of names. Last dot only, so a
			 * title with dots of its own keeps them. */
			dot = strrchr(file, '.');
			len = dot && dot != file ? (int)(dot - file) : (int)strlen(file);
			{
				/* Extension off, then the same title rule the shelf uses, so
				 * "Contra (USA).zip" reads as "Contra" here too. It was
				 * printing the cataloging: a column of names where every one
				 * ends in a region and a revision is a column you cannot
				 * scan. */
				char bare[LIB_NAME];

				snprintf(bare, sizeof bare, "%.*s", len, file);
				const char *folder = NULL;
				for (int k = 0; k < a->sys.count; k++)
					if (!strcmp(a->sys.systems[k].tag, tag)) {
						folder = a->sys.systems[k].folder;
						break;
					}
				lib_display_title(db_lib(), folder, file, bare,
				                  labels[shown], sizeof labels[0]);
			}
			}
			stats_format(secs, vals[shown], sizeof vals[0]);
			/* live, not selected. The selection is menu_draw's `sel`
			 * argument; passing it here instead drew every other row
			 * quiet and highlighted none of them. */
			rows[shown] = (menu_row){ labels[shown], vals[shown], true };
			shown++;
		}

		if (ngames == 0) {
			snprintf(labels[1], sizeof labels[1], "%s", T(S_PLAYTIME_NOTHING));
			rows[1] = (menu_row){ labels[1], NULL, false };
			shown = 2;
			detail[0] = '\0';
		} else {
			/* What the selected row knows beyond its total. This is where
			 * the extra columns went: three numbers beside a game name in
			 * 704px reads as a spreadsheet nobody can scan, and only one row
			 * is being looked at anyway. */
			long longest = 0, last = 0, secs = 0;
			int lost = 0, n = 0;
			char lg[16], ag[16];
			size_t at;

			stats_at(cursor, NULL, NULL, &secs, &n);
			stats_extra(cursor, &longest, &last, &lost);
			stats_format(longest, lg, sizeof lg);
			stats_ago(last, now, ag, sizeof ag);
			/* Written in full and left long. Four facts do not fit 704px and
			 * are not meant to: the note scrolls. */
			snprintf(detail, sizeof detail, TN(S_PLAYTIME_DETAIL, n), n, lg, ag);
			/* Only when it happened. A "0 lost" on every row would train the
			 * eye to skip the one place the number matters. */
			at = strlen(detail);
			if (lost) snprintf(detail + at, sizeof detail - at, TN(S_PLAYTIME_LOST, lost), lost);
		}

		if (detail[0]) {
			rows[shown++] = MENU_RULE;
			rows[shown++] = MENU_NOTE(detail);
		}
		rows[shown++] = MENU_NOTE(keyhints_legend((key_hint[]){
			{ "A", T(S_KEYHINT_GO) },
			{ "Y", by_sys ? T(S_KEYHINT_BY_GAME) : T(S_KEYHINT_BY_SYSTEM) },
			{ "L1/R1", T(S_KEYHINT_WINDOW) } }, 3));

		plat_input_poll(&a->in);
		ui_sounds_follow();
		if (a->in.quit_requested) { a->running = false; return false; }
		if (a->in.pressed[IN_POWER] || idle_due(a)) { power_off(a); return false; }
		if (menu_leaving(a)) done = true;
		if (a->in.pressed[IN_SELECT]) muse_open(a, NULL, NULL);

		/* A goes to the row under the cursor: the game on its own shelf, or in
		 * by-system mode that system's. Not a launch.
		 *
		 * This screen is a still of what has been played, and it knows a tag
		 * and a file. That is not enough to start a game - the core, the
		 * folder, the display mode and the save paths all hang off the shelf.
		 * It could resolve every one of them, and then there would be a second
		 * launch path to keep in step with the first. Landing on the shelf
		 * costs one button and arrives where launching already works, with the
		 * art, the autosave dot and the game's own menu.
		 *
		 * A row can outlive its ROM - these are keyed on tag and file and
		 * survive the card changing - so a miss does nothing rather than
		 * guessing, which is the answer the boot-resume path gives too. */
		if (a->in.pressed[IN_ACCEPT])
			uisound_play(ngames > 0 ? DS_SOUND_ACCEPT : DS_SOUND_DENY);
		if (a->in.pressed[IN_ACCEPT] && ngames > 0) {
			const char *tag = NULL, *file = NULL;
			int si, gi;

			if (stats_at(cursor, &tag, &file, NULL, NULL))
				for (si = 0; si < a->sys.count; si++) {
					if (strcmp(a->sys.systems[si].tag, tag) != 0) continue;
					if (a->view[si].list.count == 0) break;
					a->sys_cursor = si;
					/* By system there is no file to look for, so the shelf
					 * keeps whichever game it was already on. */
					if (!by_sys)
						for (gi = 0; gi < a->view[si].list.count; gi++)
							if (!strcmp(a->view[si].list.items[gi].file, file)) {
								a->view[si].cursor = gi;
								break;
							}
					enter_system(a);
					panel_close(a, lv, false);
					return true;
				}
		}

		{	/* A view change re-folds the same rows under a new question, so
			 * the cursor cannot survive it - row 3 of "This Week by system"
			 * is not row 3 of anything else. */
			bool changed = false;

			if (in_repeat(&a->in, IN_RIGHT)) {
				step_sound(a, IN_RIGHT, win, (win + 1) % STATS_WINDOWS);
				win = (win + 1) % STATS_WINDOWS; changed = true;
			}
			if (in_repeat(&a->in, IN_LEFT)) {
				step_sound(a, IN_LEFT, win, (win + STATS_WINDOWS - 1) % STATS_WINDOWS);
				win = (win + STATS_WINDOWS - 1) % STATS_WINDOWS;
				changed = true;
			}
			if (a->in.pressed[IN_Y]) {
				uisound_play(DS_SOUND_ACCEPT);
				by_sys = !by_sys; changed = true;
			}
			if (changed) {
				now = (long)time(NULL);
				ngames = stats_summarize(win, by_sys, now);
				stats_format(stats_total_seconds(), total, sizeof total);
				cursor = top = 0;
			}
		}

		if (ngames > 0) {
			if (in_repeat(&a->in, IN_DOWN)) {
				step_sound(a, IN_DOWN, cursor, cursor < ngames - 1 ? cursor + 1 : cursor);
				if (cursor < ngames - 1) cursor++;
			}
			if (in_repeat(&a->in, IN_UP)) {
				step_sound(a, IN_UP, cursor, cursor > 0 ? cursor - 1 : cursor);
				if (cursor > 0) cursor--;
			}
			/* The window follows the cursor rather than the other way round,
			 * so a long list scrolls one row at a time from either end. */
			if (cursor < top) top = cursor;
			if (cursor >= top + vis) top = cursor - vis + 1;
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
		/* +1 because the total is row 0 and the games start under it. */
		menu_draw(a, T(S_PLAYTIME_TITLE), rows, shown,
		          ngames ? cursor - top + 1 : -1, menu_std_width(a),
		          MENU_ZYPEROS);
		plat_draw_osd(a->r);
		plat_present();
		ui_frame_rest(8);
	}
	panel_close(a, lv, false);
	return false;
}
