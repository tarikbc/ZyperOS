/* SELECT as the hotkeys' chord key (src/chord.h): every case of the rule,
 * turn by turn, as the game would read the pad.
 *
 * Offline and instant, like check-screens: it links src/chord.c and nothing
 * else. A turn here is 16 ms, a frame at 60 Hz, and "the game sees" is what
 * cb_input_state answers: the pad, less what is hidden, with what is given. */
#include <stdio.h>
#include <string.h>

#include "chord.h"

static int g_fail, g_pass;

static void ck(int cond, const char *what)
{
	if (cond) { g_pass++; return; }
	g_fail++;
	printf("  FAIL %s\n", what);
}

#define SEL   DIATOM_BIT(DIATOM_BTN_SELECT)
#define R1    DIATOM_BIT(DIATOM_BTN_R1)
#define L1    DIATOM_BIT(DIATOM_BTN_L1)
#define R2    DIATOM_BIT(DIATOM_BTN_R2)
#define L2    DIATOM_BIT(DIATOM_BTN_L2)
#define A     DIATOM_BIT(DIATOM_BTN_A)
#define B     DIATOM_BIT(DIATOM_BTN_B)
#define X     DIATOM_BIT(DIATOM_BTN_X)
#define START DIATOM_BIT(DIATOM_BTN_START)
#define MENU  DIATOM_BIT(DIATOM_BTN_MENU)
#define ALL   CHORD_KEYS
#define TURN_MS 16

#define MAXT 128
static uint32_t seen[MAXT];
static unsigned acts[MAXT];

/* `n` turns of the pad as `held` gives them, from a fresh chord */
static void run(const uint32_t *held, int n, uint32_t live)
{
	chord c;
	uint32_t was = 0;
	int t;

	chord_reset(&c);
	for (t = 0; t < n; t++) {
		chord_out o = chord_turn(&c, held[t], was, (uint64_t)(1000 + t * TURN_MS), live);

		seen[t] = (held[t] & ~o.hide) | o.give;
		acts[t] = o.acts;
		was = held[t];
	}
}

/* `n` turns of `m` from `at` */
static void fill(uint32_t *h, int at, int n, uint32_t m)
{
	int i;

	for (i = 0; i < n; i++) h[at + i] |= m;
}

static int count(int from, int to, uint32_t bit)
{
	int t, k = 0;

	for (t = from; t < to; t++) k += (seen[t] & bit) != 0;
	return k;
}

static unsigned acts_in(int from, int to)
{
	unsigned a = 0;
	int t;

	for (t = from; t < to; t++) a |= acts[t];
	return a;
}

static void tap(void)
{
	uint32_t h[MAXT] = { 0 };

	/* SELECT held 6 turns and let go: hidden while held, then the game
	 * gets a press of 6 turns, from the turn it went up */
	fill(h, 2, 6, SEL);
	run(h, 30, ALL);
	ck(count(0, 8, SEL) == 0, "a tap: the game does not see SELECT while it is held");
	ck(count(8, 14, SEL) == 6, "a tap: the game gets SELECT for as long as it was held, as it is let go");
	ck(count(14, 30, SEL) == 0, "a tap: and then it is up again");
	ck(acts_in(0, 30) == 0, "a tap is no hotkey");

	/* A one-turn press is given two, so a core that reads every other
	 * frame cannot miss it */
	memset(h, 0, sizeof h);
	fill(h, 2, 1, SEL);
	run(h, 10, ALL);
	ck(count(0, 10, SEL) == 2, "a one-turn tap reaches the game as two turns");
}

static void chords(void)
{
	uint32_t h[MAXT] = { 0 };

	/* SELECT, then R1 three turns later */
	fill(h, 2, 12, SEL);
	fill(h, 5, 4, R1);
	run(h, 30, ALL);
	ck(acts[5] == 1u << CHORD_SAVE, "SELECT+R1 saves, on the turn R1 goes down");
	ck(acts_in(0, 5) == 0 && acts_in(6, 30) == 0, "and only on that turn");
	ck(count(0, 30, SEL) == 0, "the game never sees the SELECT of a chord, held or let go");
	ck(count(0, 30, R1) == 0, "nor its R1");

	memset(h, 0, sizeof h);
	fill(h, 2, 12, SEL);
	fill(h, 4, 2, L1);
	run(h, 30, ALL);
	ck(acts[4] == 1u << CHORD_LOAD, "SELECT+L1 loads");

	/* SELECT held, R2 twice and L2 once: three acts under one SELECT */
	memset(h, 0, sizeof h);
	fill(h, 2, 20, SEL);
	fill(h, 4, 2, R2);
	fill(h, 8, 2, R2);
	fill(h, 12, 2, L2);
	run(h, 30, ALL);
	ck(acts[4] == 1u << CHORD_FASTER && acts[8] == 1u << CHORD_FASTER &&
	   acts[12] == 1u << CHORD_SLOWER, "SELECT held: R2, R2, L2 step faster, faster, slower");
	ck(count(0, 30, SEL | R2 | L2) == 0, "none of it reaches the game");

	/* Pressed in the same turn */
	memset(h, 0, sizeof h);
	fill(h, 3, 6, SEL | R1);
	run(h, 20, ALL);
	ck(acts[3] == 1u << CHORD_SAVE, "SELECT and R1 in one turn: a save");
	ck(count(0, 20, SEL | R1) == 0, "and neither is the game's");

	/* A shoulder held after SELECT is let go stays hidden until it goes up */
	memset(h, 0, sizeof h);
	fill(h, 2, 5, SEL);
	fill(h, 4, 8, R1);
	run(h, 20, ALL);
	ck(count(0, 12, R1) == 0, "a chord's R1 held past SELECT stays hidden until let go");
	ck(count(0, 20, SEL) == 0, "and the SELECT is owed nothing");

	/* Other buttons under a chord's SELECT are the game's */
	memset(h, 0, sizeof h);
	fill(h, 2, 12, SEL);
	fill(h, 4, 2, R1);
	fill(h, 8, 3, A);
	run(h, 20, ALL);
	ck(count(8, 11, A) == 3, "A under a chord's SELECT is the game's");
	ck(count(0, 20, SEL) == 0, "the SELECT stays the hotkeys'");
}

