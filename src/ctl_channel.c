/* SPDX-License-Identifier: GPL-3.0-only */
#include "ctl_channel.h"

#include "app.h"
#include "ctl.h"
#include "panel.h"
#include "platform.h"
#include "shelfbuild.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The channel's state, which the shelf, a game's tick and the in-game menu
 * read (src/ctl_channel.h says what each is) */
app *g_ctl_app;
bool g_ctl_shelf_pass;
bool g_ctl_close_menu;
ctl_game_state g_ctl_game;
bool g_ctl_fresh;

/* ---------- px2-open: the command channel (src/ctl.h) ---------------------
 *
 * A game opened over SSH as A on it in its shelf opens it: the system its
 * Roms folder is, the core that shelf runs, its auto state, the in-game menu,
 * the logs, and a port or a "run:" program through launch.sh - the same
 * launch, by pick_launch, the way Random Game and Search Games start theirs.
 * `open` on the shelf starts it at once; over a running game, or its in-game
 * menu, it quits that game first, by the menu's own Quit (gm_quit), and opens
 * the new one when the shelf is back; on any other screen (a menu over the
 * shelf, Muse, a message) it is refused as busy, as a press there would not
 * reach the shelf either. `quit` is the menu's Quit; `status` says where the
 * launcher is. Debugging only: nothing here is a player's way in. */

static int g_ctl_fd = -1;
static ctl_cmd g_ctl_next;        /* an open waiting for the game it quit */
static bool g_ctl_next_on;

typedef struct {
	int sys, game;                /* its own shelf (never Favorites) and place */
	char core[CFG_STR];           /* what it runs on this time */
} ctl_target;

void ctl_start(app *a)
{
	g_ctl_app = a;
	g_ctl_fd = ctl_listen(ctl_dir());
	if (g_ctl_fd < 0) return;
	/* A port that ran has ended: a launcher is up to answer for itself */
	ctl_port_clear(ctl_dir());
	fprintf(stderr, "ctl: px2-open's commands on %s/%s\n", ctl_dir(), CTL_SOCK_NAME);
}

void ctl_stop(void)
{
	ctl_shut(g_ctl_fd, ctl_dir());
	g_ctl_fd = -1;
	g_ctl_app = NULL;
}

/* The shelf a folder is, and the game a file is on it: -1s for none */
static void ctl_find(app *a, const char *folder, const char *file, int *si, int *gi)
{
	const char *folders[CFG_MAX_SYSTEMS];
	const char **files;
	int n = a->sys.count < CFG_MAX_SYSTEMS ? a->sys.count : CFG_MAX_SYSTEMS, i;
	game_list *l;

	*gi = -1;
	for (i = 0; i < n; i++)
		folders[i] = a->view[i].owner || is_muse(&a->sys.systems[i]) ? NULL
		                                                             : a->sys.systems[i].folder;
	if ((*si = ctl_find_system(folder, folders, n)) < 0) return;
	l = sys_games(&a->view[*si]);
	if (l->count <= 0 || !(files = malloc(sizeof *files * (size_t)l->count))) return;
	for (i = 0; i < l->count; i++) files[i] = l->items[i].file;
	*gi = ctl_find_game(file, files, l->count);
	free(files);
}

/* What an open names, as its shelf has it, or why not: `code` one of the
 * error codes px2-open says (docs/building.md). The shelves are read again
 * once, on the shelf only, for a game copied onto the card since. */
