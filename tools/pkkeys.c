/* SPDX-License-Identifier: GPL-3.0-only */
/* The volume keys over a port, as decisions and steps: see pkkeys.h. */
#include "pkkeys.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "device/pixel2_levels.h"
#include "eqdsp.h"
#include "keyrepeat.h"

static int clampi(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

void pkkeys_init(pkkeys *k)
{
	memset(k, 0, sizeof *k);
}

/* One event, in its turn. A volume key keeps what it went down as until it
 * comes up, as src/device/pixel2.c's vol_as does: let go of MENU first and the
 * hold stays on the backlight, press MENU under a held key and it stays the
 * volume. A press of a key already down is no press, as plat_set_btn has it. */
static int handle(pkkeys *k, const pk_key *e, int64_t now_ms, pk_step *out, int max)
{
	int i;

	/* The kernel's autorepeat, of any key. A held volume key repeats on the
	 * launcher's clock (pkkeys_tick), where gpio-keys' own is 250 ms and
	 * then every 33, and stepped and saved every one of them. */
	if (e->value == 2) return 0;
	if (e->code == PK_BTN_MENU) {
		/* A press of MENU already down is no press (a resync found it) */
		if (e->value && !k->menu_held) k->menu_armed = true;
		if (!e->value && k->menu_held && k->menu_armed) k->menu_tapped = true;
		if (!e->value) k->menu_armed = false;
		k->menu_held = e->value != 0;
		return 0;
	}
	if (e->code == PK_KEY_VOLUMEUP) i = 0;
	else if (e->code == PK_KEY_VOLUMEDOWN) i = 1;
	else return 0;
	if (!e->value) {
		k->key[i].down = false;
		return 0;
	}
	if (k->key[i].down || max < 1) return 0;
	k->key[i].down = true;
	k->key[i].bright = k->menu_held;
	/* A step under MENU makes it a modifier: its release opens nothing */
	if (k->menu_held) k->menu_armed = false;
	k->key[i].next_ms = now_ms + REPEAT_DELAY_MS;
	out[0] = (pk_step){ k->menu_held ? PK_BRIGHT : PK_VOLUME, i ? -1 : +1, false };
	return 1;
}

bool pkkeys_relevant(uint16_t code, int32_t value)
{
	return (value == 0 || value == 1) &&
	       (code == PK_BTN_MENU || code == PK_KEY_VOLUMEUP || code == PK_KEY_VOLUMEDOWN);
}

int pkkeys_pass(pkkeys *k, const pk_key *ev, int n, int64_t cutoff_us, int64_t now_ms,
                pk_step *out, int max)
{
	struct event { pk_key key; bool defer; } all[PK_WAITING + PK_PASS_MAX];
	int na = 0, ready, i, j, m = 0;

	/* Never apply a prefix of an oversized batch. The caller must resync
	 * every input after -1; a release or MENU may be in the lost suffix. */
	if (n > PK_PASS_MAX) goto overflow;
	for (i = 0; i < k->nwaiting; i++)
		all[na++] = (struct event){ k->waiting[i], false };
	for (i = 0; i < n; i++) {
		if (!pkkeys_relevant(ev[i].code, ev[i].value)) continue;
		all[na++] = (struct event){ ev[i], ev[i].t_us > cutoff_us };
	}

	/* Stable time order includes pending AND newly deferred events. A
	 * newly deferred event also holds back any later pending event, or a
	 * clock adjustment could put a release before its earlier press. */
	for (i = 1; i < na; i++) {
		struct event e = all[i];

		for (j = i; j > 0 && all[j - 1].key.t_us > e.key.t_us; j--) all[j] = all[j - 1];
		all[j] = e;
	}
	for (ready = 0; ready < na && !all[ready].defer; ready++) {}
	if (na - ready > PK_WAITING) goto overflow;
	k->nwaiting = na - ready;
	for (i = ready; i < na; i++) k->waiting[i - ready] = all[i].key;
	for (i = 0; i < ready; i++) m += handle(k, &all[i].key, now_ms, out + m, max - m);
	return m;

overflow:
	pkkeys_init(k);
	return -1;
}

int pkkeys_tick(pkkeys *k, int64_t now_ms, pk_step *out, int max)
{
	int i, m = 0;

	/* A queued release must settle before any repeat, even if already due. */
	if (k->nwaiting) return 0;

	/* As in_repeat (src/platform.c): due REPEAT_DELAY_MS after the press,
	 * then REPEAT_RATE_MS after the repeat before, counted from when each
	 * was given rather than when it was due, as the launcher counts */
	for (i = 0; i < 2 && m < max; i++) {
		if (!k->key[i].down || now_ms < k->key[i].next_ms) continue;
		k->key[i].next_ms = now_ms + REPEAT_RATE_MS;
		out[m++] = (pk_step){ k->key[i].bright ? PK_BRIGHT : PK_VOLUME, i ? -1 : +1, true };
	}
	return m;
}

int pkkeys_wait(const pkkeys *k, int64_t now_ms)
{
	int64_t w = -1;
	int i;

	if (k->nwaiting) return 0;
	for (i = 0; i < 2; i++) {
		int64_t d;

		if (!k->key[i].down) continue;
		d = k->key[i].next_ms - now_ms;
		if (d < 0) d = 0;
		if (w < 0 || d < w) w = d;
	}
	return (int)w;
}

bool pkkeys_menu_tapped(pkkeys *k)
{
	bool t = k->menu_tapped;

	k->menu_tapped = false;
	return t;
}

void pkkeys_menu_state(pkkeys *k, bool down)
{
	/* What was down before the events were lost was not pressed here */
	k->menu_armed = false;
	k->menu_held = down;
}

void pkkeys_vol_state(pkkeys *k, bool up_down, bool down_down)
{
	if (!up_down) k->key[0].down = false;
	if (!down_down) k->key[1].down = false;
}

bool pkkeys_up_held(const pkkeys *k, pk_kind kind)
{
	return k->key[0].down && k->key[0].bright == (kind == PK_BRIGHT);
}

/* ---- the levels ---------------------------------------------------------------- */

/* src/db.c's "settings" table, text values, written the way db_set_int
 * writes them. The launcher is not running, so nothing else has the file
 * open. */
int pk_db_get(sqlite3 *db, const char *key, int fallback)
{
	sqlite3_stmt *st;
	int v = fallback;

	if (db && sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key=?;", -1, &st, NULL) == SQLITE_OK) {
		sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
		if (sqlite3_step(st) == SQLITE_ROW) v = sqlite3_column_int(st, 0);
		sqlite3_finalize(st);
	}
	return v;
}

void pk_db_set(sqlite3 *db, const char *key, int v)
{
	sqlite3_stmt *st;
	char text[16];

	if (!db || v < 0) return;
	snprintf(text, sizeof text, "%d", v);
	if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO settings(key,value) VALUES(?,?);",
	                       -1, &st, NULL) != SQLITE_OK) return;
	sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
	sqlite3_bind_text(st, 2, text, -1, SQLITE_STATIC);
	sqlite3_step(st);
	sqlite3_finalize(st);
}

