/* SPDX-License-Identifier: GPL-3.0-only */
#include "gameinfo_screen.h"

#include "app.h"
#include "cheevos.h"
#include "ctltab_screen.h"
#include "esmedia.h"
#include "favorites.h"
#include "gamedel.h"
#include "gamedel_screen.h"
#include "gamern.h"
#include "gamern_screen.h"
#include "ingame_screen.h"
#include "launch_screen.h"
#include "media_screen.h"
#include "panel.h"
#include "rabadge.h"
#include "rafetch.h"
#include "remap.h"
#include "collect_screen.h"
#include "remapstore.h"
#include "shelf_keys.h"
#include "shelfbuild.h"
#include "snaps.h"
#include "sysmenu_screen.h"
#include "uisound.h"

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

/* game_info and gi_rows: src/game_menu.h */

static void gi_gather(app *a, int owner, const game_entry *g, game_info *gi)
{
	const system_cfg *s = &a->sys.systems[owner];
	char p[LIB_PATH * 3];

	memset(gi, 0, sizeof *gi);
	gi->can_delete = gd_shelf_deletes(s->core);
	gi->can_rename = !is_mp4(s) && gr_renames(s->core, g->file, g->dir);
	gi->can_favorite = !is_mp4(s);
	gi->favorite = gi->can_favorite && fav_is(s->tag, g->file);
	/* The core it starts on, where the shelf has more than one: its own
	 * (core_game_get), or the shelf's, said as the shelf's */
	if (cfg_core_count(s) > 1) {
		char lb[64], own[CFG_STR];

		if (core_game_get(s, g->file, own, sizeof own))
			snprintf(gi->core, sizeof gi->core, "%s", core_label(own, lb, sizeof lb));
		else
			snprintf(gi->core, sizeof gi->core, T(S_GAMEMENU_CORE_SHELF), core_label(s->core, lb, sizeof lb));
		gi->core_live = core_offered_count(s) > 1;
	}
	/* Its file, brackets and all (DESIGN.md 11.69), and a set's discs.
	 * Only a file that is on the card: Install PortMaster's entry and
	 * Splore's are ZyperOS's own, with no file behind them to name. One
	 * stat, and for a set its folder or its .m3u read, as the page opens. */
	snprintf(p, sizeof p, "%s/%s/%s", P_ROMS, s->folder, g->file);
	if (s->folder[0] && access(p, F_OK) == 0) {
		bool hooked = lib_shelf_hooked(s->exts);
		int discs = lib_discs(P_ROMS, s->folder, g->file, g->dir, hooked);

		lib_file_shown(g->file, g->dir, hooked, gi->file, sizeof gi->file);
		if (discs > 1) snprintf(gi->discs, sizeof gi->discs, "%d", discs);
		/* A game whose file keeps its name has Rename for the name shown
		 * (src/gamern.h), when there is a file on the card */
		if (!is_mp4(s) && gr_shown_only(s->core, g->file, g->dir)) gi->can_rename = true;
	}
	/* Collections take every game a favorite can be (src/collect.h) */
	gi->can_collect = gi->can_favorite && s->folder[0];
	if (gi->can_collect) collect_value(s->folder, g->file, gi->collected, sizeof gi->collected);
	/* A key of its own on the card (map.<tag>.<file>): read here, so a reset
	 * on the Buttons page takes the row away when the page comes back */
	{
		rm_table own;

		gi->own_buttons = !is_mp4(s) && rms_game(s, g->file, &own);
	}

	/* The save slots are not read here any more. The row that showed them went
	 * on 2026-09-17 - the carousel shows them when you load, with the frames
	 * rather than a count - and with it went a stat of every slot plus the
	 * autosave each time this screen opened. */
	gi->has_art = game_media(P_ROMS, s->folder, g->file, g->name, ES_COVER, true, p, sizeof p);
	gi->has_snap = game_media(P_ROMS, s->folder, g->file, g->name, ES_VIDEO, true, p, sizeof p);
	gi->own_art = own_cover_path(a, owner, g, theme_index(), p, sizeof p);
	/* PortMaster's entry with PortMaster on the card: Reinstall PortMaster */
	gi->can_reinstall = pm_is_entry(a, owner, g) && pm_on_card(s->folder);

	/* Loading the set clobbers whatever set is loaded, which is nobody's
	 * during shelf browsing - no game is running. It stays loaded after this
	 * returns, which is what lets the Cheevos row open the list: the same set
	 * this count was taken from is the one that screen reads. */
	chv_path(P_ROMS, s->folder, g->name, p, sizeof p);
	gi->has_cheevos = chv_load(p) && chv_count() > 0;
	/* A file or a system the launcher has no hash for never gets a set, so
	 * the row says which rather than "not checked yet": a play with Wi-Fi
	 * would ask nothing (2026-11-14, a PSP .chd). From the name alone. */
	gi->cheevos_no_hash = gi->has_cheevos || g_shot_unchecked ? RA_HASH_OK
	                      : g_shot_no_hash != RA_HASH_OK ? g_shot_no_hash
	                      : ra_hash_support(s->tag, g->file, s->exts);
	gi->cheevos_unchecked = !gi->has_cheevos && gi->cheevos_no_hash == RA_HASH_OK &&
	                        (g_shot_unchecked ||
	                         chv_state_of(p, ra_signed_in()) == CHV_UNCHECKED);
	/* Its badges, behind the screen, if the card lacks them */
	if (gi->has_cheevos) rb_fetch_loaded();
	/* The count alone, as DESIGN.md 12 writes one and as the in-game menu's
	 * Cheevos row has it (src/game_menu.c): the row opens the list, whose
	 * heading has the points. "3 / 36 · 25 / 415 points" beside "Cheevos"
	 * is wider in Lexend than the room the std width leaves a value, and
	 * would be cut with an ellipsis whenever the row is not selected. */
	if (gi->has_cheevos)
		snprintf(gi->cheevos, sizeof gi->cheevos, "%d / %d",
		         chv_earned(), chv_count());
	else if (gi->cheevos_unchecked)
		snprintf(gi->cheevos, sizeof gi->cheevos, "%s", T(S_CHEEVOS_UNCHECKED));
	else if (gi->cheevos_no_hash != RA_HASH_OK)
		snprintf(gi->cheevos, sizeof gi->cheevos, "%s",
		         T(gi->cheevos_no_hash == RA_HASH_NO_SYSTEM ? S_CHEEVOS_NOT_SYSTEM
		                                                    : S_CHEEVOS_NOT_FILE));
	else
		snprintf(gi->cheevos, sizeof gi->cheevos, "%s", T(S_CHEEVOS_NONE));

	/* What a scrape left on the card, if anything. Read here with the rest
	 * rather than when the synopsis card opens: this is one indexed row, the
	 * screen already reads the save directory and the achievement set, and a
	 * row that appears a frame after the panel does reads as a glitch. */
	{
		game_meta m;

		/* A game the card's library has nothing on yet may have
		 * EmulationStation's details, which the next start writes there
		 * (es_import): read from its gamelist.xml until then */
		if (db_game_get(db_lib(), s->folder, g->file, &m) ||
		    es_details(P_ROMS, s->folder, g->file, g->name, &m, NULL)) {
			gi->scraped = true;
			gi->has_synopsis = m.synopsis[0] != '\0';
			snprintf(gi->year, sizeof gi->year, "%.4s", m.year);
			/* The scrape's own English names, in its own order - but
			 * joined with a comma AND A SPACE, which it does not do. That is
			 * typesetting rather than an opinion about somebody else's
			 * taxonomy: "Platform,Shoot'em Up" reads as one word with a
			 * stumble in it. One in three games has a second genre. */
			{
				const char *g = m.genres;
				size_t k = 0;

				while (*g && k + 2 < sizeof gi->genre) {
					gi->genre[k++] = *g;
					if (*g++ == ',' && *g != ' ') gi->genre[k++] = ' ';
				}
				gi->genre[k] = '\0';
			}
		}
	}
}