static bool ctl_resolve(app *a, const ctl_cmd *c, bool rescan, ctl_target *t,
                        const char **code, char *msg, size_t mn)
{
	char folder[CFG_STR], file[LIB_PATH], why[160];
	const system_cfg *s;
	int si, gi;

	if (!ctl_path_split(P_ROMS, c->path, folder, sizeof folder, file, sizeof file,
	                    why, sizeof why) ||
	    !ctl_path_real(P_ROMS, c->path, why, sizeof why)) {
		*code = strstr(why, "no such") ? "no-file" : "bad-path";
		snprintf(msg, mn, "%s: %s", c->path, why);
		return false;
	}
	ctl_find(a, folder, file, &si, &gi);
	if (gi < 0 && rescan) {
		fprintf(stderr, "ctl: %s/%s is not on a shelf: reading the shelves again\n", folder, file);
		rescan_all(a);
		ctl_find(a, folder, file, &si, &gi);
	}
	if (si < 0) {
		*code = "unknown-system";
		snprintf(msg, mn, "no shelf is Roms/%s (a system with no games has none)", folder);
		return false;
	}
	s = &a->sys.systems[si];
	if (gi < 0) {
		*code = "not-a-game";
		snprintf(msg, mn, "%s is not a game on the %s shelf (it takes: %s)", file, s->name,
		         s->exts[0] ? s->exts : "anything");
		return false;
	}
	t->sys = si;
	t->game = gi;
	snprintf(t->core, sizeof t->core, "%s", s->core);
	if (c->core[0]) {
		char alt[CFG_STR];
		int k, n = cfg_core_count(s);
		bool found = false;

		for (k = 0; k < n && !found; k++)
			if (cfg_core_at(s, k, alt, sizeof alt) && ctl_core_is(c->core, alt)) found = true;
		if (!found) {
			*code = "unknown-core";
			snprintf(msg, mn, "%s offers %s, not %s", s->name, s->cores[0] ? s->cores : s->core,
			         c->core);
			return false;
		}
		if (!core_present(alt)) {
			*code = "unknown-core";
			snprintf(msg, mn, "%s is not on the card", alt);
			return false;
		}
		snprintf(t->core, sizeof t->core, "%s", alt);
	}
	/* A disc that needs firmware, as launch refuses it, said here instead of
	 * in a message on the handheld that nobody over SSH would see */
	if (strcmp(t->core, PORT_CORE) && strncmp(t->core, RUN_PREFIX, strlen(RUN_PREFIX)) &&
	    s->disc_bios[0] && (s->bios_every || lib_is_disc(sys_games(&a->view[si])->items[gi].file))) {
		char bios[CFG_STR * 2];

		snprintf(bios, sizeof bios, "%s/Bios", P_CARD);
		if (!disc_bios_found(bios, s->disc_bios)) {
			*code = "needs-bios";
			snprintf(msg, mn, "%s needs Bios/%.*s", s->name, (int)strcspn(s->disc_bios, ","),
			         s->disc_bios);
			return false;
		}
	}
	return true;
}

/* As A on it: its shelf, its card, the launch. The core is the shelf's for
 * this launch alone, and nothing is written (core_step writes a choice). */
static void ctl_open_now(app *a, const ctl_target *t, bool fresh)
{
	system_cfg *s = &a->sys.systems[t->sys];
	char was[CFG_STR];

	snprintf(was, sizeof was, "%s", s->core);
	snprintf(s->core, sizeof s->core, "%s", t->core);
	g_ctl_fresh = fresh;
	memset(&a->in, 0, sizeof a->in);
	g_pick.shelf = t->sys;
	g_pick.game = t->game;
	g_pick.hold_ms = 0;
	pick_launch(a);
	g_ctl_fresh = false;
	snprintf(s->core, sizeof s->core, "%s", was);
}

/* The running game asked to end, as the in-game menu's Quit asks: over the
 * menu, the menu closes too (menu_run_body) */
static void ctl_stop_game(void)
{
	if (!g_ctl_game.stopping) gm_quit();
	g_ctl_game.stopping = true;
	if (g_ctl_game.paused) g_ctl_close_menu = true;
}

