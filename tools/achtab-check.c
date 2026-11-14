/* SPDX-License-Identifier: GPL-3.0-only */
/* The in-game menu's Achievements tab, as words and rows (src/achtab.h):
 * every state a game's set can be in says its own sentence, in the order
 * that decides it; the counts line in each of its three forms; and each
 * achievement's row as the list over the shelf draws it too.
 *
 *     make check-achtab
 *
 * No SDL, no card: the facts are made up here, the way main.c's gm_at_facts
 * gathers them on a device. */
#include <stdio.h>
#include <string.h>

#include "../src/achtab.h"
#include "../src/i18n.h"

static int bad;

#define ck(cond, what) do { \
	if (cond) printf("  ok   %s\n", what); \
	else { printf("  FAIL %s\n", what); bad++; } \
} while (0)

static at_facts none_yet(void)
{
	at_facts f;

	memset(&f, 0, sizeof f);
	f.signed_in = true;
	f.set = CHV_UNCHECKED;
	f.no_hash = RA_HASH_OK;
	return f;
}

int main(void)
{
	at_facts f;
	char buf[128], val[32];
	cheevo c;
	menu_row row;
	unsigned vcol = 99;
	int s;

	i18n_set(LANG_EN);
	printf("achtab: the states, in the order that decides them\n");

	f = none_yet();
	f.count = 36;
	f.no_hash = RA_HASH_NO_SYSTEM;
	f.signed_in = false;
	ck(at_state_of(&f) == AT_LIST, "a set on the card is the list, whatever else is true");
	f = none_yet();
	f.no_hash = RA_HASH_NO_SYSTEM;
	f.signed_in = false;
	f.fetching = true;
	ck(at_state_of(&f) == AT_NOT_SYSTEM, "no hash for the system, before the account or a fetch");
	f.no_hash = RA_HASH_NO_FILE;
	ck(at_state_of(&f) == AT_NOT_FILE, "no hash for the file, likewise");
	f = none_yet();
	f.signed_in = false;
	f.set = CHV_NONE;              /* chv_state_of's answer with nobody signed in */
	ck(at_state_of(&f) == AT_SIGNED_OUT, "nobody signed in, rather than chv_state_of's none");
	f = none_yet();
	f.fetching = true;
	ck(at_state_of(&f) == AT_WAITING, "the set on its way");
	f = none_yet();
	ck(at_state_of(&f) == AT_UNCHECKED, "never played with a network");
	f.set = CHV_NONE;
	ck(at_state_of(&f) == AT_NONE, "asked, and RetroAchievements has none");

	printf("achtab: each state but the list has a sentence of its own\n");
	for (s = AT_NOT_SYSTEM; s < AT_STATES; s++) {
		int t;

		for (t = s + 1; t < AT_STATES; t++)
			if (at_message((at_state)s) == at_message((at_state)t)) break;
		snprintf(buf, sizeof buf, "state %d: \"%.60s...\"", s, T(at_message((at_state)s)));
		ck(t == AT_STATES && *T(at_message((at_state)s)), buf);
	}
	ck(at_message(AT_UNCHECKED) == S_CHEEVOS_UNCHECKED_SAID,
	   "never played with a network says what A on the Game tab's row said");

	printf("achtab: the counts line\n");
	f = none_yet();
	f.count = 36; f.earned = 3; f.points = 25; f.points_total = 415;
	ck(!strcmp(at_counts(&f, buf, sizeof buf), "3 / 36 · 25 / 415 points"), buf);
	f.pending = 2;
	ck(!strcmp(at_counts(&f, buf, sizeof buf), "3 / 36 · 2 waiting to send"), buf);
	f.pending = 0; f.earned = 36; f.points = 415;
	ck(!strcmp(at_counts(&f, buf, sizeof buf), "Mastered · 415 points"), buf);
	f.pending = 1;
	ck(!strcmp(at_counts(&f, buf, sizeof buf), "36 / 36 · 1 waiting to send"),
	   "all earned, one still to send: not Mastered yet");

	printf("achtab: an achievement's row\n");
	memset(&c, 0, sizeof c);
	c.points = 10;
	snprintf(c.title, sizeof c.title, "Violated Heavens");
	at_row(&c, &row, &vcol, val, sizeof val);
	ck(!strcmp(row.label, "Violated Heavens") && !strcmp(row.value, "10") && !row.live && vcol == 0,
	   "not earned: its points, quiet, the plain color");
	snprintf(c.progress, sizeof c.progress, "74/100");
	at_row(&c, &row, &vcol, val, sizeof val);
	ck(!strcmp(row.value, "74 / 100"), "a count moving, where its points are");
	c.earned_now = true;
	at_row(&c, &row, &vcol, val, sizeof val);
	ck(!strcmp(row.value, "10") && row.live && vcol == AT_VCOL_EARNED,
	   "earned this game: its points again, in the earned color");
	c.earned_now = false;
	c.earned = true;
	at_row(&c, &row, NULL, val, sizeof val);
	ck(row.live, "earned before: the same, and a NULL color is no crash");

	if (bad) {
		printf("\nFAIL: %d\n", bad);
		return 1;
	}
	printf("\nok: the Achievements tab says what each state is\n");
	return 0;
}