/* One game, and the two things you can do to it from here.
 *
 * X on the shelf, because Y is already favorite and the pair reads as a
 * unit - look at this one, or mark it. Nothing here is reachable during a
 * game: the in-game menu is about the session, this is about the file.
 *
 * A still, not a live view. Everything is read when the screen opens and
 * again after an action changes something, rather than every frame - these
 * are numbers that only move when the player moves them, and the alternative
 * is a stat storm at 120 Hz for a panel nobody is interacting with. */
/* The info screen's context. owner, g and v all move when a favorite is
 * toggled, so they live here where the key handler can put them back. */
typedef struct {
	app        *a;
	sysview    *v;
	game_entry *g;
	int         owner;
	game_info   gi;
	menu_style  st;   /* the accent follows the OWNING system, which can move */
	/* Delete Game took the game: the page closes, and this is said after */
	bool        deleted;
	/* Y on the page (or A on Favorite) changed the favorite: the Favorites
	 * shelf is rebuilt once the page has closed */
	bool        fav_changed;
	/* Add to Collection changed a collection: Collections the same */
	bool        coll_changed;
	char        said[192];
} info_ctx;

static int info_build(void *ctx, menu_row *rows, int max, const char **heading)
{
	info_ctx *c = ctx;
	int n;

	(void)max;
	*heading = c->g->title;
	/* menu_wifi, not wifi_status. This loop runs every frame and wifi_status
	 * forks wpa_cli out of a 119 MB process; this screen was doing it about
	 * 125 times a second for a row that says "needs Wi-Fi". The system menu
	 * was measured at 12 fps for the same mistake. */
	n = gi_rows(rows, &c->gi, net_internet(NULL));
	return n > max ? max : n;
}

