/* SPDX-License-Identifier: GPL-3.0-only */
/* Do long tasks hold Auto Off, and only while they work?
 *
 *     make check-taskhold
 *
 * src/taskhold.h's half: what a loop says (task_note) and how long it
 * holds. The other half, which jobs are asked rather than told, is
 * long_task_busy in src/zyperdraw.c, and `grep -n long_task_busy src/` lists
 * where the rule is read. What is held HERE is the thing that cannot be
 * allowed to go wrong, which is a hold that never lets go: a loop that ends
 * by any road out must stop holding by itself, because a handheld kept on
 * until the battery is flat is worse than the powered-off one this prevents.
 *
 * It also runs the real clock (src/idle.c) with the hold wired as idle_due
 * wires it, so "Auto Off does not fire during a task and comes back fresh
 * after it" is held end to end, not only in pieces.
 *
 * No SDL, no device. Time is a number this file chooses.
 */
#include <stdio.h>

#include "../src/idle.h"
#include "../src/taskhold.h"

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

/* A screen's loop run forward at 8 ms a pass, as the launcher's are. While
 * `working_ms` of it a task says it works; true if Auto Off fired, and when. */
static bool run(idle_clock *c, unsigned from, unsigned ms, unsigned working_ms,
                task_id task, unsigned *at)
{
	unsigned t = from, elapsed;

	for (elapsed = 0; elapsed < ms; elapsed += 8, t += 8) {
		if (elapsed < working_ms) task_note(task, t);
		if (idle_check(c, t, false, task_noted(t, NULL))) {
			if (at) *at = t;
			return true;
		}
	}
	return false;
}

int main(void)
{
	idle_clock c;
	unsigned t0 = 1000, at = 0;
	task_id which = TASK_COUNT;

	printf("taskhold: long tasks and Auto Off\n");

	/* Nothing noted: nothing held */
	task_reset();
	CHECK(!task_noted(5000, &which), "nothing is held before anything is said");

	/* A note holds, for a while, and lets go */
	task_note(TASK_STORAGE, 10000);
	CHECK(task_noted(10000, &which) && which == TASK_STORAGE, "a note holds at once");
	CHECK(task_noted(10000 + TASK_HOLD_MS, NULL), "and for its whole time");
	CHECK(!task_noted(10000 + TASK_HOLD_MS + 1, NULL), "then lets go by itself");

	/* A loop that stops saying it stops holding, whatever way it left */
	task_reset();
	task_note(TASK_TRANSFER, 20000);
	CHECK(!task_noted(20000 + 60000, NULL), "a loop that ended holds nothing a minute on");

	/* Two tasks: the one said last is named */
	task_reset();
	task_note(TASK_COVERS, 30000);
	task_note(TASK_STORAGE, 30500);
	CHECK(task_noted(30600, &which) && which == TASK_STORAGE, "the latest is named");
	CHECK(task_noted(30000 + TASK_HOLD_MS, NULL), "one of them still holds after the first lets go");
	CHECK(!task_noted(30500 + TASK_HOLD_MS + 1, NULL), "and none after the last does");

	/* The clock wrapping, and a note a tick ahead of the reader */
	task_reset();
	task_note(TASK_COVERS, 0xfffffff0u);
	CHECK(task_noted(0x10u, NULL), "a note holds across the wrap");
	CHECK(!task_noted(0x10u + TASK_HOLD_MS + 1, NULL), "and lets go after it");
	task_reset();
	task_note(TASK_COVERS, 5010);
	CHECK(task_noted(5000, NULL), "a note a tick ahead of the reader is now, not ancient");

	/* The names are for the log */
	CHECK(task_name(TASK_TRANSFER)[0] && task_name(TASK_COVERS)[0] && task_name(TASK_STORAGE)[0],
	      "every task has a name");
	CHECK(task_name(TASK_COUNT)[0] == '?', "a task that is not one is a question mark");

	/* End to end: the real clock, 30 s of Auto Off, a task working for 40 s.
	 * Without the hold it would have fired at 30 s; with it, never while the
	 * task works, and a whole fresh 30 s once it stops. */
	task_reset();
	c = (idle_clock){ .seconds = 30 };
	CHECK(!run(&c, t0, 40000, 40000, TASK_STORAGE, &at),
	      "Auto Off does not fire while the task works, 40 s into a 30 s setting");
	CHECK(!run(&c, t0 + 40000, 29000, 0, TASK_STORAGE, &at),
	      "and not for 29 s after it, because the countdown started fresh");
	task_reset();
	c = (idle_clock){ .seconds = 30 };
	CHECK(!run(&c, t0, 40000, 40000, TASK_STORAGE, &at), "again, a task for 40 s");
	CHECK(run(&c, t0 + 40000, 35000, 0, TASK_STORAGE, &at),
	      "Auto Off comes back when the task ends");
	CHECK(at - (t0 + 40000) > 30000u - 3000u, "a whole countdown after it (fired %u ms on)",
	      at - (t0 + 40000));

	/* Control: no task, the clock fires at its time */
	task_reset();
	c = (idle_clock){ .seconds = 30 };
	CHECK(run(&c, t0, 40000, 0, TASK_STORAGE, &at), "with no task Auto Off fires");
	CHECK(at - t0 > 29000u && at - t0 < 32000u, "at 30 s (fired %u ms in)", at - t0);

	if (failures) {
		printf("taskhold: %d failure(s)\n", failures);
		return 1;
	}
	printf("taskhold: ok\n");
	return 0;
}