void pk_levels_load(sqlite3 *db, const char *backlight, pk_levels *l)
{
	FILE *f;
	int raw, i;

	l->vol = pk_db_get(db, "volume", -1);
	if (l->vol >= 0) l->vol = clampi(l->vol, 0, VOL_MAX);
	l->bright = pk_db_get(db, "brightness", -1);
	if (l->bright < 0 && (f = fopen(backlight, "r"))) {
		if (fscanf(f, "%d", &raw) == 1)
			for (l->bright = 0, i = 1; i <= BRIGHT_MAX; i++)
				if (abs(bright_ladder[i] - raw) < abs(bright_ladder[l->bright] - raw)) l->bright = i;
		fclose(f);
	}
	l->bright = l->bright < 0 ? BRIGHT_MAX / 2 : clampi(l->bright, 0, BRIGHT_MAX);
}

bool pk_apply(sqlite3 *db, pk_levels *l, const pk_step *s, char *line, size_t n)
{
	bool bright = s->kind == PK_BRIGHT;
	int *level = bright ? &l->bright : &l->vol;
	int top = bright ? BRIGHT_MAX : VOL_MAX;
	int from = *level < 0 ? VOL_MAX / 2 : *level, to = clampi(from + s->dir, 0, top);
	bool moved = to != *level;

	*level = to;
	if (moved) pk_db_set(db, bright ? "brightness" : "volume", to);
	/* Every press, so the log has each one, the ends of the scale too; a
	 * repeat only when it moved, or a key held at the top would be a line
	 * every REPEAT_RATE_MS */
	if (n) {
		if (moved || !s->held)
			snprintf(line, n, "portkeys: %s %d -> %d (%sVOL%c%s)\n", bright ? "brightness" : "volume",
			         from, to, bright ? "MENU+" : "", s->dir > 0 ? '+' : '-', s->held ? " held" : "");
		else
			line[0] = '\0';
	}
	return moved;
}

