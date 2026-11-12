/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_LIBRARY_H
#define ZYPEROS_LIBRARY_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

#include "db.h"

#define LIB_NAME 256
#define LIB_PATH 544

typedef struct {
	char name[LIB_NAME];  /* unchanged filename stem: art and file identity */
	char title[LIB_NAME]; /* stored catalog title, else cleaned filename */
	char file[LIB_PATH];  /* launch path relative to Roms/<folder>: a filename,
	                       * or "<folder>/<disc>" for a disc-folder game */
	/* The ROM's mtime, for the recently-added sort order, and
	 * LIB_ADDED_UNKNOWN until that order asks for it (lib_added).
	 *
	 * The scan stat'ed every file for it, every boot, whatever order the
	 * shelves were in: 266 ms of the Pixel 2's boot with the page cache
	 * dropped and 93 ms with it warm, for 6002 entries (2026-11-05), for a
	 * key only Recently Added reads. */
	long added;
	/* Sort keys, filled by sort_apply just before a sort that needs them and
	 * meaningless otherwise. They live here rather than in a parallel array
	 * so the entries can be moved by qsort with their keys attached - which
	 * is the whole reason the keys are precomputed. */
	long play_secs;
	long last_played;
} game_entry;

/* How many of the left-out names a scan keeps for the log. */
#define LIB_SKIPS_SHOWN 3

typedef struct {
	game_entry *items;
	int count;
	bool scanned;
	/* What the scan left out: files of a type this system does not take, and
	 * folders with nothing in them to launch. Counted, with the first few by
	 * name, so the log can answer "I copied it and it is not on the shelf" -
	 * which it could not, since a skip was silent. Dot entries and .media
	 * are not counted: nobody put those there expecting a game. */
	int skipped;
	char skipped_eg[LIB_SKIPS_SHOWN][LIB_NAME];
} game_list;

/* Scan Roms/<folder> for files whose extension appears in exts (a comma or
 * space separated list, without dots; empty means take everything). */
/* Whether `ext` (no dot; NULL for a name with none) is one of `list`'s, by
 * that rule and in any case: "SFC" is in "nes,sfc" and in "nes sfc". An empty
 * or NULL list allows everything. The shelf and Box Art both ask this, so the
 * two cannot disagree about which files are games. */
bool lib_ext_allowed(const char *ext, const char *list);
/* Whether a filename is a disc image rather than a cartridge. */
bool lib_is_disc(const char *name);
/* Whether a shelf's games are folders launched by a hook file (ScummVM's),
 * from its extension list. */
bool lib_shelf_hooked(const char *exts);

bool lib_scan(const char *roms_root, const char *folder, const char *exts,
              game_list *out);
/* lib_scan in its two halves, for the saved library (src/savedlib.h): the
 * listing, which is everything read off the card - the games' files and
 * names, in the folder's order, and what was left out - and the finish, the
 * titles from the database and the shelf's order. A listing kept from an
 * earlier scan and finished is what lib_scan would have made of the same
 * folder. lib_scan_listing's answer is lib_scan's. */
bool lib_scan_listing(const char *roms_root, const char *folder, const char *exts,
                      game_list *out);
void lib_scan_finish(const char *folder, game_list *l);
/* A listing's fingerprint: its files, names and left-out count, in any order */
uint64_t lib_listing_digest(const game_list *l);

/* An entry's added time before Recently Added has asked for it */
#define LIB_ADDED_UNKNOWN (-1L)
/* The added time of the entry whose launch path is `file` in Roms/<folder>:
 * the mtime of its first path part, which is the file itself for a game at
 * the top of the folder and the game's own folder for a disc set (copying a
 * disc into it touches the folder). 0 when it cannot be read, which sorts to
 * the bottom of that order, where "not known when this arrived" belongs. */
long lib_added(const char *roms_root, const char *folder, const char *file);
void lib_free(game_list *l);

/* The display title for a ROM's name: everything up to the first bracketed
 * group that follows a space, so "Contra (USA)" reads as "Contra". Exposed
 * because the play-time screen shows names too and had been printing the
 * cataloging with them - the rule belongs in one place, not two.
 *
 * Give it a name with no extension. `out` should take LIB_NAME bytes. */
void lib_title(const char *name, char *out, size_t n);

/* Resolve a display title by the unchanged folder/file key. Does not reorder
 * entries or change the launch path, stem, stats, or disc-folder grouping. */
void lib_display_title(db *d, const char *folder, const char *file,
                       const char *name, char *out, size_t n);
bool lib_refresh_title(db *d, const char *folder, game_entry *entry);

/* Shared by scans and every sort. Equal display names retain deterministic
 * file order; separate dumps never become one entry. UTF-8 stays bytewise,
 * with the existing ASCII case-insensitive order, not locale collation. */
static inline int lib_name_cmp(const game_entry *a, const game_entry *b)
{
	int c = strcasecmp(a->title[0] ? a->title : a->name,
	                   b->title[0] ? b->title : b->name);
	if (!c) c = strcasecmp(a->name, b->name);
	return c ? c : strcmp(a->file, b->file);
}

/* A folder that EmulationStation's scrapers fill inside a console's folder
 * (images/, videos/, media/ and the rest, src/esmedia.h), in any case: art
 * and video for another launcher, never a game or a folder of games. Not
 * counted as left out by a scan. */
bool lib_es_media_dir(const char *name);

/* The folder a system's shelf reads when it is not the one systems.cfg names
 * (`home`): an EmulationStation name of the system's (its es line), chosen
 * by the shelf's scan (src/shelfbuild.c, shelf_folder_choose). Keyed on the
 * home folder, which is one system's alone. lib_folder_set with `folder`
 * the home itself, or NULL, forgets it; lib_folder_get answers the folder
 * in use for `home`, `home` itself when none; lib_folder_home the home of a
 * folder in use, the folder itself when it is no other system's. Safe from
 * any thread: Box Art and ScreenScraper ask on theirs. */
void lib_folder_set(const char *home, const char *folder);
const char *lib_folder_get(const char *home, char *out, size_t n);
const char *lib_folder_home(const char *folder, char *out, size_t n);
void lib_folder_reset(void);
/* Which folder a shelf with EmulationStation names reads, by the games each
 * holds: -1 for its own (`home_games` of them) while that has any, else the
 * index into `games` of the folder with the most, the first on a tie; -1 too
 * when none has any. Pure, for tools/esmedia-check.c. */
int lib_folder_pick(int home_games, const int *games, int n);

/* Restore selection after a title sort. The owner is part of the identity on
 * Favorites, where two systems can have the same file name. */
int lib_find_entry(const game_list *list, const int *owners,
                   int owner, const char *file);

#endif
