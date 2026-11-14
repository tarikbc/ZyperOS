/* SPDX-License-Identifier: GPL-3.0-only */
/* See cheevos.h for the division of labor with Diatom, and src/rafetch.h for
 * the network half. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "atomic.h"
#include "cheevos.h"
#include "db.h"

static cheevo g_ach[CHV_MAX];
static int    g_n;
static int    g_game, g_console;
static char   g_title[CHV_TITLE];
static char   g_hash[CHV_HASH];    /* the ROM's, from the header */
static char   g_src[1024];         /* the file loaded, for chv_write_active */

/* The earned store, whole-library. Sized so a player who finishes a hundred
 * games never meets the cap; at 64 bytes a row that is 256KB, which on a
 * device with a gigabyte is not worth a smarter structure. */
#define CHV_EARNED_MAX 4096
typedef struct {
	int  game, id;
	bool synced;
	bool dirty;              /* changed since the last save */
	bool held;               /* has waited: see chv_mark_synced */
	bool hardcore;
	long when;               /* Unix time earned, 0 unknown */
	char hash[CHV_HASH];
} earned_row;
static earned_row g_earned[CHV_EARNED_MAX];
static int  g_nearned;
static bool g_earned_dirty;

void chv_path(const char *roms_root, const char *folder, const char *name,
              char *out, size_t n)
{
	snprintf(out, n, "%s/%s/.cheevos/%s.set", roms_root, folder, name);
}

void chv_none_path(const char *set_path, char *out, size_t n)
{
	size_t k = strlen(set_path);

	/* "<name>.set" to "<name>.none"; anything else gets ".none" on the end */
	if (k > 4 && !strcmp(set_path + k - 4, ".set"))
		snprintf(out, n, "%.*s.none", (int)(k - 4), set_path);
	else
		snprintf(out, n, "%s.none", set_path);
}

void chv_none_mark(const char *set_path, bool none)
{
	char p[1100];
	FILE *f;

	if (!set_path || !*set_path) return;
	chv_none_path(set_path, p, sizeof p);
	if (!none) { unlink(p); return; }
	if (access(p, F_OK) == 0) return;          /* one write, the first time */
	f = fopen(p, "w");
	if (f) fclose(f);
}

chv_state chv_state_of(const char *set_path, bool signed_in)
{
	char p[1100];

	if (!set_path || !*set_path) return CHV_NONE;
	if (access(set_path, R_OK) == 0) return CHV_SET;
	/* Nobody signed in: there is no fetch to wait for, and "not checked yet"
	 * would promise one. Such a card has sets only from tools/ra-sets.py. */
	if (!signed_in) return CHV_NONE;
	chv_none_path(set_path, p, sizeof p);
	return access(p, F_OK) == 0 ? CHV_NONE : CHV_UNCHECKED;
}

static bool is_earned(int game, int id)
{
	int i;
	for (i = 0; i < g_nearned; i++)
		if (g_earned[i].game == game && g_earned[i].id == id) return true;
	return false;
}

void chv_clear(void)
{
	memset(g_ach, 0, sizeof g_ach);
	g_n = 0;
	g_game = g_console = 0;
	g_title[0] = '\0';
	g_hash[0] = '\0';
	g_src[0] = '\0';
}

/* "#! zyperos-cheevos 2\tgame=1459\tconsole=7\ttitle=Blaster Master"
 * Fields are looked up by name rather than by position, so the generator can
 * add one without this having to be changed in step. The word after #! is not
 * read at all, so a set written under the name cards had before still loads. */
static void parse_header(char *line)
{
	char *save = NULL, *f;

	for (f = strtok_r(line, "\t", &save); f; f = strtok_r(NULL, "\t", &save)) {
		if      (!strncmp(f, "game=", 5))    g_game = atoi(f + 5);
		else if (!strncmp(f, "console=", 8)) g_console = atoi(f + 8);
		else if (!strncmp(f, "title=", 6))
			snprintf(g_title, sizeof g_title, "%s", f + 6);
		else if (!strncmp(f, "hash=", 5) && strlen(f + 5) == CHV_HASH - 1)
			snprintf(g_hash, sizeof g_hash, "%s", f + 5);
	}
}