static menu_result info_key(app *a, void *ctx, in_button key, int sel)
{
	info_ctx *c = ctx;
	menu_row  rows[GI_MAX];
	const char *heading;
	int n = info_build(c, rows, GI_MAX, &heading);

	/* X closes it, the same button that opened it. */
	if (key == IN_X) { uisound_play(DS_SOUND_BACK); return MENU_DONE; }
	/* Y, or A on the Favorite row: the favorite, written through at once */
	if (c->gi.can_favorite && (key == IN_Y || (key == IN_ACCEPT && sel < n
	    && !strcmp(rows[sel].label, T(S_CONTROLS_FAVORITE))))) {
		const char *tag = a->sys.systems[c->owner].tag;

		fav_toggle_heard(tag, c->g->file);
		fav_save();
		c->gi.favorite = fav_is(tag, c->g->file);
		c->fav_changed = true;
		return MENU_STAY;
	}
	/* The Core row: Left, Right or A steps the game's own core, the
	 * shelf's first (core_game_step), kept at once; its next start runs
	 * on it */
	if ((key == IN_LEFT || key == IN_RIGHT || key == IN_ACCEPT) && sel < n && rows[sel].live &&
	    !strcmp(rows[sel].label, T(S_SYSMENU_CORE))) {
		char now[CFG_STR];

		core_game_step(&a->sys.systems[c->owner], c->g->file, key == IN_LEFT ? -1 : 1, now, sizeof now);
		gi_gather(a, c->owner, c->g, &c->gi);
		return MENU_STAY;
	}
	if (key != IN_ACCEPT || sel >= n || !rows[sel].live) return MENU_STAY;

	if (!strcmp(rows[sel].label, T(S_GAMEMENU_SYNOPSIS))) {
		game_meta m;

		/* Read again here rather than carried in game_info: it is up to 4 KB
		 * of prose for a row that is usually not opened, and the gatherer runs
		 * on every action this screen takes. */
		const char *folder = a->sys.systems[c->owner].folder;

		if ((db_game_get(db_lib(), folder, c->g->file, &m) ||
		     es_details(P_ROMS, folder, c->g->file, c->g->name, &m, NULL)) && m.synopsis[0])
			synopsis_screen(a, c->g->title, m.synopsis, c->st.accent);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_COLLECT_ADD))) {
		/* The collections, ticked where it is in one (src/collect_screen.h);
		 * the shelf made again once this page closes */
		if (collect_page(a, c->owner, c->g, c->st.accent)) c->coll_changed = true;
		gi_gather(a, c->owner, c->g, &c->gi);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_GAMEMENU_BUTTONS))) {
		/* The Controls list for this game alone, a page over the shelf
		 * (src/ctltab_screen.h). B comes back here, and the gatherer reads
		 * the key again: a Reset Buttons there took the row away. */
		ctlscr_page(a, &a->sys.systems[c->owner], c->g->file, c->g->title, c->st.accent);
		gi_gather(a, c->owner, c->g, &c->gi);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_SYSMENU_CHEEVOS))) {
		/* The set the gatherer loaded to count this row is still the loaded
		 * one, so the list has what it needs without reloading it. Over the
		 * shelf rather than a paused frame: no game is running here. */
		if (c->gi.has_cheevos) cheevos_screen(a, NULL, true);
		else if (c->gi.cheevos_unchecked) cheevos_unchecked_message(a);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_GAMEMENU_RENAME))) {
		/* Renamed: the page stays, on the game under its new name, which
		 * its shelf may have put somewhere else */
		c->said[0] = '\0';
		if (game_rename(a, c->owner, c->g, c->said, sizeof c->said)) {
			c->v = &a->view[a->sys_cursor];
			c->g = &c->v->list.items[c->v->cursor];
			c->owner = shelf_owner(a, a->sys_cursor, c->v->cursor);
			c->st.accent = a->sys.systems[c->owner].accent;
		}
		if (c->said[0]) result_message(a, T(S_GAMEMENU_RENAME), c->said);
		gi_gather(a, c->owner, c->g, &c->gi);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_GAMEMENU_DELETE))) {
		/* Gone: this page describes a game that is not there, so it closes
		 * onto the shelf as it is now, and the message follows it. Still
		 * here (Cancel, a BIOS, a refusal): the page stays, as it was. */
		c->said[0] = '\0';
		if (game_delete(a, c->owner, c->g, c->said, sizeof c->said)) {
			c->deleted = true;
			return MENU_DONE;
		}
		if (c->said[0]) result_message(a, T(S_GAMEMENU_DELETE), c->said);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_PORTMASTER_REINSTALL_TITLE))) {
		/* PortMaster's page, to reinstall it (DESIGN.md 11.33). It can end
		 * in PortMaster itself (Open PortMaster), which closes this page
		 * with the launcher */
		pm_install_screen(a, &a->sys.systems[c->owner], true, NULL, false);
		if (!a->running) return MENU_DONE;
		gi_gather(a, c->owner, c->g, &c->gi);
		plat_input_flush();
		memset(&a->in, 0, sizeof a->in);
		return MENU_STAY;
	}

	if (!strcmp(rows[sel].label, T(S_MEDIA_ROW))) {
		/* Cover and Video, a page of its own (DESIGN.md 11.34): what the
		 * sources offer beside the cover there is, picked by A, the one
		 * replaced kept as Before; and the video snap. It replaced Get and
		 * Replace Box Art, which fetched one cover nobody saw first. */
		media_screen(a, c->owner, c->g);
		/* The title sort moved entries, but retained this shelf's selection. */
		c->g = &c->v->list.items[c->v->cursor];
		c->owner = shelf_owner(a, a->sys_cursor, c->v->cursor);
	}
	gi_gather(a, c->owner, c->g, &c->gi);
	plat_input_flush();
	memset(&a->in, 0, sizeof a->in);
	return MENU_STAY;
}

