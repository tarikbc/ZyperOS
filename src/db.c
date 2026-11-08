/* SPDX-License-Identifier: GPL-3.0-only */
/* See db.h. No SDL and no platform header, deliberately, so the check can link
 * it without a window - the same reason atomic.c is kept clean. */
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "db.h"
#include "titletext.h"

struct db { sqlite3 *h; };

/* --- the defaults, compiled in ------------------------------------------
 * Shipped in the binary rather than as a .db in the payload, so there is no
 * second artifact to drift from this code and a deleted database self-heals
 * into a working one. These values are what config/zyperos.cfg shipped. */
static const db_default device_defaults[] = {
	/* A RUNG, 0..PLAT_VOL_MAX, which is what a nudge stores. config/zyperos.cfg
	 * shipped 40 percent and the launcher converted; keeping the percentage
	 * here would put two units under one key, and 15 percent would then be
	 * indistinguishable from rung 15. (40 * 20 + 50) / 100 = 8. */
	{ "volume",     "8"  },
	{ "brightness", "7"  },
	{ "audioout",   "auto" },  /* auto | speaker - see src/audioout.h */
	{ "channels",   "stereo" },/* stereo | mono | left | right - ditto */
	{ "home",       "spotlight" }, /* home layout, independent of Appearance */
	{ "battery_icon", "bars" },  /* bars | percentage | time: the corner's battery */
	{ "autooff",    "120" },   /* seconds without input, 0 is off */
	{ "wifi",       "0"  },    /* what THIS device was last doing, not the */
	{ "bluetooth",  "0"  },    /* shipped default - seeding merges the two */
	/* TOUR_DONE_KEY (src/tour.h): 0 until the tutorial has been seen on this
	 * handheld, so it shows once - and once after the upgrade that brings
	 * it, as nothing before set it - and `--set tour.done=1` can skip it */
	{ "tour.done",  "0"  },
	/* MENU > Language (src/i18n.h): a language's code, English until the
	 * first start's step or the menu says otherwise */
	{ "language",   "en" },
	/* MENU > Game Language on the Nintendo DS shelf (src/dslang.h): "auto",
	 * or one of the language words it lists */
	{ "ds_language", "auto" },
};

