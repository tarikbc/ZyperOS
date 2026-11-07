/* SPDX-License-Identifier: GPL-3.0-only */
/* The volume keys over a port, as decisions and steps: see pkkeys.h. */
#include "pkkeys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "device/pixel2_levels.h"
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

void pkkeys_menu_state(pkkeys *k, bool down)
{
	k->menu_held = down;
}

void pkkeys_vol_state(pkkeys *k, bool up_down, bool down_down)
{
	if (!up_down) k->key[0].down = false;
	if (!down_down) k->key[1].down = false;
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
