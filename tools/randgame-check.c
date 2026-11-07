/* Random Game's pick, held on a build machine (src/randgame.h): every game as
 * likely as every other, nothing from a shelf left out, never the game picked
 * just before, and nothing at all from an empty library.
 *
 * Links src/randgame.c and nothing else. */
#include "../src/randgame.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what)
{
	if (!cond) { printf("  FAIL %s\n", what); fails++; }
}

int main(void)
{
	int count[4] = { 3, 0, 300, 2 };
	bool use[4]  = { true, true, true, false };
	int hits[4] = { 0 }, s, g, i, ok = 1, by_game[300] = { 0 };

	printf("the pick:\n");
	/* r runs over a whole cycle of the 303 games in use */
	for (i = 0; i < 303 * 20; i++) {
		if (!rg_pick(count, use, 4, (uint32_t)i, -1, -1, &s, &g)) { ok = 0; break; }
		if (s < 0 || s > 3 || g < 0 || g >= count[s]) ok = 0;
		else hits[s]++;
		if (s == 2) by_game[g]++;
	}
	ck(ok, "always a game that is on its shelf");
	ck(hits[0] == 60 && hits[2] == 6000, "every game as likely as every other, not every shelf");
	ck(hits[1] == 0, "never from an empty shelf");
	ck(hits[3] == 0, "never from a shelf left out");
	for (i = 0, ok = 1; i < 300; i++) if (by_game[i] != 20) ok = 0;
	ck(ok, "every game on a long shelf comes up");

	for (i = 0, ok = 1; i < 1000; i++) {
		if (!rg_pick(count, use, 4, (uint32_t)i * 7919u, 2, 5, &s, &g)) ok = 0;
		if (s == 2 && g == 5) ok = 0;
	}
	ck(ok, "never the game picked last time");

	{
		int one[1] = { 1 };
		bool u1[1] = { true };

		ck(rg_pick(one, u1, 1, 99, 0, 0, &s, &g) && s == 0 && g == 0,
		   "a library of one game picks it, again");
	}
	{
		int none[2] = { 0, 5 };
		bool u2[2] = { true, false };

		ck(!rg_pick(none, u2, 2, 1, -1, -1, &s, &g), "nothing to pick is no pick");
		ck(!rg_pick(none, u2, 0, 1, -1, -1, &s, &g), "and no shelves too");
	}
	{
		uint32_t a = rg_random(), b = rg_random();

		ck(a != b, "two numbers in a row differ");
	}
	if (fails) { printf("randgame-check: %d FAILED\n", fails); return 1; }
	printf("randgame-check: ok\n");
	return 0;
}
