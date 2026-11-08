/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-battery-eta
 *
 * The time left on the battery (src/battery_eta.h) against a simulated
 * gauge: a charge falling at a chosen rate, read as the nearest whole percent
 * every 5 seconds, as battery_poll reads the real one. What it holds:
 *   - no estimate before two falls, and none on the cable;
 *   - a constant load gives the true time to power-off, within a few percent,
 *     whatever the clock's value, including across its wrap;
 *   - a change of load is followed, but no single fall moves the estimate by
 *     more than a fifth, and it counts down smoothly between falls;
 *   - a stall (no fall for much longer than the rate says) slows it;
 *   - a reading that rises with no cable, a missing gauge, a fall read late
 *     and two falls read at once do not make a wrong number;
 *   - the corner's rounding.
 * No device, no SDL, no real-time sleeps. */
#include "../src/battery_eta.h"

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(cond, what) do { \
	if (!(cond)) { printf("  FAIL: %s (line %d)\n", what, __LINE__); failures++; } \
} while (0)

#define FLOOR_PCT 2   /* BATT_EMPTY_PCT */

typedef struct {
	batt_eta e;
	uint32_t now;       /* ms, the clock the launcher reads */
	double   charge;    /* the true charge, percent */
	bool     cable;
	unsigned lcg;
} sim;

/* The gauge: the nearest percent */
static int reading(const sim *s) { return (int)(s->charge + 0.5); }

/* `secs` seconds at `per_min` percent a minute, read every `step` ms */
static void run(sim *s, double secs, double per_min, unsigned step)
{
	unsigned left = (unsigned)(secs * 1000.0);

	while (left) {
		unsigned dt = left < step ? left : step;

		s->now += dt;
		s->charge -= per_min * dt / 60000.0;
		left -= dt;
		batt_eta_note(&s->e, s->now, reading(s), s->cable);
	}
}

static void start(sim *s, uint32_t clock, double charge)
{
	*s = (sim){ .now = clock, .charge = charge };
	batt_eta_note(&s->e, s->now, reading(s), false);
}

/* The true minutes to power-off at this rate */
static double truth(const sim *s, double per_min)
{
	return (s->charge - FLOOR_PCT) / per_min;
}

static int mins(const sim *s) { return batt_eta_minutes(&s->e, s->now, FLOOR_PCT); }

static bool near(double got, double want, double tol)
{
	return fabs(got - want) <= want * tol + 0.51;
}

/* A constant load, from any clock: first no estimate, then the true one */
static void constant(uint32_t clock, double per_min, unsigned step)
{
	sim s;
	int first = -1, i;
	double worst = 0;

	start(&s, clock, 80.3);
	CHECK(mins(&s) < 0, "no estimate at start");
	/* until the second fall, whenever it comes */
	for (i = 0; i < 20000 && mins(&s) < 0; i++) run(&s, 1, per_min, step);
	first = mins(&s);
	CHECK(first > 0, "an estimate once two falls were seen");
	CHECK(near(first, truth(&s, per_min), 0.08), "the first estimate is the true time");
	/* and for the next two hours of falling, within 6% of the truth */
	for (i = 0; i < 7200 && s.charge > 10; i++) {
		double want, err;

		run(&s, 1, per_min, step);
		want = truth(&s, per_min);
		/* a minute of slack, for the whole minutes the corner counts in */
		err = fmax(fabs(mins(&s) - want) - 1.0, 0.0) / (want > 5 ? want : 5);
		if (err > worst) worst = err;
	}
	CHECK(worst < 0.06, "a constant load stays within 6% of the true time");
	if (worst >= 0.06) printf("    (%.2f %%/min from clock %u: worst %.3f)\n", per_min, clock, worst);
}

static void load_change(void)
{
	sim s;
	int i, before = 0, after, prev = -1, biggest = 0;
	double rate = 0.30;   /* 3 h 20 min a 1% in 200 s: a menu */

	start(&s, 1000, 90.2);
	run(&s, 15 * 60, rate, 5000);
	before = mins(&s);
	CHECK(near(before, truth(&s, rate), 0.06), "settled on the first load");
	/* a game starts: 0.9 percent a minute. The estimate follows, and no
	 * second of it moves by more than a fifth in one step. */
	for (i = 0; i < 40 * 60; i++) {
		int m;

		run(&s, 1, 0.9, 5000);
		m = mins(&s);
		if (prev > 0 && m > 0) {
			int d = m > prev ? m - prev : prev - m;

			if (d * 5 > prev + 5 && d > biggest) biggest = d;
		}
		prev = m;
		if (s.charge <= 25) break;
	}
	CHECK(biggest == 0, "no second moves the estimate by a fifth");
	after = mins(&s);
	CHECK(near(after, truth(&s, 0.9), 0.15), "after the change it is near the new load's time");
	CHECK(after < before / 2, "the heavier load shows much less time");
}

