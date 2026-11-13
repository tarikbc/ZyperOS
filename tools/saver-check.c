/* SPDX-License-Identifier: GPL-3.0-only */
/* Does the screensaver start when it should, end when it should, and never
 * hold the device awake?
 *
 *     make check-saver
 *
 * src/saver.h's policy, with no SDL and no device: the delay's ladder, when
 * it starts and what keeps it from starting (a button, music, a long task, a
 * low battery), when it ends on its own, how each clip fades and when the
 * next is asked for, and which is picked. The launcher's half - the loop's
 * passes, the daemon, the drawing - is src/main.c's saver_*, looked at in the
 * host shots and the VM.
 *
 * It runs Auto Off's real clock beside the screensaver's, with a loop's
 * passes at 8 ms, to hold the rule the feature is built on: the screensaver
 * starts before Auto Off when its delay is the shorter and never when it is
 * not, and Auto Off fires at its own time whether the screensaver is up or
 * not, because the screensaver does nothing to Auto Off's clock.
 */
#include <stdio.h>
#include <string.h>

#include "../src/idle.h"
#include "../src/saver.h"

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

/* The shelf's loop, run forward at 8 ms a pass with nothing pressed: when the
 * screensaver comes due, and when Auto Off does (-1 for never) */
static void run(saver_start *sv, idle_clock *ao, unsigned from, unsigned ms, int saver_s,
                bool held, bool charging, long *saver_at, long *off_at)
{
	unsigned t = from, el;

	*saver_at = *off_at = -1;
	for (el = 0; el < ms; el += 8, t += 8) {
		if (*off_at < 0 && idle_check(ao, t, false, charging)) *off_at = (long)el;
		if (*saver_at < 0 && saver_due(sv, t, saver_s, false, held)) *saver_at = (long)el;
	}
}

