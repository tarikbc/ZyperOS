/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_SAVEDLIB_H
#define ZYPEROS_SAVEDLIB_H

/* The saved library: each console folder's listing as the card last had it
 * (lib_scan_listing - the games' files and names, and what was left out), so
 * a start can put the shelves up without reading the card first, and read it
 * after (src/shelfbuild.h, lib_check). Titles and the shelves' order are not
 * in it: they come from the database at every start, as they always did.
 *
 * One file in .userdata/<device>, text, one line per game. It is a cache and
 * nothing more: a missing, unreadable or foreign file (another version wrote
 * it) is no saved library, and the start reads the card as it always did. */

#include "library.h"

#include <stdbool.h>

typedef struct {
	char      folder[LIB_NAME];
	char      exts[LIB_NAME];
	game_list list;           /* a listing: no titles, the folder's order */
	bool      taken;          /* moved out by savedlib_take */
} saved_shelf;

typedef struct {
	saved_shelf *shelves;
	int          count;
} saved_lib;

/* Read `path` into `out`, true when it was a saved library `version` wrote.
 * False leaves `out` empty. */
bool savedlib_load(const char *path, const char *version, saved_lib *out);

/* Move the listing saved for `folder` with `exts` into `out` (lib_free'd
 * first), true when there was one. A folder saved with other extensions is
 * another listing, and not taken. */
bool savedlib_take(saved_lib *s, const char *folder, const char *exts, game_list *out);

/* Shelves no one took: folders the systems no longer have */
int savedlib_untaken(const saved_lib *s);

void savedlib_free(saved_lib *s);

/* One shelf's listing, for savedlib_write */
typedef struct {
	const char      *folder;
	const char      *exts;
	const game_list *list;
} savedlib_row;

/* Write `n` listings to `path` for `version`, whole or not at all */
bool savedlib_write(const char *path, const char *version, const savedlib_row *rows, int n);

#endif
