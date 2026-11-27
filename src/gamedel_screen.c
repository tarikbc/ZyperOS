/* SPDX-License-Identifier: GPL-3.0-only */
#include "gamedel_screen.h"

#include "app.h"
#include "artsrc.h"
#include "collect.h"
#include "gamedel.h"
#include "panel.h"
#include "pico8.h"
#include "portinfo.h"
#include "shelfbuild.h"
#include "sys_menu.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* ---- Delete Game (DESIGN.md 11.31, src/gamedel.h) -------------------------
 *
 * The details page's last row. A asks first, in the panel every question
 * with two answers asks in (confirm_ask, src/confirmq.h): the question,
 * then Cancel and Delete side by side, the cursor on Cancel, B the same as
 * Cancel. The question names the game, says what goes and that it cannot be
 * undone, and wraps to the panel however long the name is.
 *
 * Nothing is read or deleted until the yes: no work per frame, and the
 * listing of the other games and the plan are built once, on that press,
 * under one frame that says so (11.8). */

/* One frame of the question over the shelf, Cancel focused, for a shot */
void delete_ask_preview(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	char q[LIB_NAME + 512];

	if (v->list.count == 0) return;
	cq_text(CQ_GAME_DELETE, q, sizeof q, v->list.items[v->cursor].title);
	draw_shelf(a);
	ui_scrim(a->r, 0);
	confirm_preview(a, CQ_GAME_DELETE, q);
}

/* The yes: the plan, held against every other game on the shelves, run, the
 * rows forgotten and the shelf put right in place. True when the game went;
 * `said` is the message either way, empty for none. `wait` draws the frame
 * that says it is happening (not in a shot). */
bool game_delete_now(app *a, int owner, const game_entry *g, bool wait,
                     char *said, size_t n)
{
	const system_cfg *s = &a->sys.systems[owner];
	char tag[sizeof s->tag], folder[CFG_STR], core[CFG_STR], exts[CFG_STR];
	char file[LIB_PATH], name[LIB_NAME], card_saves[CFG_STR * 2], card_states[CFG_STR * 2];
	char card_cheats[CFG_STR * 2];
	gd_card card;
	gd_game gg;
	gd_other *others = NULL;
	gd_plan *plan;
	gd_done done;
	int no = 0, cap = 0, i, k;

	/* Copies: the shelf's entries move once the game is off it */
	snprintf(tag, sizeof tag, "%s", s->tag);
	snprintf(folder, sizeof folder, "%s", s->folder);
	snprintf(core, sizeof core, "%s", s->core);
	snprintf(exts, sizeof exts, "%s", s->exts);
	snprintf(file, sizeof file, "%s", g->file);
	snprintf(name, sizeof name, "%s", g->name);
	snprintf(card_saves, sizeof card_saves, "%s/Saves", P_CARD);
	snprintf(card_states, sizeof card_states, "%s/.zyperos", P_SHARED);
	said[0] = '\0';
	if (wait) wait_panel(a, T(S_GAMEMENU_DELETE), T(S_GAMEMENU_DELETING));

	/* Every other game on the card's own shelves and the systems off the
	 * home (app.parked) - not Favorites', whose games are on them too, and
	 * not MP4's albums */
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

	snprintf(card_cheats, sizeof card_cheats, "%s/Cheats", P_CARD);
	card = (gd_card){ P_ROMS, card_saves, card_states, card_cheats };
	gg = (gd_game){ folder, tag, core, exts, file, name, g->dir };
	if (!(plan = malloc(sizeof *plan)) || (cap > 0 && !others)) {
		free(others);
		free(plan);
		snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_FAILED));
		return false;
	}
	if (!gd_plan_make(&card, &gg, others, no, plan)) {
		fprintf(stderr, "delete: %s %s refused (%s): %s\n", tag, file,
		        plan->why == GD_BIOS ? "a BIOS set" : plan->why == GD_OUTSIDE
		        ? "outside its folders" : "too many paths", plan->bad);
		if (plan->why == GD_BIOS) snprintf(said, n, T(S_GAMEMENU_DELETE_BIOS), name);
		else if (plan->why == GD_RUNTIME) snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_RUNTIME));
		else if (plan->why == GD_PORT_UNKNOWN) snprintf(said, n, T(S_GAMEMENU_DELETE_PORT_UNKNOWN), name);
		else snprintf(said, n, "%s", plan->why == GD_OUTSIDE ? T(S_GAMEMENU_DELETE_OUTSIDE)
		                                                     : T(S_GAMEMENU_DELETE_FAILED));
		free(others);
		free(plan);
		return false;
	}
	free(others);

	/* Its video may be the one playing on the shelf behind the panel */
	snap_off();
	gd_plan_run(plan, &done);
	/* What went, and what would not: the paths with nothing there are
	 * the rest of the plan, and say nothing */
	for (i = 0; i < plan->n; i++)
		if (done.went[i] != GD_NOT_THERE)
			fprintf(stderr, "delete: %s %s %s\n", done.went[i] == GD_WENT ? "deleted" : "KEPT, would not go:",
			        plan->kind[i] == GD_FILE ? "file" : "folder", plan->path[i]);
	fprintf(stderr, "delete: %s %s: %d files and %d folders, %llu bytes, %d still there, "
	        "%d shared and kept\n", tag, file, done.files, done.folders, done.bytes,
	        done.failed, plan->kept);
	free(plan);
	if (!done.game_gone) {
		snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_FAILED));
		return false;
	}
	fprintf(stderr, "delete: %d library rows\n", gd_forget(folder, tag, file));
	/* and its lines in the collections (src/collect.h): a list naming a
	 * game that is gone would be a line nothing can play */
	fprintf(stderr, "delete: %d collection%s\n", coll_forget(folder, file), "s followed");
	/* and where its cover and Before's came from (src/artsrc.h) */
	artsrc_forget(folder, name);
	shelf_drop_game(a, tag, file);
	if (done.failed) {
		snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_PARTLY));
	} else {
		char sz[24];

		sys_menu_bytes(done.bytes, sz, sizeof sz);
		snprintf(said, n, T(S_GAMEMENU_DELETED), sz);
	}
	return true;
}

