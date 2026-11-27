/* SPDX-License-Identifier: GPL-3.0-only */
/* Collections' screens. See src/collect_screen.h. */
#include "collect_screen.h"
#include "keyboard.h"

#include "collect.h"
#include "gamedel_screen.h"
#include "menu_panel.h"
#include "panel.h"
#include "shelfbuild.h"
#include "ui.h"
#include "uisound.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

void collect_value(const char *folder, const char *file, char *out, size_t n)
{
	int in = 0;

	for (int i = 0; i < coll_count(); i++) in += coll_has(i, folder, file);
	if (in) snprintf(out, n, T(S_COLLECT_IN), in);
	else if (n) out[0] = '\0';
}

/* Why a name typed for a collection cannot be one, as a message */
static const char *name_why(coll_name_err why)
{
	return why == COLL_NAME_LONG ? T(S_COLLECT_NAME_LONG)
	     : why == COLL_NAME_TAKEN ? T(S_COLLECT_NAME_TAKEN)
	     : T(S_GAMEMENU_RENAME_INVALID);
}

/* The keyboard for a collection's name, `title` over it, on `typed`, again
 * on what was typed while the name cannot be one: true with a name that
 * can, for collection `self` (-1 a new one) */
static bool ask_name(app *a, const char *title, char *typed, size_t n, int self)
{
	coll_name_err why;

	for (;;) {
		/* A new name's first letter a capital (2026-11-19: "Name input
		 * should have the keyboard start with shift on"); a name being
		 * changed opens on its own letters, where this does nothing */
		kb_capital_next();
		if (keyboard_prompt(a, title, typed, (int)n) != KB_ACCEPT || !a->running) return false;
		if ((why = coll_name_check(typed, self)) == COLL_NAME_OK) return true;
		fprintf(stderr, "coll: \"%s\" is no name for a collection (%d)\n", typed, (int)why);
		result_message(a, title, name_why(why));
		if (!a->running) return false;
	}
}

/* ---- Add to Collection ---------------------------------------------------- */

typedef struct {
	char folder[CFG_STR], file[LIB_PATH];
	bool changed;
	unsigned char sound[COLL_MAX + 2];
} collect_ctx;

static int cp_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	collect_ctx *c = ctx;
	int n = 0;

	*heading = T(S_COLLECT_ADD);
	for (int i = 0; i < coll_count() && n < max - 2; i++) {
		rows[n] = (menu_row){ coll_name(i), coll_has(i, c->folder, c->file) ? MENU_TICK : NULL, true };
		/* Its own sounds: in, the reward; out, back */
		c->sound[n++] = MENU_SND_OWN;
	}
	if (n) {
		c->sound[n] = 0;
		rows[n++] = MENU_RULE;
	}
	c->sound[n] = 0;
	rows[n++] = (menu_row){ T(S_COLLECT_NEW), NULL, true };
	return n;
}

static menu_result cp_key(app *a, void *ctx, in_button key, int sel)
{
	collect_ctx *c = ctx;
	char typed[COLL_NAME * 2];
	int at;

	if (key != IN_ACCEPT) return MENU_STAY;
	if (sel >= 0 && sel < coll_count()) {
		bool in = coll_has(sel, c->folder, c->file);

		if (!(in ? coll_remove(sel, c->folder, c->file) : coll_add(sel, c->folder, c->file))) {
			uisound_play(DS_SOUND_DENY);
			fprintf(stderr, "coll: %s could not take %s/%s\n", coll_name(sel), c->folder, c->file);
			result_message(a, T(S_COLLECT_ADD), T(S_COLLECT_FAILED));
			return MENU_STAY;
		}
		uisound_play(in ? DS_SOUND_BACK : DS_SOUND_REWARD);
		fprintf(stderr, "coll: %s/%s %s %s\n", c->folder, c->file, in ? "out of" : "into", coll_name(sel));
		c->changed = true;
		return MENU_STAY;
	}
	/* New Collection: its name, the game its first; the cursor on it */
	typed[0] = '\0';
	if (ask_name(a, T(S_COLLECT_NEW), typed, sizeof typed, -1)) {
		if ((at = coll_create(typed, c->folder, c->file)) < 0) {
			fprintf(stderr, "coll: could not make \"%s\"\n", typed);
			result_message(a, T(S_COLLECT_NEW), T(S_COLLECT_FAILED));
		} else {
			uisound_play(DS_SOUND_REWARD);
			fprintf(stderr, "coll: made %s with %s/%s\n", coll_name(at), c->folder, c->file);
			c->changed = true;
			menu_cursor_to(at);
		}
	}
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return MENU_STAY;
}

bool collect_page(app *a, int owner, const game_entry *g, unsigned accent)
{
	collect_ctx c;

	memset(&c, 0, sizeof c);
	snprintf(c.folder, sizeof c.folder, "%s", a->sys.systems[owner].folder);
	snprintf(c.file, sizeof c.file, "%s", g->file);
	menu_run(a, &(menu_style){ .accent = accent, .fixed_w = menu_std_width(a), .sound = c.sound },
	         cp_build, cp_key, &c);
	return c.changed;
}

/* ---- the Collections shelf's menu ---------------------------------------- */