static const db_default library_defaults[] = {
	{ "timezone",       "America/New_York" },
	{ "startup_system", "NES" },

	/* Turbo, per system tag. `x:a~3,y:b~3` says X is a turbo A and Y a turbo
	 * B, three frames pressed and three released - about ten presses a second
	 * at 60 Hz.
	 *
	 * Listed only where BOTH of those are spare. X is the north button and Y
	 * is the west one; a two-button console uses south and east and leaves
	 * them both. A three-button Genesis takes west for its A - measured
	 * 2026-09-06 with Streets of Rage 2 - so it has one free button where
	 * turbo needs two, and SNES uses all four. MD and SFC are absent for that
	 * reason and not by oversight. */
	{ "turbo.NES",  "x:a~3,y:b~3" },
	{ "turbo.SMS",  "x:a~3,y:b~3" },
	{ "turbo.PCE",  "x:a~3,y:b~3" },
	{ "turbo.GB",   "x:a~3,y:b~3" },
	{ "turbo.GBC",  "x:a~3,y:b~3" },
	{ "turbo.NGP",  "x:a~3,y:b~3" },
	{ "turbo.NGPC", "x:a~3,y:b~3" },
	{ "turbo.GBA",  "x:a~3,y:b~3" },
	{ "turbo.GG",   "x:a~3,y:b~3" },

	/* Core options, from config/coreopts.cfg. The segment after "coreopt." is
	 * the system tag and an EMPTY one means global, which is why the first key
	 * has two dots. A tagged entry overrides a global of the same name. */
	{ "coreopt..mgba_sgb_borders",  "OFF" },
	{ "coreopt.GB.mgba_gb_model",   "Game Boy" },
	{ "coreopt.GB.mgba_gb_colors",  "DMG Green" },
	/* pcsx_rearmed's frameskip on "auto": Diatom drives it with a hint
	 * (diatom/src/env.c), and its "auto" skips a frame reported as about to
	 * underrun, which Diatom says only of frames fast forward will not show.
	 * The PlayStation's fast forward is not yet measured on the Pixel 2. */
	{ "coreopt..pcsx_rearmed_frameskip_type", "auto" },
	/* PicoDrive's, for the Sega CD and 32X, the same way: not yet measured
	 * on the Pixel 2. */
	{ "coreopt..picodrive_frameskip", "auto" },
	/* And the three cores whose heavy games fell short of fast forward's
	 * speed on the Pixel 2, every frame drawn though most are never shown:
	 * mGBA (the Game Boys and the GBA), Snes9x 2010 and Genesis Plus GX (the
	 * Genesis, Master System and Game Gear). Each core's own name for the
	 * option and for its automatic value, "auto", which skips a frame only
	 * when the frontend says it is about to underrun - in Diatom, only a
	 * frame fast forward will not show, so at 1x nothing changes. Read from
	 * each core's libretro_core_options.h and the strcmp in its libretro.c
	 * (libretro/mgba at e31759b, the tree mk/build-mgba-bridge.sh builds;
	 * libretro/snes9x2010 and libretro/Genesis-Plus-GX), where each one's
	 * own default is "disabled". */
	{ "coreopt..mgba_frameskip",            "auto" },
	{ "coreopt..snes9x_2010_frameskip",     "auto" },
	{ "coreopt..genesis_plus_gx_frameskip", "auto" },
	/* PicoDrive's own default, PAR, reports square pixels, 1.4286 for
	 * 320x224, where genesis_plus_gx reports 1.3061 for the same Genesis
	 * picture. CRT reports 1.2991, so the Sega CD and 32X shelves take the
	 * Genesis shelf's shape. Measured in Diatom's desktop port, 2026-11-03. */
	{ "coreopt..picodrive_aspect",  "CRT" },
	/* flycast2021, the Dreamcast's core (config/systems.cfg). The fork starts on
	 * its own HLE BIOS, which left Crazy Taxi 2 on the license screen; the
	 * real one is in Bios/dc. And 320x240, not 640x480: on the Pixel 2's
	 * Mali-G31 under Panfrost 640x480 held Crazy Taxi 2's attract mode to
	 * 65-75% of its speed, 320x240 ran it at full speed, 25 to 28 new frames a
	 * second on the panel (2026-11-04). */
	{ "coreopt..reicast_hle_bios", "disabled" },
	{ "coreopt..reicast_internal_resolution", "320x240" },
	/* Mupen64Plus-Next, the Nintendo 64's: GLideN64 at 320x240, the N64's
	 * own size, where the core's default is 640x480, on the same Mali-G31
	 * the Dreamcast's 320x240 above is for. */
	{ "coreopt..mupen64plus-43screensize", "320x240" },
	/* And its Rumble Pak in the controller, where the core's default is the
	 * Controller Pak: the Pixel 2 has a motor (2026-11-05). The trade is the
	 * Controller Pak's saves, which few games need - most save on the
	 * cartridge, as Mario Kart 64's records do; its ghosts are the loss. */
	{ "coreopt.N64.mupen64plus-pak1", "rumble" },
	/* The PlayStation's DualShock (config/systems.cfg, controller|PS1) turns
	 * analog on a button combo, L1+R1+SELECT by default, which a game's own
	 * buttons can press. On L3+R3 it cannot be: the Pixel 2 has neither,
	 * and with no sticks there is nothing analog to turn on. */
	{ "coreopt.PS1.pcsx_rearmed_analog_combo", "l3+r3" },
	/* ScummVM's buttons. The core's own defaults put the mouse's two buttons
	 * on L and R and the keyboard on the faces (A space, B return), where a
	 * point-and-click game is played with the pointer: the A cap clicks, the
	 * B cap right-clicks, and space and return move to X and L1. Y stays
	 * escape (skips a cutscene), START the ScummVM menu (save and load),
	 * SELECT the virtual keyboard, R2 the slow pointer, and R1 joins it. */
	{ "coreopt.SCUMMVM.scummvm_mapper_a", "RETROKE_LEFT_BUTTON" },
	{ "coreopt.SCUMMVM.scummvm_mapper_b", "RETROKE_RIGHT_BUTTON" },
	{ "coreopt.SCUMMVM.scummvm_mapper_x", "RETROK_SPACE" },
	{ "coreopt.SCUMMVM.scummvm_mapper_l", "RETROK_RETURN" },
	{ "coreopt.SCUMMVM.scummvm_mapper_r", "RETROKE_FINE_CONTROL" },
	/* MAME 2003-Plus, straight to the game: past the disclaimer and the
	 * warning screens, which a press would dismiss on every start. */
	{ "coreopt..mame2003-plus_skip_disclaimer", "enabled" },
	{ "coreopt..mame2003-plus_skip_warnings",   "enabled" },
	/* Opera, the 3DO's: its DSP on a thread of its own. Wolfenstein 3D's
	 * attract mode ran at 43 fps of 60 without it and 51 with it on a
	 * Pixel 2 (2026-11-04). */
	{ "coreopt..opera_dsp_threaded", "enabled" },
	/* The CDZ's BIOS, where the core's default is the front loader's: with
	 * front-sp1.bin, Metal Slug sat in the CD player screen with PUSH START
	 * BUTTON after 40 seconds; with neocd.bin it was at its own menu. The
	 * value is the core's own label for the file, which is why the Neo Geo
	 * CD shelf's disc bios is neocd/neocd.bin (config/systems.cfg). */
	{ "coreopt..neocd_bios",        "neocd.bin (CDZ)" },
};

