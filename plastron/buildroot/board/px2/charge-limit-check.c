/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * Host checks for the GKD Pixel 2's charge limit: rk817_charge_limit and
 * RK817_CHARGE_LIMIT_GAP, taken out of the kernel patch as they stand
 * (patches/linux/9-px2-rk817-charge-limit.patch) into rule.c, and run here
 * against a battery made of the Pixel 2's numbers. plastron's
 * `make charge-limit-check` builds rule.c and runs this; no Docker, no
 * kernel, no device. Every check states the rule it holds, so a failure
 * says which one broke.
 */
#include <stdbool.h>
#include <stdio.h>

/* What of the driver's state the rule reads and writes */
struct rk817_charger {
	int soc;		/* thousandths of a percent, as the driver keeps it */
	int end_threshold;
	int start_threshold;
	bool held;
	bool plugged_in;
	void *dev;
	void *bat_ps;
	int input_writes;	/* rk817_set_input_limit, here only counted */
};

static void rk817_set_input_limit(struct rk817_charger *charger)
{
	charger->input_writes++;
}

static void power_supply_changed(void *ps)
{
	(void)ps;
}

#define dev_dbg(dev, ...) ((void)(dev))

#include "rule.c"

#ifndef ZYPEROS_CHARGE_LIMIT_GAP
#error "no CHARGE_LIMIT_GAP found in ZyperOS's src/device/pixel2.c"
#endif

static int fails, checks;

#define CHECK(cond, ...) do { \
	checks++; \
	if (!(cond)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

/* The battery: the Pixel 2's 1730 mAh (charge-full-design in its device
 * tree), and the level as the driver works it out of the coulomb counter
 * (rk817_read_props) and shows it (capacity, rounded to whole points) */
#define FCC_MAH 1730
#define TICK_S 8	/* rk817_charging_monitor's period */

static int soc_of(long long uah)
{
	return (int)(uah * 100 / FCC_MAH);
}

static int shown(int soc)
{
	return (soc + 500) / 1000;
}

/* The end written, and the start the driver puts under it */
static struct rk817_charger limit(int end)
{
	struct rk817_charger c = { 0 };

	c.end_threshold = end;
	c.start_threshold = end - RK817_CHARGE_LIMIT_GAP;
	c.plugged_in = true;
	return c;
}

static void at(struct rk817_charger *c, int soc)
{
	c->soc = soc;
	rk817_charge_limit(c);
}

/* Hours on the cable from 60%: charging at CHARGE_MA, held giving DRAIN_MA.
 * What the shown level did after the first hold, and how long the shortest
 * hold and the shortest top-up lasted. */
struct run {
	int lo, hi;		/* shown level after the first hold */
	int min_hold_s, min_charge_s;
	int topups;		/* charging stretches after the first hold */
	int hours;
};

static struct run on_the_cable(int end, int charge_ma, int drain_ma, int hours)
{
	struct rk817_charger c = limit(end);
	long long uah = (long long)FCC_MAH * 1000 * 60 / 100;
	struct run r = { 1000, -1, 1 << 30, 1 << 30, 0, hours };
	bool was = false, settled = false;
	int t, since = 0;

	for (t = 0; t < hours * 3600; t += TICK_S) {
		at(&c, soc_of(uah));
		if (c.held != was) {
			if (settled && was)
				r.min_hold_s = since < r.min_hold_s ? since : r.min_hold_s;
			else if (settled)
				r.min_charge_s = since < r.min_charge_s ? since : r.min_charge_s;
			if (settled && !c.held)
				r.topups++;
			settled = settled || c.held;
			was = c.held;
			since = 0;
		}
		if (settled) {
			int s = shown(c.soc);

			r.lo = s < r.lo ? s : r.lo;
			r.hi = s > r.hi ? s : r.hi;
		}
		uah += (long long)(c.held ? -drain_ma : charge_ma) * 1000 * TICK_S / 3600;
		since += TICK_S;
	}
	return r;
}

static void band(int end)
{
	/* A computer's port and a wall charger, and the 110 to 140 mA the held
	 * battery gave the system on a Pixel 2 (2026-11-06) */
	static const int charge[] = { 300, 1800 }, drain[] = { 110, 140 };
	int i, j;

	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++) {
			struct run r = on_the_cable(end, charge[i], drain[j], 12);

			CHECK(r.lo == end - 2 && r.hi == end,
			      "%d%%, %d mA in, %d mA out: shown between %d and %d (%d to %d)",
			      end, charge[i], drain[j], end - 2, end, r.lo, r.hi);
			CHECK(r.min_hold_s >= 5 * 60,
			      "%d%%, %d mA out: every hold lasts 5 min or more (%d s)",
			      end, drain[j], r.min_hold_s);
			CHECK(r.min_charge_s >= 2 * TICK_S,
			      "%d%%, %d mA in: every top-up lasts past the next reading (%d s)",
			      end, charge[i], r.min_charge_s);
			if (end == 80)
				printf("  80%%, %4d mA in, %d mA out: %d to %d%%, holds of %d min or more, "
				       "%.1f top-ups an hour of %d s or more\n",
				       charge[i], drain[j], r.lo, r.hi, r.min_hold_s / 60,
				       (double)r.topups / r.hours, r.min_charge_s);
		}
}

int main(void)
{
	struct rk817_charger c;
	int writes;

	printf("charge-limit-check: the band\n");
	CHECK(RK817_CHARGE_LIMIT_GAP == 2, "the start follows the end 2 under it (%d)",
	      RK817_CHARGE_LIMIT_GAP);
	CHECK(ZYPEROS_CHARGE_LIMIT_GAP == RK817_CHARGE_LIMIT_GAP,
	      "ZyperOS writes the start the driver would put (%d, the driver %d)",
	      ZYPEROS_CHARGE_LIMIT_GAP, RK817_CHARGE_LIMIT_GAP);
	band(80);
	band(85);
	band(90);

	printf("charge-limit-check: the rule\n");
	c = limit(80);
	at(&c, 79499);
	CHECK(!c.held, "79.499%% (79 shown): still charging");
	at(&c, 79500);
	CHECK(c.held, "79.5%% (80 shown): held");
	at(&c, 79000);
	at(&c, 79600);
	at(&c, 78900);
	at(&c, 79500);
	CHECK(c.held, "a level that wavers between 79 and 80 stays held");
	at(&c, 78500);
	CHECK(c.held, "78.5%% (79 shown): still held");
	at(&c, 78499);
	CHECK(!c.held, "78.499%% (78 shown): charging again");
	at(&c, 79000);
	at(&c, 78000);
	at(&c, 79400);
	CHECK(!c.held, "a level that wavers between 78 and 79 keeps charging");

	c = limit(80);
	at(&c, 80000);
	writes = c.input_writes;
	at(&c, 80000);
	at(&c, 79000);
	CHECK(c.input_writes == writes, "a reading that changes nothing writes nothing");

	c = limit(80);
	at(&c, 85000);
	CHECK(c.held, "on the cable over the limit: held at once");
	c.plugged_in = false;
	at(&c, 85000);
	CHECK(!c.held, "the cable out: let go");
	c.plugged_in = true;
	at(&c, 79000);
	CHECK(!c.held, "the cable back in under the limit: charging");

	c = limit(100);
	at(&c, 100000);
	CHECK(!c.held, "an end of 100 is no limit");

	printf("charge-limit-check: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