int main(void)
{
	saver_start sv;
	idle_clock ao;
	long sa, oa;
	int i;

	printf("saver: the screensaver's policy\n");

	/* ---- the ladder ---- */
	CHECK(saver_step(60, +1) == 120 && saver_step(120, +1) == 300 && saver_step(300, +1) == 600,
	      "right goes up the ladder");
	CHECK(saver_step(600, +1) == 600, "and stops at 10 minutes");
	CHECK(saver_step(60, -1) == 30 && saver_step(30, -1) == 0, "left goes down to off");
	CHECK(saver_step(0, -1) == 0, "and stops at off");
	CHECK(saver_step(0, +1) == 30, "off goes up to 30 s");
	CHECK(saver_step(45, +1) == 300 || saver_step(45, +1) == 120,
	      "a value off the ladder lands on a rung (%d)", saver_step(45, +1));
	CHECK(SAVER_DEFAULT_S == 60, "a minute to start with");

	/* ---- starting ---- */
	memset(&sv, 0, sizeof sv);
	CHECK(!saver_due(&sv, 1000, 60, false, false), "not at the first pass");
	{
		unsigned t = 1000;
		bool fired = false;

		for (i = 0; i < 8000 && !fired; i++, t += 8)
			fired = saver_due(&sv, t, 60, false, false);
		CHECK(fired, "starts after a minute with nothing pressed");
		CHECK(t - 1000 > 59000u && t - 1000 < 62000u, "at a minute (%u ms)", t - 1000);
	}

	/* a button restarts the countdown */
	memset(&sv, 0, sizeof sv);
	{
		unsigned t = 1000;
		bool fired = false;

		for (i = 0; i < 6000; i++, t += 8) saver_due(&sv, t, 60, false, false);   /* 48 s */
		saver_due(&sv, t, 60, true, false);                                         /* a press */
		t += 8;
		for (i = 0; i < 6000 && !fired; i++, t += 8)
			fired = saver_due(&sv, t, 60, false, false);                          /* 48 s more */
		CHECK(!fired, "a press 48 s in starts the minute again");
	}

	/* never: delay 0 */
	memset(&sv, 0, sizeof sv);
	{
		unsigned t = 1000;
		bool fired = false;

		for (i = 0; i < 50000 && !fired; i++, t += 8)
			fired = saver_due(&sv, t, 0, false, false);
		CHECK(!fired, "off is never");
	}

	/* held: music, File Transfer, a long task, a low battery */
	memset(&sv, 0, sizeof sv);
	{
		unsigned t = 1000;
		bool fired = false;

		for (i = 0; i < 20000 && !fired; i++, t += 8)
			fired = saver_due(&sv, t, 30, false, true);     /* 160 s held */
		CHECK(!fired, "never while something holds the device awake");
		for (i = 0; i < 3000 && !fired; i++, t += 8)
			fired = saver_due(&sv, t, 30, false, false);
		CHECK(!fired, "and a fresh 30 s after it ends (not yet at %u ms)", i * 8);
		for (i = 0; i < 1000 && !fired; i++, t += 8)
			fired = saver_due(&sv, t, 30, false, false);
		CHECK(fired, "then it comes");
	}

	/* a gap restarts it: a menu or a game in between */
	memset(&sv, 0, sizeof sv);
	{
		unsigned t = 1000;
		bool fired = false;

		for (i = 0; i < 6000; i++, t += 8) saver_due(&sv, t, 60, false, false);   /* 48 s */
		t += 30000;                                                                /* a game */
		for (i = 0; i < 5000 && !fired; i++, t += 8)
			fired = saver_due(&sv, t, 60, false, false);                          /* 40 s */
		CHECK(!fired, "time away from the shelf does not count");
	}

	/* ---- the battery ---- */
	CHECK(saver_low_battery(19, false) && !saver_low_battery(20, false), "under 20 percent on battery is low");
	CHECK(!saver_low_battery(5, true), "on the cable it is never low");
	CHECK(!saver_low_battery(-1, false), "an unreadable battery is no reason");

	/* ---- ending on its own ---- */
	CHECK(saver_must_end(5000, 1000, false, 80, false) == SAVER_GO_ON, "goes on");
	CHECK(saver_must_end(5000, 1000, true, 80, false) == SAVER_END_HELD, "a task or music ends it");
	CHECK(saver_must_end(5000, 1000, false, 12, false) == SAVER_END_BATTERY, "a low battery ends it");
	CHECK(saver_must_end(1000 + SAVER_BATTERY_MAX_MS, 1000, false, 80, false) == SAVER_END_CAP,
	      "on battery it ends at its cap");
	CHECK(saver_must_end(1000 + SAVER_BATTERY_MAX_MS - 1, 1000, false, 80, false) == SAVER_GO_ON,
	      "a moment before it");
	CHECK(saver_must_end(1000 + 5 * SAVER_BATTERY_MAX_MS, 1000, false, 80, true) == SAVER_GO_ON,
	      "on the cable it has no cap");
	CHECK(saver_must_end(0x10u, 0xfffffff0u, false, 80, false) == SAVER_GO_ON, "across the clock's wrap");

	/* spent: it waits for a button */
	memset(&sv, 0, sizeof sv);
	{
		unsigned t = 1000;
		bool fired = false;

		for (i = 0; i < 8000 && !fired; i++, t += 8) fired = saver_due(&sv, t, 60, false, false);
		CHECK(fired, "due");
		saver_spend(&sv);
		fired = false;
		for (i = 0; i < 20000 && !fired; i++, t += 8) fired = saver_due(&sv, t, 60, false, false);
		CHECK(!fired, "spent: not again by itself, however long");
		saver_due(&sv, t, 60, true, false);
		t += 8;
		for (i = 0; i < 8000 && !fired; i++, t += 8) fired = saver_due(&sv, t, 60, false, false);
		CHECK(fired, "a press ends being spent, and a minute later it comes");
	}

	/* ---- against Auto Off ---- */
	/* Auto Off at 2 minutes, the screensaver at 1: the screensaver first */
	memset(&sv, 0, sizeof sv);
	ao = (idle_clock){ .seconds = 120 };
	run(&sv, &ao, 1000, 200000, 60, false, false, &sa, &oa);
	CHECK(sa > 59000 && sa < 62000, "the screensaver at a minute (%ld)", sa);
	CHECK(oa > 119000 && oa < 122000, "and Auto Off at two, whatever the screensaver does (%ld)", oa);
	/* at the same time or longer: Auto Off first, the screensaver never shown on battery */
	memset(&sv, 0, sizeof sv);
	ao = (idle_clock){ .seconds = 60 };
	run(&sv, &ao, 1000, 200000, 120, false, false, &sa, &oa);
	CHECK(oa > 59000 && oa < 62000 && (sa < 0 || sa > oa),
	      "a delay longer than Auto Off's never beats it (saver %ld, off %ld)", sa, oa);
	/* Auto Off never: the screensaver still comes, and nothing ends it but the cap */
	memset(&sv, 0, sizeof sv);
	ao = (idle_clock){ .seconds = 0 };
	run(&sv, &ao, 1000, 100000, 60, false, false, &sa, &oa);
	CHECK(sa > 59000 && sa < 62000 && oa < 0, "with Auto Off at never the screensaver comes (%ld)", sa);
	/* on the cable: Auto Off's clock is held, the screensaver's is not */
	memset(&sv, 0, sizeof sv);
	ao = (idle_clock){ .seconds = 120 };
	run(&sv, &ao, 1000, 200000, 60, false, true, &sa, &oa);
	CHECK(sa > 59000 && sa < 62000 && oa < 0, "on the cable it comes, and Auto Off waits (%ld, %ld)", sa, oa);

	/* ---- a clip's life ---- */
	{
		saver_show s;
		float a = -1;
		unsigned t = 10000;
		bool next = false;

		saver_show_begin(&s, t, false);
		CHECK(!saver_show_step(&s, t, false, &a) && a == 0, "black while it loads");
		t += SAVER_LOAD_MS - 1;
		CHECK(!saver_show_step(&s, t, false, &a), "still waiting a moment before the timeout");
		t += 1;
		CHECK(saver_show_step(&s, t, false, &a), "a clip that never opens is skipped");

		saver_show_begin(&s, t, false);
		t += 300;
		saver_show_step(&s, t, true, &a);                 /* the first picture */
		CHECK(s.phase == SHOW_IN, "the picture starts the fade up");
		t += SAVER_IN_MS / 2;
		saver_show_step(&s, t, true, &a);
		CHECK(a > 0.5f && a < 1.0f, "halfway through the out curve it is past half (%.2f)", a);
		CHECK(saver_show_moving(&s), "a frame is wanted while it fades");
		t += SAVER_IN_MS;
		saver_show_step(&s, t, true, &a);
		CHECK(a == 1.0f && s.phase == SHOW_ON, "full, and shown");
		CHECK(!saver_show_moving(&s), "no frame wanted for a still picture");
		t += SAVER_VIDEO_MS - 1;
		saver_show_step(&s, t, true, &a);
		CHECK(s.phase == SHOW_ON, "shown for SAVER_VIDEO_MS");
		t += 1;
		saver_show_step(&s, t, true, &a);
		CHECK(s.phase == SHOW_OUT, "then it fades to black");
		t += SAVER_OUT_MS / 2;
		next = saver_show_step(&s, t, true, &a);
		CHECK(!next && a > 0.4f && a < 0.6f, "linearly (%.2f)", a);
		t += SAVER_OUT_MS;
		next = saver_show_step(&s, t, true, &a);
		CHECK(next && a == 0.0f, "and the next is asked for, once");

		saver_show_begin(&s, t, true);
		CHECK(s.on_ms == SAVER_STILL_MS && SAVER_STILL_MS < SAVER_VIDEO_MS, "a cover is shown for less");
	}

	/* ---- which one ---- */
	{
		saver_recent r;
		int seen[40], n = 0, k, picked;

		saver_recent_clear(&r);
		CHECK(saver_pick(0, 5, &r) == -1, "nothing to pick from");
		CHECK(saver_pick(1, 12345, &r) == 0, "one is the one");
		for (i = 0; i < 40; i++) seen[i] = 0;
		/* 30 in the pool: none of the last 12 comes round again */
		for (k = 0; k < 400; k++) {
			unsigned x = (unsigned)k * 2654435761u + 7u;
			int j;

			picked = saver_pick(30, x, &r);
			CHECK(picked >= 0 && picked < 30, "in the pool (%d)", picked);
			for (j = 0; j < r.n && j < SAVER_RECENT; j++)
				CHECK(r.ring[j] != picked, "%d was one of the last %d", picked, r.n);
			saver_recent_add(&r, picked);
			if (!seen[picked]) n++;
			seen[picked] = 1;
		}
		CHECK(n == 30, "every one comes up in time (%d of 30)", n);
		/* two in the pool: never the same twice running */
		saver_recent_clear(&r);
		picked = saver_pick(2, 0, &r);
		saver_recent_add(&r, picked);
		for (k = 0; k < 50; k++) {
			int next = saver_pick(2, (unsigned)k, &r);

			CHECK(next != picked, "a pool of two alternates");
			picked = next;
			saver_recent_add(&r, picked);
		}
	}

	if (failures) {
		printf("saver: %d failure(s)\n", failures);
		return 1;
	}
	printf("saver: ok\n");
	return 0;
}
