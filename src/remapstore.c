/* SPDX-License-Identifier: GPL-3.0-only */
#include "remapstore.h"

#include <stdio.h>
#include <string.h>

#include "db.h"
#include "library.h"

/* platform.c's, declared here so this file links without SDL */
const char *plat_turbo_map(const char *tag);

/* As long as a game's whole key, so a long name is never cut: Rename and
 * Delete Game build the same key whole (src/gamern.c, src/gamedel.c) */
#define KEY_MAX (CFG_STR + LIB_PATH + 8)

static void key_of(const system_cfg *s, const char *file, char *out, size_t n)
{
	if (file) snprintf(out, n, "map.%s.%s", s->tag, file);
	else      snprintf(out, n, "map.%s", s->tag);
}

void rms_default(const system_cfg *s, rm_table *out)
{
	const char *tm = plat_turbo_map(s->tag);
	char spec[sizeof s->map + 100];

	snprintf(spec, sizeof spec, "%s%s%s", s->map,
	         s->map[0] && tm && *tm ? "," : "", tm ? tm : "");
	rm_identity(out);
	/* A bad line in systems.cfg leaves the identity, as no map at all */
	if (!rm_parse(spec, out)) rm_identity(out);
}

/* A stored value this release cannot read (a bad pair, or a newer version)
 * is said once per key: the menu reads the card on its frames, and one line
 * a frame would bury the log. The last few keys said are kept. */
static void said_once(const char *key)
{
	static char said[8][KEY_MAX];
	static int next;
	int i;

	for (i = 0; i < 8; i++)
		if (!strcmp(said[i], key)) return;
	fprintf(stderr, "remap: %s: not read, ZyperOS's default\n", key);
	snprintf(said[next], sizeof said[next], "%s", key);
	next = (next + 1) % 8;
}

/* The table stored under key over def; false when there is no such key */
static bool stored(const char *key, const rm_table *def, rm_table *out)
{
	char v[256];

	if (!db_get_str(db_lib(), key, v, sizeof v, NULL)) {
		*out = *def;
		return false;
	}
	if (!rm_load(def, v, out)) said_once(key);
	return true;
}

bool rms_system(const system_cfg *s, rm_table *out)
{
	char key[KEY_MAX];
	rm_table def;

	rms_default(s, &def);
	key_of(s, NULL, key, sizeof key);
	return stored(key, &def, out);
}

bool rms_game(const system_cfg *s, const char *file, rm_table *out)
{
	char key[KEY_MAX];
	rm_table sys, def;

	rms_system(s, &sys);
	rms_default(s, &def);
	key_of(s, file, key, sizeof key);
	/* The game's pairs are over ZyperOS's default, the same base as the
	 * system's: the first change copied the whole table into the game, and a
	 * later change to the system's does not reach it */
	return stored(key, &def, out) ? true : (*out = sys, false);
}

void rms_put(const system_cfg *s, const char *file, const rm_table *t)
{
	char key[KEY_MAX], v[256];
	rm_table def, same_as;

	rms_default(s, &def);
	/* The key goes when t equals what the scope falls back to: the system's
	 * table for a game, ZyperOS's for the system */
	if (file) rms_system(s, &same_as);
	else      same_as = def;
	key_of(s, file, key, sizeof key);
	rm_store(&def, t, v, sizeof v);
	if (rm_equal(&same_as, t)) {
		db_del(db_lib(), key);
		return;
	}
	/* A game set back to ZyperOS's default under a system that differs holds
	 * no pairs, and still holds its own table */
	db_set_str(db_lib(), key, v[0] ? v : "1;");
}

void rms_reset(const system_cfg *s, const char *file)
{
	char key[KEY_MAX];

	key_of(s, file, key, sizeof key);
	db_del(db_lib(), key);
}

bool rms_game_tidy(const system_cfg *s, const char *file)
{
	char key[KEY_MAX], v[256];
	rm_table def, own, sys;

	if (!file) return false;
	key_of(s, file, key, sizeof key);
	if (!db_get_str(db_lib(), key, v, sizeof v, NULL)) return false;
	rms_default(s, &def);
	/* A value this release cannot read (a newer release's) is left as it
	 * is: it reads as the default here, which is no reason to delete it */
	if (!rm_load(&def, v, &own)) return false;
	rms_system(s, &sys);
	if (!rm_equal(&own, &sys)) return false;
	db_del(db_lib(), key);
	return true;
}

static bool mode_is(const system_cfg *s, const char *m)
{
	return !strcmp(s->stick, m);
}

bool rms_stick_offered(const system_cfg *s)
{
	return s && s->stick[0];
}

/* The PSP's toggle starts with the d-pad as the d-pad; the others by their
 * `stick|` line's last field */
bool rms_stick_default(const system_cfg *s)
{
	return rms_stick_offered(s) && !mode_is(s, "toggle") && !s->stick_off;
}

static void stick_key(const system_cfg *s, const char *file, char *out, size_t n)
{
	snprintf(out, n, "stick.%s.%s", s->tag, file ? file : "");
}

bool rms_stick_get(const system_cfg *s, const char *file)
{
	char key[KEY_MAX], v[4];

	if (!rms_stick_offered(s)) return false;
	stick_key(s, file, key, sizeof key);
	if (!db_get_str(db_lib(), key, v, sizeof v, NULL)) return rms_stick_default(s);
	return v[0] == '1';
}

void rms_stick_put(const system_cfg *s, const char *file, bool on)
{
	char key[KEY_MAX];

	if (!rms_stick_offered(s)) return;
	stick_key(s, file, key, sizeof key);
	if (on == rms_stick_default(s)) db_del(db_lib(), key);
	else                            db_set_str(db_lib(), key, on ? "1" : "0");
}

void rms_run_stick(const system_cfg *s, bool on, char *key, size_t n)
{
	const char *v = "";

	if (!n) return;
	if (rms_stick_offered(s)) {
		if (mode_is(s, "toggle"))    v = on ? "stick=toggle-stick" : "stick=toggle";
		else if (on)                 v = mode_is(s, "plain") ? "stick=plain" : "stick=dpad";
	}
	snprintf(key, n, "%s", v);
}

void rms_sent(const system_cfg *s, const char *file, bool stick_on, rm_table *out)
{
	rm_table def;

	rms_game(s, file, out);
	if (!stick_on || !mode_is(s, "dpad")) return;
	rms_default(s, &def);
	/* The default's own SELECT, not a plain one: the N64's is select:r2,
	 * which its C buttons need */
	if (rm_changed(&def, out, RM_SELECT)) out->b[RM_SELECT] = def.b[RM_SELECT];
}
