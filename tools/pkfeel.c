/* SPDX-License-Identifier: GPL-3.0-only */
/* A screenshot's feel over your PICO-8: see pkfeel.h. */
#include "pkfeel.h"

#include <stdio.h>
#include <string.h>

#include "audioout.h"
#include "pkkeys.h"
#include "uisound.h"

/* A word the launcher keeps in its device database (src/db.c's settings),
 * or `fallback` for none */
static void setting(sqlite3 *db, const char *key, char *out, size_t n, const char *fallback)
{
	sqlite3_stmt *st;

	snprintf(out, n, "%s", fallback);
	if (!db || sqlite3_prepare_v2(db, "SELECT value FROM settings WHERE key=?;", -1, &st, NULL) != SQLITE_OK)
		return;
	sqlite3_bind_text(st, 1, key, -1, SQLITE_STATIC);
	if (sqlite3_step(st) == SQLITE_ROW && sqlite3_column_text(st, 0))
		snprintf(out, n, "%s", (const char *)sqlite3_column_text(st, 0));
	sqlite3_finalize(st);
}

void pk_shot_feel(sqlite3 *db, bool dac, bool wired, pk_feel *f)
{
	char v[32];
	int s;

	memset(f, 0, sizeof *f);
	f->buzz_pct = pk_rumble_pct(db);
	/* The launcher's own default when the row was never set (src/main.c) */
	setting(db, "uisounds", v, sizeof v, uisound_setting_id(DS_VOICE_DEFAULT));
	s = uisound_setting_parse(v);
	f->voice = uisound_shot(s).level > 0.0f ? s : -1;
	f->device = dac ? AOUT_USB_DEVICE : "default";
	/* Audio Channels as src/aout_route.c's channels_load reads it */
	setting(db, "channels", v, sizeof v, "auto");
	uisound_sides(aout_channels_parse(v), dac ? AOUT_USB : wired ? AOUT_WIRED : AOUT_SPK,
	              &f->left, &f->right);
}