/* "#:\t24698\t0\tBringing the Heat-Seeker\tUse a homing missile.\t85741"
 *
 * By position, and an empty field is a field: strsep, not strtok, which
 * runs two tabs together - so an achievement with no description had the
 * badge read as its description. The badge is the last, and a set written
 * before it (format 1) simply has none. */
static void parse_meta(char *line)
{
	char *rest = line, *f;
	cheevo *c;

	if (g_n >= CHV_MAX) return;
	c = &g_ach[g_n];
	memset(c, 0, sizeof *c);

	f = strsep(&rest, "\t");                      /* "#:" */
	if (!f || !(f = strsep(&rest, "\t"))) return;
	c->id = atoi(f);
	if (c->id <= 0) return;
	if ((f = strsep(&rest, "\t"))) c->points = atoi(f);
	if ((f = strsep(&rest, "\t")))
		snprintf(c->title, sizeof c->title, "%s", f);
	if ((f = strsep(&rest, "\t")))
		snprintf(c->desc, sizeof c->desc, "%s", f);
	if ((f = strsep(&rest, "\t")))
		snprintf(c->badge, sizeof c->badge, "%s", f);

	c->earned = is_earned(g_game, c->id);
	g_n++;
}

bool chv_load(const char *set_path)
{
	char *line = NULL;
	size_t cap = 0;
	FILE *f;

	chv_clear();
	if (!set_path || !*set_path) return false;

	f = fopen(set_path, "r");
	if (!f) return false;
	snprintf(g_src, sizeof g_src, "%s", set_path);

	/* getline, not a fixed buffer: a condition line has been measured at
	 * 30,897 characters. This only reads the comment lines, but it has to get
	 * PAST the long ones intact, and a short buffer would split one and feed
	 * its tail back as another record. */
	while (getline(&line, &cap, f) > 0) {
		line[strcspn(line, "\r\n")] = '\0';
		if (!strncmp(line, "#!", 2))      parse_header(line);
		else if (!strncmp(line, "#:", 2)) parse_meta(line);
	}
	free(line);
	fclose(f);

	if (g_n == 0) { chv_clear(); return false; }
	return true;
}

int chv_count(void) { return g_n; }
const char *chv_source(void) { return g_src; }

bool chv_note_progress(int id, const char *text)
{
	int i;

	for (i = 0; i < g_n; i++)
		if (g_ach[i].id == id) {
			snprintf(g_ach[i].progress, sizeof g_ach[i].progress, "%s", text ? text : "");
			return true;
		}
	return false;
}
int chv_game(void) { return g_game; }
int chv_console(void) { return g_console; }
const char *chv_game_title(void) { return g_title; }
const char *chv_game_hash(void) { return g_hash; }

const cheevo *chv_at(int i)
{
	return (i >= 0 && i < g_n) ? &g_ach[i] : NULL;
}

int chv_earned(void)
{
	int i, k = 0;
	for (i = 0; i < g_n; i++) if (g_ach[i].earned || g_ach[i].earned_now) k++;
	return k;
}

int chv_new_this_session(void)
{
	int i, k = 0;
	for (i = 0; i < g_n; i++) if (g_ach[i].earned_now) k++;
	return k;
}

int chv_points_earned(void)
{
	int i, p = 0;
	for (i = 0; i < g_n; i++)
		if (g_ach[i].earned || g_ach[i].earned_now) p += g_ach[i].points;
	return p;
}

int chv_points_total(void)
{
	int i, p = 0;
	for (i = 0; i < g_n; i++) p += g_ach[i].points;
	return p;
}

bool chv_write_active(const char *path)
{
	char  *line = NULL;
	size_t cap = 0;
	FILE  *in, *out;
	int    kept = 0;

	if (!g_src[0] || !path || !*path) return false;
	in = fopen(g_src, "r");
	if (!in) return false;
	/* Atomically too, though this one is scratch: Diatom is handed the path
	 * moments after and a torn file would be a half a set it takes at face
	 * value. Cheap, and one rule rather than two. */
	out = atomic_open(path, 0644);
	if (!out) { fclose(in); return false; }

	while (getline(&line, &cap, in) > 0) {
		int id;
		char *tab;
		int i;

		if (line[0] == '#') continue;             /* Diatom skips these anyway */
		tab = strchr(line, '\t');
		if (!tab) continue;
		id = atoi(line);
		if (id <= 0) continue;

		/* Already earned: leave it out. Sending it would have Diatom fire it
		 * again the moment its conditions happen to be true, and a player who
		 * beat a boss last week would be told they just did. */
		for (i = 0; i < g_n; i++)
			if (g_ach[i].id == id &&
			    (g_ach[i].earned || g_ach[i].earned_now)) break;
		if (i < g_n) continue;

		fputs(line, out);
		kept++;
	}
	free(line);
	fclose(in);

	/* Nothing left to watch. Discard rather than commit, so the previous
	 * game's active set is not left standing under this game's name. */
	if (kept == 0) { atomic_abort(out, path); remove(path); return false; }
	return atomic_commit(out, path);
}

