/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* When the shelf's cover turns into its video snap, and back.
 *
 *     make check-snapshelf
 *
 * The real src/snapshelf.c on a clock this file moves by hand: nothing is
 * asked before 3 s of a still focus, a scroll never asks, the cover turns
 * only once a picture is there, a move drops the snap at once and a menu
 * fades it back over quick, and the daemon is told to stop the same frame
 * either way - the rule that keeps decoding to what is on screen. */
#include <stdio.h>
#include <string.h>

#include "../src/design.h"
#include "../src/snaps.h"
#include "../src/snapshelf.h"

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

#define A "/card/Roms/SNES/.media/Super Metroid.mp4"
#define B "/card/Roms/SNES/.media/Star Fox.mp4"

static snapshelf s;
static float k;

static const char *at(const char *path, bool front, bool picture, unsigned now)
{
	return snapshelf_step(&s, path, front, picture, now, &k);
}

int main(void)
{
	unsigned t;

	printf("snapshelf: the cover and its video snap\n");

	printf("  nothing is asked before 3 s on a game:\n");
	memset(&s, 0, sizeof s);
	CHECK(!*at(A, true, false, 1000), "asked at once");
	CHECK(!*at(A, true, false, 1000 + SNAPS_WAIT_MS - 1), "asked before the wait was over");
	CHECK(!strcmp(at(A, true, false, 1000 + SNAPS_WAIT_MS), A), "not asked once it was");
	CHECK(k == 0, "the cover turned before any picture came (%.2f)", k);

	printf("  the cover turns once the first picture is there, over turn:\n");
	t = 1000 + SNAPS_WAIT_MS + 200;
	CHECK(!strcmp(at(A, true, false, t), A) && k == 0, "turned with no picture");
	at(A, true, true, t);
	CHECK(k == 0 && s.phase == SNS_IN, "the turn did not start from the cover");
	at(A, true, true, t + DS_MS_TURN / 2);
	CHECK(k > .3f && k < .7f, "halfway through turn it is %.2f", k);
	CHECK(snapshelf_moving(&s), "a turn in flight wants frames");
	at(A, true, true, t + DS_MS_TURN);
	CHECK(k == 1 && s.phase == SNS_PLAY, "not all the way at the end of turn (%.2f)", k);
	CHECK(!snapshelf_moving(&s), "at rest it wants no frames of its own");

	printf("  a menu over it: the daemon stops that frame, the cover is back in quick:\n");
	t += 2000;
	CHECK(!*at(A, false, true, t), "still asked for under a menu");
	CHECK(k == 1, "the cover jumped back (%.2f)", k);
	at(A, false, true, t + DS_MS_QUICK / 2);
	CHECK(k > .3f && k < .7f, "halfway back it is %.2f", k);
	at(A, false, true, t + DS_MS_QUICK);
	CHECK(k == 0 && s.phase == SNS_IDLE, "not back to the cover after quick (%.2f)", k);

	printf("  back on the shelf on the same game, the wait starts over:\n");
	t += 500;
	CHECK(!*at(A, true, false, t), "asked again at once");
	CHECK(!*at(A, true, false, t + SNAPS_WAIT_MS - 1), "asked again before the wait");
	CHECK(!strcmp(at(A, true, false, t + SNAPS_WAIT_MS), A), "not asked after it");

	printf("  the focus moves while it plays: gone at once, the next game waits:\n");
	t += SNAPS_WAIT_MS;
	at(A, true, true, t);
	at(A, true, true, t + DS_MS_TURN);
	CHECK(k == 1, "not playing (%.2f)", k);
	CHECK(!*at(B, true, true, t + DS_MS_TURN + 10), "the old snap or the new one asked at once");
	CHECK(k == 0, "the moved-away snap lingered on the new focus (%.2f)", k);
	CHECK(!strcmp(at(B, true, false, t + DS_MS_TURN + 10 + SNAPS_WAIT_MS), B),
	      "the new game was not asked after its own wait");

	printf("  a scroll never asks:\n");
	memset(&s, 0, sizeof s);
	for (t = 0; t < 20000; t += 200) {
		const char *got = at(t / 200 % 2 ? A : B, true, false, t);

		CHECK(!*got, "a snap was asked at %u ms of scrolling", t);
		if (*got) break;
	}

	printf("  the row still moving counts as not in front:\n");
	memset(&s, 0, sizeof s);
	at(A, false, false, 0);
	CHECK(!*at(A, false, false, SNAPS_WAIT_MS * 2), "asked while the row moved");
	CHECK(!*at(A, true, false, SNAPS_WAIT_MS * 2), "asked the moment it stopped");

	printf("  a game with no snap keeps its cover:\n");
	memset(&s, 0, sizeof s);
	CHECK(!*at("", true, true, 0) && !*at("", true, true, 60000) && k == 0,
	      "something was asked for a game with no snap");

	printf("  no daemon, no picture: the cover stays, nothing hangs:\n");
	memset(&s, 0, sizeof s);
	at(A, true, false, 0);
	CHECK(!strcmp(at(A, true, false, SNAPS_WAIT_MS + 60000), A) && k == 0,
	      "the cover turned into nothing");

	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("\nok: every check passed\n");
	return 0;
}
