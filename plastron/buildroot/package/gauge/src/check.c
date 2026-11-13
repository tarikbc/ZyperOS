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

/* Files moving (my picks in the transfer lights lab, 2026-11-13): a wave
 * that fills the four and empties them, from the top down for files coming
 * in to the card, from the bottom up for files going out of it */
static const int WIN[8] = { G(3), G(3) | G(2), G(3) | G(2) | G(1), L_ALL, G(2) | G(1) | G(0), G(1) | G(0), G(0), 0 };
static const int WOUT[8] = { G(0), G(0) | G(1), G(0) | G(1) | G(2), L_ALL, G(1) | G(2) | G(3), G(2) | G(3), G(3), 0 };

/* A step's length for a speed: the second step's time after a line at 0 */
static long pace_of(const char *line)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s = battery(60, 0, 0);
	long t;
	int first;

	lights_line(&s, &c, line, 0);
	first = at(&c, &s, 0);
	for (t = 1; t < 400 && at(&c, &s, t) == first; t++) { }
	return t;
}

static void moving(void)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s = battery(60, 0, 0);
	int w;

	/* Download at 4.2 MB/s, a line a second as Flash Drive sends them */
	for (int k = 0; k < 24; k++) {
		if (k % 10 == 0) lights_line(&s, &c, "move 4200000 0", k * 100);
		CHECK(at(&c, &s, k * 100) == WIN[k % 8], "download: step %d", k);
	}
	lights_frame(&c, &s, 2330, &w);
	CHECK(w == 70, "wakes at each step (%d)", w);
	for (long t = 2350; t < 3499; t += 50) at(&c, &s, t);
	CHECK(at(&c, &s, 3499) == WIN[34 % 8], "still moving 1.5 s after the last line");
	CHECK(at(&c, &s, 3500) == 0 && !s.mv_on, "then over: dark off the cable");
	lights_frame(&c, &s, 3600, &w);
	CHECK(w == IDLE_MS, "and nothing to wake for (%d)", w);

	/* Upload: the same wave upside down */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move 0 4200000", 0);
	for (int k = 0; k < 10; k++)
		CHECK(at(&c, &s, k * 100) == WOUT[k % 8], "upload: step %d", k);

	/* The pace follows the speed of the way that shows */
	CHECK(pace_of("move 600000 0") == 200, "600 KB/s: 200 ms (%ld)", pace_of("move 600000 0"));
	CHECK(pace_of("move 999999 0") == 200, "under 1 MB/s: 200 ms");
	CHECK(pace_of("move 1000000 0") == 100, "1 MB/s: 100 ms");
	CHECK(pace_of("move 9999999 0") == 100, "under 10 MB/s: 100 ms");
	CHECK(pace_of("move 10000000 0") == 60, "10 MB/s: 60 ms");
	CHECK(pace_of("move 0 24000000") == 60, "24 MB/s out: 60 ms");
	CHECK(pace_of("move 500000 24000000") == 60, "both: the pace of the way that shows");

	/* Both ways: the bigger one, chosen as each run begins */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move 3000000 20000000", 0);
	CHECK(at(&c, &s, 0) == WOUT[0], "more out than in: the upload wave");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move 20000000 3000000", 0);
	CHECK(at(&c, &s, 0) == WIN[0], "more in than out: the download wave");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move 5000000 5000000", 0);
	CHECK(at(&c, &s, 0) == WIN[0], "as much each way: the download wave");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move 9000000 1000000", 0);
	for (long t = 0; t <= 350; t += 50) at(&c, &s, t);
	lights_line(&s, &c, "move 1000000 9000000", 350);
	CHECK(at(&c, &s, 400) == WIN[4], "a run is never cut");
	for (long t = 450; t < 700; t += 50) at(&c, &s, t);
	CHECK(at(&c, &s, 700) == WIN[7], "a run is never cut, to its end");
	CHECK(at(&c, &s, 800) == WOUT[0], "the next run takes the bigger way");

	/* "move 0 0": over at once */
	lights_line(&s, &c, "move 0 0", 850);
	CHECK(at(&c, &s, 850) == 0 && !s.mv_on, "move 0 0 ends it");

	/* Over the charging bar, and the bar back after */
	s = battery(40, 1, 0);
	CHECK(at(&c, &s, 0) == BAR(1), "the bar first");
	lights_line(&s, &c, "move 18000000 0", 100);
	CHECK(at(&c, &s, 100) == WIN[0] && at(&c, &s, 160) == WIN[1], "the wave over the bar");
	CHECK(at(&c, &s, 520) == 0, "its dark step dark on the cable too");
	lights_line(&s, &c, "move 0 0", 1000);
	{
		int b = at(&c, &s, 1000);

		CHECK(b == BAR(1) || b == BAR(2) || b == BAR(3) || b == BAR(4), "then the bar again (%x)", b);
	}

	/* The newest pattern on top, then the wave again */
	s = battery(60, 0, 0);
	for (long t = 0; t <= 2000; t += 50) {
		if (t % 1000 == 0) lights_line(&s, &c, "move 4200000 0", t);
		if (t == 300) lights_line(&s, &c, "level volume 20 20", t);
		if (t == 300) CHECK(at(&c, &s, t) == L_ALL, "a level over the wave");
		if (t == 1850) CHECK(at(&c, &s, t) == WIN[18 % 8], "the wave once the level's hold is over");
		at(&c, &s, t);
	}
	lights_line(&s, &c, "done", 2000);
	CHECK(at(&c, &s, 2000) == G(0) && at(&c, &s, 2300) == G(3), "Done over the wave");

	/* The low-battery red shows through; L2+R2 over everything */
	s = battery(8, 0, 0);
	lights_line(&s, &c, "move 4200000 0", 0);
	CHECK(at(&c, &s, 0) == (WIN[0] | L_RED), "the red through the wave");
	s = battery(80, 0, 0);
	lights_line(&s, &c, "move 4200000 0", 0);
	lights_ask(&s, 0);
	CHECK(at(&c, &s, 0) == L_ALL && at(&c, &s, 250) == 0, "asked: over the wave");

	/* Files and Covers off: nothing, and off while it moves: gone */
	c = cfg("auto -done");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move 4200000 0", 0);
	CHECK(at(&c, &s, 0) == 0 && !s.mv_on, "the part off: nothing");
	c = cfg("off");
	lights_line(&s, &c, "move 4200000 0", 0);
	lights_frame(&c, &s, 0, &w);
	CHECK(!s.mv_on && w == IDLE_MS, "every part off: nothing, nothing to wake for");
	c = cfg("auto");
	lights_line(&s, &c, "move 4200000 0", 0);
	c = cfg("auto -done");   /* MENU, then SIGHUP */
	CHECK(at(&c, &s, 100) == 0 && !s.mv_on, "turned off while it moves: gone");

	/* Lines it does not take */
	c = cfg("auto");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move", 0);
	lights_line(&s, &c, "move 5", 0);
	lights_line(&s, &c, "move -5 3", 0);
	lights_line(&s, &c, "move x 3", 0);
	lights_line(&s, &c, "move 1 2 3", 0);
	lights_line(&s, &c, "move 1 2x", 0);
	lights_line(&s, &c, "move 123456789012345678901234 0", 0);
	lights_line(&s, &c, "move 123456789012345678901", 0);
	CHECK(!s.mv_on && at(&c, &s, 0) == 0, "a line that is not two numbers: nothing");
	lights_line(&s, &c, "move 7 0\n", 0);
	CHECK(s.mv_on && s.mv_in == 7, "a line with its newline still read");

	/* Even again at a run's end: files coming in */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "move 0 9000000", 0);
	for (long t = 0; t <= 700; t += 50) at(&c, &s, t);
	lights_line(&s, &c, "move 9000000 9000000", 700);
	CHECK(at(&c, &s, 800) == WIN[0], "as much each way at a run's end: the download wave");

	/* Files and Covers off: "move 0 0" leaves nothing on either */
	c = cfg("auto -done");
	lights_line(&s, &c, "move 0 0", 850);
	CHECK(at(&c, &s, 850) == 0 && !s.mv_on, "the part off and move 0 0: nothing left on");
	c = cfg("auto");

	/* Woken late: on from there, no catching up and no wait under 1 ms */
	lights_line(&s, &c, "move 24000000 0", 0);
	lights_line(&s, &c, "move 24000000 0", 1400);
	lights_frame(&c, &s, 1450, &w);
	CHECK(w >= 1 && w <= 60, "a late wake waits a step at most (%d)", w);
}

