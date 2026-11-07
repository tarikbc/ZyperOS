/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "message.h"
#include "design.h"

void message_begin(message_time *m, unsigned now, int rows, unsigned arrival)
{
	*m = (message_time){ .start = now, .arrival = arrival,
	                    .hold = rows == 1 ? DS_MS_HOLD_SHORT : DS_MS_HOLD_LONG };
}

message_phase message_step(message_time *m, unsigned now, bool press,
                           bool back, bool power, bool quit)
{
	unsigned elapsed = now - m->start;

	if (quit) return MESSAGE_QUIT;
	if (power) return MESSAGE_POWER;
	if (m->closing)
		return press || now - m->close_at >= DS_MS_EXIT ? MESSAGE_DONE : MESSAGE_CLOSE;
	if (press && elapsed < m->arrival) m->arrival = elapsed;
	if (!back && elapsed < m->arrival + m->hold) return MESSAGE_SHOW;
	m->closing = true;
	m->close_at = back ? now : m->start + m->arrival + m->hold;
	return now - m->close_at >= DS_MS_EXIT ? MESSAGE_DONE : MESSAGE_CLOSE;
}
