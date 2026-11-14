/* What portkeys reads to draw its bar in the launcher's theme over a port,
 * without the Pixel 2.
 *
 *     make check-portkeys
 *
 * The launcher exits for a port, so nothing rewrites /tmp/zyperos-theme
 * while one runs, and portkeys (tools/portkeys.c) follows Auto itself: the
 * Appearance word from the device database, the zone Auto's hour is read in
 * from the card's library (the launcher sets TZ in its own process alone),
 * and the theme file only when the database cannot be read. Every one of
 * those fails where nobody is looking - over a game, at 19:00 - so the parts
 * of it that are not ioctls are here, in tools/pkset.c, against scratch
 * databases:
 *
 *   a setting read, missing, empty, from a database that is not one, from
 *   none, and from one locked past the busy timeout;
 *   the zone: one on Date & Time's list taken, and one off it that the
 *   system has (Date & Time's Auto sets those), and New York for a missing
 *   key, an empty one, one the system does not have, a path, a folder, an
 *   unreadable library, a locked one and none given; a WAL library being
 *   written by another connection still read; TZ set to the answer;
 *   the bar's theme: the word over the file, the file only with no word, a
 *   hand edit of the file not reaching a bar that has a word, and Auto at
 *   its edges in the zone the library names.
 *
 * Links tools/pkset.c, src/theme.c, src/themefile.c, src/clock.c and SQLite,
 * and NOT SDL. Writes under a scratch directory of its own, never
 * /tmp/zyperos-theme.
 */
#include "pkset.h"

#include "../src/design.h"
#include "../src/themefile.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

static char dir[64];

static void path_of(char *out, size_t n, const char *name)
{
	snprintf(out, n, "%s/%s", dir, name);
}

/* A database with the launcher's settings table, in `mode` (WAL, as the
 * launcher's are, or DELETE for one a lock can keep a reader out of), holding
 * key = value when key is not NULL. */
static void make_db(const char *name, const char *mode, const char *key, const char *value)
{
	char p[128], sql[256];
	sqlite3 *d;

	path_of(p, sizeof p, name);
	if (sqlite3_open(p, &d) != SQLITE_OK) { ck(0, "a scratch database opens"); return; }
	snprintf(sql, sizeof sql, "PRAGMA journal_mode=%s;"
	         "CREATE TABLE IF NOT EXISTS settings(key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);",
	         mode);
	sqlite3_exec(d, sql, NULL, NULL, NULL);
	if (key) {
		sqlite3_stmt *st;

		sqlite3_prepare_v2(d, "INSERT OR REPLACE INTO settings(key,value) VALUES(?,?);", -1, &st, NULL);
		sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
		sqlite3_bind_text(st, 2, value, -1, SQLITE_STATIC);
		sqlite3_step(st);
		sqlite3_finalize(st);
	}
	sqlite3_close(d);
}

static sqlite3 *open_db(const char *name)
{
	char p[128];
	sqlite3 *d = NULL;

	path_of(p, sizeof p, name);
	if (sqlite3_open_v2(p, &d, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK) {
		sqlite3_close(d);
		return NULL;
	}
	sqlite3_busy_timeout(d, 200);
	return d;
}

/* The zone pkset_zone_read gives for the library `name`, and whether it took
 * it from there */
static bool zone_of(const char *name, char *tz, size_t n)
{
	char p[128];

	path_of(p, sizeof p, name);
	return pkset_zone_read(p, tz, n);
}

static long ms_since(const struct timespec *t0)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (t.tv_sec - t0->tv_sec) * 1000L + (t.tv_nsec - t0->tv_nsec) / 1000000L;
}

/* 2026-11-04 at h:00 UTC, whatever TZ says */
static time_t utc(int h)
{
	return (time_t)1793750400 + h * 3600;    /* 2026-11-04 00:00:00 UTC */
}

