/* SPDX-License-Identifier: GPL-3.0-only */
/* What makes way for a video snap at rest (src/snaprest.h): the Video
 * view's band (DESIGN.md 11.57) and the List view's details over the media
 * (11.39), one implementation.
 *
 *     make check-snaprest
 *
 * The real src/snaprest.c on a clock this file moves by hand. My pick in
 * the List View lab, 2026-11-23: "collapse the details in 2s so u can see
 * the full media". Up while the snap is not playing, however long; down
 * after hold-band (2 s) counted from the snap's start, not from the focus;
 * a press in the rest starts it again; once down, a press brings them up in
 * quick, and so does the snap going (a move, the dark screen); the frame
 * asked for is the end of the rest, then every frame of the slide. */
#include <stdint.h>
#include <stdio.h>

#include "../src/design.h"
#include "../src/snaprest.h"

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

static snaprest m;
static uint32_t wait;

static float at(bool playing, uint32_t now)
{
	return snaprest_step(&m, playing, now, &wait);
}

int main(void)
{
	const uint32_t t0 = 1000, play = 1500;   /* focused at t0, the snap's picture in at play */
	uint32_t t;

	printf("snaprest: the details over the media at rest\n");

	printf("  up until the snap plays, however long:\n");
	snaprest_reset(&m, t0);
	CHECK(at(false, t0) == 0, "not up as it is set");
	CHECK(at(false, t0 + DS_MS_QUICK) == 0 && wait == UINT32_MAX,
	      "a frame asked with no snap once quick is over (%u)", wait);
	CHECK(at(false, t0 + 60000) == 0, "down with no snap after a minute");

	printf("  down after hold-band from the snap's start:\n");
	snaprest_reset(&m, t0);
	CHECK(at(false, t0 + 100) == 0, "not up while the snap comes");
	CHECK(at(true, play) == 0 && wait == DS_MS_HOLD_BAND,
	      "the snap's start: up, the next frame hold-band on (%u)", wait);
	CHECK(at(true, play + DS_MS_HOLD_BAND - 1) == 0 && wait == 1,
	      "up just before hold-band, a frame at its end (%u)", wait);
	CHECK(DS_MS_HOLD_BAND == 2000, "hold-band is not Tarik's 2 s: %d", DS_MS_HOLD_BAND);
	CHECK(at(true, play + DS_MS_HOLD_BAND) == 0 && wait == 0,
	      "the slide down does not start at hold-band, a frame each (%u)", wait);
	{
		float half = at(true, play + DS_MS_HOLD_BAND + DS_MS_MOVE / 2);

		CHECK(half > .5f && half < 1 && wait == 0, "halfway through move, out: not past half and short of down (%g)", half);
	}
	CHECK(at(true, play + DS_MS_HOLD_BAND + DS_MS_MOVE) == 1 && wait == UINT32_MAX,
	      "not down after move, or still asking for frames (%u)", wait);
	CHECK(at(true, play + 60000) == 1, "not down a minute on");

	printf("  a press brings them up in quick, and starts the rest again:\n");
	t = play + 10000;
	snaprest_touch(&m, t);
	{
		float k = at(true, t + DS_MS_QUICK / 2);

		CHECK(k > 0 && k < .5f && wait == 0, "halfway through quick, not on the way up (%g)", k);
	}
	CHECK(at(true, t + DS_MS_QUICK) == 0, "not up after quick");
	CHECK(at(true, t + DS_MS_HOLD_BAND - 1) == 0, "down again before hold-band from the press");
	CHECK(at(true, t + DS_MS_HOLD_BAND) == 0 && wait == 0, "not going down hold-band after the press");
	CHECK(at(true, t + DS_MS_HOLD_BAND + DS_MS_MOVE) == 1, "not down again");

	printf("  a press during the rest puts the end of the rest off:\n");
	snaprest_reset(&m, t0);
	at(true, play);
	snaprest_touch(&m, play + 1500);
	CHECK(at(true, play + DS_MS_HOLD_BAND) == 0 && wait == 1500,
	      "went down at hold-band from the snap's start, not from the press (%u)", wait);
	CHECK(at(true, play + 1500 + DS_MS_HOLD_BAND) == 0 && wait == 0, "not going down hold-band after the press");
	CHECK(at(true, play + 1500 + DS_MS_HOLD_BAND + DS_MS_MOVE) == 1, "not down hold-band and move after the press");

	printf("  the snap going brings them up in quick (a move, the dark screen):\n");
	t = play + 20000;
	CHECK(at(false, t) == 1, "jumped up the frame the snap went");
	CHECK(at(false, t + DS_MS_QUICK) == 0, "not up quick after the snap went");
	printf("  ...and the next snap's start begins the rest again:\n");
	CHECK(at(true, t + 5000) == 0 && wait == DS_MS_HOLD_BAND, "the next snap's start did not begin the rest (%u)", wait);
	CHECK(at(true, t + 5000 + DS_MS_HOLD_BAND - 1) == 0, "down before hold-band from the next snap's start");

	printf("  the Screenshots viewer's A: down at once in move, the next press back up:\n");
	snaprest_reset(&m, t0);
	at(true, play);
	snaprest_rest(&m, play + 100);
	CHECK(at(true, play + 100 + DS_MS_MOVE) == 1, "not down move after A, short of hold-band");
	snaprest_touch(&m, play + 1000);
	CHECK(at(true, play + 1000 + DS_MS_QUICK) == 0, "not back up quick after the next press");
	CHECK(snaprest_at(&(snaprest){ 0 }, 5000) == 0, "a rest never set is not up");

	printf("snaprest: %s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
