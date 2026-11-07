/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The low-battery start screen's rules. See src/lowbatt.h. */
#include "lowbatt.h"

#include <string.h>

#include "design.h"
#include "status.h"

bool lowbatt_wanted(bool valid, int pct, bool charging)
{
	return valid && !charging && pct >= 0 && pct <= BATT_EMPTY_PCT;
}

void lowbatt_start(lowbatt *l, unsigned now)
{
	memset(l, 0, sizeof *l);
	l->shown_at = now;
	l->step = LB_WAIT;
}

bool lowbatt_needs_reading(const lowbatt *l, unsigned now)
{
	return l->step == LB_WAIT && !l->charged && now - l->shown_at >= DS_MS_HOLD_LOW_BATTERY;
}

lb_step lowbatt_tick(lowbatt *l, unsigned now, bool power, bool charging, const bool *empty)
{
	/* Done is done: the caller acts on it once */
	if (l->step == LB_GO_ON || l->step == LB_POWER_OFF) return l->step;
	/* POWER means what it means everywhere, and before the charger: a press
	 * in the same pass as the cable going in still powers off */
	if (power) return l->step = LB_POWER_OFF;
	if (!l->charged && charging) {
		l->charged = true;
		l->charged_at = now;
	}
	/* Once seen, the charger's moment runs out whatever the cable does next:
	 * unplugged at once, the home's own battery_empty takes it from there */
	if (l->charged)
		return l->step = now - l->charged_at >= DS_MS_HOLD_SHORT ? LB_GO_ON : LB_CHARGING;
	if (now - l->shown_at < DS_MS_HOLD_LOW_BATTERY) return l->step = LB_WAIT;
	/* The end of the hold, with a fresh reading: still empty powers off; a
	 * reading that came up says the first was a stray one */
	if (!empty) return l->step = LB_WAIT;
	return l->step = *empty ? LB_POWER_OFF : LB_GO_ON;
}

float lowbatt_spent(const lowbatt *l, unsigned now)
{
	unsigned t = now - l->shown_at;

	if (t >= DS_MS_HOLD_LOW_BATTERY) return 1.0f;
	return (float)t / (float)DS_MS_HOLD_LOW_BATTERY;
}