/* ---- the boost past the top ----------------------------------------------------- */

static const char *const KEEP[2] = { PK_VOL_BOOST, PK_BRIGHT_BOOST };
static const int CELLS[2] = { BOOST_VOL_CELLS, BOOST_BRIGHT_CELLS };
static const int TOP[2] = { VOL_MAX, BRIGHT_MAX };

void pk_boost_init(pk_boost *b, const char *share_path)
{
	const char *e = getenv("ZYPEROS_EQ_FILE");
	int k;

	memset(b, 0, sizeof *b);
	b->share_path = share_path ? share_path : e && *e ? e : EQ_SHARE_PATH;
	for (k = 0; k < 2; k++) boost_init(&b->bar[k], TOP[k], TOP[k], CELLS[k], 0);
}

/* The shared file, as src/eq.c's share_map makes it: its folder at need
 * (/run/zyperos is the launcher's, and is there unless a port was started
 * without it), the file at its size, and a new one, or one a writer left half
 * done, whole and flat. The launcher's file, as it left it, is not touched. */
static void *share_map(const char *path)
{
	char dir[256], *slash;
	eq_share *map;
	void *m;
	int fd;

	if (!path || !*path) return NULL;
	snprintf(dir, sizeof dir, "%s", path);
	if ((slash = strrchr(dir, '/')) && slash != dir) {
		*slash = '\0';
		if (mkdir(dir, 0755) < 0 && errno != EEXIST) return NULL;
	}
	if ((fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0644)) < 0) return NULL;
	if (ftruncate(fd, (off_t)sizeof *map) < 0) {
		close(fd);
		return NULL;
	}
	m = mmap(NULL, sizeof *map, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (m == MAP_FAILED) return NULL;
	map = m;
	if (map->magic != EQ_SHARE_MAGIC || (map->seq & 1)) {
		map->seq = 0;
		memset(map->db, 0, sizeof map->db);
		map->gain = 0;
		map->cut = 0;
		map->pad = 0;
		map->magic = EQ_SHARE_MAGIC;
	}
	return map;
}

bool pk_boost_share(pk_boost *b)
{
	volatile eq_share *s;
	const int db = boost_db(&b->bar[PK_VOLUME]);

	if (!b->share) b->share = share_map(b->share_path);
	if (!(s = b->share)) return false;
	/* In place with the seqlock, the bands left as the launcher set them */
	if (s->gain != db) eq_share_put(s, NULL, db);
	return true;
}

void pk_boost_free(pk_boost *b)
{
	if (b->share) munmap(b->share, sizeof(eq_share));
	b->share = NULL;
}

bool pk_boost_load(sqlite3 *db, const pk_levels *l, pk_boost *b)
{
	const int level[2] = { l->vol < 0 ? VOL_MAX / 2 : l->vol, l->bright };
	int k;

	for (k = 0; k < 2; k++) {
		boost_init(&b->bar[k], level[k], TOP[k], CELLS[k], pk_db_get(db, KEEP[k], 0));
		b->up_was[k] = b->up_seen[k] = false;
	}
	return pk_boost_share(b);
}

int pk_boost_cells(const pk_boost *b, pk_kind kind)
{
	const boost_bar *bar = &b->bar[kind == PK_BRIGHT];

	return bar->open ? bar->cells : 0;
}