/* A new row, or the existing one's index; -1 when there is no room */
static int note_row(int game, int id, bool synced, bool *added)
{
	int i;

	*added = false;
	for (i = 0; i < g_nearned; i++) {
		if (g_earned[i].game != game || g_earned[i].id != id) continue;
		/* Already known. Learning it is on the account is still news to the
		 * store, even though the achievement is not. */
		if (synced && !g_earned[i].synced) {
			g_earned[i].synced = true;
			g_earned[i].dirty = true;
			g_earned_dirty = true;
		}
		return i;
	}
	if (g_nearned >= CHV_EARNED_MAX) return -1;
	memset(&g_earned[g_nearned], 0, sizeof g_earned[g_nearned]);
	g_earned[g_nearned].game   = game;
	g_earned[g_nearned].id     = id;
	g_earned[g_nearned].synced = synced;
	g_earned[g_nearned].dirty  = true;
	g_earned_dirty = true;
	*added = true;
	return g_nearned++;
}

bool chv_note_earned(int game, int id, bool synced)
{
	bool added;
	int i;

	if (game <= 0 || id <= 0) return false;
	if (note_row(game, id, synced, &added) < 0 || !added) return false;

	/* If it belongs to the game that is loaded, the list on screen should say
	 * so without waiting for a reload. */
	if (game == g_game)
		for (i = 0; i < g_n; i++)
			if (g_ach[i].id == id) { g_ach[i].earned = true; break; }
	return true;
}

int chv_pending_count(void)
{
	int i, k = 0;
	for (i = 0; i < g_nearned; i++) if (!g_earned[i].synced) k++;
	return k;
}

int chv_pending_for(int game)
{
	int i, k = 0;
	for (i = 0; i < g_nearned; i++)
		if (!g_earned[i].synced && g_earned[i].game == game) k++;
	return k;
}

bool chv_is_pending(int id)
{
	int i;
	for (i = 0; i < g_nearned; i++)
		if (g_earned[i].game == g_game && g_earned[i].id == id)
			return !g_earned[i].synced;
	return false;
}

bool chv_pending_get(int n, chv_owed *out)
{
	int i;

	for (i = 0; i < g_nearned; i++) {
		if (g_earned[i].synced) continue;
		if (n-- > 0) continue;
		memset(out, 0, sizeof *out);
		out->game     = g_earned[i].game;
		out->id       = g_earned[i].id;
		out->when     = g_earned[i].when;
		out->hardcore = g_earned[i].hardcore;
		snprintf(out->hash, sizeof out->hash, "%s", g_earned[i].hash);
		return true;
	}
	return false;
}

bool chv_pending_at(int n, int *game, int *id)
{
	chv_owed o;

	if (!chv_pending_get(n, &o)) return false;
	*game = o.game;
	*id   = o.id;
	return true;
}

bool chv_mark_synced(int game, int id)
{
	bool held = false;
	int i;

	for (i = 0; i < g_nearned; i++)
		if (g_earned[i].game == game && g_earned[i].id == id &&
		    !g_earned[i].synced) {
			g_earned[i].synced = true;
			g_earned[i].dirty = true;
			g_earned_dirty = true;
			held = held || g_earned[i].held;
		}
	return held;
}

void chv_hold_pending(void)
{
	int i;
	for (i = 0; i < g_nearned; i++)
		if (!g_earned[i].synced) g_earned[i].held = true;
}