/* MENU > Side Lights: the row under the cursor plays its part on the lights,
 * in a loop, On or Off, over everything but L2+R2 */
static void previews(void)
{
	struct lights_cfg c = cfg("auto");
	struct lights_state s = battery(60, 0, 0);
	int w;

	/* Charging Bar: filling from none to four, 400 ms a light, again */
	lights_line(&s, &c, "preview bar", 0);
	CHECK(at(&c, &s, 0) == 0 && at(&c, &s, 400) == BAR(1) && at(&c, &s, 800) == BAR(2) &&
	      at(&c, &s, 1200) == BAR(3) && at(&c, &s, 1600) == L_ALL && at(&c, &s, 2000) == 0, "bar preview");
	lights_frame(&c, &s, 2100, &w);
	CHECK(w == 300, "bar preview: wakes at its next step (%d)", w);

	/* Low Battery: one red flash, then two */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "preview low", 0);
	CHECK(at(&c, &s, 0) == L_RED && at(&c, &s, 199) == L_RED && at(&c, &s, 200) == 0 && at(&c, &s, 999) == 0 &&
	      at(&c, &s, 1000) == L_RED && at(&c, &s, 1150) == 0 && at(&c, &s, 1300) == L_RED &&
	      at(&c, &s, 1450) == 0, "low preview");
	lights_line(&s, &c, "preview low", 2000);
	CHECK(at(&c, &s, 2599) == 0 && at(&c, &s, 2600) == L_RED,
	      "low preview: again from 2.6 s, the same part said again not restarting it");

	/* Volume and Brightness: up the eight steps, half a light blinking, then
	 * the bottom red for silence */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "preview level", 0);
	CHECK(at(&c, &s, 0) == G(0) && at(&c, &s, 250) == 0, "level preview: a half, blinking");
	CHECK(at(&c, &s, 500) == BAR(1) && at(&c, &s, 1500) == BAR(2), "level preview: up the steps");
	lights_line(&s, &c, "preview level", 2000);
	CHECK(at(&c, &s, 3500) == L_ALL, "level preview: all four at the top");
	CHECK(at(&c, &s, 4000) == L_RED, "level preview: silence");
	lights_line(&s, &c, "preview level", 4000);
	CHECK(at(&c, &s, 4500) == G(0), "level preview: and again");

	/* Files and Covers: the wave down, the wave up, then Done */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "preview done", 0);
	CHECK(at(&c, &s, 0) == WIN[0] && at(&c, &s, 300) == WIN[3], "files preview: the wave down");
	CHECK(at(&c, &s, 800) == WOUT[0] && at(&c, &s, 1100) == WOUT[3], "files preview: the wave up");
	CHECK(at(&c, &s, 1600) == G(0) && at(&c, &s, 1900) == G(3), "files preview: Done");
	lights_line(&s, &c, "preview done", 2000);
	CHECK(at(&c, &s, 2300) == G(3) && at(&c, &s, 2400) == 0 && at(&c, &s, 2999) == 0, "files preview: a rest");
	CHECK(at(&c, &s, 3000) == WIN[0], "files preview: and again");

	/* Achievements: the sparkle, a rest, again */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "preview cheer", 0);
	CHECK(at(&c, &s, 0) == (G(0) | G(2)) && at(&c, &s, 150) == (G(1) | G(3)) && at(&c, &s, 900) == 0 &&
	      at(&c, &s, 1500) == (G(0) | G(2)), "cheer preview");

	/* A part turned off still previews: the row is there to show it */
	c = cfg("off");
	s = battery(60, 0, 0);
	lights_line(&s, &c, "preview cheer", 0);
	CHECK(at(&c, &s, 0) == (G(0) | G(2)), "a preview with every part off");
	c = cfg("auto");

	/* Another row: its own, from its start */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "preview bar", 0);
	at(&c, &s, 500);
	lights_line(&s, &c, "preview cheer", 500);
	CHECK(at(&c, &s, 500) == (G(0) | G(2)) && at(&c, &s, 650) == (G(1) | G(3)), "the next row from its start");

	/* Over the bar, a wave and a pattern; the low red hidden under it;
	 * L2+R2 over it */
	s = battery(40, 1, 0);
	lights_line(&s, &c, "move 4200000 0", 0);
	lights_line(&s, &c, "done", 0);
	lights_line(&s, &c, "preview cheer", 0);
	CHECK(at(&c, &s, 0) == (G(0) | G(2)), "the preview over everything");
	s = battery(8, 0, 0);
	lights_line(&s, &c, "preview cheer", 0);
	CHECK(at(&c, &s, 0) == (G(0) | G(2)), "the low red hidden under a preview");
	lights_ask(&s, 100);
	CHECK(at(&c, &s, 100) == L_RED, "L2+R2 over a preview (the level at 8%%: red)");

	/* Its end: 2.5 s past the last line, or "preview off" at once; then
	 * what was there before */
	s = battery(40, 1, 0);
	lights_line(&s, &c, "preview cheer", 0);
	CHECK(at(&c, &s, 2499) == 0, "the preview at 2.5 s less a ms (the sparkle's rest)");
	{
		int b = at(&c, &s, 2500);

		CHECK(s.pv_part == PV_NONE && (b == BAR(1) || b == BAR(2) || b == BAR(3) || b == BAR(4)),
		      "2.5 s with no line: the bar again (%x)", b);
	}
	lights_line(&s, &c, "preview level", 3000);
	lights_line(&s, &c, "preview off", 3100);
	CHECK(s.pv_part == PV_NONE, "preview off ends it at once");

	/* Lines it does not take */
	s = battery(60, 0, 0);
	lights_line(&s, &c, "preview", 0);
	lights_line(&s, &c, "preview shiny", 0);
	lights_line(&s, &c, "preview bar x", 0);
	lights_line(&s, &c, "previewbar", 0);
	CHECK(s.pv_part == PV_NONE && at(&c, &s, 0) == 0, "a preview line it does not know: nothing");
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
		if (t >= 14000 && t < 17000 && t % 1000 < 7)
			lights_line(&s, &c, t < 15000 ? "move 600000 0" : t < 16000 ? "move 0 24000000" : "move 9000000 9000000", t);
		if (t >= 17000 && t < 19500 && t % 500 < 7) {
			static const char *const PV[] = { "preview bar", "preview low", "preview level", "preview done", "preview cheer" };

			lights_line(&s, &c, PV[(t - 17000) / 500 % 5], t);
		}
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
	moving();
	previews();
	asked();
	never_spins();
	printf("gauge-check: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