int pk_boost_gain(const pk_boost *b) { return boost_db(&b->bar[PK_VOLUME]); }
int pk_boost_lift(const pk_boost *b) { return boost_lift(&b->bar[PK_BRIGHT]); }

/* The cells moved: kept as the launcher keeps them (src/levels.c's keep),
 * and the volume's gain in the file. The lift is the display's, portkeys'. */
static void boost_moved(sqlite3 *db, pk_boost *b, pk_kind kind)
{
	pk_db_set(db, KEEP[kind == PK_BRIGHT], pk_boost_cells(b, kind));
	if (kind == PK_VOLUME) pk_boost_share(b);
}

int pk_press(sqlite3 *db, pk_levels *l, pk_boost *b, const pk_step *s, uint32_t now,
             char *line, size_t n)
{
	const bool bright = s->kind == PK_BRIGHT;
	boost_bar *bar = &b->bar[bright];
	const int *level = bright ? &l->bright : &l->vol;
	const int was = pk_boost_cells(b, s->kind);
	int did = 0;

	/* The level as portkeys has it, should anything else have moved it; a
	 * level under the top closes the boost. A volume never saved is the
	 * middle (pk_boost_load) until its first step. */
	if (*level >= 0 && *level != bar->level) {
		const bool open = bar->open;

		boost_set_level(bar, *level);
		if (open && !bar->open) did |= BOOST_GAIN;
	}
	if (s->dir > 0) b->up_seen[bright] = true;
	{
		const int key = boost_key(bar, s->dir, s->held, BOOST_OPEN, now);

		if ((key & BOOST_LEVEL) || !(key & BOOST_GAIN)) {
			/* A step, or a press at an end that moved nothing, as it always
			 * was: pk_apply moves the level just as boost_key did, clamped
			 * at the ends, from the middle for a volume never saved */
			pk_apply(db, l, s, line, n);
		} else if (n) {
			const int to = pk_boost_cells(b, s->kind);
			const char d = s->dir > 0 ? '+' : '-';
			const char *h = s->held ? " held" : "";

			if (bright)
				snprintf(line, n, "portkeys: brightness boost %d -> %d (MENU+VOL%c%s)\n", was, to, d, h);
			else
				snprintf(line, n, "portkeys: volume boost +%d -> +%d dB (VOL%c%s)\n",
				         was * BOOST_DB_CELL, to * BOOST_DB_CELL, d, h);
		}
		did |= key;
	}
	if (did & BOOST_GAIN) boost_moved(db, b, s->kind);
	return did;
}

int pk_boost_tick(sqlite3 *db, pk_boost *b, const pkkeys *k, pk_kind kind, uint32_t now)
{
	const int i = kind == PK_BRIGHT;
	const bool up = pkkeys_up_held(k, kind);
	int did;

	/* The let-go, which clears the stop a held key's opening left (a repeat
	 * then goes on again). A press and its release in one pass are both seen
	 * here only as the step that came, so up_seen stands for the press. */
	if ((b->up_was[i] || b->up_seen[i]) && !up) boost_release(&b->bar[i], now);
	b->up_was[i] = up;
	b->up_seen[i] = false;
	did = boost_tick(&b->bar[i], up, BOOST_OPEN, now);
	if (did & BOOST_GAIN) boost_moved(db, b, kind);
	return did;
}

bool pk_boost_moving(const pk_boost *b, uint32_t now)
{
	int i;

	for (i = 0; i < 2; i++) {
		const boost_bar *bar = &b->bar[i];

		if (boost_moving(bar, now)) return true;
		if (b->up_was[i] && !bar->open && bar->ncells > 0 && bar->level == bar->max) return true;
	}
	return false;
}

int pk_boost_gone(sqlite3 *db, pk_boost *b)
{
	int i, closed = 0;

	for (i = 0; i < 2; i++) {
		const bool was = b->bar[i].open;

		boost_bar_gone(&b->bar[i]);
		if (was && !b->bar[i].open) {
			boost_moved(db, b, i ? PK_BRIGHT : PK_VOLUME);
			closed |= 1 << i;
		}
	}
	return closed;
}

