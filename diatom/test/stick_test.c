/* The d-pad as the left analog stick (src/stick.h): the axes for each
 * direction, and the SELECT rule that keeps the N64's own D-pad in reach,
 * poll by poll.
 *
 * Offline and instant, like check-chord: it links src/stick.c and nothing
 * else. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "stick.h"

static int g_fail, g_pass;

static void ck(int cond, const char *what)
{
	if (cond) { g_pass++; return; }
	g_fail++;
	printf("  FAIL %s\n", what);
}

#define UP    DIATOM_BIT(DIATOM_BTN_UP)
#define DOWN  DIATOM_BIT(DIATOM_BTN_DOWN)
#define LEFT  DIATOM_BIT(DIATOM_BTN_LEFT)
#define RIGHT DIATOM_BIT(DIATOM_BTN_RIGHT)
#define SEL   DIATOM_BIT(DIATOM_BTN_SELECT)
#define A     DIATOM_BIT(DIATOM_BTN_A)
#define B     DIATOM_BIT(DIATOM_BTN_B)
#define R1    DIATOM_BIT(DIATOM_BTN_R1)
#define MENU  DIATOM_BIT(DIATOM_BTN_MENU)

#define MAXT 64
static bool dpad[MAXT];

/* `n` polls of the pad as `held` gives them, from a fresh stick */
static void run(const uint32_t *held, int n)
{
	stick s;
	uint32_t was = 0;
	int t;

	stick_reset(&s);
	for (t = 0; t < n; t++) {
		dpad[t] = stick_turn(&s, held[t], was);
		was = held[t];
	}
}

static void fill(uint32_t *h, int at, int n, uint32_t m)
{
	int i;

	for (i = 0; i < n; i++) h[at + i] |= m;
}

static void axes(void)
{
	int16_t x, y;
	double r;

	stick_axes(0, &x, &y);
	ck(x == 0 && y == 0, "nothing held: the stick in the middle");
	stick_axes(UP, &x, &y);
	ck(x == 0 && y == -STICK_FULL, "up: y full up");
	stick_axes(DOWN, &x, &y);
	ck(x == 0 && y == STICK_FULL, "down: y full down");
	stick_axes(LEFT, &x, &y);
	ck(x == -STICK_FULL && y == 0, "left: x full left");
	stick_axes(RIGHT, &x, &y);
	ck(x == STICK_FULL && y == 0, "right: x full right");
	stick_axes(UP | RIGHT, &x, &y);
	ck(x == STICK_DIAG && y == -STICK_DIAG, "up-right: both axes at the diagonal's share");
	r = sqrt((double)x * x + (double)y * y);
	ck(fabs(r - STICK_FULL) < 2.0, "a diagonal as far out as a straight push");
	stick_axes(DOWN | LEFT, &x, &y);
	ck(x == -STICK_DIAG && y == STICK_DIAG, "down-left");
	stick_axes(UP | DOWN, &x, &y);
	ck(x == 0 && y == 0, "up and down together cancel");
	stick_axes(LEFT | RIGHT | UP, &x, &y);
	ck(x == 0 && y == -STICK_FULL, "left and right cancel, up stays a straight push");
	stick_axes(A | B | SEL, &x, &y);
	ck(x == 0 && y == 0, "buttons that are not the d-pad move nothing");
}

static void select_rule(void)
{
	uint32_t h[MAXT];
	int t, k;

	/* The d-pad alone: the stick, always */
	memset(h, 0, sizeof h);
	fill(h, 2, 10, RIGHT);
	run(h, 14);
	for (t = 0, k = 0; t < 14; t++) k += dpad[t];
	ck(k == 0, "the d-pad alone is the stick");

	/* SELECT, then the d-pad: the D-pad until SELECT is let go */
	memset(h, 0, sizeof h);
	fill(h, 2, 10, SEL);
	fill(h, 4, 2, UP);
	fill(h, 8, 6, LEFT);            /* held past SELECT */
	run(h, 16);
	ck(!dpad[3], "SELECT alone: the d-pad is still the stick");
	ck(dpad[4] && dpad[5], "SELECT then up: the D-pad");
	ck(dpad[6] && dpad[7], "and still the D-pad between presses, SELECT held");
	ck(dpad[8] && dpad[11], "a second direction under the same SELECT: the D-pad");
	ck(!dpad[12] && !dpad[13], "SELECT let go: the stick again, the direction still held");

	/* SELECT and a direction in the same poll: the D-pad */
	memset(h, 0, sizeof h);
	fill(h, 2, 4, SEL | DOWN);
	run(h, 8);
	ck(dpad[2], "SELECT and down in one poll: the D-pad");

	/* Running, then SELECT and a face button: a C button, the run goes on */
	memset(h, 0, sizeof h);
	fill(h, 1, 20, RIGHT);
	fill(h, 5, 6, SEL);
	fill(h, 6, 2, B);
	run(h, 22);
	for (t = 0, k = 0; t < 22; t++) k += dpad[t];
	ck(k == 0, "a direction held before SELECT stays the stick");

	/* SELECT and a face button, then the d-pad: still the stick */
	memset(h, 0, sizeof h);
	fill(h, 2, 12, SEL);
	fill(h, 3, 8, A);
	fill(h, 5, 4, UP);
	run(h, 16);
	for (t = 0, k = 0; t < 16; t++) k += dpad[t];
	ck(k == 0, "the d-pad after a C button under the same SELECT is the stick");

	/* A hotkey first (SELECT+R1), then the d-pad: the stick */
	memset(h, 0, sizeof h);
	fill(h, 2, 10, SEL);
	fill(h, 3, 2, R1);
	fill(h, 6, 2, LEFT);
	run(h, 14);
	ck(!dpad[6], "the d-pad after a hotkey under the same SELECT is the stick");

	/* MENU is no button */
	memset(h, 0, sizeof h);
	fill(h, 2, 10, SEL);
	fill(h, 3, 2, MENU);
	fill(h, 6, 2, LEFT);
	run(h, 14);
	ck(dpad[6], "MENU under SELECT does not take the D-pad away");

	/* A new SELECT starts over */
	memset(h, 0, sizeof h);
	fill(h, 2, 4, SEL);
	fill(h, 3, 2, A);
	fill(h, 8, 4, SEL);
	fill(h, 9, 2, RIGHT);
	run(h, 14);
	ck(dpad[9], "a C button under one SELECT leaves the next one free");
}

int main(void)
{
	axes();
	select_rule();
	printf("stick: %d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