const db_default *db_defaults(db_scope scope, size_t *count)
{
	if (scope == DB_DEVICE) {
		*count = sizeof device_defaults / sizeof *device_defaults;
		return device_defaults;
	}
	*count = sizeof library_defaults / sizeof *library_defaults;
	return library_defaults;
}

/* --- schema --------------------------------------------------------------
 * WAL and synchronous=NORMAL for both. NORMAL leaves the same ~33 s writeback
 * window an unsynced write has, which is the right trade for a volume level:
 * losing the last nudge to a power cut costs nothing, and FULL costs 5x per
 * write to prevent it. Measured on the card, 2026-09-05. */
static const char SCHEMA[] =
	"PRAGMA journal_mode=WAL;"
	"PRAGMA synchronous=NORMAL;"
	"CREATE TABLE IF NOT EXISTS settings("
	"  key TEXT PRIMARY KEY NOT NULL,"
	"  value TEXT NOT NULL"
	");";

/* The card's games, one row each. Library scope only - a handheld has no
 * business holding a synopsis for a card that is not in it. See db.h. */
static const char GAMES_SCHEMA[] =
	"CREATE TABLE IF NOT EXISTS games("
	"  folder TEXT NOT NULL,"
	"  file TEXT NOT NULL,"
	"  year TEXT,"
	"  publisher TEXT,"
	"  developer TEXT,"
	"  players TEXT,"
	"  genres TEXT,"
	"  esrb TEXT,"
	"  note TEXT,"
	"  synopsis TEXT,"
	"  scraped INTEGER,"
	"  title TEXT NOT NULL DEFAULT '',"
	"  PRIMARY KEY(folder, file)"
	");";

static bool run(db *d, const char *sql)
{
	char *err = NULL;
	int rc = sqlite3_exec(d->h, sql, NULL, NULL, &err);
	if (err) sqlite3_free(err);
	return rc == SQLITE_OK;
}

/* Existing cards predate title. Inspect the column rather than ignoring an
 * ALTER error: an unreadable or locked schema must not look like an upgrade.
 * ALTER adds a default without replacing rows, keys, settings or timestamps. */
static bool games_schema(db *d)
{
	sqlite3_stmt *st = NULL;
	bool has_title = false;
	int rc;

	if (!run(d, GAMES_SCHEMA)) return false;
	if (sqlite3_prepare_v2(d->h, "PRAGMA table_info(games);", -1, &st, NULL) != SQLITE_OK)
		return false;
	while ((rc = sqlite3_step(st)) == SQLITE_ROW) {
		const char *name = (const char *)sqlite3_column_text(st, 1);
		if (name && !strcmp(name, "title")) has_title = true;
	}
	sqlite3_finalize(st);
	return rc == SQLITE_DONE && (has_title || run(d,
	       "ALTER TABLE games ADD COLUMN title TEXT NOT NULL DEFAULT '';"));
}