bool pk_boost_output_changed(sqlite3 *db, pk_boost *b)
{
	if (!b->bar[PK_VOLUME].open) return false;
	boost_shut(&b->bar[PK_VOLUME]);
	boost_moved(db, b, PK_VOLUME);
	return true;
}

void pk_boost_look(pk_boost *b, pk_kind kind, uint32_t now, osdbar_look *l)
{
	const bool bright = kind == PK_BRIGHT;

	boost_look(&b->bar[bright], bright ? OSDBAR_BRIGHT : OSDBAR_VOLUME, BOOST_OPEN, now, l);
}

/* ---- the boost's buzz as it opens ------------------------------------------------ */

/* src/rumble.c's words and shares: the launcher's IDS and PCT, in its order.
 * tools/pkkeys-check.c holds the two the same through the launcher's own
 * rumble_level_parse and rumble_level_pct. */
static const char *const RUMBLE_WORD[] = { "off", "low", "medium", "full" };
static const int RUMBLE_PCT[] = { 0, 35, 65, 100 };

int pk_rumble_pct(sqlite3 *db)
{
	sqlite3_stmt *st;
	int pct = 100;
	unsigned i;

	if (db && sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key='rumble';", -1, &st, NULL) ==
	          SQLITE_OK) {
		if (sqlite3_step(st) == SQLITE_ROW) {
			const char *v = (const char *)sqlite3_column_text(st, 0);

			for (i = 0; v && i < sizeof RUMBLE_WORD / sizeof RUMBLE_WORD[0]; i++)
				if (!strcmp(v, RUMBLE_WORD[i])) pct = RUMBLE_PCT[i];
		}
		sqlite3_finalize(st);
	}
	return pct;
}

int pk_buzz_pct(sqlite3 *db, int did)
{
	return (did & BOOST_OPENED) ? pk_rumble_pct(db) : 0;
}

uint16_t pk_buzz_strength(int pct)
{
	if (pct <= 0) return 0;
	if (pct >= 100) return 0xFFFF;
	return (uint16_t)((float)0xFFFF * ((float)pct / 100.0f) + 0.5f);
}

/* ---- whose display may be borrowed --------------------------------------------- */

void pk_looks_init(pk_looks *l)
{
	memset(l, 0, sizeof *l);
	l->settles_at = -1;
}

void pk_looks_begin(pk_looks *l, long now)
{
	int i;

	(void)now;
	for (i = 0; i < l->n; i++) l->e[i].seen = false;
	l->waiting = 0;
	l->settles_at = -1;
}

bool pk_looks_seen(pk_looks *l, int pid, int fd, long now)
{
	pk_look *e = NULL;
	bool ok;
	int i;

	for (i = 0; i < l->n && !e; i++)
		if (l->e[i].pid == pid && l->e[i].fd == fd) e = &l->e[i];
	if (!e && l->n < PK_LOOKS_MAX) {
		e = &l->e[l->n++];
		e->pid = pid;
		e->fd = fd;
		e->first = e->last = now;
	}
	if (!e) return pid == l->trusted && pid > 0;
	/* A gap in the looks, and what was open then may have closed and opened
	 * again: from now on */
	if (now - e->last > PK_LOOK_GAP_MS) e->first = now;
	e->last = now;
	e->seen = true;
	ok = (pid == l->trusted && pid > 0) || now - e->first >= PK_SETTLE_MS;
	if (!ok) {
		l->waiting++;
		if (l->settles_at < 0 || e->first + PK_SETTLE_MS < l->settles_at) l->settles_at = e->first + PK_SETTLE_MS;
	}
	return ok;
}

void pk_looks_end(pk_looks *l, bool whole)
{
	int i, k = 0;

	if (!whole) return;
	for (i = 0; i < l->n; i++)
		if (l->e[i].seen) l->e[k++] = l->e[i];
	l->n = k;
}

void pk_looks_trust(pk_looks *l, int pid)
{
	l->trusted = pid;
}

long pk_looks_wait(const pk_looks *l, long now)
{
	if (!l->waiting || l->settles_at < 0) return -1;
	return l->settles_at > now ? l->settles_at - now : 0;
}