static void setting(void)
{
	char v[32];
	sqlite3 *d;

	printf("a setting, from the device database\n");
	make_db("dev.db", "WAL", "appearance", "auto");
	d = open_db("dev.db");
	ck(pkset_text(d, "appearance", v, sizeof v) && !strcmp(v, "auto"), "a saved word is read");
	ck(pkset_text(d, "volume", v, sizeof v) && !v[0], "a key not there answers, with nothing");
	sqlite3_close(d);

	make_db("empty.db", "WAL", "appearance", "");
	d = open_db("empty.db");
	ck(pkset_text(d, "appearance", v, sizeof v) && !v[0], "an empty word answers, empty");
	sqlite3_close(d);

	strcpy(v, "junk");
	ck(!pkset_text(NULL, "appearance", v, sizeof v) && !v[0], "no database does not answer");

	{
		char p[128];
		FILE *f;

		path_of(p, sizeof p, "notadb.db");
		f = fopen(p, "w");
		if (f) { fputs("this is not a database, it is a sentence\n", f); fclose(f); }
		d = open_db("notadb.db");
		strcpy(v, "junk");
		ck(!pkset_text(d, "appearance", v, sizeof v) && !v[0], "a file that is not a database does not answer");
		sqlite3_close(d);
	}

	/* No settings table: a database, but not the launcher's */
	{
		char p[128];
		sqlite3 *o;

		path_of(p, sizeof p, "other.db");
		if (sqlite3_open(p, &o) == SQLITE_OK) {
			sqlite3_exec(o, "CREATE TABLE t(x);", NULL, NULL, NULL);
			sqlite3_close(o);
		}
		d = open_db("other.db");
		ck(!pkset_text(d, "appearance", v, sizeof v), "a database with no settings table does not answer");
		sqlite3_close(d);
	}

	/* Locked: another connection holds the file for writing, in a journal
	 * mode where that keeps readers out, past the busy timeout */
	{
		sqlite3 *w;
		struct timespec t0;
		bool got;

		make_db("locked.db", "DELETE", "appearance", "dark");
		w = open_db("locked.db");
		sqlite3_exec(w, "BEGIN EXCLUSIVE;", NULL, NULL, NULL);
		d = open_db("locked.db");
		clock_gettime(CLOCK_MONOTONIC, &t0);
		got = pkset_text(d, "appearance", v, sizeof v);
		ck(!got, "a database locked past the timeout does not answer");
		ck(ms_since(&t0) < 2000, "and the bar is not kept waiting for it");
		sqlite3_close(d);
		sqlite3_exec(w, "ROLLBACK;", NULL, NULL, NULL);
		sqlite3_close(w);
	}
}