void game_info_screen(app *a)
{
	info_ctx c = { 0 };

	c.a = a;
	c.v = &a->view[a->sys_cursor];
	if (c.v->list.count == 0) return;
	c.owner = shelf_owner(a, a->sys_cursor, c.v->cursor);
	c.g = &c.v->list.items[c.v->cursor];
	gi_gather(a, c.owner, c.g, &c.gi);
	c.st.accent = a->sys.systems[c.owner].accent;
	/* Without this the panel sized itself to its own content, and its heading
	 * is a GAME TITLE - so it drew 976px wide beside every other menu's 800.
	 * info_preview next door was given the shared width and this was not,
	 * which is the same one-of-two-paths miss as every other bug of the day. */
	c.st.fixed_w = menu_std_width(a);

	menu_run(a, &c.st, info_build, info_key, &c);

	/* The card may now hold art it did not before. */
	free_all_textures(a);
	if (c.fav_changed) refresh_favorites_shelf(a);
	if (c.coll_changed && !c.deleted) refresh_collections_shelf(a);
	if (c.deleted && c.said[0] && a->running)
		result_message(a, T(S_GAMEMENU_DELETE), c.said);
}

/* One frame of it, from the real gatherer where possible: the fixture is the
 * game actually under the cursor, so a shot is a picture of this library
 * rather than of numbers somebody typed. */
