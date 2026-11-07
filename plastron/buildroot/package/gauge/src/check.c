/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host checks for lights.c, the side lights' rules:
 *
 *   cc -Wall -Wextra -o /tmp/gauge-check check.c lights.c && /tmp/gauge-check
 *
 * (plastron's `make gauge-check` runs it.) Every check states the rule it
 * holds, so a failure says which one broke.
 */
#include <stdio.h>
#include <string.h>

#include "lights.h"

static int fails, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

#define G(i) (L_GREEN << (i))   /* one green light, 0 the bottom one */
#define BAR(n) ((n) == 0 ? 0 : (n) == 1 ? G(0) : (n) == 2 ? G(0) | G(1) : (n) == 3 ? G(0) | G(1) | G(2) : L_ALL)

static struct lights_cfg cfg(const char *text)
{
	struct lights_cfg c;

	memset(&c, 0x55, sizeof c);   /* nothing left from before */
	lights_parse(&c, text);
	return c;
}

static int at(const struct lights_cfg *c, struct lights_state *s, long now)
{
	int w;

	return lights_frame(c, s, now, &w);
}

static struct lights_state battery(int pct, int plugged, int held)
{
	struct lights_state s;

	lights_init(&s);
	lights_battery(&s, pct, plugged, held, 0);
	return s;
}

static void parse(void)
{
	struct lights_cfg c;

	c = cfg(NULL);
	CHECK(c.bar && c.low && c.level && c.done && c.cheer, "no file: every part");
	c = cfg("auto\n");
	CHECK(c.bar && c.low && c.level && c.done && c.cheer, "a bare auto (today's cards): every part");
	c = cfg("charging\n");
	CHECK(c.bar && !c.low && !c.level && !c.done && !c.cheer, "charging: the bar only, as it said");
	c = cfg("low");
	CHECK(!c.bar && c.low && !c.level && !c.done, "low: the red flash only");
	c = cfg("off");
	CHECK(!c.bar && !c.low && !c.level && !c.done && !c.cheer, "off: nothing");
	c = cfg("auto -level -cheer\n");
	CHECK(c.bar && c.low && !c.level && c.done && !c.cheer, "auto with two parts off");
	c = cfg("off level done");
	CHECK(!c.bar && !c.low && c.level && c.done && !c.cheer, "off with two parts on");
	c = cfg("charging low -bar");
	CHECK(!c.bar && c.low && !c.level, "a later bar or low word wins over the first");
	c = cfg("auto shiny -done game");
	CHECK(c.level && !c.done && c.cheer, "an unknown word is left alone");
	c = cfg("rainbow");
	CHECK(c.bar && c.low && c.level, "an unknown first word reads as auto");
}

static void charging_bar(void)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s;
	int w;

	s = battery(50, 1, 0);
	CHECK(at(&c, &s, 0) == BAR(2), "50%%: two held");
	CHECK(at(&c, &s, 400) == BAR(3), "then one more each 400 ms");
	CHECK(at(&c, &s, 800) == BAR(4), "up to all four");
	CHECK(at(&c, &s, 1200) == BAR(2), "and again");
	lights_frame(&c, &s, 100, &w);
	CHECK(w == 300, "wakes at the next step, not before (%d)", w);

	s = battery(100, 1, 0);
	CHECK(at(&c, &s, 0) == L_ALL && at(&c, &s, 400) == L_ALL, "full: all four, still");
	lights_frame(&c, &s, 0, &w);
	CHECK(w == IDLE_MS, "full and still: nothing to wake for (%d)", w);

	s = battery(85, 1, 1);
	CHECK(at(&c, &s, 0) == BAR(3) && at(&c, &s, 400) == BAR(3), "held at the limit: the level, still");
	s = battery(10, 1, 1);
	CHECK(at(&c, &s, 0) == BAR(1), "still under 25%%: one light, not none");

	s = battery(30, 1, 0);
	lights_line(&s, &c, "game 1", 0);
	CHECK(at(&c, &s, 1200) != at(&c, &s, 1600), "a game changes nothing: moving (my pick)");

	lights_battery(&s, 30, 0, 0, 2000);
	CHECK(at(&c, &s, 2000) == 0, "the cable out: dark");
	lights_battery(&s, 30, 1, 0, 2300);
	CHECK(at(&c, &s, 2300) == BAR(1), "the cable in: the bar starts from its held lights");

	c = cfg("low");
	s = battery(50, 1, 0);
	CHECK(at(&c, &s, 0) == 0, "bar off: nothing on the cable");
}

