/* SPDX-License-Identifier: GPL-3.0-only */
/* The old screens' strength, kept: see src/strength.h. */
#include "strength.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_ALL "shader_strength"

int strength_clamp(int pct)
{
	if (pct <= 0 || pct > STRENGTH_MAX) return STRENGTH_MAX;
	pct = (pct + STRENGTH_STEP / 2) / STRENGTH_STEP * STRENGTH_STEP;
	return pct < STRENGTH_MIN ? STRENGTH_MIN : pct;
}

int strength_step(int pct, int d)
{
	pct = strength_clamp(pct) + (d < 0 ? -STRENGTH_STEP : d > 0 ? STRENGTH_STEP : 0);
	if (pct < STRENGTH_MIN) pct = STRENGTH_MIN;
	if (pct > STRENGTH_MAX) pct = STRENGTH_MAX;
	return pct;
}

/* As Charge Limit and Menu Music's volume write a percentage */
void strength_label(int pct, char *out, size_t n)
{
	snprintf(out, n, "%d%%", strength_clamp(pct));
}

int strength_read_all(db *d)
{
	return strength_clamp(db_get_int(d, KEY_ALL, STRENGTH_MAX));
}

int strength_read(db *d, const char *tag)
{
	char key[96];

	snprintf(key, sizeof key, KEY_ALL ".%s", tag);
	return strength_clamp(db_get_int(d, key, strength_read_all(d)));
}

void strength_write(db *d, const char *tag, int pct)
{
	char key[96];

	snprintf(key, sizeof key, KEY_ALL ".%s", tag);
	db_set_int(d, key, strength_clamp(pct));
}

/* Every key a system's own is kept under, gathered before any goes: a walk is
 * not the place to delete what it walks */
typedef struct { char key[128][96]; int n; } strength_keys;

static bool key_note(const char *key, const char *value, void *ctx)
{
	strength_keys *k = ctx;

	(void)value;
	if (k->n >= (int)(sizeof k->key / sizeof k->key[0])) return false;
	snprintf(k->key[k->n++], sizeof k->key[0], "%s", key);
	return true;
}

void strength_write_all(db *d, int pct)
{
	static strength_keys keys;
	int i;

	db_set_int(d, KEY_ALL, strength_clamp(pct));
	keys.n = 0;
	db_each_prefix(d, KEY_ALL ".", key_note, &keys);
	for (i = 0; i < keys.n; i++) db_del(d, keys.key[i]);
}
