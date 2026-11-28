/* SPDX-License-Identifier: GPL-3.0-only */
/* See hotkeys.h. The table and its strings, and nothing else. */
#include "hotkeys.h"

#include <stdio.h>
#include <string.h>

#include "inmodel.h"   /* each key's name and word, the logical button's */

/* The logical button each key is (src/inmodel.h): its name as printed and
 * its word for Diatom are that button's, one copy for the capture screens,
 * MENU's chords and the RUN line */
static const in_button KEY_IN[HK_KEYS] = {
	IN_ACCEPT, IN_BACK, IN_X, IN_Y, IN_L1, IN_R1, IN_L2, IN_R2, IN_START
};
static const char *const ACT_NAME[HK_ACTS] = {
	"save", "load", "faster", "slower", "shot", "shader", "display", "reset", "quit", "ffwd"
};
static const char *const FF_WORD[HK_FF_STEPS] = { "1.5", "2", "3", "max" };
static const int FF_HALF[HK_FF_STEPS] = { 3, 4, 6, 0 };
/* Fast Forward's speed in the store: a pair as an action's, not an action */
#define FF_PAIR "ffspeed"

const hk_act hk_order[HK_ACTS] = {
	HK_SAVE, HK_LOAD, HK_FASTER, HK_SLOWER, HK_FFWD,
	HK_SHOT, HK_SHADER, HK_DISPLAY, HK_RESET, HK_QUIT
};

void hk_default(hk_table *t)
{
	int i;

	for (i = 0; i < HK_ACTS; i++) t->key[i] = HK_NONE;
	t->key[HK_SAVE] = HK_R1;
	t->key[HK_LOAD] = HK_L1;
	t->key[HK_FASTER] = HK_R2;
	t->key[HK_SLOWER] = HK_L2;
	t->ff = HK_FF_DEFAULT;
}

bool hk_equal(const hk_table *a, const hk_table *b)
{
	return !memcmp(a->key, b->key, sizeof a->key) && a->ff == b->ff;
}

const char *hk_key_name(int key)
{
	return key >= 0 && key < HK_KEYS ? in_info_of(KEY_IN[key])->name : NULL;
}

const char *hk_key_map(int key)
{
	return key >= 0 && key < HK_KEYS ? in_info_of(KEY_IN[key])->word : NULL;
}

in_button hk_button(int key)
{
	return key >= 0 && key < HK_KEYS ? KEY_IN[key] : IN_NONE;
}

int hk_key_of(in_button b)
{
	int k;

	for (k = 0; k < HK_KEYS; k++)
		if (KEY_IN[k] == b) return k;
	return HK_NONE;
}

const char *hk_act_name(hk_act a)
{
	return (unsigned)a < HK_ACTS ? ACT_NAME[a] : "";
}

const char *hk_ff_word(int ff)
{
	return FF_WORD[ff >= 0 && ff < HK_FF_STEPS ? ff : HK_FF_DEFAULT];
}

int hk_ff_half(int ff)
{
	return FF_HALF[ff >= 0 && ff < HK_FF_STEPS ? ff : HK_FF_DEFAULT];
}

int hk_act_on(const hk_table *t, int key, int not)
{
	int i;

	if (key < 0) return -1;
	for (i = 0; i < HK_ACTS; i++)
		if (i != not && t->key[i] == key) return i;
	return -1;
}

void hk_set(hk_table *t, hk_act a, int key)
{
	if ((unsigned)a < HK_ACTS) t->key[a] = (signed char)(key >= 0 && key < HK_KEYS ? key : HK_NONE);
}

void hk_swap(hk_table *t, hk_act a, int key)
{
	int other = hk_act_on(t, key, (int)a);

	if ((unsigned)a >= HK_ACTS) return;
	if (other >= 0) t->key[other] = t->key[a];
	hk_set(t, a, key);
}

static int index_of(const char *const *names, int count, const char *s, size_t n)
{
	int i;

	for (i = 0; i < count; i++)
		if (strlen(names[i]) == n && !strncmp(names[i], s, n)) return i;
	return -1;
}