db *db_open(const char *path, db_scope scope)
{
	db *d;
	const db_default *def;
	size_t n, i;

	if (!path || !*path) return NULL;
	if (!(d = calloc(1, sizeof *d))) return NULL;

	/* Serialized, whatever the library's default: the start's scan thread
	 * (main.c, scan_thread) reads both databases while the main thread reads
	 * the settings. The device's SQLite is serialized already; macOS's is
	 * multi-thread, where one connection on two threads at once corrupts
	 * the heap, and a host shot crashed in db_get_str that way (2026-11-07). */
	if (sqlite3_open_v2(path, &d->h, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
	                    SQLITE_OPEN_FULLMUTEX, NULL) != SQLITE_OK) {
		if (d->h) sqlite3_close(d->h);
		free(d);
		return NULL;
	}
	if (!run(d, SCHEMA)) { db_close(d); return NULL; }
	if (scope == DB_LIBRARY && !games_schema(d)) { db_close(d); return NULL; }

	/* The device database holds the RetroAchievements session token, so it
	 * asks for 0600. Sidecars included: a -wal holding the same pages at 0644
	 * would be the same secret in a different file. After the schema and not
	 * before, because journal_mode=WAL is what creates them.
	 *
	 * ON THE CARD THIS ACHIEVES NOTHING. /mnt/SDCARD is exfat mounted
	 * fmask=0022; exfat has no Unix permissions, so everything on it reads
	 * back 0755 whatever was asked for. Measured 2026-09-06.
	 *
	 * So do not delete this as dead, and do not trust it as protection. It is
	 * honored on ext4, on a development host, and on any card that is not
	 * exfat. */
	if (scope == DB_DEVICE) {
		char side[1100];
		chmod(path, 0600);
		snprintf(side, sizeof side, "%s-wal", path); chmod(side, 0600);
		snprintf(side, sizeof side, "%s-shm", path); chmod(side, 0600);
	}

	/* Seed only what is absent, so a default never overwrites a choice. That
	 * is the bug the old zyperos.cfg had until 2026-08: it was applied over
	 * the top of levels.cfg at every boot, putting the config ahead of the
	 * player. */
	def = db_defaults(scope, &n);
	for (i = 0; i < n; i++)
		if (!db_has(d, def[i].key))
			db_set_str(d, def[i].key, def[i].value);

	return d;
}

void db_close(db *d)
{
	if (!d) return;
	if (d->h) sqlite3_close(d->h);
	free(d);
}

/* memset through a volatile pointer, which the compiler cannot see through,
 * so a wipe of a buffer nothing reads again is not dropped as a dead store. */
static void *(*const volatile wipe)(void *, int, size_t) = memset;

bool db_get_account(db *d, const char *user_key, char *user, size_t un,
                    const char *secret_key, char *secret, size_t sn)
{
	/* Longer than any value that fits, so an over-long one arrives long
	 * enough to be seen to be over-long. */
	char u[257], s[257];
	bool ok;

	db_get_str(d, user_key, u, sizeof u, "");
	db_get_str(d, secret_key, s, sizeof s, "");
	ok = un < sizeof u && sn < sizeof s && strlen(u) < un && strlen(s) < sn;
	wipe(user, 0, un);
	wipe(secret, 0, sn);
	if (ok) {
		memcpy(user, u, strlen(u) + 1);
		memcpy(secret, s, strlen(s) + 1);
	}
	wipe(u, 0, sizeof u);
	wipe(s, 0, sizeof s);
	return ok;
}

bool db_get_str(db *d, const char *key, char *out, size_t n, const char *fallback)
{
	sqlite3_stmt *st = NULL;
	const unsigned char *v;
	bool got = false;

	if (out && n) out[0] = '\0';
	if (d && key && out && n &&
	    sqlite3_prepare_v2(d->h, "SELECT value FROM settings WHERE key=?;", -1, &st, NULL) == SQLITE_OK) {
		sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
		if (sqlite3_step(st) == SQLITE_ROW && (v = sqlite3_column_text(st, 0))) {
			snprintf(out, n, "%s", (const char *)v);
			got = true;
		}
		sqlite3_finalize(st);
	}
	if (!got && out && n) snprintf(out, n, "%s", fallback ? fallback : "");
	return got;
}