/* No single fall moves the number much: the estimate just before a fall and
 * the one just after it differ by little at a steady load with the gauge's
 * 5 s reading, and the clock itself counts it down in between */
static void smooth(void)
{
	sim s;
	int i, jumps = 0;
	int prev = -1;
	double per_min = 0.4;
	int at_t0 = 0, at_t1 = 0;

	start(&s, 5000, 70.1);
	run(&s, 20 * 60, per_min, 5000);
	for (i = 0; i < 1800; i++) {
		int m;

		run(&s, 1, per_min, 1000);
		m = mins(&s);
		if (prev > 0 && m > 0 && (m - prev > 2 || prev - m > 3)) jumps++;
		prev = m;
	}
	CHECK(jumps == 0, "the estimate falls with the clock, at most a few minutes at a time");

	/* a minute of waiting takes a minute off, near enough */
	start(&s, 5000, 60.1);
	run(&s, 12 * 60, per_min, 5000);
	at_t0 = mins(&s);
	run(&s, 60, per_min, 5000);
	at_t1 = mins(&s);
	CHECK(at_t0 - at_t1 >= 0 && at_t0 - at_t1 <= 3, "a minute of use is about a minute off the time");
}

static void stall(void)
{
	sim s;
	int at_fall, later;
	double per_min = 0.4;   /* a fall every 150 s */

	start(&s, 77, 70.1);
	run(&s, 15 * 60, per_min, 5000);
	/* hold the reading: the true charge stops falling (a lull, a gauge
	 * stuck) so no fall comes for 10 minutes */
	at_fall = mins(&s);
	{
		unsigned i;
		for (i = 0; i < 10 * 60 * 1000 / 5000; i++) {
			s.now += 5000;
			batt_eta_note(&s.e, s.now, s.e.pct, false);
		}
	}
	later = mins(&s);
	/* nothing fell for 10 minutes at a rate that says a fall every 2.5: the
	 * rate was too high, so the time left is more than it was, not less */
	CHECK(later >= at_fall + 30, "a stall pushes the time out");
}

/* Near the floor a percent is a large part of what is left, and the half a
 * percent the gauge's rounding hides is a minute or more: the estimate there
 * is within a minute of the truth */
static void low_end(double per_min)
{
	sim s;
	double worst = 0;
	int seen = 0;

	start(&s, 40, 20.3);
	while (s.charge > FLOOR_PCT + 1.2) {
		int m;

		run(&s, 1, per_min, 1000);
		m = mins(&s);
		if (m > 0) {
			double err = fabs(m - truth(&s, per_min));

			seen++;
			if (err > worst) worst = err;
		}
	}
	CHECK(seen > 100, "an estimate all the way down to the floor");
	CHECK(worst < 0.9, "within a minute of the true time near the floor");
	if (worst >= 0.9) printf("    (%.2f %%/min: worst %.2f min)\n", per_min, worst);
}

static void cable_and_gauge(void)
{
	sim s;
	int m;

	/* The cable: nothing on it, and a start from nothing after it */
	start(&s, 9, 60.1);
	run(&s, 12 * 60, 0.4, 5000);
	CHECK(mins(&s) > 0, "an estimate before the cable");
	s.cable = true;
	run(&s, 60, -0.8, 5000);
	CHECK(mins(&s) < 0, "none on the cable");
	s.cable = false;
	run(&s, 60, 0.4, 5000);
	CHECK(mins(&s) < 0, "none a minute after the cable came out");
	run(&s, 12 * 60, 0.4, 5000);
	m = mins(&s);
	CHECK(m > 0 && near(m, truth(&s, 0.4), 0.1), "an estimate again after a few falls");

	/* A reading that rises with no cable: start over from it */
	start(&s, 9, 60.1);
	run(&s, 12 * 60, 0.4, 5000);
	CHECK(mins(&s) > 0, "an estimate before the gauge corrects itself");
	batt_eta_note(&s.e, s.now + 5000, reading(&s) + 4, false);
	s.now += 5000;
	CHECK(mins(&s) < 0, "a reading that rises clears the estimate");

	/* A failed read of the gauge (-1) is ignored: it forgets nothing */
	start(&s, 9, 60.1);
	run(&s, 12 * 60, 0.4, 5000);
	m = mins(&s);
	batt_eta_note(&s.e, s.now + 1, -1, false);
	CHECK(batt_eta_minutes(&s.e, s.now, FLOOR_PCT) == m, "a failed read changes nothing");
	run(&s, 12 * 60, 0.4, 5000);
	CHECK(near(mins(&s), truth(&s, 0.4), 0.1), "and the estimate carries on after it");
	/* never any reading: nothing */
	{
		batt_eta none = { 0 };

		batt_eta_note(&none, 5, -1, false);
		CHECK(batt_eta_minutes(&none, 6, FLOOR_PCT) < 0, "no gauge at all, no estimate");
	}

	/* At or under the floor: powering off, there is no time to show */
	start(&s, 9, 12.1);
	run(&s, 12 * 60, 0.4, 5000);
	CHECK(mins(&s) > 0, "an estimate above the floor");
	run(&s, 30 * 60, 0.4, 5000);
	CHECK(s.e.pct <= FLOOR_PCT && mins(&s) < 0, "none at the floor");
}

