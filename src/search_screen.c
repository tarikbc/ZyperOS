/* SPDX-License-Identifier: GPL-3.0-only */
#include "search_screen.h"

#include "app.h"
#include "panel.h"
#include "search.h"
#include "uisound.h"

#include <stdio.h>
#include <string.h>

/* ---------- MENU > Search Games ---------------------------------------- */

/* The query on the on-screen keyboard, then what it finds (src/search.h) as a
 * list - the title, and the system it is on. A on one picks it: the menu
 * closes, and pick_launch puts the shelf on it and starts it, as A there
 * would. B on the list goes back to the keyboard with the query still in it,
 * to change it; B on the keyboard back to the menu. The query is kept for
 * the next search. Every system's shelf but Muse's, whose cards are albums,
 * and Favorites', whose games are on their own systems' shelves too. */
typedef struct {
	app       *a;
	sr_item    hit[SR_SHOWN];
	int        n, found, pick;
	char       heading[160], note[160];
} search_view;

static int search_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	search_view *v = ctx;
	int i, n = 0;

	*heading = v->heading;
	/* The system by its name when that is short, else by its tag - "GBA",
	 * "A7800", "PS1": on the Pixel 2, "Game Boy Advance" left a title nine
	 * letters ("036 The L...") */
	for (i = 0; i < v->n && n < max - 2; i++) {
		const system_cfg *sc = &v->a->sys.systems[v->hit[i].shelf];

		rows[n++] = (menu_row){ v->hit[i].title,
		                        strlen(sc->name) <= 8 || !sc->tag[0] ? sc->name : sc->tag,
		                        true };
	}
	rows[n++] = MENU_RULE;
	if (v->found > v->n)
		snprintf(v->note, sizeof v->note, T(S_SEARCH_SOME_OF), v->n, v->found);
	else
		snprintf(v->note, sizeof v->note, TN(S_SEARCH_FOUND, v->found), v->found);
	rows[n++] = MENU_NOTE(v->note);
	return n;
}

static menu_result search_key(app *a, void *ctx, in_button key, int sel)
{
	search_view *v = ctx;

	(void)a;
	if (key != IN_ACCEPT || sel < 0 || sel >= v->n) return MENU_STAY;
	v->pick = sel;
	return MENU_DONE;
}

/* True with the game picked in *shelf and *game: the menu is to close */
bool search_screen(app *a, int *shelf, int *game)
{
	static char query[64];
	search_view v;
	sr_item *items;
	int i, k, n = 0, total = 0;
	bool picked = false;

	for (i = 0; i < a->sys.count; i++) total += sys_games(&a->view[i])->count;
	if (total <= 0 || !(items = malloc((size_t)total * sizeof *items))) {
		uisound_play(DS_SOUND_DENY);
		return false;
	}
	for (i = 0; i < a->sys.count; i++) {
		const sysview *sv = &a->view[i];

		if (is_muse(&a->sys.systems[i]) || sv->owner) continue;
		const game_list *gl = sys_games_c(sv);

		/* Every game, in its folders too: the hit is an index of
		 * sys_games, which pick_launch opens the folder of */
		for (k = 0; k < gl->count; k++)
			items[n++] = (sr_item){ gl->items[k].title, gl->items[k].name, i, k };
	}
	memset(&v, 0, sizeof v);
	v.a = a;
	while (!picked && a->running && !want_quit && !g_menu_closing) {
		menu_style st = { .fixed_w = menu_std_width(a), .accent = MENU_ZYPEROS };
		kb_result kr = keyboard_prompt(a, T(S_SEARCH_TITLE), query, (int)sizeof query);

		if (kr == KB_POWER) { power_off(a); break; }
		if (kr != KB_ACCEPT || !query[0]) break;
		v.found = sr_search(query, items, n, v.hit, SR_SHOWN);
		v.n = v.found < SR_SHOWN ? v.found : SR_SHOWN;
		if (!v.n) {
			char msg[192];

			uisound_play(DS_SOUND_DENY);
			snprintf(msg, sizeof msg, T(S_SEARCH_NONE), query);
			result_message(a, T(S_SEARCH_TITLE), msg);
			continue;
		}
		snprintf(v.heading, sizeof v.heading, T(S_SEARCH_HEADING), query);
		v.pick = -1;
		menu_run(a, &st, search_build, search_key, &v);
		if (v.pick >= 0) {
			*shelf = v.hit[v.pick].shelf;
			*game = v.hit[v.pick].game;
			picked = true;
		}
	}
	free(items);
	return picked;
}
