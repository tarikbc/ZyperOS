/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Brick's rumble motor (diatom/port/brick_motor.h) on a clock
 * moved by hand:
 *
 *     make check-brickmotor
 *
 * The levels' duty, the pin over cycles, a hold, a buzz that ends itself,
 * a wish that goes quiet, and the clock's wrap. No device, no file. */
#include <stdio.h>

#include "../diatom/port/brick_motor.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* Milliseconds high in [from, from + ms), a pass every millisecond */
static unsigned high_ms(bm_motor *m, unsigned from, unsigned ms)
{
	unsigned t, n = 0;

	for (t = 0; t < ms; t++) n += bm_pin(m, from + t);
	return n;
}

static void duty(void)
{
	printf("the levels' duty\n");
	CHECK(bm_duty(0, 0, 1.0f) == 0, "nothing asked: off");
	CHECK(bm_duty(0xFFFF, 0, 0.0f) == 0, "Off: off, whatever the core asks");
	CHECK(bm_duty(0xFFFF, 0, 1.0f) == 1000, "Full, a full wish: always on");
	CHECK(bm_duty(0, 0xFFFF, 1.0f) == 1000, "the weak motor alone runs it too");
	CHECK(bm_duty(0xFFFF, 0, 0.65f) == 650, "Medium: 65%%: %d", bm_duty(0xFFFF, 0, 0.65f));
	CHECK(bm_duty(0xFFFF, 0, 0.35f) == 350, "Low: 35%%: %d", bm_duty(0xFFFF, 0, 0.35f));
	CHECK(bm_duty(0x8000, 0xFFFF, 1.0f) == 1000, "the stronger of the two");
	CHECK(bm_duty(0xFFFF, 0, 0.35f * 0.35f) == 250,
	      "Low over Doom's 35%% share (12%%) is raised to a quarter, felt: %d",
	      bm_duty(0xFFFF, 0, 0.35f * 0.35f));
	CHECK(bm_duty(0x0800, 0, 1.0f) == 0, "a wish under 5%%: off, as a core eases to 0: %d",
	      bm_duty(0x0800, 0, 1.0f));
	CHECK(bm_duty(0xFFFF, 0, 2.0f) == 1000, "a scale over 1 is 1");
}

static void cycles(void)
{
	bm_motor m;
	unsigned t0 = 1000;

	printf("the pin over its cycles\n");
	bm_init(&m);
	CHECK(!bm_pin(&m, 0) && bm_wake(&m, 0) == BM_NEVER, "at the start: low, no wake");
	bm_set(&m, 1000, t0);
	CHECK(high_ms(&m, t0, 1000) == 1000, "Full: high all the time");
	bm_set(&m, 0, t0 + 1000);
	CHECK(!bm_pin(&m, t0 + 1000), "0: low at once");
	/* Off a cycle's edge, so only a fresh cycle is high here */
	bm_set(&m, 500, t0 + 2013);
	CHECK(bm_pin(&m, t0 + 2013), "a new wish: high first");
	CHECK(high_ms(&m, t0 + 2013, 1000) == 500, "half: 500 ms of 1000 high: %u",
	      high_ms(&m, t0 + 2013, 1000));
	bm_set(&m, 350, t0 + 3000);
	CHECK(high_ms(&m, t0 + 3000, 1000) == 350, "Low's 35%%: 350 ms of 1000: %u",
	      high_ms(&m, t0 + 3000, 1000));
	{
		/* The wake lands on each edge: high 7 ms, low 13 ms */
		unsigned now = t0 + 4000, w;

		bm_set(&m, 350, now);
		w = bm_wake(&m, now);
		CHECK(bm_pin(&m, now) && w == 7, "high, the fall in 7 ms: %u", w);
		now += w;
		w = bm_wake(&m, now);
		CHECK(!bm_pin(&m, now) && w == 13, "low, the rise in 13 ms: %u", w);
		now += w;
		CHECK(bm_pin(&m, now), "high again at the next cycle");
	}
}

static void stops(void)
{
	bm_motor m;
	unsigned t0 = 50;

	printf("holds, quiet wishes and buzzes\n");
	bm_init(&m);
	bm_set(&m, 1000, t0);
	bm_off(&m, t0 + 100);
	CHECK(!bm_pin(&m, t0 + 100) && bm_wake(&m, t0 + 100) == BM_NEVER, "a hold: low, no wake");

	bm_set(&m, 1000, t0 + 200);
	CHECK(bm_pin(&m, t0 + 200 + BM_QUIET_MS - 1), "still asked: high to the last ms");
	CHECK(bm_wake(&m, t0 + 200 + BM_QUIET_MS - 1) == 1, "and the wake at the quiet's end");
	CHECK(!bm_pin(&m, t0 + 200 + BM_QUIET_MS), "no word for %u ms: low", BM_QUIET_MS);
	CHECK(!bm_pin(&m, t0 + 200 + BM_QUIET_MS + 1000) && m.duty == 0, "and it stays low");
	bm_set(&m, 1000, t0 + 10000);
	CHECK(bm_pin(&m, t0 + 10000), "the next word starts it again");
	bm_set(&m, 1000, t0 + 10000 + BM_QUIET_MS - 1);
	CHECK(bm_pin(&m, t0 + 10000 + BM_QUIET_MS + 10), "a word, the same wish, holds it on");

	bm_buzz(&m, 650, 300, t0 + 20000);
	CHECK(high_ms(&m, t0 + 20000, 300) == 195, "a 300 ms buzz at Medium: 195 ms high: %u",
	      high_ms(&m, t0 + 20000, 300));
	CHECK(!bm_pin(&m, t0 + 20300) && bm_wake(&m, t0 + 20300) == BM_NEVER, "and it ends by itself");
	bm_buzz(&m, 1000, 60000, t0 + 30000);
	CHECK(bm_pin(&m, t0 + 30000 + BM_BUZZ_MAX_MS - 1) && !bm_pin(&m, t0 + 30000 + BM_BUZZ_MAX_MS),
	      "a buzz is never longer than %u ms", BM_BUZZ_MAX_MS);
	bm_buzz(&m, 0, 300, t0 + 40000);
	CHECK(!bm_pin(&m, t0 + 40000), "a buzz at Off: nothing");
	bm_buzz(&m, 1000, 300, t0 + 50000);
	bm_off(&m, t0 + 50100);
	CHECK(!bm_pin(&m, t0 + 50100), "a buzz stopped by a hold");
}

static void wrap(void)
{
	bm_motor m;
	unsigned t0 = 0xffffffffu - 100;

	printf("the clock's wrap\n");
	bm_init(&m);
	bm_set(&m, 500, t0);
	CHECK(high_ms(&m, t0, 1000) == 500, "half across the wrap: %u", high_ms(&m, t0, 1000));
	CHECK(!bm_pin(&m, t0 + BM_QUIET_MS), "quiet past the wrap: low");
}

int main(void)
{
	printf("brickmotor: the Brick's rumble motor\n");
	duty();
	cycles();
	stops();
	wrap();
	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("ok\n");
	return 0;
}
