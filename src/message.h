/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MESSAGE_H
#define ZYPEROS_MESSAGE_H
#include <stdbool.h>

/* A result's clock only. main.c owns input and uses menumotion to draw it.
 * Arrival is the existing panel's lead plus open; a press settles that open.
 * All times are unsigned differences, including across clock wrap. */
typedef struct {
	unsigned start, arrival, hold, close_at;
	bool closing;
} message_time;

typedef enum {
	MESSAGE_SHOW, MESSAGE_CLOSE, MESSAGE_DONE, MESSAGE_POWER, MESSAGE_QUIT
} message_phase;

void message_begin(message_time *m, unsigned now, int rows, unsigned arrival);
message_phase message_step(message_time *m, unsigned now, bool press,
                           bool back, bool power, bool quit);
#endif
