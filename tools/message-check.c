/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The message clock, not a renderer: a slow frame cannot extend a hold,
 * B closes early, and power/quit win even on the last frame of an exit. */
#include "../src/message.h"
#include "../src/design.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
	message_time m;
	unsigned start = UINT_MAX - 100u;
	int rows;

	for (rows = 1; rows <= 2; rows++) {
		unsigned hold = rows == 1 ? DS_MS_HOLD_SHORT : DS_MS_HOLD_LONG;

		message_begin(&m, start, rows, DS_MS_EXIT + DS_MS_QUICK);
		assert(message_step(&m, start + 259, false, false, false, false) == MESSAGE_SHOW);
		assert(message_step(&m, start + 260 + hold - 1, false, false, false, false) == MESSAGE_SHOW);
		assert(message_step(&m, start + 260 + hold, false, false, false, false) == MESSAGE_CLOSE);
		assert(message_step(&m, start + 260 + hold + 139, false, false, false, false) == MESSAGE_CLOSE);
		assert(message_step(&m, start + 260 + hold + 140, false, false, false, false) == MESSAGE_DONE);
	}
	message_begin(&m, 0, 1, DS_MS_QUICK);
	assert(message_step(&m, 9000, false, false, false, false) == MESSAGE_DONE);
	assert(m.close_at == DS_MS_QUICK + DS_MS_HOLD_SHORT);
	/* An ordinary press settles arrival, but never dismisses the message. */
	message_begin(&m, 0, 1, DS_MS_EXIT + DS_MS_QUICK);
	assert(message_step(&m, 30, true, false, false, false) == MESSAGE_SHOW);
	assert(message_step(&m, 1629, false, false, false, false) == MESSAGE_SHOW);
	assert(message_step(&m, 1630, false, false, false, false) == MESSAGE_CLOSE);
	/* B during arrival, hold, and exit. No second hold or exit is booked. */
	for (unsigned at = 0; at <= 800; at += 40) {
		message_begin(&m, 0, 2, DS_MS_QUICK);
		assert(message_step(&m, at, true, true, false, false) == MESSAGE_CLOSE);
		assert(message_step(&m, at + 1, true, true, false, false) == MESSAGE_DONE);
	}
	for (unsigned at = 0; at <= 2500; at += 20) {
		message_begin(&m, 0, 2, DS_MS_QUICK);
		assert(message_step(&m, at, true, true, true, false) == MESSAGE_POWER);
		message_begin(&m, 0, 2, DS_MS_QUICK);
		assert(message_step(&m, at, true, true, true, true) == MESSAGE_QUIT);
	}
	message_begin(&m, 0, 1, DS_MS_QUICK);
	assert(message_step(&m, 1720, false, false, false, false) == MESSAGE_CLOSE);
	assert(message_step(&m, 1860, false, false, true, false) == MESSAGE_POWER);
	puts("ok: message holds, dismissal, clock wrap, slow frames, power and quit");
	return 0;
}
