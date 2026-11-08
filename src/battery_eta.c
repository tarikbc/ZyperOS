/* SPDX-License-Identifier: GPL-3.0-only */
/* Time left on the battery. See src/battery_eta.h. */
#include "battery_eta.h"

#include <math.h>
#include <string.h>

void batt_eta_note(batt_eta *e, uint32_t now, int pct, bool charging)
{
	if (charging) {
		/* On the cable: what was learned is about a discharge that is over */
		memset(e, 0, sizeof *e);
		return;
	}
	/* No reading this time (a failed read of the gauge): nothing to learn
	 * from, and nothing forgotten either; the next one carries on */
	if (pct < 0) return;
	if (!e->have) {
		/* The reading at start is somewhere inside a percent, so it is no
		 * edge; the first fall after it is */
		e->have = true;
		e->pct = pct;
		return;
	}
	if (pct > e->pct) {
		/* Up with no cable: the gauge corrected itself. Start over from it */
		memset(e, 0, sizeof *e);
		e->have = true;
		e->pct = pct;
		return;
	}
	if (pct == e->pct) return;

	e->pct = pct;
	if (!e->edge) {
		e->edge = true;
		e->edge_at = now;
		e->edge_pct = pct;
		return;
	}
	{
		uint32_t dt = now - e->edge_at;   /* wraps with the clock; so does this */
		double r, k;

		/* Too soon after the last: the fall is counted in the next one,
		 * the edge staying where it was */
		if (dt < BETA_MIN_FALL_MS) return;
		r = (double)(e->edge_pct - pct) / (double)dt;
		if (!e->rated) {
			e->rated = true;
			e->rated_at = now;
			e->rate = r;
		} else {
			k = 1.0 - exp(-(double)dt / (double)BETA_TAU_MS);
			e->rate += (r - e->rate) * k;
		}
		e->edge_at = now;
		e->edge_pct = pct;
	}
}

int batt_eta_minutes(const batt_eta *e, uint32_t now, int floor_pct)
{
	double rate, gone, left, minutes;
	uint32_t waited;

	if (!e->have || !e->rated || !(e->rate > 0.0)) return -1;
	if (e->pct <= floor_pct) return -1;
	rate = e->rate;
	waited = now - e->edge_at;
	if (waited > 0x80000000u) waited = 0;   /* a clock read from before the fall */
	if (waited > 0) {
		/* No fall for `waited` ms: under a percent in that time. Past the
		 * point where the rate says one should have come, the rate gives
		 * way to that, by the weight the fall would have had */
		double cap = 1.0 / (double)waited;

		if (cap < rate) rate += (cap - rate) * (1.0 - exp(-(double)waited / (double)BETA_TAU_MS));
	}
	/* The gauge reads the nearest percent, so when it has just fallen to
	 * `pct` the charge is half a percent above it, and the part of a percent
	 * used since is taken off, never all of it */
	gone = rate * (double)waited;
	if (gone > 0.99) gone = 0.99;
	left = (double)(e->pct - floor_pct) + 0.5 - gone;
	minutes = left / rate / 60000.0;
	if (!(minutes <= (double)BETA_MAX_MIN)) return -1;
	return minutes < 1.0 ? 1 : (int)(minutes + 0.5);
}

int batt_eta_round(int minutes)
{
	if (minutes < 1) return 1;
	if (minutes < 10) return minutes;
	if (minutes < 600) return (minutes + 2) / 5 * 5;
	return (minutes + 30) / 60 * 60;
}

int batt_gauge_left(int now_mah, int full_mah, int ma, int floor_pct)
{
	double usable, minutes;

	if (now_mah < 0 || ma > -BATT_GAUGE_MIN_MA) return -1;
	if (full_mah > 0 && now_mah > full_mah + full_mah / 5) return -1;
	usable = now_mah;
	if (full_mah > 0 && floor_pct > 0) usable -= (double)full_mah * floor_pct / 100.0;
	if (usable <= 0.0) return -1;
	minutes = usable / (double)(-ma) * 60.0;
	if (!(minutes <= (double)BETA_MAX_MIN)) return -1;
	return minutes < 1.0 ? 1 : (int)(minutes + 0.5);
}

int batt_gauge_to_full(int now_mah, int full_mah, int limit, int ma)
{
	double need, minutes;

	if (now_mah < 0 || full_mah <= 0 || ma < BATT_GAUGE_CHARGE_MIN_MA) return -1;
	need = full_mah;
	if (limit > 0 && limit < 100) need = (double)full_mah * limit / 100.0;
	need -= now_mah;
	if (need <= 0.0) return 0;
	minutes = need / (double)ma * 60.0;
	if (!(minutes <= (double)BETA_MAX_MIN)) return -1;
	return minutes < 1.0 ? 1 : (int)(minutes + 0.5);
}

int batt_ramp(uint32_t elapsed, uint32_t start, uint32_t span)
{
	if (elapsed > 0x80000000u) return 0;   /* a clock read from before */
	if (elapsed <= start) return 0;
	if (span == 0 || elapsed - start >= span) return 100;
	return (int)((uint64_t)(elapsed - start) * 100u / span);
}

int batt_eta_weight(const batt_eta *e, uint32_t now)
{
	if (!e->have || !e->rated) return 0;
	return batt_ramp(now - e->rated_at, 0, 2u * BETA_TAU_MS);
}

int batt_eta_blend(int quick, int slow, int weight)
{
	if (weight < 0) weight = 0;
	if (weight > 100) weight = 100;
	if (quick < 0) return slow < 0 ? -1 : slow;
	if (slow < 0) return quick;
	return (int)(((long)slow * weight + (long)quick * (100 - weight) + 50) / 100);
}

corner_kind batt_corner_kind(const corner_in *in)
{
	if (in->charging && in->full) return CORNER_FULL;
	if (in->charging && in->held) return CORNER_HELD;
	if (in->minutes > 0) return CORNER_TIME;
	return in->expect ? CORNER_CALC : CORNER_PCT;
}

int batt_corner_dots(uint32_t now)
{
	return (int)(now / CORNER_DOT_MS % 4u);
}
