/* SPDX-License-Identifier: GPL-3.0-only */
/* src/pace.h held: a still panel draws its first frame, then only its
 * heartbeat; anything moving or pressed draws every pass; a change in what
 * it shows, or the redraw clock's moment, draws once; the waits stop at the
 * heartbeat and the clock; the memory idles after 2 s still. Then the
 * frames a still File Transfer screen draws in ten minutes, against the
 * 36,000 it drew at 60 Hz on 2026-11-10. */
#include <stdio.h>

#include "../src/pace.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

int main(void)
{
	pace p;
	unsigned t, frames, wrap = 0xfffff000u;

	pace_init(&p, 1000);
	CHECK(pace_due(&p, 7, 1000, PACE_NEVER, false), "the first pass did not draw");
	pace_drawn(&p, 7, 1000);
	CHECK(!pace_due(&p, 7, 1016, PACE_NEVER, false), "a still picture drew again");
	CHECK(!pace_due(&p, 7, 1999, PACE_NEVER, false), "drew before the heartbeat");
	CHECK(pace_due(&p, 7, 2000, PACE_NEVER, false), "no heartbeat after 1 s");
	pace_drawn(&p, 7, 2000);
	CHECK(pace_due(&p, 8, 2010, PACE_NEVER, false), "a new picture did not draw");
	pace_drawn(&p, 8, 2010);
	CHECK(pace_due(&p, 8, 2020, PACE_NEVER, true), "something moving did not draw");
	pace_drawn(&p, 8, 2020);
	CHECK(pace_due(&p, 8, 2036, PACE_NEVER, true), "moving: a pass skipped");
	pace_drawn(&p, 8, 2036);
	CHECK(!pace_due(&p, 8, 2040, 2100, false), "the clock's moment drew early");
	CHECK(pace_due(&p, 8, 2100, 2100, false), "the clock's moment did not draw");
	pace_drawn(&p, 8, 2100);

	/* The waits: the cap, the clock, the heartbeat, never 0 */
	CHECK(pace_rest(&p, 2110, PACE_NEVER, 50) == 50, "rest %u, not the cap", pace_rest(&p, 2110, PACE_NEVER, 50));
	CHECK(pace_rest(&p, 2110, 2130, 50) == 20, "rest %u, not to the clock", pace_rest(&p, 2110, 2130, 50));
	CHECK(pace_rest(&p, 3090, PACE_NEVER, 50) == 10, "rest %u, not to the heartbeat", pace_rest(&p, 3090, PACE_NEVER, 50));
	CHECK(pace_rest(&p, 2110, 2000, 50) == 1, "rest past the clock's moment not 1");
	CHECK(pace_rest(&p, 5000, PACE_NEVER, 50) == 1, "rest past the heartbeat not 1");

	/* The memory: idle 2 s after the last busy pass, not sooner */
	pace_init(&p, 0);
	pace_due(&p, 1, 0, PACE_NEVER, true);
	CHECK(!pace_ram_idle(&p, 1999), "the memory idled before 2 s");
	CHECK(pace_ram_idle(&p, 2000), "the memory not idle after 2 s");
	pace_due(&p, 1, 2500, PACE_NEVER, true);
	CHECK(!pace_ram_idle(&p, 2600), "a press left the memory idle");

	/* A clock about to wrap */
	pace_init(&p, wrap);
	pace_drawn(&p, 1, wrap);
	CHECK(!pace_due(&p, 1, wrap + 500, PACE_NEVER, false), "drew early across the wrap");
	CHECK(pace_due(&p, 1, wrap + 1000, PACE_NEVER, false), "no heartbeat across the wrap");
	CHECK(pace_due(&p, 1, wrap + 300, wrap + 200, false), "the clock's moment missed across the wrap");

	/* Ten minutes of a still screen, a pass whenever the rest says, with a
	 * picture that changes once a minute (the corner's clock): about 600
	 * heartbeats and 10 changes, where 60 Hz drew 36,000 */
	pace_init(&p, 0);
	for (t = 0, frames = 0; t < 600000; ) {
		unsigned key = t / 60000;

		if (pace_due(&p, key, t, PACE_NEVER, false)) {
			pace_drawn(&p, key, t);
			frames++;
		}
		t += pace_rest(&p, t, PACE_NEVER, 50);
	}
	CHECK(frames >= 600 && frames <= 620, "a still screen drew %u frames in 10 min", frames);
	/* And one with files moving the whole time: every pass */
	pace_init(&p, 0);
	for (t = 0, frames = 0; t < 10000; t += 16) {
		if (pace_due(&p, 1, t, PACE_NEVER, true)) { pace_drawn(&p, 1, t); frames++; }
	}
	CHECK(frames == 625, "moving: %u frames of 625 passes", frames);

	/* A panel's rows with every one of menu.h's marks: hashed, never read
	 * (the address sanitizer is on in make check) */
	{
		menu_row rows[] = {
			{ "Cable", "\xe2\x9c\x93", true }, MENU_RULE, MENU_HR,
			{ "a hint under the steps", MENU_NOTE_MARK, false },
			{ "a body", MENU_BODY_MARK, false }, { NULL, NULL, false },
		};
		unsigned a = pace_menu(1, "Heading", rows, 6, 0);

		CHECK(a == pace_menu(1, "Heading", rows, 6, 0), "the same rows hash apart");
		CHECK(a != pace_menu(1, "Heading", rows, 6, 1), "the cursor is not in the key");
		rows[0].value = "waiting";
		CHECK(a != pace_menu(1, "Heading", rows, 6, 0), "a value is not in the key");
		CHECK(pace_menu(1, NULL, NULL, 0, -1) == pace_menu(1, NULL, NULL, 0, -1), "no rows: not the same key");
	}

	/* The key's pieces */
	CHECK(pace_str(1, "a") != pace_str(1, "b") && pace_str(1, NULL) != pace_str(1, ""),
	      "the key does not tell strings apart");
	CHECK(pace_int(1, 2) != pace_int(1, 3), "the key does not tell numbers apart");

	if (fails) {
		fprintf(stderr, "pace-check: %d failed\n", fails);
		return 1;
	}
	printf("pace-check: a still panel draws its heartbeat (610 frames in 10 min, not 36,000); "
	       "a change, the clock's moment or anything moving draws at once; the memory idles after 2 s\n");
	return 0;
}
