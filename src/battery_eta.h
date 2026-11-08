/* SPDX-License-Identifier: GPL-3.0-only */
/* Time left on the battery, for MENU > Battery Icon > Time Left.
 *
 * The gauge answers in whole percent, so the estimate is read from how long
 * the percent takes to fall, not from the percent itself. Each time the
 * reading falls, the time since the last fall gives one rate (percent a
 * millisecond); the rate is kept as an exponential average over BETA_TAU_MS,
 * by the time each fall took, so a game starting or ending moves the estimate
 * over several falls and a single odd one cannot throw it. Between two falls
 * the part of a percent already gone is counted (the time left counts down
 * as the clock does, and a new reading barely moves it; the gauge reads the
 * nearest percent, so a reading that has just fallen is half a percent above
 * its number), and a fall that is
 * taking longer than the rate says slows the rate as it waits: the reading
 * has not fallen for X ms, so the rate is under 1 percent in X ms.
 *
 * The time is to power-off, not to 0%: at BATT_EMPTY_PCT the launcher saves
 * the game and turns the handheld off (src/status.h), so that is where the
 * charge a player can use ends. The caller says which percent.
 *
 * Nothing is shown until two falls have been seen, because the first only
 * marks where a percent began (the reading at start is somewhere inside one):
 * a few minutes after the handheld starts, longer at a light load. The cable
 * clears it all, and so does a reading that rises with no cable (a gauge
 * correcting itself), since what was learned was about another charge.
 *
 * Plain C, no SDL, no files; the caller gives the millisecond clock (a
 * uint32_t that wraps, as plat_now_ms is) and the readings, so
 * tools/battery-eta-check.c holds every rule. */
#ifndef ZYPEROS_BATTERY_ETA_H
#define ZYPEROS_BATTERY_ETA_H

#include <stdbool.h>
#include <stdint.h>

/* The average's time constant, 10 minutes: a fall takes 1.5 to 5 minutes at
 * the loads the handheld sees, so a change of load is mostly taken in after
 * three to five falls and each fall moves the estimate by about a quarter of
 * its own difference. */
#define BETA_TAU_MS      (10u * 60u * 1000u)
/* Two falls closer than this are one: the gauge was read late, and a rate of
 * more than 6 percent a minute is no battery's. The fall is kept and counted
 * in the next one. */
#define BETA_MIN_FALL_MS 10000u
/* More than a day is no estimate: the reading has barely moved */
#define BETA_MAX_MIN     (24 * 60)

typedef struct {
	bool     have;       /* a reading to compare the next with */
	int      pct;        /* the last reading */
	bool     edge;       /* a fall has been seen: edge_* mark where it began */
	uint32_t edge_at;    /* when the reading fell to edge_pct */
	int      edge_pct;
	bool     rated;      /* a rate has been read from two falls */
	double   rate;       /* percent a millisecond, smoothed */
} batt_eta;

/* One reading, at `now` ms, whenever one is made (the corner's poll): `pct`
 * from the gauge, or -1 for none (ignored: a failed read changes nothing),
 * and `charging` for the cable in. */
void batt_eta_note(batt_eta *e, uint32_t now, int pct, bool charging);

/* The minutes of use left at `now` before the reading reaches `floor_pct`,
 * or -1 when there is no estimate: fewer than two falls seen, the reading
 * already at the floor, or more than BETA_MAX_MIN. At least 1. */
int batt_eta_minutes(const batt_eta *e, uint32_t now, int floor_pct);

/* The minutes as the corner shows them, so a number changes when it means
 * something: to the minute under 10, to 5 minutes under 10 hours, and to the
 * hour from there. A multiple of 60 from 600 up, which the caller writes as
 * hours alone. */
int batt_eta_round(int minutes);

#endif
