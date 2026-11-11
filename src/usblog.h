/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_USBLOG_H
#define ZYPEROS_USBLOG_H

/* What happened on the Pixel 2's USB port, plug by plug, kept on the card
 * for support: `.userdata/pixel2/logs/usb.log`, readable over SSH and in
 * the logs File Transfer's Download logs button packs (src/logpack.h).
 *
 * One line a plug, the last USBLOG_MAX of them (a few KB), each grown as
 * the plug goes on: when it came, what it said (wants power, power in),
 * what was asked and answered, what the port did (powered, powered
 * automatically, a computer on the bus, Automatic paused), the replug
 * message, the computer's question and its answer, and how long it was in.
 *
 *   2026-11-11 14:02:33  plug 7: wants power, no power in · asked: USB
 *     Accessory · powered · out after 3 min 12 s
 *
 * (one line in the file). No trace of every reading: those are usbrole's
 * data build's (plastron), never a release's. Made from usbrole's state
 * (src/usbpower.h) as the launcher follows it, and the launcher's own notes.
 *
 * No SDL, no clock and no files of its own: tools/usblog-check.c feeds it
 * readings and reads its text. */

#include <stdbool.h>
#include <stddef.h>

#include "usbpower.h"

#define USBLOG_MAX  50
#define USBLOG_LINE 320

typedef struct {
	char     line[USBLOG_MAX][USBLOG_LINE];   /* oldest first */
	int      n;
	bool     open;          /* the last line's plug is still in */
	long long since;        /* when it came, unix seconds */
	unsigned plug;          /* usbrole's number for it; 0 for power in alone */
	bool     host, computer, paused, vbus;
	bool     dirty;         /* changed since the caller last saved it */
} usblog;

/* From the file's text: its lines, the last USBLOG_MAX; none open */
void usblog_load(usblog *l, const char *text);

/* usbrole's state now, at `now` (unix seconds): a new plug starts a line
 * (the pin going to "wants power", or power coming in with none), what the
 * port does is added to it, and the plug ending (no pin, no power) closes it
 * with its time in */
void usblog_follow(usblog *l, const usb_power *p, long long now);

/* The launcher's own word for the plug that is in: "asked: Computer",
 * "replug message", "Computer Connected: File Transfer". Nothing when no
 * plug is in. */
void usblog_note(usblog *l, const char *what);

/* The file's text, every line and a newline after each; its length */
size_t usblog_text(const usblog *l, char *out, size_t n);

#endif