/* The key whose word (hk_key_map) is the n bytes at s, or -1 */
static int key_by_word(const char *s, size_t n)
{
	int k;

	for (k = 0; k < HK_KEYS; k++)
		if (strlen(hk_key_map(k)) == n && !strncmp(hk_key_map(k), s, n)) return k;
	return -1;
}

void hk_store(const hk_table *t, char *out, size_t n)
{
	hk_table def;
	size_t at;
	int i;

	if (!n) return;
	out[0] = '\0';
	hk_default(&def);
	if (hk_equal(&def, t)) return;
	at = (size_t)snprintf(out, n, "1;");
	for (i = 0; i < HK_ACTS && at < n; i++) {
		if (t->key[i] == def.key[i]) continue;
		at += (size_t)snprintf(out + at, n - at, "%s%s:%s", out[at - 1] == ';' ? "" : ",",
		                       ACT_NAME[i], t->key[i] >= 0 ? hk_key_map(t->key[i]) : "none");
	}
	if (t->ff != def.ff && at < n)
		snprintf(out + at, n - at, "%s%s:%s", out[at - 1] == ';' ? "" : ",", FF_PAIR, hk_ff_word(t->ff));
}

bool hk_load(const char *stored, hk_table *out)
{
	hk_table next;
	const char *p;

	hk_default(out);
	if (!stored || !*stored) return true;
	if (strncmp(stored, "1;", 2)) return false;
	next = *out;
	p = stored + 2;
	while (*p) {
		const char *end = strchr(p, ','), *colon;
		size_t len = end ? (size_t)(end - p) : strlen(p);
		int a, k;

		colon = memchr(p, ':', len);
		if (!colon) return false;
		if ((size_t)(colon - p) == strlen(FF_PAIR) && !strncmp(p, FF_PAIR, strlen(FF_PAIR))) {
			int f = index_of(FF_WORD, HK_FF_STEPS, colon + 1, (size_t)(p + len - colon - 1));

			if (f < 0) return false;
			next.ff = (signed char)f;
			if (!end) break;
			p = end + 1;
			continue;
		}
		a = index_of(ACT_NAME, HK_ACTS, p, (size_t)(colon - p));
		if (a < 0) return false;
		if ((size_t)(p + len - colon - 1) == 4 && !strncmp(colon + 1, "none", 4)) k = HK_NONE;
		else if ((k = key_by_word(colon + 1, (size_t)(p + len - colon - 1))) < 0)
			return false;
		next.key[a] = (signed char)k;
		if (!end) break;
		p = end + 1;
	}
	/* One action a chord: a key stored for a second action (a default's
	 * key given to another, the default's own pair unchanged) leaves the
	 * action that had it from the start with none */
	{
		int i, j;

		for (i = 0; i < HK_ACTS; i++)
			for (j = 0; j < HK_ACTS; j++)
				if (i != j && next.key[i] >= 0 && next.key[i] == next.key[j] &&
				    next.key[j] == out->key[j] && next.key[i] != out->key[i])
					next.key[j] = HK_NONE;
		for (i = 0; i < HK_ACTS; i++)
			for (j = i + 1; j < HK_ACTS; j++)
				if (next.key[i] >= 0 && next.key[i] == next.key[j]) next.key[j] = HK_NONE;
	}
	*out = next;
	return true;
}

void hk_run(const hk_table *t, char *out, size_t n)
{
	size_t at = 0;
	int i;

	if (!n) return;
	out[0] = '\0';
	for (i = 0; i < HK_ACTS && at < n; i++)
		if (t->key[i] >= 0)
			at += (size_t)snprintf(out + at, n - at, "%s%s:%s", at ? "," : "", ACT_NAME[i],
			                       hk_key_map(t->key[i]));
	if (!out[0]) snprintf(out, n, "none");
}

void hk_chord(const hk_table *t, hk_act a, char *out, size_t n)
{
	if (!n) return;
	if ((unsigned)a >= HK_ACTS || t->key[a] < 0) out[0] = '\0';
	else snprintf(out, n, "SELECT+%s", hk_key_name(t->key[a]));
}