static void low_battery(void)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s;
	int w;

	s = battery(9, 0, 0);
	CHECK(at(&c, &s, 0) == L_RED, "under 10%%: the red flash at once");
	CHECK(at(&c, &s, 199) == L_RED && at(&c, &s, 200) == 0, "200 ms long");
	CHECK(at(&c, &s, 5000) == L_RED, "every 5 s");
	lights_frame(&c, &s, 300, &w);
	CHECK(w == 4700, "sleeps to the next flash (%d)", w);

	s = battery(4, 0, 0);
	CHECK(at(&c, &s, 0) == L_RED && at(&c, &s, 150) == 0 && at(&c, &s, 300) == L_RED
	      && at(&c, &s, 450) == 0 && at(&c, &s, 2000) == L_RED, "under 5%%: twice every 2 s");

	s = battery(12, 0, 0);
	CHECK(at(&c, &s, 1000) == 0, "12%%: dark");
	lights_battery(&s, 9, 0, 0, 1234);
	CHECK(at(&c, &s, 1234) == L_RED, "falling under 10%%: the first flash at once");

	s = battery(9, 1, 0);
	CHECK(at(&c, &s, 0) != L_RED && !(at(&c, &s, 0) & L_RED), "on the cable: no red");

	c = cfg("charging");
	s = battery(9, 0, 0);
	CHECK(at(&c, &s, 0) == 0, "low off: no red");
	c = cfg("off level");
	s = battery(9, 0, 0);
	lights_line(&s, &c, "level volume 10 20", 0);
	CHECK(!(at(&c, &s, 0) & L_RED), "off with the level on: still no red");

	/* never hidden */
	c = cfg("auto");
	s = battery(9, 0, 0);
	lights_line(&s, &c, "done", 0);
	CHECK(at(&c, &s, 0) & L_RED, "under a pattern the red still flashes");
	lights_line(&s, &c, "level volume 20 20", 5000);
	CHECK((at(&c, &s, 5000) & L_RED) && (at(&c, &s, 5000) & L_ALL) == L_ALL, "with the level shown too");
	lights_ask(&s, 10000);
	CHECK(at(&c, &s, 10000) == L_RED, "asked under 10%%: red");
}

static void level(void)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s = battery(60, 0, 0);
	int w;

	/* Eight steps: two a light, the light above the lit ones blinking for
	 * a half, 250 ms on and 250 off from the first change */
	lights_line(&s, &c, "level volume 1 20", 0);
	CHECK(at(&c, &s, 0) == G(0) && at(&c, &s, 250) == 0 && at(&c, &s, 500) == G(0),
	      "1 of 20: the bottom light blinks, a half");
	lights_frame(&c, &s, 100, &w);
	CHECK(w == 150, "wakes for the blink (%d)", w);
	lights_line(&s, &c, "level volume 5 20", 600);
	CHECK(at(&c, &s, 600) == BAR(1) && at(&c, &s, 750) == BAR(1), "5 of 20: one light, still");
	lights_frame(&c, &s, 600, &w);
	CHECK(w == 1500, "nothing blinks: wakes when it ends (%d)", w);
	lights_line(&s, &c, "level volume 6 20", 700);
	CHECK(at(&c, &s, 700) == (BAR(1) | G(1)) && at(&c, &s, 750) == BAR(1), "6 of 20: one, and the second blinking");
	lights_line(&s, &c, "level volume 16 20", 1100);
	CHECK(at(&c, &s, 1100) == (BAR(3) | G(3)) && at(&c, &s, 1250) == BAR(3), "16 of 20: three, the top one blinking");
	lights_line(&s, &c, "level volume 20 20", 1200);
	CHECK(at(&c, &s, 1200) == BAR(4) && at(&c, &s, 1250) == BAR(4), "20 of 20: all four, still");
	lights_line(&s, &c, "level volume 0 20", 1300);
	CHECK(at(&c, &s, 1300) == L_RED, "silence: the bottom one red");
	lights_frame(&c, &s, 1300, &w);
	CHECK(w == 1500, "held 1.5 s after the last change (%d)", w);
	CHECK(at(&c, &s, 2799) == L_RED && at(&c, &s, 2800) == 0, "then dark");

	lights_line(&s, &c, "level brightness 0 11", 3000);
	CHECK(at(&c, &s, 3000) == G(0) && at(&c, &s, 3250) == 0, "the dimmest brightness: a half, never red");
	lights_line(&s, &c, "level brightness 11 11", 3100);
	CHECK(at(&c, &s, 3100) == BAR(4), "the brightest: four");
	lights_line(&s, &c, "level brightness 99 11", 3200);
	CHECK(at(&c, &s, 3200) == BAR(4), "a level over its top reads as the top");
	lights_line(&s, &c, "level brightness 3 0", 3300);
	CHECK(at(&c, &s, 3300) == BAR(4), "a line with no top is left alone");
	lights_line(&s, &c, "level volume 10 20", 3400);
	CHECK(at(&c, &s, 3400) == BAR(2), "volume after brightness: its own, from its first change");

	c = cfg("auto -level");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "level volume 10 20", 0);
	CHECK(at(&c, &s, 0) == 0, "level off: nothing");
}

