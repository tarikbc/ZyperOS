/* SPDX-License-Identifier: GPL-3.0-only */
#include "gamern_screen.h"

#include "app.h"
#include "collect.h"
#include "gamedel.h"
#include "gamern.h"
#include "keyboard.h"
#include "panel.h"
#include "shelfbuild.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* ---- Rename (DESIGN.md 11.41, src/gamern.h) --------------------------------
 *
 * The details page's row above Delete Game. A opens the keyboard on the
 * game's name, its file's stem, as the card has it; START takes the new
 * one. A name no card takes, or one another game or file has, says why
 * and opens the keyboard again on what was typed. Then the plan, held
 * against every other game on the shelves as Delete Game's is, is run, the
 * library's rows follow, and the shelf puts the game in its place under its
 * new name, its title emptied so the new name is what shows. True when it
 * was renamed; `said` the message either way, empty for none.
 *
 * A game whose file keeps its name (gr_shown_only: an arcade set, a port)
 * has the same keyboard on the name it shows, under its own title, its
 * quiet line led by a note that the file keeps its name, and the name it
 * would have back dimmed in the field once that is empty: START sets the
 * name every screen shows (gr_shown_set), an empty field the one it had
 * back, and the shelves show it at once (refresh_game_titles). No file is
 * touched. */
static bool rename_shown(app *a, int owner, const game_entry *g, char *said, size_t n)
{
	const system_cfg *s = &a->sys.systems[owner];
	char folder[CFG_STR], file[LIB_PATH], name[LIB_NAME], typed[LIB_NAME], plain[LIB_NAME];
	gr_why why;

	snprintf(folder, sizeof folder, "%s", s->folder);
	snprintf(file, sizeof file, "%s", g->file);
	snprintf(name, sizeof name, "%s", g->name);
	snprintf(typed, sizeof typed, "%s", g->title[0] ? g->title : g->name);
	said[0] = '\0';
	gr_shown_plain(folder, file, name, plain, sizeof plain);
	kb_note_next(T(S_GAMEMENU_RENAME_SHOWN_NOTE));
	kb_placeholder_next(plain);
	if (keyboard_prompt(a, T(S_GAMEMENU_RENAME_SHOWN), typed, (int)sizeof typed) != KB_ACCEPT ||
	    !a->running)
		return false;
	why = gr_shown_set(folder, file, name, typed);
	fprintf(stderr, "rename: %s %s shown as \"%s\" (%d)\n", s->tag, file, typed, (int)why);
	if (why == GR_SAME) return false;
	if (why != GR_OK) {
		snprintf(said, n, "%s", T(S_GAMEMENU_RENAME_FAILED));
		return false;
	}
	/* Every shelf with the game on it, Favorites, Recents and the
	 * collections too, each in its order again with its cursor kept */
	refresh_game_titles(a);
	return true;
}

