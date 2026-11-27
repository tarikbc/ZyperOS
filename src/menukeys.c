/* SPDX-License-Identifier: GPL-3.0-only */
/* See menukeys.h. The table and its strings, and nothing else. */
#include "menukeys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const unsigned MK_HOLD_MS[MK_HOLDS] = { 500, 1000, 2000, 3000, 5000 };

static const char *const ACT_NAME[MK_ACTS] = { "shot", "lock" };

void mk_default(mk_table *t)
{
	t->key[MK_SHOT] = HK_L1;
	t->key[MK_LOCK] = HK_L2;
	t->hold_ms = MK_HOLD_DEFAULT;
}

bool mk_key_ok(int key)
{
	return key >= HK_A && key <= HK_R2;
}

int mk_act_on(const mk_table *t, int key)
{
	int i;

	if (!mk_key_ok(key)) return -1;
	for (i = 0; i < MK_ACTS; i++)
		if (t->key[i] == key) return i;
	return -1;
}

void mk_swap(mk_table *t, mk_act a, int key)
{
	int other;

	if ((unsigned)a >= MK_ACTS || !mk_key_ok(key)) return;
	other = mk_act_on(t, key);
	if (other >= 0 && other != (int)a) t->key[other] = t->key[a];
	t->key[a] = (signed char)key;
}

static int hold_at(unsigned ms)
{
	int i;

	for (i = 0; i < MK_HOLDS; i++)
		if (MK_HOLD_MS[i] == ms) return i;
	return -1;
}

unsigned mk_hold_step(unsigned ms, int dir)
{
	int i = hold_at(ms);

	if (i < 0) i = hold_at(MK_HOLD_DEFAULT);
	i = ((i + (dir < 0 ? -1 : 1)) % MK_HOLDS + MK_HOLDS) % MK_HOLDS;
	return MK_HOLD_MS[i];
}

void mk_store(const mk_table *t, char *out, size_t n)
{
	mk_table def;

	if (!n) return;
	out[0] = '\0';
	mk_default(&def);
	if (!memcmp(def.key, t->key, sizeof def.key) && def.hold_ms == t->hold_ms) return;
	snprintf(out, n, "1;shot:%s,lock:%s,hold:%u", hk_key_map(t->key[MK_SHOT]),
	         hk_key_map(t->key[MK_LOCK]), t->hold_ms);
}

static int key_of(const char *s, size_t len)
{
	int k;

	for (k = HK_A; k <= HK_R2; k++)
		if (strlen(hk_key_map(k)) == len && !strncmp(hk_key_map(k), s, len)) return k;
	return -1;
}

bool mk_load(const char *stored, mk_table *out)
{
	mk_table next;
	const char *p;

	mk_default(out);
	if (!stored || !*stored) return true;
	if (strncmp(stored, "1;", 2)) return false;
	next = *out;
	p = stored + 2;
	while (*p) {
		const char *end = strchr(p, ','), *colon;
		size_t len = end ? (size_t)(end - p) : strlen(p), vlen;

		colon = memchr(p, ':', len);
		if (!colon) return false;
		vlen = (size_t)(p + len - colon - 1);
		if ((size_t)(colon - p) == 4 && !strncmp(p, "hold", 4)) {
			unsigned ms = (unsigned)strtoul(colon + 1, NULL, 10);

			if (hold_at(ms) < 0) return false;
			next.hold_ms = ms;
		} else {
			int a = -1, k = key_of(colon + 1, vlen), i;

			for (i = 0; i < MK_ACTS; i++)
				if (strlen(ACT_NAME[i]) == (size_t)(colon - p) && !strncmp(ACT_NAME[i], p, (size_t)(colon - p)))
					a = i;
			if (a < 0 || k < 0) return false;
			next.key[a] = (signed char)k;
		}
		if (!end) break;
		p = end + 1;
	}
	/* One action a chord: two on one key is nothing this wrote */
	if (next.key[MK_SHOT] == next.key[MK_LOCK]) return false;
	*out = next;
	return true;
}

void mk_run(const mk_table *t, char *out, size_t n)
{
	if (!n) return;
	snprintf(out, n, "shot:%s,lock:%s", hk_key_map(t->key[MK_SHOT]), hk_key_map(t->key[MK_LOCK]));
}

void mk_secs(unsigned ms, bool comma, char *out, size_t n)
{
	if (!n) return;
	if (ms % 1000 == 0) snprintf(out, n, "%u", ms / 1000);
	else snprintf(out, n, "%u%c%u", ms / 1000, comma ? ',' : '.', ms % 1000 / 100);
}

void mk_chord(const mk_table *t, mk_act a, char *out, size_t n)
{
	if (!n) return;
	if ((unsigned)a >= MK_ACTS || !mk_key_ok(t->key[a])) out[0] = '\0';
	else snprintf(out, n, "MENU+%s", hk_key_name(t->key[a]));
}
