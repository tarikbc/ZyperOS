/* SPDX-License-Identifier: GPL-3.0-only */
/* One game's rows in the library database, out to a file and back, for
 * px2-snap (tools/px2-snap.sh) through `zyperos.elf --game-rows`.
 *
 * A game's rows are the ones keyed on its tag and its file as the shelf
 * names it: its play sessions, which are its play time and when it was last
 * played (sess.<start>.<clock>.<tag>\t<file>, src/stats.h), its favorite
 * (fav.<tag>\t<file>, src/favorites.c) and its N64 Pak (pak.<tag>.<file>,
 * src/game_menu.h). There is no sqlite3 on the device, and --set only changes
 * a key that is already there, so a snapshot of them needs the launcher's own
 * database code.
 *
 * The file is a line a row: the key, a tab, the value, each with a backslash,
 * a tab, a newline and a carriage return written as \\, \t, \n and \r, since
 * a session's key and value both hold a tab. */
#ifndef ZYPEROS_GAMEROWS_H
#define ZYPEROS_GAMEROWS_H

#include <stdbool.h>
#include <stdio.h>

#include "db.h"

/* Whether `key` is one of this game's rows */
bool gamerows_mine(const char *key, const char *tag, const char *file);

/* This game's rows into `out`, in key order. How many, or -1. */
int gamerows_dump(db *d, const char *tag, const char *file, FILE *out);

/* This game's rows made what `in` holds: every row of the game's that is
 * there now goes, then each row read is written. A line that is not a row of
 * this game's is refused before anything changes, and so is a line that does
 * not read. How many rows were written, or -1 with nothing changed. */
int gamerows_put(db *d, const char *tag, const char *file, FILE *in);

/* The escaping above, exposed for the check: `in` to `out`, false when it
 * does not fit. gamerows_unescape is false too for a backslash before
 * anything but \, t, n or r. */
bool gamerows_escape(const char *in, char *out, size_t n);
bool gamerows_unescape(const char *in, char *out, size_t n);

#endif
