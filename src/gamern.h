/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GAMERN_H
#define ZYPEROS_GAMERN_H

/* Rename, from a game's details page (DESIGN.md 11.41): the game's file, or
 * its folder for a game that is one, and everything the launcher, Diatom and
 * the cores find by its name - its saves, its states and Auto, its cover,
 * its video snap and Before's cover, its cached achievement set, the
 * EmulationStation pictures and video named after it and its gamelist.xml
 * entry (src/esmedia.h) - and its rows in the card's library database:
 * details, favorite, play sessions, Pak and where its cover came from.
 *
 * THE RULE IS DELETE GAME'S (src/gamedel.h): every path is built from the
 * game's own key the way the launcher and the cores build it, and a file
 * another game builds too (a save two systems' games share, a cover a .cue
 * and a .chd of one name share) is COPIED to the new name, so that game
 * keeps its own. Nothing is found by a pattern.
 *
 * NOTHING IS WRITTEN OVER. A new name that another game on the shelf has,
 * or that any of the paths would take from a file already there, is
 * refused before anything moves (GR_TAKEN). A change of case alone is a
 * rename on a card that ignores case, done through a name of its own.
 *
 * What keeps its name: an arcade set, whose name is the game for FinalBurn
 * Neo, MAME and Flycast; a BIOS set; a port, whose script and folders
 * PortMaster knows by name (GR_KEEPS_NAME). The details page offers no
 * Rename for those.
 *
 * No SDL: tools/gamern-check.c links this with src/gamedel.c,
 * src/esmedia.c, src/library.c, src/db.c, src/favorites.c, src/stats.c and
 * src/portinfo.c. */

#include <stdbool.h>
#include <stddef.h>

#include "esmedia.h"
#include "gamedel.h"

/* A name as typed on the keyboard: its spaces at either end dropped, into
 * `out`. False for one no card can take or no core would find: empty, a
 * dot first or last, a / \ : * ? " < > | or a control character, broken
 * UTF-8, or longer than GR_NAME_MAX bytes. */
#define GR_NAME_MAX 160
bool gr_name_ok(const char *typed, char *out, size_t n);

/* Whether a game on this shelf can be renamed at all: GR_KEEPS_NAME's.
 * `dir` is game_entry.dir: the folders of games in front of its own part. */
bool gr_renames(const char *core, const char *file, unsigned dir);

typedef enum {
	GR_OK,
	GR_BAD_NAME,     /* gr_name_ok said no */
	GR_SAME,         /* the name it has */
	GR_TAKEN,        /* another game, or a file one of the paths would take */
	GR_KEEPS_NAME,   /* an arcade set, a BIOS set, a port */
	GR_GONE,         /* the game's own file is not on the card */
	GR_TOO_MANY      /* more paths than a plan holds; a path too long */
} gr_why;

#define GR_MAX 96
#define GR_ES  24

typedef struct {
	/* Each move, the game first: from, to, and whether it is a copy (a
	 * file another game shares, which it keeps) */
	char          from[GR_MAX][GD_PATH], to[GR_MAX][GD_PATH];
	unsigned char copy[GR_MAX];
	int           n;
	/* The game's new launch path and name */
	char          new_file[LIB_PATH], new_name[LIB_NAME];
	gr_why        why;
	char          bad[GD_PATH];      /* the path refused, for the log */
	/* gamelist.xml's entry, which follows: its pictures as they move */
	char          es_rel[GR_ES][ES_PATH], es_to[GR_ES][ES_PATH];
	int           nes;
	char          roms[GD_PATH], folder[LIB_NAME], file[LIB_PATH], name[LIB_NAME];
} gr_plan;

/* The plan for naming game `g` `typed`. False, with `why`, when it is
 * refused; nothing has moved then. `others` excludes the game itself. */
bool gr_plan_make(const gd_card *card, const gd_game *g, const gd_other *others, int nothers,
                  const char *typed, gr_plan *p);

typedef struct {
	bool game_moved;      /* the game's own file or folder has its new name */
	int  moved, copied;   /* the rest */
	int  failed;          /* paths still under the old name only */
	bool es_list;         /* gamelist.xml's entry followed */
} gr_done;

/* Run a plan: the game first, and if it cannot move (a card turned
 * read-only) nothing else is touched */
void gr_plan_run(const gr_plan *p, gr_done *d);

/* Its rows in the card's library database (db_lib()) after the files: the
 * games table's (its title emptied, db_game_move), its favorite, its play
 * sessions, its Pak and where its covers came from. How many rows moved. */
int gr_follow(const char *folder, const char *tag, const char *file, const char *name,
              const char *new_file, const char *new_name);

#endif
