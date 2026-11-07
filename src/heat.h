/* SPDX-License-Identifier: GPL-3.0-only */
/* "Getting hot": when to say that the chip has passed the point where the
 * kernel slows it down hard (DESIGN.md 11.23).
 *
 * The Pixel 2's kernel (px30.dtsi's soc-thermal and gpu-thermal) slows the
 * CPU and the GPU at two passive trips, 70 and 85 C, and powers off at its
 * critical trip, 115 C; the chip's own tsadc cuts it at 120 C, outside Linux.
 * ZyperOS said none of it: a game simply got slower. Now, at 85 C, the
 * second trip, where the slow-down is one a player sees, Zyper says so on
 * the home and a notice says so over a game.
 *
 * Read every HEAT_POLL_MS from the thermal zones (plat_chip_temp), the hotter
 * zone counting. Two readings in a row at or over HEAT_WARN_MC before it is
 * said, so one odd reading says nothing. Said again only once the chip has
 * cooled under HEAT_REARM_MC since (the hysteresis), and not before
 * HEAT_QUIET_MS from the last time (once in a few minutes at most, however
 * the temperature swings about the trip). Said only where it can be seen:
 * the caller takes it (heat_take) on the home, a games shelf or a running
 * game, and a warning due while a menu was open waits for one of them - and
 * goes, unsaid, if the chip cools under the trip first.
 *
 * Pure: no SDL, no files, the caller's milliseconds and readings, so
 * tools/heat-check.c holds it on a build machine. */
#ifndef ZYPEROS_HEAT_H
#define ZYPEROS_HEAT_H

#include <stdbool.h>

#define HEAT_WARN_MC   85000u   /* the kernel's second passive trip, in mC */
#define HEAT_REARM_MC  80000u   /* cooled this far before it is said again */
#define HEAT_POLL_MS   5000u    /* between two readings */
#define HEAT_QUIET_MS  300000u  /* five minutes between two warnings, at least */
#define HEAT_SEEN      2        /* readings in a row at the trip before it is said */

typedef struct {
	unsigned read_at;    /* when the zones were last read */
	unsigned shown_at;   /* when it was last said */
	int      mc;         /* the last reading, in mC, or -1 for none */
	int      hot;        /* readings in a row at or over HEAT_WARN_MC */
	bool     primed;     /* read at least once */
	bool     armed;      /* it may be said: cooled since the last time */
	bool     shown;      /* said at least once */
	bool     due;        /* said as soon as a caller can show it */
} heat_state;

/* Armed, nothing read */
void heat_init(heat_state *h);

/* One reading, in mC, at `now`: the hysteresis and the warning's due */
void heat_note(heat_state *h, unsigned now, int mc);

/* Reads with `read` once HEAT_POLL_MS has passed since the last reading (at
 * once the first time), and notes it; a failed read changes nothing. True
 * when a reading was taken. */
bool heat_poll(heat_state *h, unsigned now, bool (*read)(int *mc));

/* True once for each warning, when one is due: the caller shows it now. */
bool heat_take(heat_state *h, unsigned now);

#endif