bool shot_info_art;

void info_preview(app *a, bool net, const char *on_row)
{
	sysview   *v = &a->view[a->sys_cursor];
	menu_row   rows[GI_MAX];
	game_info  gi;
	int        owner, n, sel;

	if (v->list.count == 0) return;
	owner = shelf_owner(a, a->sys_cursor, v->cursor);
	gi_gather(a, owner, &v->list.items[v->cursor], &gi);
	/* --info-art: as a game with a cover and no video, Cover and Video's
	 * other value */
	if (shot_info_art) gi.has_art = true;
	n = gi_rows(rows, &gi, net);
	/* Where the screen actually opens, asked the way menu_run_body asks it
	 * rather than hardcoded. It was a literal 5, which was the Box Art action
	 * until the rows were reordered and then was Saves - so the shot showed a
	 * highlight on a dead row, which the real screen cannot do. */
	sel = rows[0].live ? 0 : menu_step_sel(rows, n, 0, +1);
	/* --info-delete, --info-rename, --info-collect and --info-on-buttons:
	 * the cursor on Delete Game, Rename, Add to Collection or Buttons, by its
	 * label as info_key finds it */
	for (int i = 0; on_row && i < n; i++)
		if (!strcmp(rows[i].label, on_row)) sel = i;

	draw_shelf(a);
	ui_scrim(a->r, 0);
	menu_draw(a, v->list.items[v->cursor].title, rows, n, sel, menu_std_width(a),
	          a->sys.systems[owner].accent);
}

/* --info-buttons: the page the Buttons row opens, over the shelf, for the
 * game under the cursor; with --gm-pick (`pick`), Y's choices as A opens
 * them there, over the backdrop the page gives its own screens */
void info_buttons_preview(app *a, bool pick)
{
	sysview *v = &a->view[a->sys_cursor];
	int owner;

	if (v->list.count == 0) return;
	owner = shelf_owner(a, a->sys_cursor, v->cursor);
	if (pick) {
		ctlscr_page_shot_pick(a, &a->sys.systems[owner], v->list.items[v->cursor].file,
		                      v->list.items[v->cursor].title, a->sys.systems[owner].accent,
		                      CT_BUTTON + RM_Y);
		return;
	}
	draw_shelf(a);
	ui_scrim(a->r, 0);
	ctlscr_page_shot(a, &a->sys.systems[owner], v->list.items[v->cursor].file,
	                 v->list.items[v->cursor].title, a->sys.systems[owner].accent, 0);
}
