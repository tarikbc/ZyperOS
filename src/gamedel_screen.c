/* SPDX-License-Identifier: GPL-3.0-only */
#include "gamedel_screen.h"

#include "app.h"
#include "artsrc.h"
#include "gamedel.h"
#include "panel.h"
#include "portinfo.h"
#include "shelfbuild.h"
#include "sys_menu.h"
#include "ui.h"

#include <stdio.h>
#include <string.h>

/* ---- Delete Game (DESIGN.md 11.31, src/gamedel.h) -------------------------
 *
 * The details page's last row. A asks first, in the confirm panel every
 * destructive row in the launcher asks with (confirm_rows): the question,
 * then Delete and Cancel, the cursor on Cancel, B the same as Cancel. The
 * question is a paragraph, which no other confirm has needed: it names the
 * game, says what goes and that it cannot be undone, so it is wrapped to the
 * panel and read left to right, a BODY row a line, in text-soft.
 *
 * Nothing is read or deleted until the yes: no work per frame, and the
 * listing of the other games and the plan are built once, on that press,
 * under one frame that says so (11.8). */

/* How many of the question's lines fit over its two answers before the
 * panel windows (menu_list_fit's arithmetic): a window would leave the top
 * of the question where no cursor can go */
int delete_ask_fit(bool *rule)
{
	int pad = menu_pad();
	int note_h = ui_font_line(UI_F_MENU) + pad / 3;
	int avail = MENU_AVAIL_H - (ui_font_line(UI_F_LABEL) + pad + pad / 2) - pad
	          - 2 * menu_row_h();
	int n = note_h > 0 && avail > note_h ? avail / note_h : 1;

	/* And whether a rule still fits under that many (menu_panel's rule_h) */
	*rule = avail - n * note_h >= pad / 2 + 2;
	return n;
}

/* The question's rows and its heading, shared by the screen and
 * --delete-ask; `rows` holds DEL_ASK_LINES + 2, for the answers
 * confirm_rows puts after them. Lines centered, as the one-line question of
 * every other confirm is, and quiet as it is (a dead row): the answers are
 * what the eye goes to. A rule between the two when there is room, the
 * heading's own (MENU_HR), so four lines of question do not read on into
 * Delete.
 *
 * A name too long for the question to fit names the game in the heading
 * instead, as the details page under it does, and the question says "this
 * game": a name may slide in a heading, a question may not be cut. */
static int delete_ask_rows_for(app *a, const char *title, bool port, char (*lines)[CHV_WRAP_COLS],
                               menu_row *rows, const char **heading)
{
	char q[LIB_NAME + 512];
	bool rule;
	int n, i, fit = delete_ask_fit(&rule), w = menu_std_width(a);
	TTF_Font *f = ui_font(UI_F_MENU);

	/* A port's question says its folders go, its saves in them (DESIGN.md
	 * 11.31): a port keeps nothing in Saves/ */
	snprintf(q, sizeof q, T(port ? S_GAMEMENU_DELETE_ASK_PORT : S_GAMEMENU_DELETE_ASK), title);
	n = wrap_text(f, q, w, lines, DEL_ASK_LINES);
	*heading = T(S_GAMEMENU_DELETE);
	if (n > fit) {
		n = wrap_text(f, T(port ? S_GAMEMENU_DELETE_ASK_PORT_THIS : S_GAMEMENU_DELETE_ASK_THIS), w,
		              lines, DEL_ASK_LINES);
		*heading = title;
	}
	for (i = 0; i < n; i++)
		rows[i] = (menu_row){ lines[i], MENU_NOTE_MARK, false };
	if (n < fit || rule) rows[n++] = MENU_HR;
	return n;
}

int delete_ask_rows(app *a, const char *title, char (*lines)[CHV_WRAP_COLS],
                    menu_row *rows, const char **heading)
{
	return delete_ask_rows_for(a, title, false, lines, rows, heading);
}

/* One frame of the question over the shelf, Cancel focused, for a shot */
void delete_ask_preview(app *a)
{
	sysview *v = &a->view[a->sys_cursor];
	char lines[DEL_ASK_LINES][CHV_WRAP_COLS];
	menu_row rows[DEL_ASK_LINES + 2];
	const char *heading;
	int nq;

	if (v->list.count == 0) return;
	nq = delete_ask_rows(a, v->list.items[v->cursor].title, lines, rows, &heading);
	rows[nq]     = (menu_row){ T(S_GAMEMENU_DELETE_YES), NULL, true };
	rows[nq + 1] = (menu_row){ T(S_PANEL_CANCEL), NULL, true };
	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw(a, heading, rows, nq + 2, nq + 1, menu_std_width(a), MENU_ZYPEROS);
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

	/* Every other game on the card's own shelves - not Favorites', whose
	 * games are on them too, and not MP4's albums */
	for (i = 0; i < a->sys.count; i++)
		if (!a->view[i].owner && !is_muse(&a->sys.systems[i])) cap += sys_games(&a->view[i])->count;
	if (cap > 0 && !(others = malloc((size_t)cap * sizeof *others))) cap = 0;
	for (i = 0; i < a->sys.count && others; i++) {
		const system_cfg *o = &a->sys.systems[i];
		bool hooked = lib_shelf_hooked(o->exts);

		if (a->view[i].owner || is_muse(o) || !o->folder[0]) continue;
		const game_list *gl = sys_games(&a->view[i]);

		for (k = 0; k < gl->count && no < cap; k++) {
			const game_entry *e = &gl->items[k];

			if (!strcmp(o->folder, folder) && !strcmp(e->file, file)) continue;
			others[no++] = (gd_other){ o->folder, e->file, e->name, hooked, e->dir };
		}
	}

	card = (gd_card){ P_ROMS, card_saves, card_states };
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
	char lines[DEL_ASK_LINES][CHV_WRAP_COLS];
	menu_row rows[DEL_ASK_LINES + 2];
	const char *heading;
	int nq, lv;
	bool yes;

	const system_cfg *s = owner >= 0 && owner < a->sys.count ? &a->sys.systems[owner] : NULL;
	bool port = s && !strcmp(s->core, "port");

	said[0] = '\0';
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
	nq = delete_ask_rows_for(a, g->title, port, lines, rows, &heading);
	lv = panel_open();
	yes = confirm_rows(a, heading, rows, nq, T(S_GAMEMENU_DELETE_YES), menu_std_width(a), lv);
	panel_close(a, lv, !a->running);
	if (!yes || !a->running) return false;
	return game_delete_now(a, owner, g, true, said, n);
}
