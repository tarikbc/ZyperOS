/* SPDX-License-Identifier: GPL-3.0-only */
/* See taskhold.h. */
#include "taskhold.h"

static unsigned g_at[TASK_COUNT];
static bool     g_set[TASK_COUNT];

static const char *const NAME[TASK_COUNT] = {
	[TASK_TRANSFER] = "File Transfer",
	[TASK_COVERS]   = "album covers",
	[TASK_STORAGE]  = "Storage scan",
};

void task_note(task_id t, unsigned now_ms)
{
	if ((int)t < 0 || t >= TASK_COUNT) return;
	g_at[t] = now_ms;
	g_set[t] = true;
}

bool task_noted(unsigned now_ms, task_id *which)
{
	bool any = false;
	unsigned newest = 0;
	int t;

	for (t = 0; t < TASK_COUNT; t++) {
		unsigned age;

		if (!g_set[t]) continue;
		age = now_ms - g_at[t];
		/* A note a hair newer than `now` (a caller one tick behind) has an
		 * age near UINT_MAX and would read as ancient: it is "now". */
		if (age > 0x80000000u) age = 0;
		if (age > TASK_HOLD_MS) continue;
		if (!any || age < newest) {
			newest = age;
			if (which) *which = (task_id)t;
		}
		any = true;
	}
	return any;
}

const char *task_name(task_id t)
{
	return (int)t >= 0 && t < TASK_COUNT ? NAME[t] : "?";
}

void task_reset(void)
{
	int t;

	for (t = 0; t < TASK_COUNT; t++) {
		g_at[t] = 0;
		g_set[t] = false;
	}
}
