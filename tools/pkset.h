/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PKSET_H
#define ZYPEROS_PKSET_H

/* What portkeys (tools/portkeys.c) reads from the launcher's databases to
 * draw its bar in the launcher's theme over a port, which the launcher has
 * exited for: MENU > Appearance from the device database, the time zone
 * Auto's hour is read in from the card's library, and the choice between the
 * setting and THEME_FILE. Plain C, SQLite and no SDL and no device, so that
 * tools/portkeys-check.c links it with scratch databases: the rest of
 * portkeys is ioctls on the Pixel 2's panel and sound card. */

#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* The zone the launcher falls back on when its library holds none
 * (src/main.c, after db_init), and so does this. */
#define PKSET_ZONE_DEFAULT "America/New_York"

/* A text setting of `d`, src/db.c's db_get_str without the default: true
 * when the database answered, with "" in `out` for a key it does not hold;
 * false, `out` "", when it could not be asked (no database, no settings
 * table, not a database, locked past the connection's busy timeout). */
bool pkset_text(sqlite3 *d, const char *key, char *out, size_t n);

/* The handheld's time zone from the library database at `path`, into `out`:
 * true when the library names a zone this system has (clock_zone_have: on
 * src/clock.c's list, or a zone database name Date & Time's Auto set), and
 * otherwise false with PKSET_ZONE_DEFAULT. Waits up to 200 ms for a lock
 * held by another connection, as the device database's does. */
bool pkset_zone_read(const char *path, char *out, size_t n);

/* pkset_zone_read, and TZ set to the answer for this process: the launcher
 * sets TZ in its own process alone (plat_clock_zone), and the Pixel 2's
 * system has no other way to know the zone. A line on stderr when it falls
 * back to PKSET_ZONE_DEFAULT. */
void pkset_zone_load(const char *path);

/* The bar's theme at `now`, DS_LIGHT or DS_DARK: the Appearance word the
 * device database gave (pkset_text; "" for none saved) resolved as the
 * launcher resolves it (src/theme.c's theme_saved_at), or, when `saved` is
 * NULL because the database could not be read, what the theme file at
 * `file` says (src/themefile.h). The file is not read when there is a word:
 * a hand edit of it changes the bar over a port only when the database
 * cannot be read. */
int pkset_bar_theme(const char *saved, time_t now, const char *file);

#endif