int db_get_int(db *d, const char *key, int fallback)
{
	char buf[64];
	char *end;
	long v;

	if (!db_get_str(d, key, buf, sizeof buf, NULL) || !buf[0]) return fallback;
	v = strtol(buf, &end, 10);
	/* A value that is not a number is not a zero. Saying so beats silently
	 * turning a typo into a setting. */
	if (end == buf || *end) return fallback;
	return (int)v;
}

bool db_has(db *d, const char *key)
{
	sqlite3_stmt *st = NULL;
	bool found = false;

	if (!d || !key) return false;
	if (sqlite3_prepare_v2(d->h, "SELECT 1 FROM settings WHERE key=?;", -1, &st, NULL) != SQLITE_OK)
		return false;
	sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
	found = sqlite3_step(st) == SQLITE_ROW;
	sqlite3_finalize(st);
	return found;
}

bool db_set_str(db *d, const char *key, const char *val)
{
	sqlite3_stmt *st = NULL;
	bool ok;

	if (!d || !key || !val) return false;
	/* INSERT OR REPLACE rather than UPSERT: 3.12.2 predates ON CONFLICT,
	 * and the device is the oldest sqlite this has to run against. */
	if (sqlite3_prepare_v2(d->h, "INSERT OR REPLACE INTO settings(key,value) VALUES(?,?);",
	               -1, &st, NULL) != SQLITE_OK)
		return false;
	sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 2, val, -1, SQLITE_TRANSIENT);
	ok = sqlite3_step(st) == SQLITE_DONE;
	sqlite3_finalize(st);
	return ok;
}

bool db_set_int(db *d, const char *key, int val)
{
	char buf[32];
	snprintf(buf, sizeof buf, "%d", val);
	return db_set_str(d, key, buf);
}

bool db_del(db *d, const char *key)
{
	sqlite3_stmt *st = NULL;
	bool ok;

	if (!d || !key) return false;
	if (sqlite3_prepare_v2(d->h, "DELETE FROM settings WHERE key=?;", -1, &st, NULL) != SQLITE_OK)
		return false;
	sqlite3_bind_text(st, 1, key, -1, SQLITE_TRANSIENT);
	ok = sqlite3_step(st) == SQLITE_DONE;
	sqlite3_finalize(st);
	return ok;
}

/* --- the games table ------------------------------------------------------ */

static void col_str(sqlite3_stmt *st, int i, char *out, size_t n)
{
	const unsigned char *v = sqlite3_column_text(st, i);
	snprintf(out, n, "%s", v ? (const char *)v : "");
}

bool db_game_get(db *d, const char *folder, const char *file, game_meta *out)
{
	sqlite3_stmt *st = NULL;
	bool got = false;

	if (out) memset(out, 0, sizeof *out);
	if (!d || !folder || !file || !out) return false;
	if (sqlite3_prepare_v2(d->h,
	               "SELECT year,publisher,developer,players,genres,esrb,note,synopsis,title"
	               " FROM games WHERE folder=? AND file=?;", -1, &st, NULL) != SQLITE_OK)
		return false;
	sqlite3_bind_text(st, 1, folder, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 2, file, -1, SQLITE_TRANSIENT);
	if (sqlite3_step(st) == SQLITE_ROW) {
		col_str(st, 0, out->year,      sizeof out->year);
		col_str(st, 1, out->publisher, sizeof out->publisher);
		col_str(st, 2, out->developer, sizeof out->developer);
		col_str(st, 3, out->players,   sizeof out->players);
		col_str(st, 4, out->genres,    sizeof out->genres);
		col_str(st, 5, out->esrb,      sizeof out->esrb);
		col_str(st, 6, out->note,      sizeof out->note);
		col_str(st, 7, out->synopsis,  sizeof out->synopsis);
		title_text((const char *)sqlite3_column_text(st, 8), out->title, sizeof out->title);
		got = true;
	}
	sqlite3_finalize(st);
	return got;
}