static void ctl_status(app *a, int at, int cfd)
{
	long now = (long)time(NULL);
	char next[CTL_PATH + 8] = "";

	if (g_ctl_next_on) snprintf(next, sizeof next, "\tnext=%s", g_ctl_next.path);
	if (g_ctl_game.on) {
		const char *state = g_ctl_game.stopping ? "stopping"
		                  : g_ctl_game.paused   ? "paused"
		                  : plat_resident_started() ? "running" : "starting";

		ctl_answer(cfd, "ok\tstate=%s\tsystem=%s\tname=%s\tpath=%s\tcore=%s\tsince=%ld\tup=%ld%s",
		           state, g_ctl_game.tag, g_ctl_game.name, g_ctl_game.path, g_ctl_game.core,
		           g_ctl_game.since, now - g_ctl_game.since, next);
	} else if (at == CTL_AT_SHELF && a->sys.count > 0) {
		const sysview *v = &a->view[a->sys_cursor];
		const system_cfg *s = &a->sys.systems[a->sys_cursor];
		char on[LIB_PATH + 8] = "";

		if (a->screen == SCREEN_GAMES && v->cursor >= 0 && v->cursor < v->list.count)
			snprintf(on, sizeof on, "\tgame=%s", v->list.items[v->cursor].file);
		ctl_answer(cfd, "ok\tstate=shelf\tscreen=%s\tsystem=%s\tname=%s%s%s",
		           a->screen == SCREEN_GAMES ? "games" : "systems", s->tag, s->name, on, next);
	} else {
		ctl_answer(cfd, "ok\tstate=%s%s", at == CTL_AT_SHELF ? "shelf" : "screen", next);
	}
}

/* One command, if one is waiting. True when it started a game (on the shelf
 * only), which has ended by the time this returns. */
bool ctl_poll(app *a, int at)
{
	char line[CTL_LINE], err[200], msg[CTL_PATH + 200];
	const char *code = "";
	ctl_target t;
	ctl_cmd c;
	int cfd;

	if (g_ctl_fd < 0 || !a) return false;
	if ((cfd = ctl_take(g_ctl_fd, line, sizeof line)) < 0) return false;
	if (!ctl_parse(line, &c, err, sizeof err)) {
		ctl_answer(cfd, "error\tcode=bad-command\tmsg=%s", err);
		return false;
	}
	if (c.verb == CTL_STATUS) {
		ctl_status(a, at, cfd);
		return false;
	}
	fprintf(stderr, "ctl: %s\n", line);
	if (c.verb == CTL_QUIT) {
		if (!g_ctl_game.on) {
			ctl_answer(cfd, "error\tcode=not-running\tmsg=no game is running");
			return false;
		}
		ctl_stop_game();
		ctl_answer(cfd, "ok\tquit=%s", g_ctl_game.path);
		return false;
	}
	if (!ctl_resolve(a, &c, at == CTL_AT_SHELF, &t, &code, msg, sizeof msg)) {
		fprintf(stderr, "ctl: refused, %s\n", msg);
		ctl_answer(cfd, "error\tcode=%s\tmsg=%s", code, msg);
		return false;
	}
	if (at == CTL_AT_SHELF) {
		const system_cfg *s = &a->sys.systems[t.sys];

		ctl_answer(cfd, "ok\topening=%s\tsystem=%s\tcore=%s%s", c.path, s->tag, t.core,
		           !strcmp(t.core, PORT_CORE) || !strncmp(t.core, RUN_PREFIX, strlen(RUN_PREFIX))
		               ? "\tport=1" : "");
		ctl_open_now(a, &t, c.fresh);
		return true;
	}
	if (g_ctl_game.on) {
		/* Quit first, then this one, from the shelf's loop (ctl_shelf_turn) */
		g_ctl_next = c;
		g_ctl_next_on = true;
		ctl_stop_game();
		ctl_answer(cfd, "ok\tquit=%s\topening=%s", g_ctl_game.path, c.path);
		return false;
	}
	ctl_answer(cfd, "error\tcode=busy\tmsg=another screen is open over the shelf "
	                "(a menu, Muse, a message): B back to the shelf first");
	return false;
}

/* The shelf's turn: an open waiting for the game it quit, else a command */
bool ctl_shelf_turn(app *a)
{
	if (g_ctl_next_on) {
		ctl_cmd c = g_ctl_next;
		char msg[CTL_PATH + 200];
		const char *code;
		ctl_target t;

		g_ctl_next_on = false;
		if (!ctl_resolve(a, &c, true, &t, &code, msg, sizeof msg)) {
			fprintf(stderr, "ctl: the open after the quit refused, %s\n", msg);
			return false;
		}
		fprintf(stderr, "ctl: opening %s, the game before it over\n", c.path);
		ctl_open_now(a, &t, c.fresh);
		return true;
	}
	return ctl_poll(a, CTL_AT_SHELF);
}