/* The row, owed, with what the sending needs to know about it */
static bool note_owed(int id, long when, bool hardcore, bool offline)
{
	bool added;
	int r;

	if (g_game <= 0 || id <= 0) return false;
	r = note_row(g_game, id, false, &added);
	if (r < 0 || !added) return false;
	g_earned[r].when     = when;
	g_earned[r].hardcore = hardcore;
	g_earned[r].held     = offline;
	snprintf(g_earned[r].hash, sizeof g_earned[r].hash, "%s", g_hash);
	return true;
}

bool chv_note_unlock(int id)
{
	return chv_note_unlock_at(id, (long)time(NULL), false, false);
}

bool chv_note_unlock_at(int id, long when, bool hardcore, bool offline)
{
	int i;

	for (i = 0; i < g_n; i++) {
		if (g_ach[i].id != id) continue;
		if (g_ach[i].earned || g_ach[i].earned_now) return false;
		g_ach[i].earned_now = true;
		/* Pending: the send happens where the launcher owns the screen, not
		 * here. This runs inside the wait loop, which is also the power
		 * button's watchdog, and a blocking request would make the device
		 * stop answering it. */
		note_owed(id, when, hardcore, offline);
		return true;
	}
	/* An id we do not have metadata for. It still happened, so it is still
	 * recorded - a set refreshed mid-session is the obvious way to get here,
	 * and losing the unlock would be worse than not being able to name it. */
	return note_owed(id, when, hardcore, offline);
}

/* "p\t1762905600\t0\t<32 hex>" into the row; a bare "p", or fields that
 * are not what they should be, leave them unknown. */
static void parse_owed(earned_row *r, const char *value)
{
	char buf[96], *rest = buf, *f;

	snprintf(buf, sizeof buf, "%s", value);
	if (!strsep(&rest, "\t")) return;            /* the "p" */
	if ((f = strsep(&rest, "\t"))) r->when = strtol(f, NULL, 10);
	if (r->when < 0) r->when = 0;
	if ((f = strsep(&rest, "\t"))) r->hardcore = f[0] == '1';
	if ((f = strsep(&rest, "\t")) && strlen(f) == CHV_HASH - 1)
		snprintf(r->hash, sizeof r->hash, "%s", f);
}

static bool chv_earned_row(const char *key, const char *value, void *ctx)
{
	const char *rest = key + strlen("chv.");
	earned_row *r;
	int game, id;

	(void)ctx;
	if (g_nearned >= CHV_EARNED_MAX) return false;
	/* Both halves or the row is not one. chv_earned_save writes both. */
	if (sscanf(rest, "%d.%d", &game, &id) != 2) return true;
	if (game <= 0 || id <= 0) return true;
	r = &g_earned[g_nearned];
	memset(r, 0, sizeof *r);
	r->game   = game;
	r->id     = id;
	r->synced = value[0] == 's';
	/* Owed since before this launcher started: it has waited */
	if (!r->synced) { r->held = true; parse_owed(r, value); }
	g_nearned++;
	return true;
}

void chv_earned_load(void)
{
	g_nearned = 0;
	g_earned_dirty = false;
	db_each_prefix(db_lib(), "chv.", chv_earned_row, NULL);
}

bool chv_earned_save(void)
{
	char key[48], val[96];
	int i;

	if (!g_earned_dirty) return true;
	/* The rows that changed, not every row. Nothing here is ever removed -
	 * the load is the only thing that resets the count - so there is no stale
	 * row to leave behind and no deletion pass to get wrong.
	 *
	 * It was every row every time, each its own commit: with a library's
	 * worth earned, thousands of writes to the card for one unlock, mid-game.
	 *
	 * This is the only record of anything earned that the account has not
	 * seen, and it is written on every unlock - including the ones that
	 * happen just before somebody presses the power button. A row that does
	 * not write stays dirty and is written by the next save. */
	for (i = 0; i < g_nearned; i++) {
		const earned_row *r = &g_earned[i];

		if (!r->dirty) continue;
		snprintf(key, sizeof key, "chv.%d.%d", r->game, r->id);
		if (r->synced)
			snprintf(val, sizeof val, "s");
		else
			snprintf(val, sizeof val, "p\t%ld\t%d\t%s", r->when,
			         r->hardcore ? 1 : 0, r->hash);
		if (!db_set_str(db_lib(), key, val)) return false;
		g_earned[i].dirty = false;
	}
	g_earned_dirty = false;
	return true;
}
