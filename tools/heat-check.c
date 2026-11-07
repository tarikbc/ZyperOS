/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* make check-heat
 *
 * "Getting hot" (src/heat.h): the trip, two readings in a row, the
 * hysteresis, the quiet minutes, a warning that waits for the home and one
 * that goes unsaid, the poll's clock and its wrap. No device, no SDL. */
#include "../src/heat.h"

#include <stdio.h>

static int failures;

#define CHECK(cond, what) do { \
	if (!(cond)) { printf("  FAIL: %s (line %d)\n", what, __LINE__); failures++; } \
} while (0)

/* Readings every HEAT_POLL_MS from `t`, `n` of them at `mc`; the time after */
static unsigned feed(heat_state *h, unsigned t, int n, int mc)
{
	for (int i = 0; i < n; i++, t += HEAT_POLL_MS) heat_note(h, t, mc);
	return t;
}

/* Feeds and takes at each reading, as the shelf does; how many were said */
static int run(heat_state *h, unsigned *t, int n, int mc)
{
	int said = 0;

	for (int i = 0; i < n; i++, *t += HEAT_POLL_MS) {
		heat_note(h, *t, mc);
		said += heat_take(h, *t);
	}
	return said;
}

static int fake_mc = 50000, reads;
static bool fake_ok = true;

static bool read_temp(int *mc)
{
	reads++;
	if (!fake_ok) return false;
	*mc = fake_mc;
	return true;
}

int main(void)
{
	heat_state h;
	unsigned t;

	printf("heat: the trip and two readings\n");
	heat_init(&h);
	t = 1000;
	CHECK(run(&h, &t, 100, 84999) == 0, "under the trip is never said");
	heat_note(&h, t, 85000);
	CHECK(!heat_take(&h, t), "one reading at the trip says nothing yet");
	t += HEAT_POLL_MS;
	heat_note(&h, t, 85000);
	CHECK(heat_take(&h, t), "the second reading in a row says it");
	CHECK(!heat_take(&h, t), "said once, not twice");

	printf("heat: a stray reading\n");
	heat_init(&h);
	t = 0;
	for (int i = 0; i < 20; i++) {
		heat_note(&h, t, i % 2 ? 70000 : 95000);
		CHECK(!heat_take(&h, t), "alternating readings never make two in a row");
		t += HEAT_POLL_MS;
	}
	heat_note(&h, t, -1);
	CHECK(!heat_take(&h, t), "no reading is not hot");

	printf("heat: staying hot says it once\n");
	heat_init(&h);
	t = 0;
	CHECK(run(&h, &t, 2, 90000) == 1, "said at the second reading");
	CHECK(run(&h, &t, 1000, 90000) == 0, "an hour and more at 90 C: not again");

	printf("heat: the hysteresis\n");
	heat_init(&h);
	t = 0;
	CHECK(run(&h, &t, 2, 86000) == 1, "said");
	/* 81 to 86 and back, for longer than the quiet minutes: never under 80,
	 * so never again */
	for (int i = 0; i < 200; i++)
		CHECK(run(&h, &t, 2, i % 2 ? 81000 : 86000) == 0, "swinging over 80 says nothing");
	CHECK(run(&h, &t, 1, 79999) == 0, "cooling under 80 says nothing");
	CHECK(run(&h, &t, 2, 85000) == 1, "hot again after cooling, past the quiet minutes");

	printf("heat: the quiet minutes\n");
	heat_init(&h);
	t = 0;
	CHECK(run(&h, &t, 2, 90000) == 1, "said");
	{
		unsigned said_at = t - HEAT_POLL_MS;
		int again = 0;
		unsigned when = 0;

		CHECK(run(&h, &t, 1, 60000) == 0, "cooled under 80 at once");
		while (t - said_at < HEAT_QUIET_MS + 3 * HEAT_POLL_MS) {
			heat_note(&h, t, 90000);
			if (heat_take(&h, t)) { again++; when = t; }
			t += HEAT_POLL_MS;
		}
		CHECK(again == 1, "said again once");
		CHECK(when - said_at >= HEAT_QUIET_MS, "not inside five minutes of the last");
		CHECK(when - said_at < HEAT_QUIET_MS + HEAT_POLL_MS, "and at the first reading after them");
	}

	printf("heat: waiting for the home\n");
	heat_init(&h);
	t = feed(&h, 0, 2, 88000);   /* due over a menu: nobody takes it */
	t = feed(&h, t, 10, 88000);
	CHECK(heat_take(&h, t), "back on the home, still hot: said there");
	heat_init(&h);
	t = feed(&h, 0, 2, 88000);
	t = feed(&h, t, 1, 84000);   /* cooled under the trip before the home */
	CHECK(!heat_take(&h, t), "cooled under the trip first: unsaid");
	t = feed(&h, t, 2, 88000);
	CHECK(heat_take(&h, t), "it never was said, so it is still armed");

	printf("heat: the poll's clock\n");
	heat_init(&h);
	reads = 0;
	fake_mc = 90000;
	CHECK(heat_poll(&h, 7, read_temp), "the first pass reads at once");
	CHECK(!heat_poll(&h, 7 + HEAT_POLL_MS - 1, read_temp), "not again before the interval");
	CHECK(reads == 1, "one read");
	CHECK(heat_poll(&h, 7 + HEAT_POLL_MS, read_temp), "again at the interval");
	CHECK(heat_take(&h, 7 + HEAT_POLL_MS), "two polls at 90 C say it");
	fake_ok = false;
	CHECK(!heat_poll(&h, 7 + 2 * HEAT_POLL_MS, read_temp), "a failed read is no reading");
	CHECK(h.mc == 90000, "and changes nothing");
	fake_ok = true;
	/* across the wrap of the millisecond clock */
	heat_init(&h);
	reads = 0;
	CHECK(heat_poll(&h, 0xFFFFFFFFu - 1000u, read_temp), "first read near the wrap");
	CHECK(!heat_poll(&h, 2000u, read_temp), "3 s later, past the wrap: not yet");
	CHECK(heat_poll(&h, 4000u, read_temp), "5 s later, past the wrap: read");
	CHECK(reads == 2, "two reads across the wrap");

	if (failures) {
		printf("heat: %d FAILED\n", failures);
		return 1;
	}
	printf("heat: ok\n");
	return 0;
}
