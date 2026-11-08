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
 * That estimate needs two falls, because the first only marks where a percent
 * began (the reading at start is somewhere inside one): a few minutes after
 * the handheld starts, longer at a light load. So the first number comes from
 * the gauge instead (batt_gauge_left, batt_gauge_to_full): the charge it
 * holds in mAh over the current it is drawing or taking, which is there from
 * the first read, and which the fall-based number takes over from as it earns
 * trust (batt_eta_blend, batt_eta_weight): a rate read from falls is the
 * better long-run number, the gauge's current being a few seconds' average.
 * The cable clears the fall-based part, and so does a reading that rises with
 * no cable (a gauge correcting itself), since what was learned was about
 * another charge.
 *
 * What the corner writes in its place is batt_corner_kind's: the time, three
 * dots while there is no number yet (but one is coming), "On hold" at the
 * charge limit, "Charged" when full, or the percentage when none can come.
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
	uint32_t rated_at;   /* when the first rate was read */
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

/* ---- the gauge's own estimate, from the first read ------------------------- */

/* A current under this, in mA, is no current: a gauge at rest or one that
 * cannot say (a missing file reads 0), and the quotient would be days */
#define BATT_GAUGE_MIN_MA        20
/* The same on the cable, where the PMIC's own "not charging" is 50 mA
 * (CH_MA_CHARGING in charging.h) */
#define BATT_GAUGE_CHARGE_MIN_MA 50

/* Minutes of use left off the cable, from the charge in the battery and the
 * current it draws: `now_mah` and `full_mah` in mAh (-1 for unknown), `ma`
 * into the battery, so negative while it empties (the rk817's current_avg).
 * The charge under `floor_pct` of `full_mah` is not counted (where the
 * launcher powers off), as batt_eta_minutes does not. -1 with no charge
 * number, a current near zero or into the battery, a charge at or under the
 * floor, a charge well over full, or more than BETA_MAX_MIN; else at least 1. */
int batt_gauge_left(int now_mah, int full_mah, int ma, int floor_pct);

/* Minutes to full on the cable, or to `limit` percent when it is 1 to 99, at
 * the current `ma` into the battery. 0 when it is there already; -1 with no
 * charge numbers, a current under BATT_GAUGE_CHARGE_MIN_MA, or more than
 * BETA_MAX_MIN; else at least 1. A straight line: the charging screen's own
 * (src/charging.h) knows the CV tail, which the gauge's first read does not. */
int batt_gauge_to_full(int now_mah, int full_mah, int limit, int ma);

/* How much of the slower, steadier estimate to trust, 0 to 100: none until
 * it has a rate, then rising in a straight line over 2 x BETA_TAU_MS from the
 * first rate, so the shown time moves into it a little at a time */
int batt_eta_weight(const batt_eta *e, uint32_t now);

/* Percent along a ramp: 0 until `elapsed` passes `start` ms, then a straight
 * line to 100 over `span` ms. For the charging estimate, which has no state
 * of its own to say how sure it is: it settles for CH_SETTLE_MS from the
 * cable's coming and is blended in over as long again */
int batt_ramp(uint32_t elapsed, uint32_t start, uint32_t span);

/* The two minutes (-1 for none) as one: `slow` by `weight` percent, `quick`
 * by the rest; the one that exists when only one does, -1 when neither */
int batt_eta_blend(int quick, int slow, int weight);

/* ---- what the corner writes ------------------------------------------------ */

typedef enum {
	CORNER_PCT,    /* the percentage: no time can come */
	CORNER_CALC,   /* three dots: none yet, one is coming */
	CORNER_TIME,   /* the time, `minutes` of it */
	CORNER_HELD,   /* "On hold": the cable in and held at the charge limit */
	CORNER_FULL,   /* "Charged": the cable in and the battery full */
} corner_kind;

typedef struct {
	bool charging;   /* the cable in */
	bool full;       /* the gauge says full (read on the cable) */
	bool held;       /* held off at the charge limit */
	int  minutes;    /* the blended estimate, -1 for none */
	/* No estimate yet, and one is coming: off the cable above the floor, or
	 * on it with a current to read from, in its first seconds */
	bool expect;
} corner_in;

/* Full beats held (a full battery is not waiting on a limit), both beat a
 * time, a time beats the dots, and the dots beat the percentage. */
corner_kind batt_corner_kind(const corner_in *in);

/* The dots' frame, 0 to 3 filled, for a clock at `now` ms: a step every
 * CORNER_DOT_MS, so the animation asks for a frame 2 or 3 times a second, and
 * only while the dots are on screen */
#define CORNER_DOT_MS     400u
int batt_corner_dots(uint32_t now);

#endif