bool db_game_title(db *d, const char *folder, const char *file, char *out, size_t n)
{
	sqlite3_stmt *st = NULL;

	if (!out || !n) return false;
	out[0] = '\0';
	if (!d || !folder || !file) return false;
	if (sqlite3_prepare_v2(d->h, "SELECT title FROM games WHERE folder=? AND file=?;",
	                       -1, &st, NULL) != SQLITE_OK) return false;
	sqlite3_bind_text(st, 1, folder, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 2, file, -1, SQLITE_TRANSIENT);
	if (sqlite3_step(st) == SQLITE_ROW)
		title_text((const char *)sqlite3_column_text(st, 0), out, n);
	sqlite3_finalize(st);
	return out[0] != '\0';
}

static int title_row_cmp(const void *a, const void *b)
{
	return strcmp(((const db_title_row *)a)->file, ((const db_title_row *)b)->file);
}

bool db_titles_load(db *d, const char *folder, db_titles *out)
{
	sqlite3_stmt *st = NULL;
	int cap = 0;

	if (!out) return false;
	memset(out, 0, sizeof *out);
	if (!d || !folder) return false;
	/* An empty title is no title (db_game_title's false), so it is not kept */
	if (sqlite3_prepare_v2(d->h, "SELECT file, title FROM games WHERE folder=? AND title<>'';",
	                       -1, &st, NULL) != SQLITE_OK) return false;
	sqlite3_bind_text(st, 1, folder, -1, SQLITE_TRANSIENT);
	while (sqlite3_step(st) == SQLITE_ROW) {
		const char *file = (const char *)sqlite3_column_text(st, 0);
		const char *title = (const char *)sqlite3_column_text(st, 1);
		size_t lf, lt;
		char *both;

		if (!file || !title) continue;
		if (out->n == cap) {
			int bigger = cap ? cap * 2 : 256;
			db_title_row *rows = realloc(out->rows, (size_t)bigger * sizeof *rows);

			if (!rows) break;
			out->rows = rows;
			cap = bigger;
		}
		/* The file and its title in one block: one allocation a row */
		lf = strlen(file);
		lt = strlen(title);
		if (!(both = malloc(lf + lt + 2))) break;
		memcpy(both, file, lf + 1);
		memcpy(both + lf + 1, title, lt + 1);
		out->rows[out->n].file = both;
		out->rows[out->n].title = both + lf + 1;
		out->n++;
	}
	sqlite3_finalize(st);
	/* As strcmp orders them, which is how db_titles_find looks: SQLite's
	 * BINARY order is the same, but the search should not lean on it */
	if (out->n > 1) qsort(out->rows, (size_t)out->n, sizeof *out->rows, title_row_cmp);
	return true;
}

const char *db_titles_find(const db_titles *t, const char *file)
{
	int lo = 0, hi = t ? t->n - 1 : -1;

	while (lo <= hi) {
		int mid = lo + (hi - lo) / 2, c = strcmp(file, t->rows[mid].file);

		if (!c) return t->rows[mid].title;
		if (c < 0) hi = mid - 1;
		else lo = mid + 1;
	}
	return NULL;
}

void db_titles_free(db_titles *t)
{
	if (!t) return;
	for (int i = 0; i < t->n; i++) free((void *)t->rows[i].file);
	free(t->rows);
	memset(t, 0, sizeof *t);
}

