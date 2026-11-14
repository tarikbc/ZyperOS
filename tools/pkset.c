/* SPDX-License-Identifier: GPL-3.0-only */
/* portkeys' settings. See pkset.h. */
#include "pkset.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clock.h"
#include "theme.h"
#include "themefile.h"

bool pkset_text(sqlite3 *d, const char *key, char *out, size_t n)
{
	sqlite3_stmt *st;
	int rc;

	if (n) out[0] = '\0';
	if (!d || sqlite3_prepare_v2(d, "SELECT value FROM settings WHERE key=?;", -1, &st, NULL) != SQLITE_OK)
		return false;
	sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
	rc = sqlite3_step(st);
	if (rc == SQLITE_ROW && n) {
		const unsigned char *v = sqlite3_column_text(st, 0);

		snprintf(out, n, "%s", v ? (const char *)v : "");
	}
	sqlite3_finalize(st);
	return rc == SQLITE_ROW || rc == SQLITE_DONE;
}

bool pkset_zone_read(const char *path, char *out, size_t n)
{
	sqlite3 *lib = NULL;
	char tz[64];
	bool ok = false;

	/* Read-write, as the device database is opened, and not created: a
	 * read-only connection to a database in WAL mode cannot always read it,
	 * and a missing library is a fallback, not a file to make. Nothing is
	 * written. */
	if (path && sqlite3_open_v2(path, &lib, SQLITE_OPEN_READWRITE, NULL) == SQLITE_OK) {
		sqlite3_busy_timeout(lib, 200);
		/* Only a zone this system has (clock_zone_have): one on Date &
		 * Time's list (src/clock.c), or a name of the zone database whose
		 * file is here, as Date & Time's Auto sets (src/autozone.h). TZ is
		 * handed to the C library, which reads it as the name of a zone file
		 * and would read a path in it as one; a word from a file is not
		 * passed there unchecked, and clock_zone_name_ok takes no path. A
		 * zone edited by hand to something else is New York here. */
		ok = pkset_text(lib, "timezone", tz, sizeof tz) && clock_zone_have(tz);
	}
	sqlite3_close(lib);
	snprintf(out, n, "%s", ok ? tz : PKSET_ZONE_DEFAULT);
	return ok;
}

void pkset_zone_load(const char *path)
{
	char tz[64];

	if (!pkset_zone_read(path, tz, sizeof tz))
		fprintf(stderr, "portkeys: no time zone this system has in %s, so %s\n",
		        path ? path : "(no library given)", tz);
	setenv("TZ", tz, 1);
	tzset();
}

int pkset_bar_theme(const char *saved, time_t now, const char *file)
{
	return saved ? theme_saved_at(saved, now) : themefile_read(file);
}