static void odd_reads(void)
{
	sim s;
	int m;

	/* A fall read late: the gauge asked only every minute, then every 4, so
	 * each read finds one or two percent gone. The rate is the same, a
	 * little less sure, as it must be on so few falls. */
	start(&s, 3000, 80.1);
	run(&s, 30 * 60, 0.45, 60000);
	m = mins(&s);
	CHECK(m > 0 && near(m, truth(&s, 0.45), 0.10), "a gauge read every minute is as good");
	start(&s, 3000, 80.1);
	run(&s, 30 * 60, 0.45, 240000);
	m = mins(&s);
	CHECK(m > 0 && near(m, truth(&s, 0.45), 0.25), "a gauge read every 4 minutes is near");

	/* Two falls in the same second (a jittery read): the second is counted
	 * in the next, not as a rate of a hundred percent an hour */
	start(&s, 3000, 80.1);
	run(&s, 10 * 60, 0.3, 5000);
	batt_eta_note(&s.e, s.now + 2000, s.e.pct - 1, false);
	s.now += 2000;
	run(&s, 10 * 60, 0.3, 5000);
	m = mins(&s);
	CHECK(m > 0 && near(m, truth(&s, 0.3), 0.5), "two falls close together are not a rate");

	/* Barely moving: more than a day is none */
	start(&s, 3000, 90.1);
	run(&s, 4 * 3600, 0.0166, 5000);   /* 1% an hour: 88 hours */
	CHECK(mins(&s) < 0, "more than a day is no estimate");
	/* and 1% in 20 min, 3 h 40 min of it, is */
	start(&s, 3000, 40.1);
	run(&s, 2 * 3600, 0.05, 5000);
	m = mins(&s);
	CHECK(m > 0 && near(m, truth(&s, 0.05), 0.1), "a light load's time, long as it is");

	/* A clock read from before the fall is not a wrapped one */
	start(&s, 3000, 60.1);
	run(&s, 12 * 60, 0.4, 5000);
	m = batt_eta_minutes(&s.e, s.e.edge_at - 1000, FLOOR_PCT);
	CHECK(m > 0, "a clock a second before the fall still answers");
}

static void rounding(void)
{
	CHECK(batt_eta_round(0) == 1, "0 shows 1");
	CHECK(batt_eta_round(1) == 1, "1 is 1");
	CHECK(batt_eta_round(9) == 9, "to the minute under 10");
	CHECK(batt_eta_round(10) == 10, "10 is 10");
	CHECK(batt_eta_round(12) == 10, "12 is 10");
	CHECK(batt_eta_round(13) == 15, "13 is 15");
	CHECK(batt_eta_round(62) == 60, "62 is 60");
	CHECK(batt_eta_round(63) == 65, "63 is 65");
	CHECK(batt_eta_round(200) == 200, "to 5 under 10 hours");
	CHECK(batt_eta_round(597) == 595, "597 is 595");
	CHECK(batt_eta_round(598) == 600, "598 is 600: 10 hours");
	CHECK(batt_eta_round(629) == 600, "from 600 to the hour");
	CHECK(batt_eta_round(630) == 660, "630 is 660");
	CHECK(batt_eta_round(1440) == 1440, "a day is 24 hours");
	{
		int m;

		for (m = 1; m <= 1440; m++) {
			int r = batt_eta_round(m);

			if (r >= 600 && r % 60) { CHECK(0, "600 and up is whole hours"); break; }
			if (r < 600 && r >= 10 && r % 5) { CHECK(0, "10 to 599 is whole fives"); break; }
			if (r < 1) { CHECK(0, "never under 1"); break; }
		}
	}
}

int main(void)
{
	constant(0, 0.4, 5000);          /* a fall every 150 s */
	constant(0, 0.15, 5000);         /* a light load, a fall every 400 s */
	constant(0, 1.2, 5000);          /* a heavy one, every 50 s */
	constant(1234567, 0.5, 1000);    /* read every second */
	/* the clock's wrap, an hour and a half before it */
	constant(0xFFFFFFFFu - 90u * 60u * 1000u, 0.4, 5000);
	constant(0xFFFFFFFFu - 5000u, 0.4, 5000);
	load_change();
	smooth();
	stall();
	low_end(0.4);
	low_end(0.8);
	cable_and_gauge();
	odd_reads();
	rounding();

	if (failures) {
		printf("check-battery-eta: %d failure(s)\n", failures);
		return 1;
	}
	printf("check-battery-eta: ok\n");
	return 0;
}