bool db_game_set(db *d, const char *folder, const char *file, const game_meta *m)
{
	sqlite3_stmt *st = NULL;
	bool ok;
	char title[GAME_TITLE_MAX];

	if (!d || !folder || !file || !m) return false;
	/* INSERT OR REPLACE for the same reason db_set_str uses it: the device
	 * ships sqlite 3.12.2, which predates ON CONFLICT. */
	if (sqlite3_prepare_v2(d->h,
	               "INSERT OR REPLACE INTO games(folder,file,year,publisher,developer,"
	               "players,genres,esrb,note,synopsis,scraped,title)"
	               " VALUES(?,?,?,?,?,?,?,?,?,?,?,COALESCE(NULLIF(?,''),"
	               " (SELECT title FROM games WHERE folder=? AND file=?),''));", -1, &st, NULL) != SQLITE_OK)
		return false;
	sqlite3_bind_text(st,  1, folder,        -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  2, file,          -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  3, m->year,       -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  4, m->publisher,  -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  5, m->developer,  -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  6, m->players,    -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  7, m->genres,     -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  8, m->esrb,       -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st,  9, m->note,       -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 10, m->synopsis,   -1, SQLITE_TRANSIENT);
	/* When, so a re-scrape can skip what it already has and a stale row can be
	 * told from one that was never written. Not in game_meta: nothing on a
	 * screen asks for it. */
	sqlite3_bind_int(st, 11, (int)time(NULL));
	title_text(m->title, title, sizeof title);
	sqlite3_bind_text(st, 12, title, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 13, folder, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 14, file, -1, SQLITE_TRANSIENT);
	ok = sqlite3_step(st) == SQLITE_DONE;
	sqlite3_finalize(st);
	return ok;
}

bool db_game_del(db *d, const char *folder, const char *file)
{
	sqlite3_stmt *st = NULL;
	bool ok;

	if (!d || !folder || !file ||
	    sqlite3_prepare_v2(d->h, "DELETE FROM games WHERE folder=? AND file=?;", -1, &st,
	                       NULL) != SQLITE_OK)
		return false;
	sqlite3_bind_text(st, 1, folder, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 2, file, -1, SQLITE_TRANSIENT);
	ok = sqlite3_step(st) == SQLITE_DONE && sqlite3_changes(d->h) > 0;
	sqlite3_finalize(st);
	return ok;
}

