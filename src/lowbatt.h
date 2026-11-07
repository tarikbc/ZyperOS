/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The start with a battery too low to play: its rules (DESIGN.md 11.24).
 *
 * At BATT_EMPTY_PCT or under, off the charger, a start used to come up to the
 * home, or straight into a game, or the tutorial, and then battery_empty
 * powered the handheld off ten seconds later with nothing said. Now the first
 * screen says so: Zyper, "Battery too low", "Plug in a charger.", a bar that
 * empties over DS_MS_HOLD_LOW_BATTERY, and the power-off. A charger plugged
 * in meanwhile says "Charging." for DS_MS_HOLD_SHORT and the start goes on as
 * it would have; POWER powers off at once.
 *
 * In the launcher and not in plastron's splash: the launcher reads the
 * battery for the status corner before its first frame anyway, so a normal
 * start pays nothing for the question; it has the fonts, Zyper and the
 * themes, and the clean power-off that keeps the game a start would have
 * gone back into. The splash would need a system image, pictures drawn ahead
 * for every line and theme, and a gauge read in the first second of boot.
 *
 * One reading decides the screen; a second, fresh, at the end decides the
 * power-off, so one stray reading never powers the handheld off: a battery
 * that reads above the line then goes on to the home, where battery_empty
 * keeps watching.
 *
 * Pure: no SDL, the caller's milliseconds and readings, so
 * tools/lowbatt-check.c holds it. */
#ifndef ZYPEROS_LOWBATT_H
#define ZYPEROS_LOWBATT_H

#include <stdbool.h>

/* Whether a start shows the screen: a reading (`valid`) at or under
 * BATT_EMPTY_PCT, off the charger */
bool lowbatt_wanted(bool valid, int pct, bool charging);

typedef enum {
	LB_WAIT,       /* the screen, the bar emptying */
	LB_CHARGING,   /* "Charging.", then the start goes on */
	LB_GO_ON,      /* the start goes on: the home, a game, the tutorial */
	LB_POWER_OFF,  /* the power-off, now */
} lb_step;

typedef struct {
	unsigned shown_at;     /* when the screen came up */
	unsigned charged_at;   /* when the charger was first seen */
	bool     charged;      /* a charger seen while it showed */
	lb_step  step;
} lowbatt;

void lowbatt_start(lowbatt *l, unsigned now);

/* One pass at `now`: POWER pressed this pass, the cable as the corner reads
 * it, and `empty`, a fresh reading's verdict (lowbatt_wanted), which only
 * the end of the hold asks for: NULL until the caller is asked
 * (lowbatt_needs_reading). Returns the step, kept in l->step. */
lb_step lowbatt_tick(lowbatt *l, unsigned now, bool power, bool charging, const bool *empty);

/* True when the next tick decides the end of the hold and wants `empty` */
bool lowbatt_needs_reading(const lowbatt *l, unsigned now);

/* How far the bar has emptied, 0 full to 1 empty */
float lowbatt_spent(const lowbatt *l, unsigned now);

#endif
