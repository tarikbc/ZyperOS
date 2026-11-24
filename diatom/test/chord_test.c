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

	/* SELECT alone, held: the game's after CHORD_HOLD_MS, held, and a
	 * shoulder pressed after that is still a hotkey: a player who holds
	 * SELECT and then looks for R1 gets the save, not an R1 */
	memset(h, 0, sizeof h);
	fill(h, 2, 60, SEL);
	fill(h, 50, 3, R1);
	run(h, 70, ALL);
	{
		int wait = (CHORD_HOLD_MS + TURN_MS - 1) / TURN_MS;

		ck(count(2, 2 + wait, SEL) == 0, "SELECT held alone: hidden for CHORD_HOLD_MS");
		ck(count(2 + wait, 62, SEL) == 60 - wait, "then the game's, held");
		ck(acts[50] == 1u << CHORD_SAVE && acts_in(0, 50) == 0 && acts_in(51, 70) == 0,
		   "and an R1 after it still saves, on its turn");
		ck(count(50, 53, R1) == 0, "and that R1 is not the game's");
	}

	/* SELECT held long, then A, then R1: A made it the game's SELECT, so
	 * the shoulders are the game's again */
	memset(h, 0, sizeof h);
	fill(h, 2, 60, SEL);
	fill(h, 40, 3, A);
	fill(h, 50, 3, R1);
	run(h, 70, ALL);
	ck(acts_in(0, 70) == 0, "SELECT held, A, then R1: no hotkey");
	ck(count(50, 53, R1) == 3, "the R1 is the game's");

	/* SELECT and A in one turn: the game's at once */
	memset(h, 0, sizeof h);
	fill(h, 3, 4, SEL | A);
	run(h, 12, ALL);
	ck(count(3, 7, SEL) == 4, "SELECT and A in one turn: the game's from that turn");

	/* A shoulder held well before SELECT is no chord: it went down for
	 * the game, longer ago than CHORD_LEAD_MS */
	memset(h, 0, sizeof h);
	fill(h, 1, 30, R1);
	fill(h, 14, 4, SEL);
	run(h, 35, ALL);
	ck(acts_in(0, 35) == 0, "R1 held long, then SELECT: no hotkey");
	ck(count(18, 22, SEL) == 4, "and the SELECT is the game's tap");
	ck(count(1, 31, R1) == 30, "and R1 stays the game's throughout");
}

/* Two buttons a player means as one press reach the pad a frame or two
 * apart, either one first */
