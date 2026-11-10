/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PACE_H
#define ZYPEROS_PACE_H

/* When a panel screen draws: the frames a picture needs, and none more.
 *
 * The shelf has drawn only when something changed since 2026-09-13 (the
 * redraw clock, src/app.h), but the screens over it drew every pass at the
 * panel's 60 Hz: USB File Transfer left open with nothing moving kept the
 * launcher at 30% of a core, the GPU at 520 MHz and the memory at 1056 MHz,
 * and the Pixel 2's chip at 51 to 55 C against the shelf's 41 (2026-11-10, a
 * user's report of a handheld hotter than charging ever made it). MENU open
 * cost the same.
 *
 * So a panel screen's loop asks pace_due each pass. A frame comes when
 * anything that makes the picture different came: a press or a held button,
 * a change in what the screen shows (`key`, a hash of its rows and of every
 * moving thing's pose, which the screen makes), a moment the drawing asked
 * for (the redraw clock), or `busy`, something moving whose every frame
 * counts (files on their way, a panel opening, the volume bar). And once a
 * second regardless, as the shelf's heartbeat. Power saving never costs a
 * frame while something moves: when it moves, every pass draws.
 *
 * Between frames the loop waits for a button (plat_input_wait) for
 * pace_rest, so a press is drawn the moment it comes.
 *
 * And the memory clock: pace_ram_idle says when the screen has been still
 * long enough (PACE_RAM_IDLE_MS, the shelf's 2 s) for the memory's slower
 * clock, which costs a still screen nothing (the shelf's rule, 2026-11-04).
 *
 * All decisions, no device and no clock of its own, so tools/pace-check.c
 * holds them. Times are milliseconds on a clock that only goes forward;
 * every comparison is of a difference. */

#include <stdbool.h>

#include "menu.h"

#define PACE_HEARTBEAT_MS 1000u
#define PACE_RAM_IDLE_MS  2000u
#define PACE_NEVER        0xffffffffu   /* the redraw clock's REDRAW_NEVER */

typedef struct {
	bool     drawn;       /* a frame has been drawn */
	unsigned key;         /* the picture it showed */
	unsigned drawn_at;
	unsigned busy_at;     /* the last pass with something moving or pressed */
	bool     seen;        /* busy_at is set */
} pace;

/* A new screen: its first pass draws */
void pace_init(pace *p, unsigned now);

/* Whether this pass draws. `redraw_at` is the redraw clock's moment
 * (PACE_NEVER for none), `busy` a press, a held button or something whose
 * every frame counts. */
bool pace_due(pace *p, unsigned key, unsigned now, unsigned redraw_at, bool busy);

/* The frame was drawn, showing `key` */
void pace_drawn(pace *p, unsigned key, unsigned now);

/* How long to wait for a button before the next pass: until the redraw
 * clock's moment or the heartbeat, but no more than `cap` and at least 1 */
unsigned pace_rest(const pace *p, unsigned now, unsigned redraw_at, unsigned cap);

/* The memory's slower clock now: nothing pressed or moving for
 * PACE_RAM_IDLE_MS */
bool pace_ram_idle(const pace *p, unsigned now);

/* A panel's heading and rows in the key: every row's label and value, its
 * live flag, the count and the cursor. menu.h's marks (MENU_NOTE_MARK and
 * the rest, small addresses) count by their address and are never read: the
 * first build read them as text and stopped the launcher on the first steps
 * row with a hint (2026-11-10, on the device). */
unsigned pace_menu(unsigned h, const char *heading, const menu_row *rows, int n, int sel);

/* The key's hash, a piece at a time: a string (NULL counts), a number */
unsigned pace_str(unsigned h, const char *s);
unsigned pace_int(unsigned h, int v);

#endif
