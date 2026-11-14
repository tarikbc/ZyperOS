/* SPDX-License-Identifier: GPL-3.0-only */
#include "remapstore.h"

#include <stdio.h>
#include <string.h>

#include "db.h"

/* platform.c's, declared here so this file links without SDL */
const char *plat_turbo_map(const char *tag);

#define KEY_MAX (CFG_STR + 160)

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

/* The table stored under key over def; false when there is no such key */
static bool stored(const char *key, const rm_table *def, rm_table *out)
{
	char v[256];

	if (!db_get_str(db_lib(), key, v, sizeof v, NULL)) {
		*out = *def;
		return false;
	}
	rm_load(def, v, out);
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
	rm_table sys;

	rms_system(s, &sys);
	key_of(s, file, key, sizeof key);
	return stored(key, &sys, out);
}

void rms_put(const system_cfg *s, const char *file, const rm_table *t)
{
	char key[KEY_MAX], v[256];
	rm_table def;

	if (file) rms_system(s, &def);
	else      rms_default(s, &def);
	key_of(s, file, key, sizeof key);
	rm_store(&def, t, v, sizeof v);
	if (v[0]) db_set_str(db_lib(), key, v);
	else      db_del(db_lib(), key);
}

void rms_reset(const system_cfg *s, const char *file)
{
	char key[KEY_MAX];

	key_of(s, file, key, sizeof key);
	db_del(db_lib(), key);
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
	if (rm_changed(&def, out, RM_SELECT))
		out->b[RM_SELECT] = (rm_choice){ (signed char)rm_own(RM_SELECT), 0 };
}