/* A on Delete Game: a BIOS set says why it stays; anything else asks first.
 * True when the game went (game_delete_now). */
bool game_delete(app *a, int owner, const game_entry *g, char *said, size_t n)
{
	char q[LIB_NAME + 512];
	bool yes;

	const system_cfg *s = owner >= 0 && owner < sys_kept(a) ? &a->sys.systems[owner] : NULL;
	bool port = s && !strcmp(s->core, "port");

	said[0] = '\0';
	/* Splore's entry is your PICO-8's browser, no file on the card */
	if (s && !strcmp(s->tag, P8_TAG) && splore_file(g->file)) {
		snprintf(said, n, "%s", T(S_PICO8_SPLORE_STAYS));
		return false;
	}
	if (gd_is_bios(g->file)) {
		snprintf(said, n, T(S_GAMEMENU_DELETE_BIOS), g->name);
		return false;
	}
	/* A port says why it stays before anything is asked: PortMaster, which
	 * every port runs on, and a port nothing names its folders for
	 * (src/portinfo.h) */
	if (port) {
		char dir[CFG_STR * 2], (*f)[PI_NAME] = malloc(PI_ITEMS * sizeof *f);
		pi_port *ports = malloc(PI_MAX * sizeof *ports);
		int np, nf = 0;

		snprintf(dir, sizeof dir, "%s/%s", P_ROMS, s->folder);
		if (pi_runtime(g->file)) {
			snprintf(said, n, "%s", T(S_GAMEMENU_DELETE_RUNTIME));
		} else if (f && ports) {
			np = pi_scan(dir, ports, PI_MAX);
			nf = pi_folders(dir, ports, np, g->file, f, PI_ITEMS);
			if (!nf) snprintf(said, n, T(S_GAMEMENU_DELETE_PORT_UNKNOWN), g->title);
		}
		free(f);
		free(ports);
		if (said[0]) return false;
	}
	/* A port's question says its folders go, its saves in them (DESIGN.md
	 * 11.31): a port keeps nothing in Saves/. The name stays in the
	 * question, however long: it wraps (my pick, 2026-11-22) */
	cq_text(port ? CQ_PORT_DELETE : CQ_GAME_DELETE, q, sizeof q, g->title);
	yes = confirm_ask(a, port ? CQ_PORT_DELETE : CQ_GAME_DELETE, q);
	if (!yes || !a->running) return false;
	return game_delete_now(a, owner, g, true, said, n);
}