/* The collection the shelf is on: shown, or under the cursor on its top */
static int shelf_collection(app *a, sysview **vout)
{
	int s = shelf_coll_at(a);
	sysview *v;

	if (s < 0 || s != a->sys_cursor) return -1;
	v = &a->view[s];
	if (vout) *vout = v;
	if (v->dir[0]) return coll_find(v->dir);
	if (v->cursor < 0 || v->cursor >= v->list.count || !v->list.items[v->cursor].folder) return -1;
	return coll_find(v->list.items[v->cursor].file);
}

/* After a collection left the top (deleted, or its last game taken out), the
 * cursor on the one after it in A to Z, or the last */
static void cursor_after(app *a, const char *gone)
{
	int s = shelf_coll_at(a), i;
	sysview *v;

	if (s < 0 || a->sys_cursor != s) return;
	v = &a->view[s];
	if (v->dir[0]) return;
	for (i = 0; i < v->list.count; i++)
		if (strcasecmp(v->list.items[i].file, gone) > 0) break;
	v->cursor = i < v->list.count ? i : v->list.count - 1;
	if (v->cursor < 0) v->cursor = 0;
	cf_reset(&v->cf, v->cursor);
	games_reset(&v->row, v->cursor, v->list.count, plat_now_ms());
}

bool collect_rename_screen(app *a)
{
	sysview *v = NULL;
	int c = shelf_collection(a, &v), at;
	char typed[COLL_NAME * 2], was[COLL_NAME];

	if (c < 0) return false;
	snprintf(was, sizeof was, "%s", coll_name(c));
	snprintf(typed, sizeof typed, "%s", was);
	if (!ask_name(a, T(S_GAMEMENU_RENAME), typed, sizeof typed, c)) return false;
	if ((at = coll_rename(c, typed)) < 0) {
		fprintf(stderr, "coll: could not rename %s to \"%s\"\n", was, typed);
		result_message(a, T(S_GAMEMENU_RENAME), T(S_COLLECT_FAILED));
		return false;
	}
	fprintf(stderr, "coll: %s is %s\n", was, coll_name(at));
	/* Kept by its new name: in it, the shelf stays in it; on the top, the
	 * cursor stays on its card */
	if (v->dir[0]) snprintf(v->dir, sizeof v->dir, "%s", coll_name(at));
	else snprintf(v->list.items[v->cursor].file, sizeof v->list.items[v->cursor].file, "%s", coll_name(at));
	refresh_collections_shelf(a);
	return true;
}

void collect_remove_game(app *a)
{
	sysview *v = NULL;
	int c = shelf_collection(a, &v), o;
	char name[COLL_NAME];

	if (c < 0 || !v->dir[0] || v->cursor < 0 || v->cursor >= v->list.count) return;
	o = v->owner[v->cursor];
	snprintf(name, sizeof name, "%s", coll_name(c));
	if (!coll_remove(c, a->sys.systems[o].folder, v->list.items[v->cursor].file)) {
		uisound_play(DS_SOUND_DENY);
		result_message(a, T(S_COLLECT_REMOVE), T(S_COLLECT_FAILED));
		return;
	}
	fprintf(stderr, "coll: %s/%s out of %s\n", a->sys.systems[o].folder, v->list.items[v->cursor].file, name);
	refresh_collections_shelf(a);
	/* Its last game: the shelf on its top, on the collection after it */
	cursor_after(a, name);
}

bool collect_delete_screen(app *a)
{
	sysview *v = NULL;
	int c = shelf_collection(a, &v);
	char q[COLL_NAME + 512], name[COLL_NAME];

	if (c < 0) return false;
	snprintf(name, sizeof name, "%s", coll_name(c));
	cq_text(CQ_COLLECT_DELETE, q, sizeof q, name);
	if (!confirm_ask(a, CQ_COLLECT_DELETE, q) || !a->running) return false;
	if (!coll_delete(c)) {
		fprintf(stderr, "coll: could not delete %s\n", name);
		result_message(a, T(S_COLLECT_DELETE), T(S_COLLECT_FAILED));
		return false;
	}
	fprintf(stderr, "coll: deleted %s\n", name);
	v->dir[0] = '\0';
	refresh_collections_shelf(a);
	cursor_after(a, name);
	return true;
}

/* ---- one frame of each, for a shot ---------------------------------------- */

void collect_page_preview(app *a, int sel)
{
	sysview *v = &a->view[a->sys_cursor];
	collect_ctx c;
	menu_row rows[COLL_MAX + 2];
	const char *heading;
	int owner, n;

	if (v->list.count == 0 || v->list.items[v->cursor].folder) return;
	owner = shelf_owner(a, a->sys_cursor, v->cursor);
	memset(&c, 0, sizeof c);
	snprintf(c.folder, sizeof c.folder, "%s", a->sys.systems[owner].folder);
	snprintf(c.file, sizeof c.file, "%s", v->list.items[v->cursor].file);
	n = cp_build(&c, rows, COLL_MAX + 2, &heading);
	if (sel < 0 || sel >= n || !rows[sel].live) sel = 0;
	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw(a, heading, rows, n, sel, menu_std_width(a), a->sys.systems[owner].accent);
}

void collect_delete_preview(app *a)
{
	int c = shelf_collection(a, NULL);
	char q[COLL_NAME + 512];

	if (c < 0) return;
	cq_text(CQ_COLLECT_DELETE, q, sizeof q, coll_name(c));
	draw_shelf(a);
	ui_scrim(a->r, 0);
	confirm_preview(a, CQ_COLLECT_DELETE, q);
}