bool game_rename(app *a, int owner, const game_entry *g, char *said, size_t n)
{
	const system_cfg *s = &a->sys.systems[owner];
	char tag[sizeof s->tag], folder[CFG_STR], core[CFG_STR], exts[CFG_STR];
	char file[LIB_PATH], name[LIB_NAME], typed[LIB_NAME], clean[LIB_NAME];
	char card_saves[CFG_STR * 2], card_states[CFG_STR * 2], card_cheats[CFG_STR * 2];
	gd_card card;
	gd_game gg;
	gd_other *others = NULL;
	gr_plan *plan;
	gr_done done;
	int no = 0, cap = 0, i, k;

	if (gr_shown_only(s->core, g->file, g->dir)) return rename_shown(a, owner, g, said, n);
	/* Copies: the shelf's entries move once the game takes its name */
	snprintf(tag, sizeof tag, "%s", s->tag);
	snprintf(folder, sizeof folder, "%s", s->folder);
	snprintf(core, sizeof core, "%s", s->core);
	snprintf(exts, sizeof exts, "%s", s->exts);
	snprintf(file, sizeof file, "%s", g->file);
	snprintf(name, sizeof name, "%s", g->name);
	snprintf(typed, sizeof typed, "%s", g->name);
	snprintf(card_saves, sizeof card_saves, "%s/Saves", P_CARD);
	snprintf(card_states, sizeof card_states, "%s/.zyperos", P_SHARED);
	said[0] = '\0';

	/* Every other game of every system, at every depth of its folders
	 * (sys_games), the systems off the home too (app.parked): what one
	 * folder of a shelf shows is not all that shares a name */
	for (i = 0; i < sys_kept(a); i++)
		if (!a->view[i].owner && !is_mp4(&a->sys.systems[i])) cap += sys_games(&a->view[i])->count;
	if (cap > 0 && !(others = malloc((size_t)cap * sizeof *others))) cap = 0;
	for (i = 0; i < sys_kept(a) && others; i++) {
		const system_cfg *o = &a->sys.systems[i];
		bool hooked = lib_shelf_hooked(o->exts);

		if (a->view[i].owner || is_mp4(o) || !o->folder[0]) continue;
		const game_list *gl = sys_games(&a->view[i]);

		for (k = 0; k < gl->count && no < cap; k++) {
			const game_entry *e = &gl->items[k];

			if (!strcmp(o->folder, folder) && !strcmp(e->file, file)) continue;
			others[no++] = (gd_other){ o->folder, e->file, e->name, hooked, e->dir };
		}
	}
	if (!(plan = malloc(sizeof *plan)) || (cap > 0 && !others)) {
		free(others);
		free(plan);
		snprintf(said, n, "%s", T(S_GAMEMENU_RENAME_FAILED));
		return false;
	}
	snprintf(card_cheats, sizeof card_cheats, "%s/Cheats", P_CARD);
	card = (gd_card){ P_ROMS, card_saves, card_states, card_cheats };
	gg = (gd_game){ folder, tag, core, exts, file, name, g->dir };
	for (;;) {
		if (keyboard_prompt(a, T(S_GAMEMENU_RENAME), typed, (int)sizeof typed) != KB_ACCEPT ||
		    !a->running) {
			free(others);
			free(plan);
			return false;
		}
		if (gr_plan_make(&card, &gg, others, no, typed, plan)) break;
		fprintf(stderr, "rename: %s %s to \"%s\" refused (%d): %s\n", tag, file, typed,
		        (int)plan->why, plan->bad);
		if (plan->why == GR_SAME) { free(others); free(plan); return false; }
		if (plan->why != GR_BAD_NAME && plan->why != GR_TAKEN) {
			snprintf(said, n, "%s", T(S_GAMEMENU_RENAME_FAILED));
			free(others);
			free(plan);
			return false;
		}
		/* Said, and the keyboard again on what was typed, tidied */
		result_message(a, T(S_GAMEMENU_RENAME),
		               T(plan->why == GR_TAKEN ? S_GAMEMENU_RENAME_TAKEN : S_GAMEMENU_RENAME_INVALID));
		if (!a->running) { free(others); free(plan); return false; }
		if (gr_name_ok(typed, clean, sizeof clean)) snprintf(typed, sizeof typed, "%s", clean);
	}
	free(others);

	/* Its video may be the one playing on the shelf behind the panel */
	snap_off();
	gr_plan_run(plan, &done);
	for (i = 0; i < plan->n; i++)
		fprintf(stderr, "rename: %s %s -> %s\n", plan->copy[i] ? "copy" : "move", plan->from[i], plan->to[i]);
	fprintf(stderr, "rename: %s %s is %s: %d moved, %d copied, %d stayed, gamelist.xml %s\n", tag, file,
	        plan->new_file, done.moved + done.game_moved, done.copied, done.failed,
	        done.es_list ? "followed" : "untouched");
	if (!done.game_moved) {
		free(plan);
		snprintf(said, n, "%s", T(S_GAMEMENU_RENAME_FAILED));
		return false;
	}
	fprintf(stderr, "rename: %d library rows\n",
	        gr_follow(folder, tag, file, name, plan->new_file, plan->new_name));
	/* and its lines in the collections (src/collect.h) */
	fprintf(stderr, "rename: %d collection%s\n", coll_follow(folder, file, plan->new_file), "s followed");
	shelf_rename_game(a, tag, file, plan->new_file, plan->new_name);
	/* On Favorites, the game where its copy went */
	{
		sysview *v = &a->view[a->sys_cursor];

		if (v->owner)
			for (k = 0; k < v->list.count; k++)
				if (!strcmp(v->list.items[k].file, plan->new_file) &&
				    !strcmp(a->sys.systems[v->owner[k]].tag, tag)) {
					v->cursor = k;
					cf_reset(&v->cf, k);
					break;
				}
	}
	if (done.failed) snprintf(said, n, "%s", T(S_GAMEMENU_RENAME_PARTLY));
	free(plan);
	return true;
}