static void zone(void)
{
	char tz[64], p[128];

	printf("the zone, from the card's library\n");
	make_db("lib.db", "WAL", "timezone", "Asia/Tokyo");
	ck(zone_of("lib.db", tz, sizeof tz) && !strcmp(tz, "Asia/Tokyo"), "a zone on the list is taken");

	make_db("nokey.db", "WAL", NULL, NULL);
	ck(!zone_of("nokey.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "no zone saved is New York, as the launcher has it");
	make_db("emptyzone.db", "WAL", "timezone", "");
	ck(!zone_of("emptyzone.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT), "an empty one is New York");
	/* Date & Time's Auto sets the zone the service names, on the list or
	 * not (src/autozone.h): one the system has is taken */
	make_db("autozone.db", "WAL", "timezone", "Asia/Ho_Chi_Minh");
	ck(zone_of("autozone.db", tz, sizeof tz) && !strcmp(tz, "Asia/Ho_Chi_Minh"),
	   "a zone off the list that the system has, as Auto sets, is taken");
	make_db("offlist.db", "WAL", "timezone", "Mars/Olympus_Mons");
	ck(!zone_of("offlist.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "one the system does not have is New York");
	make_db("pathzone.db", "WAL", "timezone", ":/etc/passwd");
	ck(!zone_of("pathzone.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "a path is New York, and never reaches TZ");
	make_db("upzone.db", "WAL", "timezone", "../../../../etc/passwd");
	ck(!zone_of("upzone.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "and so is one that climbs out of the zone files");
	make_db("rootzone.db", "WAL", "timezone", "/etc/localtime");
	ck(!zone_of("rootzone.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "or starts at the root");
	make_db("dirzone.db", "WAL", "timezone", "America");
	ck(!zone_of("dirzone.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "and a folder of zones is no zone");
	ck(!zone_of("missing.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT), "no library is New York");
	ck(!zone_of("notadb.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "a library that is not a database is New York");
	ck(!pkset_zone_read(NULL, tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
	   "no library given (an older launch.sh) is New York");
	path_of(p, sizeof p, "missing.db");
	ck(access(p, F_OK) != 0, "and a library that was not there is not made");

	{
		sqlite3 *w;

		make_db("locked-lib.db", "DELETE", "timezone", "Asia/Tokyo");
		w = open_db("locked-lib.db");
		sqlite3_exec(w, "BEGIN EXCLUSIVE;", NULL, NULL, NULL);
		ck(!zone_of("locked-lib.db", tz, sizeof tz) && !strcmp(tz, PKSET_ZONE_DEFAULT),
		   "a library locked past the timeout is New York");
		sqlite3_exec(w, "ROLLBACK;", NULL, NULL, NULL);
		sqlite3_close(w);
	}
	/* The launcher's databases are WAL, where a writer does not keep a
	 * reader out: the zone is read while another connection writes */
	{
		sqlite3 *w = open_db("lib.db");

		sqlite3_exec(w, "BEGIN IMMEDIATE; INSERT OR REPLACE INTO settings VALUES('x','y');",
		             NULL, NULL, NULL);
		ck(zone_of("lib.db", tz, sizeof tz) && !strcmp(tz, "Asia/Tokyo"),
		   "a WAL library being written is still read");
		sqlite3_exec(w, "ROLLBACK;", NULL, NULL, NULL);
		sqlite3_close(w);
	}

	path_of(p, sizeof p, "lib.db");
	pkset_zone_load(p);
	ck(getenv("TZ") && !strcmp(getenv("TZ"), "Asia/Tokyo"), "loading the zone sets TZ to it");
	printf("  (the line below, for a zone the system does not have, is expected)\n");
	path_of(p, sizeof p, "offlist.db");
	pkset_zone_load(p);
	ck(getenv("TZ") && !strcmp(getenv("TZ"), PKSET_ZONE_DEFAULT), "and to New York when it falls back");
}

static void bar(void)
{
	char file[128], p[128], word[16];
	sqlite3 *d;

	printf("the bar's theme\n");
	setenv("TZ", "UTC", 1);
	tzset();
	path_of(file, sizeof file, "theme");

	themefile_write(file, DS_DARK);
	ck(pkset_bar_theme(NULL, utc(12), file) == DS_DARK, "with no word, the file: dark");
	themefile_write(file, DS_LIGHT);
	ck(pkset_bar_theme(NULL, utc(12), file) == DS_LIGHT, "and light, read again at each press");
	unlink(file);
	ck(pkset_bar_theme(NULL, utc(0), file) == DS_LIGHT, "no file is Beard, the default, at night too");

	themefile_write(file, DS_DARK);
	ck(pkset_bar_theme("light", utc(12), file) == DS_LIGHT,
	   "a word read is the bar's: the file edited to dark does not reach it");
	ck(pkset_bar_theme("", utc(0), file) == DS_LIGHT, "no word saved is Light, whatever the file says");
	ck(pkset_bar_theme("dark", utc(12), file) == DS_DARK, "dark is dark at noon");
	ck(pkset_bar_theme("auto", utc(6) + 3599, file) == DS_DARK, "auto at 6:59:59 is night");
	ck(pkset_bar_theme("auto", utc(7), file) == DS_LIGHT, "auto at 7:00 is day");
	ck(pkset_bar_theme("auto", utc(19), file) == DS_DARK, "auto at 19:00 is night");

	/* The wiring as main has it: the word from the device database, the
	 * zone from the library, the theme at a press */
	make_db("dev-auto.db", "WAL", "appearance", "auto");
	d = open_db("dev-auto.db");
	ck(pkset_text(d, "appearance", word, sizeof word), "the device database answers");
	sqlite3_close(d);
	path_of(p, sizeof p, "lib.db");
	pkset_zone_load(p);
	themefile_write(file, DS_LIGHT);
	ck(pkset_bar_theme(word, utc(12), file) == DS_DARK, "auto at noon UTC in Tokyo is 21:00: night");
	ck(pkset_bar_theme(word, utc(23), file) == DS_LIGHT, "and at 23:00 UTC, 8:00 there: day");
	setenv("TZ", "UTC", 1);
	tzset();
}

static void cleanup(void)
{
	static const char *names[] = {
		"dev.db", "empty.db", "notadb.db", "other.db", "locked.db", "lib.db", "nokey.db",
		"emptyzone.db", "offlist.db", "pathzone.db", "locked-lib.db", "dev-auto.db",
		"autozone.db", "upzone.db", "rootzone.db", "dirzone.db",
	};
	static const char *tails[] = { "", "-wal", "-shm", "-journal" };
	char p[160];
	size_t i, k;

	for (i = 0; i < sizeof names / sizeof *names; i++)
		for (k = 0; k < sizeof tails / sizeof *tails; k++) {
			snprintf(p, sizeof p, "%s/%s%s", dir, names[i], tails[k]);
			unlink(p);
		}
	snprintf(p, sizeof p, "%s/theme", dir);
	unlink(p);
	snprintf(p, sizeof p, "%s/theme.tmp", dir);
	unlink(p);
	if (rmdir(dir) != 0) printf("  (scratch %s left: %s)\n", dir, strerror(errno));
}

int main(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
	snprintf(dir, sizeof dir, "/tmp/zyperos-portkeys-check-%ld", (long)getpid());
	if (mkdir(dir, 0700) != 0) { printf("portkeys: no scratch directory\n"); return 1; }

	setting();
	zone();
	bar();
	cleanup();

	if (fails) { printf("portkeys: %d FAILED\n", fails); return 1; }
	printf("portkeys: ok\n");
	return 0;
}