static void events(void)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s = battery(60, 0, 0);
	int w;

	lights_line(&s, &c, "game 1", 0);
	CHECK(at(&c, &s, 0) == 0, "a game: no light of its own (my pick)");
	lights_frame(&c, &s, 0, &w);
	CHECK(w == IDLE_MS, "nothing lit, nothing to wake for (%d)", w);

	lights_line(&s, &c, "done", 1000);
	for (int k = 0; k < 8; k++)
		CHECK(at(&c, &s, 1000 + k * 100) == G(k % 4), "done: step %d is light %d", k, k % 4);
	lights_frame(&c, &s, 1050, &w);
	CHECK(w == 50, "wakes at each step (%d)", w);
	CHECK(at(&c, &s, 1800) == 0, "0.8 s, then dark");
	CHECK(s.ev == LE_NONE, "and the pattern is over");

	lights_line(&s, &c, "cheer", 2000);
	CHECK(at(&c, &s, 2000) == (G(0) | G(2)) && at(&c, &s, 2150) == (G(1) | G(3)), "cheer: pairs take turns");
	CHECK(at(&c, &s, 2899) == (G(1) | G(3)) && at(&c, &s, 2900) == 0, "0.9 s, then dark");

	lights_line(&s, &c, "done", 3000);
	lights_line(&s, &c, "level volume 20 20", 3050);
	CHECK(at(&c, &s, 3050) == L_ALL, "the newest pattern on top");
	lights_line(&s, &c, "done", 3100);
	CHECK(at(&c, &s, 3100) == G(0), "and again: a pattern ends the level's hold");

	/* on the cable */
	s = battery(40, 1, 0);
	lights_line(&s, &c, "done", 0);
	CHECK(at(&c, &s, 0) == G(0) && at(&c, &s, 300) == G(3), "a pattern over the charging bar");
	CHECK(at(&c, &s, 800) == BAR(1) || at(&c, &s, 800) == BAR(2) || at(&c, &s, 800) == BAR(3) || at(&c, &s, 800) == BAR(4),
	      "then the bar again");

	c = cfg("auto -done -cheer");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "done", 0);
	lights_line(&s, &c, "cheer", 10);
	CHECK(at(&c, &s, 10) == 0, "parts off: nothing");
	lights_line(&s, &c, "nonsense 1 2", 20);
	lights_line(&s, &c, "", 20);
	lights_line(&s, &c, "level", 20);
	lights_line(&s, &c, "level volume x 20", 20);
	CHECK(at(&c, &s, 20) == 0, "a line it does not know: nothing");
}

static void asked(void)
{
	struct lights_cfg c = cfg("off");
	struct lights_state s = battery(60, 1, 0);

	lights_ask(&s, 0);
	CHECK(at(&c, &s, 0) == BAR(3) && at(&c, &s, 250) == 0 && at(&c, &s, 500) == BAR(3), "asked: shows even when off");
	CHECK(at(&c, &s, 2000) == 0 && s.asked_at < 0, "two seconds");
	CHECK(lights_level_bits(100) == L_ALL && lights_level_bits(74) == BAR(3) && lights_level_bits(25) == BAR(2)
	      && lights_level_bits(10) == BAR(1) && lights_level_bits(9) == L_RED, "the level's lights");

	c = cfg("auto");
	s = battery(80, 0, 0);
	lights_line(&s, &c, "level volume 1 20", 0);
	lights_ask(&s, 0);
	CHECK(at(&c, &s, 0) == L_ALL, "asked: over the patterns");
}

/* No wake of zero ms anywhere: the gauge would spin */
static void never_spins(void)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s = battery(3, 0, 0);
	int w, worst = IDLE_MS;

	for (long t = 0; t < 20000; t += 7) {
		if (t == 3000) lights_line(&s, &c, "done", t);
		if (t == 7000) lights_line(&s, &c, "level volume 3 20", t);
		if (t == 9000) lights_ask(&s, t);
		if (t == 12000) lights_battery(&s, 30, 1, 0, t);
		lights_frame(&c, &s, t, &w);
		if (w < worst) worst = w;
		CHECK(w >= 1 && w <= IDLE_MS, "wake in range at %ld (%d)", t, w);
	}
	CHECK(worst >= 1, "the shortest wake is %d ms", worst);
}

int main(void)
{
	parse();
	charging_bar();
	low_battery();
	level();
	events();
	asked();
	never_spins();
	printf("gauge-check: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