void db_each_prefix(db *d, const char *prefix, db_each_fn fn, void *ctx)
{
	sqlite3_stmt *st = NULL;
	const unsigned char *k, *v;
	char hi[256];
	size_t n;

	if (!d || !prefix || !fn) return;
	/* A range rather than LIKE: no escaping question for keys that contain
	 * the wildcards, and it uses the primary key index. The upper bound is
	 * the prefix with its last byte incremented, which is the next key that
	 * cannot share it. */
	n = strlen(prefix);
	if (!n || n + 1 >= sizeof hi) return;
	memcpy(hi, prefix, n + 1);
	hi[n - 1]++;

	if (sqlite3_prepare_v2(d->h, "SELECT key,value FROM settings "
	                     "WHERE key >= ? AND key < ? ORDER BY key;",
	               -1, &st, NULL) != SQLITE_OK)
		return;
	sqlite3_bind_text(st, 1, prefix, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(st, 2, hi, -1, SQLITE_TRANSIENT);
	while (sqlite3_step(st) == SQLITE_ROW) {
		k = sqlite3_column_text(st, 0);
		v = sqlite3_column_text(st, 1);
		if (!fn(k ? (const char *)k : "", v ? (const char *)v : "", ctx)) break;
	}
	sqlite3_finalize(st);
}

/* Whether a key's VALUE is a credential, by the name it was given.
 *
 * A convention rather than a list, so a key added next year is covered by
 * having been named honestly rather than by somebody remembering this
 * function. Three exist today: ra.token, ss.password and sgdb.key. */
static bool secret_key(const char *k)
{
	size_t n = k ? strlen(k) : 0;
	const char *suffix[] = { ".token", ".password", ".key" };
	size_t i, m;

	for (i = 0; i < sizeof suffix / sizeof suffix[0]; i++) {
		m = strlen(suffix[i]);
		if (n >= m && strcmp(k + n - m, suffix[i]) == 0) return true;
	}
	return false;
}

void db_dump(db *d, FILE *out)
{
	sqlite3_stmt *st = NULL;
	const unsigned char *k, *v;

	if (!d || !out) return;
	if (sqlite3_prepare_v2(d->h, "SELECT key,value FROM settings ORDER BY key;",
	               -1, &st, NULL) != SQLITE_OK) {
		fprintf(out, "  (cannot read settings)\n");
		return;
	}
	while (sqlite3_step(st) == SQLITE_ROW) {
		k = sqlite3_column_text(st, 0);
		v = sqlite3_column_text(st, 1);
		/* THE SECRETS ARE COUNTED, NOT PRINTED. This tool exists because
		 * there is no sqlite3 on the device, so it is run over adb and its
		 * output lands in a terminal, a log, a paste or a screen share. It
		 * printed ra.token from the day the account arrived and would have
		 * printed ss.password from today - and unlike a token, that one is
		 * the credential itself and is reusable.
		 *
		 * A length is still worth having: it is what tells "nothing stored"
		 * apart from "stored, and something is wrong with it". */
		if (secret_key(k ? (const char *)k : "")) {
			fprintf(out, "  %-16s [hidden, %d characters]\n",
			        (const char *)k, v ? (int)strlen((const char *)v) : 0);
			continue;
		}
		fprintf(out, "  %-16s %s\n", k ? (const char *)k : "?",
		        v ? (const char *)v : "");
	}
	sqlite3_finalize(st);
}

/* --- the two open handles ------------------------------------------------ */
static db *g_dev, *g_lib;
static char g_boot_env[1024];

bool db_init(const char *device_path, const char *library_path,
             const char *boot_env_path)
{
	db_shutdown();
	g_dev = db_open(device_path, DB_DEVICE);
	g_lib = db_open(library_path, DB_LIBRARY);
	snprintf(g_boot_env, sizeof g_boot_env, "%s", boot_env_path ? boot_env_path : "");
	return g_dev && g_lib;
}

void db_shutdown(void)
{
	db_close(g_dev); g_dev = NULL;
	db_close(g_lib); g_lib = NULL;
	g_boot_env[0] = '\0';
}

db *db_dev(void) { return g_dev; }
db *db_lib(void) { return g_lib; }

/* --- boot.env, see db.h -------------------------------------------------- */
bool db_write_boot_env(void)
{
	static char last[512];
	char body[512];
	char tmp[1100];
	FILE *f;
	int n;

	if (!g_dev || !g_lib || !g_boot_env[0]) return false;

	/* Shell-sourced, so every value is quoted and nothing is computed here.
	 * The launcher already resolved "the player's choice, else the shipped
	 * default" by seeding, which is why launch.sh needs no fallback for
	 * either. */
	n = snprintf(body, sizeof body,
	             "# Written by ZyperOS from the settings database. Derived, not\n"
	             "# authoritative: delete it and the next settings change\n"
	             "# rewrites it. Sourced by launch.sh before zyperos.elf runs.\n"
	             "WIFI='%d'\n"
	             "BLUETOOTH='%d'\n",
	             db_get_int(g_dev, "wifi", 0) ? 1 : 0,
	             db_get_int(g_dev, "bluetooth", 0) ? 1 : 0);
	if (n < 0 || n >= (int)sizeof body) return false;

	/* NOTHING TO DO IF NOTHING CHANGED, and this is not an optimization for
	 * its own sake. Writing a file that has not changed put a replacement on
	 * paths the database was meant to take it off (the volume rocker used to
	 * call this): measured at 3.03 ms median and 9.27 ms at the tail, against a
	 * 16.7 ms frame. */
	/* Unless it is not there. The memo is about avoiding a rewrite, not about
	 * refusing to create the file: this thing's whole license to be a cache is
	 * that deleting it costs nothing, and skipping here would make a deleted
	 * boot.env stay deleted until some unrelated setting moved. */
	{
		struct stat sb;
		if (!strcmp(body, last) && stat(g_boot_env, &sb) == 0) return true;
	}

	/* Written to a temporary and renamed, but NOT fsynced. rename is what
	 * makes it atomic, so launch.sh can never source half a file; the fsync
	 * would only add durability, and this file is derived - losing it to a
	 * power cut costs one boot at the shipped defaults and the next settings
	 * change rewrites it. That is the whole reason it is allowed to be a
	 * cache. */
	snprintf(tmp, sizeof tmp, "%s.new", g_boot_env);
	if (!(f = fopen(tmp, "w"))) return false;
	if (fputs(body, f) == EOF) { fclose(f); unlink(tmp); return false; }
	if (fclose(f) != 0) { unlink(tmp); return false; }
	if (rename(tmp, g_boot_env) != 0) { unlink(tmp); return false; }

	snprintf(last, sizeof last, "%s", body);
	return true;
}