static void together(void)
{
	uint32_t h[MAXT] = { 0 };

	/* R1 two turns before SELECT, both held */
	fill(h, 3, 8, R1);
	fill(h, 5, 8, SEL);
	run(h, 20, ALL);
	ck(acts[5] == 1u << CHORD_SAVE && acts_in(0, 5) == 0 && acts_in(6, 20) == 0,
	   "R1 a moment before SELECT: a save, on SELECT's turn");
	ck(count(0, 20, SEL) == 0, "the game never sees that SELECT");
	ck(count(3, 5, R1) == 2 && count(5, 20, R1) == 0,
	   "the R1 is the game's only until SELECT came");

	/* L1 one turn before */
	memset(h, 0, sizeof h);
	fill(h, 3, 6, L1);
	fill(h, 4, 6, SEL);
	run(h, 15, ALL);
	ck(acts[4] == 1u << CHORD_LOAD, "L1 a moment before SELECT: a load");

	/* The latest a lead may come */
	memset(h, 0, sizeof h);
	{
		int lead = CHORD_LEAD_MS / TURN_MS;

		fill(h, 3, lead + 6, R1);
		fill(h, 3 + lead, 6, SEL);
		run(h, lead + 15, ALL);
		ck(acts[3 + lead] == 1u << CHORD_SAVE, "R1 CHORD_LEAD_MS before SELECT: still a save");
	}

	/* A shoulder let go before SELECT came is no lead */
	memset(h, 0, sizeof h);
	fill(h, 3, 2, R1);
	fill(h, 6, 4, SEL);
	run(h, 15, ALL);
	ck(acts_in(0, 15) == 0, "R1 tapped and let go, then SELECT: no hotkey");
	ck(count(10, 14, SEL) == 4, "and the SELECT is the game's tap");

	/* A lead is the hotkeys' once: a second SELECT over the same R1 is
	 * no second save */
	memset(h, 0, sizeof h);
	fill(h, 3, 20, R1);
	fill(h, 4, 3, SEL);
	fill(h, 9, 3, SEL);
	run(h, 25, ALL);
	ck(acts[4] == 1u << CHORD_SAVE && acts_in(5, 25) == 0,
	   "one R1 under two SELECTs: one save");

	/* No states: a lead of R1 is no hotkey */
	memset(h, 0, sizeof h);
	fill(h, 3, 8, R1);
	fill(h, 5, 4, SEL);
	run(h, 20, R2 | L2);
	ck(acts_in(0, 20) == 0, "no states: R1 then SELECT does nothing");
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

/* The keys from the launcher (chord_keys_parse, chord_set_keys): a face
 * button as a hotkey's, a shoulder that is none any more is the game's
 * under SELECT, and a bad spec changes nothing */
static void keys_from_launcher(void)
{
	int key[CHORD_ACTS], def[CHORD_ACTS], i;
	uint32_t h[MAXT] = { 0 };
	chord c;
	uint32_t was = 0;
	unsigned got[MAXT];
	uint32_t vis[MAXT];

	chord_default_keys(def);
	ck(def[CHORD_SAVE] == DIATOM_BTN_R1 && def[CHORD_SHOT] == -1 && def[CHORD_QUIT] == -1,
	   "the keys from the start: the shoulders' four, none for the rest");
	ck(chord_keys_parse("save:r1,load:l1,faster:r2,slower:l2,shot:y,quit:start", key) &&
	   key[CHORD_SHOT] == DIATOM_BTN_Y && key[CHORD_QUIT] == DIATOM_BTN_START &&
	   key[CHORD_SHADER] == -1, "a launcher's keys");
	memcpy(key, def, sizeof key);
	ck(!chord_keys_parse("shot:y,reset:y", key) && !chord_keys_parse("shot:select", key) &&
	   !chord_keys_parse("shot:menu", key) && !chord_keys_parse("zoom:y", key) &&
	   !chord_keys_parse("shot", key) && !chord_keys_parse("shot:y,shot:x", key) &&
	   !memcmp(key, def, sizeof key), "a bad spec, SELECT, MENU or a button twice: refused, untouched");
	ck(chord_keys_parse("none", key) && key[CHORD_SAVE] == -1 && key[CHORD_SLOWER] == -1,
	   "none: no hotkey at all");
	ck(def[CHORD_FFWD] == -1 && chord_keys_parse("faster:r2,ffwd:y", key) &&
	   key[CHORD_FFWD] == DIATOM_BTN_Y && key[CHORD_FASTER] == DIATOM_BTN_R2,
	   "Fast Forward: none from the start, ffwd:y a launcher's key");

	/* SELECT+Y with Y the screenshot's, and R1 no hotkey: SELECT then R1 is
	 * the game's, R1 and SELECT both */
	chord_keys_parse("shot:y", key);
	chord_reset(&c);
	chord_set_keys(&c, key);
	fill(h, 2, 10, SEL);
	fill(h, 5, 3, DIATOM_BIT(DIATOM_BTN_Y));
	fill(h, 30, 10, SEL);
	fill(h, 33, 3, R1);
	for (i = 0; i < 45; i++) {
		chord_out o = chord_turn(&c, h[i], was, (uint64_t)(1000 + i * TURN_MS), chord_keys(&c));

		vis[i] = (h[i] & ~o.hide) | o.give;
		got[i] = o.acts;
		was = h[i];
	}
	ck(got[5] == 1u << CHORD_SHOT, "SELECT+Y: the screenshot, on Y's turn");
	ck(!(vis[5] & DIATOM_BIT(DIATOM_BTN_Y)) && !(vis[6] & DIATOM_BIT(DIATOM_BTN_Y)),
	   "and the game never sees that Y");
	ck(!got[33] && (vis[33] & R1) && (vis[33] & SEL),
	   "R1 no hotkey: SELECT then R1 is the game's, both");
	ck(chord_keys(&c) == DIATOM_BIT(DIATOM_BTN_Y) && chord_key_of(&c, CHORD_SAVE) == 0,
	   "chord_keys: Y alone");
	/* a reset is the keys from the start again */
	chord_reset(&c);
	ck(chord_keys(&c) == CHORD_KEYS, "chord_reset: the shoulders' four again");
}

/* A slow game's turns (chord_view, as main.c's sel_poll feeds the rule): a
 * tenth of a second each, a Saturn game's 10 fps, and `pr` what went down at
 * each poll, a tap let go again before it too. `seen` is what the core is
 * answered: the pad as it is, less what is hidden, with what is given. */
#define SLOW_MS 100
static void run_slow(const uint32_t *held, const uint32_t *pr, int n, uint32_t live)
{
	chord c;
	uint32_t was = 0, pad_was = 0;
	int t;

	chord_reset(&c);
	for (t = 0; t < n; t++) {
		const uint32_t view = chord_view(&c, held[t], pad_was, pr[t], live);
		chord_out o = chord_turn(&c, view, was, (uint64_t)(1000 + t * SLOW_MS), live);

		seen[t] = (held[t] & ~o.hide) | o.give;
		acts[t] = o.acts;
		was = view;
		pad_was = held[t];
	}
}

static void taps(void)
{
	uint32_t h[MAXT], pr[MAXT];
	const unsigned SAVE = 1u << CHORD_SAVE;

	/* SELECT held, R1 down and up between two polls: the hotkey once, on
	 * that turn, and the game never sees SELECT or R1 */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	fill(h, 2, 6, SEL);
	pr[2] = SEL;
	pr[4] = R1;
	run_slow(h, pr, 12, ALL);
	ck(acts[4] == SAVE && acts_in(0, 12) == SAVE, "slow: SELECT held, R1 tapped between two polls: save once");
	ck(count(0, 12, SEL) == 0 && count(0, 12, R1) == 0, "  and the game sees neither SELECT nor R1");

	/* The same tap with SELECT let go in that same moment: the hotkey still,
	 * and no SELECT given to the game for it */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	fill(h, 2, 2, SEL);
	pr[2] = SEL;
	pr[4] = R1;
	run_slow(h, pr, 12, ALL);
	ck(acts_in(0, 12) == SAVE && acts[4] == SAVE, "slow: R1 tapped as SELECT is let go: save once");
	ck(count(0, 12, SEL) == 0, "  and the game is given no SELECT");

	/* SELECT held past CHORD_HOLD_MS, the game's then: a tapped R1 is a
	 * hotkey still, as a held one is */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	fill(h, 2, 12, SEL);
	pr[2] = SEL;
	pr[10] = R1;
	run_slow(h, pr, 16, ALL);
	ck(acts_in(0, 16) == SAVE && acts[10] == SAVE, "slow: SELECT held 0.8 s, R1 tapped: save once");
	ck(count(10, 12, R1) == 0, "  and the game never sees that R1");

	/* R1 tapped with no SELECT: the game's, and nothing more than libretro's
	 * poll gave it before (nothing: it was up at the poll) */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	pr[3] = R1;
	run_slow(h, pr, 8, ALL);
	ck(acts_in(0, 8) == 0 && count(0, 8, R1) == 0, "slow: R1 tapped alone: no hotkey, nothing given");

	/* SELECT down in the same turn R1 was tapped: R1 may have been let go
	 * before SELECT came, so no hotkey (a game's R1, then SELECT) */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	fill(h, 3, 6, SEL);
	pr[3] = SEL | R1;
	run_slow(h, pr, 12, ALL);
	ck(acts_in(0, 12) == 0, "slow: R1 tapped in SELECT's own turn: no hotkey");

	/* SELECT tapped alone between two polls: never added, so the game's as
	 * libretro polls it */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	pr[3] = SEL;
	run_slow(h, pr, 8, ALL);
	ck(acts_in(0, 8) == 0 && count(0, 8, SEL) == 0, "slow: SELECT tapped alone: not made up");

	/* A key that is no hotkey now (no states: save's R1 the game's) */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	fill(h, 2, 6, SEL);
	pr[2] = SEL;
	pr[4] = R1;
	run_slow(h, pr, 12, ALL & ~R1);
	ck(acts_in(0, 12) == 0, "slow: R1 tapped under SELECT, R1 no live hotkey: nothing");

	/* A hold under SELECT, many polls long, is one hotkey, as before */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	fill(h, 2, 10, SEL);
	fill(h, 4, 5, R1);
	pr[2] = SEL;
	pr[4] = R1;
	run_slow(h, pr, 16, ALL);
	ck(acts_in(0, 16) == SAVE && acts[4] == SAVE, "slow: SELECT+R1 held 0.5 s: save once");
	ck(count(0, 16, R1) == 0 && count(0, 16, SEL) == 0, "  and the game sees neither");

	/* Two taps of R2 under one SELECT, a poll between them up: two steps */
	memset(h, 0, sizeof h); memset(pr, 0, sizeof pr);
	fill(h, 2, 10, SEL);
	pr[2] = SEL;
	pr[4] = R2;
	pr[6] = R2;
	run_slow(h, pr, 16, ALL);
	ck(acts[4] == 1u << CHORD_FASTER && acts[6] == 1u << CHORD_FASTER &&
	   acts_in(0, 16) == 1u << CHORD_FASTER && acts_in(5, 6) == 0,
	   "slow: R2 tapped twice under SELECT: faster twice");
}

int main(void)
{
	tap();
	chords();
	game_select();
	together();
	live_keys();
	presses();
	keys_from_launcher();
	taps();
	printf("chord: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