static void game_select(void)
{
	uint32_t h[MAXT] = { 0 };

	/* SELECT, then A: SELECT reaches the game with A, on A's turn */
	fill(h, 2, 12, SEL);
	fill(h, 5, 4, A);
	run(h, 30, ALL);
	ck(count(2, 5, SEL) == 0, "SELECT then A: hidden until A goes down");
	ck((seen[5] & (SEL | A)) == (SEL | A), "and seen with A on A's turn");
	ck(count(5, 14, SEL) == 9, "then the game's, held, until let go");
	ck(count(14, 30, SEL) == 0, "and nothing owed after");

	/* Once SELECT is the game's, R1 is the game's too */
	memset(h, 0, sizeof h);
	fill(h, 2, 12, SEL);
	fill(h, 4, 6, X);
	fill(h, 6, 2, R1);
	run(h, 20, ALL);
	ck(acts_in(0, 20) == 0, "SELECT+X then R1: no hotkey (the N64's C buttons, then R)");
	ck(count(6, 8, R1) == 2, "the R1 is the game's");

	/* The soft reset: A+B+START held, then SELECT. SELECT waits its hold
	 * time, then the game has all four */
	memset(h, 0, sizeof h);
	fill(h, 1, 60, A | B | START);
	fill(h, 3, 50, SEL);
	run(h, 70, ALL);
	{
		int wait = (CHORD_HOLD_MS + TURN_MS - 1) / TURN_MS;

		ck(count(3, 3 + wait, SEL) == 0, "A+B+START then SELECT: SELECT waits");
		ck((seen[3 + wait] & (A | B | START | SEL)) == (A | B | START | SEL),
		   "and after CHORD_HOLD_MS the game sees all four, the soft reset");
	}

	/* SELECT alone, held: the game's after CHORD_HOLD_MS, then R1 is the
	 * game's */
	memset(h, 0, sizeof h);
	fill(h, 2, 60, SEL);
	fill(h, 50, 3, R1);
	run(h, 70, ALL);
	{
		int wait = (CHORD_HOLD_MS + TURN_MS - 1) / TURN_MS;

		ck(count(2, 2 + wait, SEL) == 0, "SELECT held alone: hidden for CHORD_HOLD_MS");
		ck(count(2 + wait, 62, SEL) == 60 - wait, "then the game's, held");
		ck(acts_in(0, 70) == 0 && count(50, 53, R1) == 3, "and an R1 after it is the game's");
	}

	/* SELECT and A in one turn: the game's at once */
	memset(h, 0, sizeof h);
	fill(h, 3, 4, SEL | A);
	run(h, 12, ALL);
	ck(count(3, 7, SEL) == 4, "SELECT and A in one turn: the game's from that turn");

	/* A shoulder held before SELECT is no chord: it did not go down
	 * under SELECT */
	memset(h, 0, sizeof h);
	fill(h, 1, 20, R1);
	fill(h, 4, 4, SEL);
	run(h, 25, ALL);
	ck(acts_in(0, 25) == 0, "R1 held, then SELECT: no hotkey");
	ck(count(8, 12, SEL) == 4, "and the SELECT is the game's tap");
}

static void live_keys(void)
{
	uint32_t h[MAXT] = { 0 };

	/* No state on this core: R1 and L1 are no hotkeys, R2 and L2 still are */
	fill(h, 2, 12, SEL);
	fill(h, 5, 3, R1);
	run(h, 20, R2 | L2);
	ck(acts_in(0, 20) == 0, "no states: SELECT+R1 does nothing");
	ck((seen[5] & (SEL | R1)) == (SEL | R1), "and both reach the game on R1's turn");

	memset(h, 0, sizeof h);
	fill(h, 2, 12, SEL);
	fill(h, 5, 3, R2);
	run(h, 20, R2 | L2);
	ck(acts[5] == 1u << CHORD_FASTER, "no states: SELECT+R2 still steps the speed");
}

static void presses(void)
{
	uint32_t h[MAXT] = { 0 };

	/* Two taps close together read as two presses */
	fill(h, 2, 5, SEL);
	fill(h, 8, 5, SEL);
	run(h, 40, ALL);
	{
		int t, ups = 0, on = 0;

		for (t = 1; t < 40; t++) {
			if ((seen[t] & SEL) && !(seen[t - 1] & SEL)) on++;
			if (!(seen[t] & SEL) && (seen[t - 1] & SEL)) ups++;
		}
		ck(on == 2 && ups == 2, "two taps close together reach the game as two presses");
		ck(count(0, 40, SEL) == 10, "of five turns each");
	}

	/* MENU while SELECT waits is no other button */
	memset(h, 0, sizeof h);
	fill(h, 2, 12, SEL);
	fill(h, 4, 2, MENU);
	fill(h, 8, 2, R1);
	run(h, 20, ALL);
	ck(acts[8] == 1u << CHORD_SAVE, "MENU under a waiting SELECT does not give it to the game");
}

int main(void)
{
	tap();
	chords();
	game_select();
	live_keys();
	presses();
	printf("chord: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
