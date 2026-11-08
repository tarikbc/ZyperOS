/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GAMEDEL_H
#define ZYPEROS_GAMEDEL_H

/* Delete Game, from a game's details page (DESIGN.md 11.31): the game's
 * files, its saves, its states, its cover and video, its cached achievement
 * set and its rows in the card's library database, and nothing else.
 *
 * THE RULE IS THE KEY. Every path is built from the game's own key - its
 * system's folder and tag, its launch path and its name - the way the
 * launcher, Diatom and the cores build the same paths when they write them.
 * Nothing is found by a pattern, a wildcard or a listing of a folder, so a
 * file that only looks like this game's ("Tetris (World) (Rev 1).gb" beside
 * "Tetris (World).gb") is never in reach. The one listing is the game's own
 * folder, for a game that IS a folder (a multi-disc set, a ScummVM game),
 * which the shelf shows as one game and File Transfer already deletes whole.
 *
 * A FILE ANOTHER GAME SHARES STAYS. Saves are flat in Saves/ and named after
 * the ROM's stem alone, so "Tetris (World).gb" and "Tetris (World).gbc" on
 * two shelves write the same .srm; a .cue and a .chd of one name share a
 * cover; two .cue sheets can name one .bin. Each path is held against every
 * other game on the card's shelves, and the ones another game builds too are
 * left where they are.
 *
 * NOTHING LEAVES THE GAME'S FOLDERS. A path is refused, and with it the whole
 * delete, if it would land outside Roms/<console>/, Saves/ or the console's
 * states folder: a sheet that names "../" or an absolute path, a folder that
 * is a link to somewhere else. Lexically first, then by realpath, since a
 * host or an ext4 card can have links where the exFAT card cannot.
 *
 * No SDL: tools/gamedel-check.c links this, src/library.c, src/db.c,
 * src/favorites.c and src/stats.c with a card made up of small files. */

#include <stdbool.h>
#include <stddef.h>

#define GD_PATH 1200
/* Paths in one plan. A disc set's sheet can name 99 tracks; the rest is
 * about forty (saves, states, media, the set) */
#define GD_MAX  192

/* The card's three roots */
typedef struct {
	const char *roms;     /* Roms/ */
	const char *saves;    /* Saves/ */
	const char *states;   /* .userdata/shared/.zyperos, a folder per console */
} gd_card;

/* The game, as its OWN system has it: on Favorites, its owner's */
typedef struct {
	const char *folder;   /* Roms/<folder> */
	const char *tag;
	const char *core;     /* the core that runs it now */
	const char *exts;     /* the shelf's extension list */
	const char *file;     /* the launch path: "x.gb", or "<dir>/<disc>" */
	const char *name;     /* the stem: art, achievements, identity */
} gd_game;

/* Every other game on the card's shelves, for what they share */
typedef struct {
	const char *folder;
	const char *file;
	const char *name;
	bool        hooked;   /* its shelf's games are folders (ScummVM's) */
} gd_other;

typedef enum {
	GD_FILE,        /* one file */
	GD_TREE,        /* a folder and everything in it: the game's own */
	GD_EMPTY_DIR    /* a folder, only when nothing is left in it */
} gd_kind;

typedef enum {
	GD_OK,
	GD_BIOS,        /* a BIOS set other games need: neogeo.zip, pgm.zip */
	GD_OUTSIDE,     /* a path would leave the game's folders */
	GD_TOO_MANY     /* more paths than a plan holds; a path too long */
} gd_why;

typedef struct {
	char          path[GD_MAX][GD_PATH];
	unsigned char kind[GD_MAX];
	int           n;
	int           kept;           /* paths another game shares, left */
	gd_why        why;
	char          bad[GD_PATH];   /* the path refused, for the log */
} gd_plan;

/* A BIOS set: an arcade .zip or .7z whose name is one the cores read as the
 * board rather than a game (neogeo, pgm, naomi, awbios...). It can sit on a
 * shelf, since FinalBurn Neo also looks beside the game, and is never
 * deleted from there. */
bool gd_is_bios(const char *file);

/* What the states folder calls a game: its launch file's base, or on a
 * hooked shelf (its games are folders) the folder's name. MENU > Storage
 * asks it of each game, to know whose old states are whose (src/storage.h). */
const char *gd_state_base(const char *file, const char *name, bool hooked);

/* Whether a shelf's games can be deleted here at all. A port is a script, a
 * folder beside it and PortMaster's shared libraries, and nothing says which
 * of those are its own; so Ports has no Delete Game. */
bool gd_shelf_deletes(const char *core);

/* The plan: every path this game's delete would take, the game's own files
 * first. False, with `why` saying so, when it is refused; nothing is
 * deleted then. `others` excludes the game itself. */
bool gd_plan_make(const gd_card *card, const gd_game *g,
                  const gd_other *others, int nothers, gd_plan *p);

/* What became of each path of a plan */
enum { GD_NOT_THERE, GD_WENT, GD_STAYED };

typedef struct {
	unsigned char went[GD_MAX]; /* each plan path's: GD_WENT, GD_STAYED... */
	int  files;                 /* files deleted */
	int  folders;               /* folders deleted */
	int  failed;                /* paths that are still there */
	bool game_gone;             /* the game's own files went */
	unsigned long long bytes;   /* the room given back */
} gd_done;

/* Run a plan. The game's own files go first, and if they cannot (a card
 * that turned read-only) nothing else is touched: saves without their game
 * are worth more than a game without its saves. */
void gd_plan_run(const gd_plan *p, gd_done *out);

/* Its rows in the card's library database (db_lib()): the games table's
 * details and title, the favorite, and every play session, which are its
 * play time and the facts the shelf sorts by. How many rows went. Earned
 * achievements stay: they are the player's, kept by the RetroAchievements
 * game rather than by the file. */
int gd_forget(const char *folder, const char *tag, const char *file);

/* Where a shelf's cursor goes when entry `at` of `count` is taken off it:
 * the next game, which is now at `at`, or the one before at the end. -1
 * when the shelf is left empty. */
static inline int gd_cursor_after(int count, int at)
{
	if (count <= 1) return -1;
	if (at < 0) at = 0;
	return at < count - 1 ? at : count - 2;
}

#endif
