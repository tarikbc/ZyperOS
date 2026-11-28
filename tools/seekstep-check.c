/* SPDX-License-Identifier: GPL-3.0-only */
/* MP4's seek steps (src/seekstep.h), held as the launcher's clock repeats a
 * held d-pad (src/keyrepeat.h: the press, then a step every 90 ms after
 * 300 ms): a five-minute song is crossed in several seconds, never under
 * five, and never faster than a share of it a second; a seventeen-hour book
 * as fast as before (about twenty seconds); a tap ten seconds; a track of
 * no known length the steps of old. */
#include <stdio.h>

#include "../src/keyrepeat.h"
#include "../src/seekstep.h"

static int failures;

#define CHECK(c, ...) do { if (!(c)) { failures++; printf("  FAIL: "); \
	printf(__VA_ARGS__); printf("\n        at %s:%d\n", __FILE__, __LINE__); } } while (0)

/* Held from 0 until `len` seconds are crossed: the ms it took, and the most
 * one second of holding covered (into *worst) */
static unsigned cross(double len, double *worst)
{
	double at = seek_step_for(true, 0, len), win[64] = { 0 };
	unsigned t;

	*worst = 0;
	for (t = REPEAT_DELAY_MS; at < len && t < 600000; t += REPEAT_RATE_MS) {
		double s = seek_step_for(false, t, len);

		at += s;
		if (t / 1000 < 64) win[t / 1000] += s;
	}
	for (int i = 0; i < 64; i++)
		if (win[i] > *worst) *worst = win[i];
	return t;
}

int main(void)
{
	double worst;
	unsigned ms;

	CHECK(seek_step_for(true, 0, 300) == SEEK_TAP_S, "a tap is ten seconds");
	CHECK(seek_step_for(true, 5000, 300) == SEEK_TAP_S, "a tap is ten seconds whenever");

	/* The tester's song: "a 5 minute song in less than 2 seconds" */
	ms = cross(300, &worst);
	printf("  a 5:00 song: crossed in %.1f s, at most %.0f s of it a second\n", ms / 1000.0, worst);
	CHECK(ms >= 5000 && ms <= 12000, "a five-minute song takes several seconds, took %u ms", ms);
	/* 12 repeats can fall in one second of the clock, 11.1 on average */
	CHECK(worst <= 300 / 6.0 * 1.1, "never more than a sixth of the song a second, %.1f s", worst);

	/* A three-minute song and a ten-minute one: about as long each */
	ms = cross(180, &worst);
	printf("  a 3:00 song: crossed in %.1f s\n", ms / 1000.0);
	CHECK(ms >= 5000 && ms <= 12000, "a three-minute song takes several seconds, took %u ms", ms);
	ms = cross(600, &worst);
	printf("  a 10:00 song: crossed in %.1f s\n", ms / 1000.0);
	CHECK(ms >= 5000 && ms <= 12000, "a ten-minute song takes several seconds, took %u ms", ms);

	/* A short track: a step is never under a second */
	CHECK(seek_step_for(false, 400, 20) == 1, "a step is at least a second, %.2f",
	      seek_step_for(false, 400, 20));

	/* The steps grow as the hold goes on */
	CHECK(seek_step_for(false, 400, 300) < seek_step_for(false, 1500, 300) &&
	      seek_step_for(false, 1500, 300) < seek_step_for(false, 4000, 300),
	      "the steps grow with the hold");

	/* Eric's book, seventeen hours: as before, ten seconds, a minute, five */
	CHECK(seek_step_for(false, 400, 17 * 3600) == 10, "a book's first steps ten seconds");
	CHECK(seek_step_for(false, 1500, 17 * 3600) == 60, "a book's a minute after a second");
	CHECK(seek_step_for(false, 4000, 17 * 3600) == 300, "a book's five minutes after three");
	ms = cross(17 * 3600, &worst);
	printf("  a 17-hour book: crossed in %.1f s\n", ms / 1000.0);
	CHECK(ms <= 25000, "a seventeen-hour book in about twenty seconds, took %u ms", ms);

	/* A length not known yet: the steps of old */
	CHECK(seek_step_for(false, 400, 0) == 10 && seek_step_for(false, 1500, 0) == 60 &&
	      seek_step_for(false, 4000, 0) == 300, "no length: ten seconds, a minute, five");

	if (failures) {
		printf("seekstep: %d FAILED\n", failures);
		return 1;
	}
	printf("seekstep: ok\n");
	return 0;
}
