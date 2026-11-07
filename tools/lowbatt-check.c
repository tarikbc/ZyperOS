/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* make check-lowbatt
 *
 * The low-battery start screen's rules (src/lowbatt.h): which start shows
 * it, the hold, POWER, the charger's moment, the fresh reading at the end,
 * the bar, and the clock's wrap. No device, no SDL. */
#include "../src/lowbatt.h"

#include <stdio.h>

#include "../src/design.h"
#include "../src/status.h"

static int failures;

#define CHECK(cond, what) do { \
	if (!(cond)) { printf("  FAIL: %s (line %d)\n", what, __LINE__); failures++; } \
} while (0)

static const bool YES = true, NO = false;

/* Ticks every 16 ms from `from` to `to` with nothing happening; the last step */
static lb_step idle(lowbatt *l, unsigned from, unsigned to)
{
	lb_step s = l->step;

	for (unsigned t = from; t != to; t += 16) {
		s = lowbatt_tick(l, t, false, false, NULL);
		if (to - t < 16) break;
	}
	return s;
}

static void at_base(unsigned base)
{
	lowbatt l;
	lb_step s;

	/* The hold, with no reading asked for before its end */
	lowbatt_start(&l, base);
	s = idle(&l, base, base + DS_MS_HOLD_LOW_BATTERY - 16);
	CHECK(s == LB_WAIT, "waits through the hold");
	CHECK(!lowbatt_needs_reading(&l, base + DS_MS_HOLD_LOW_BATTERY - 1), "no reading before the end");
	CHECK(lowbatt_needs_reading(&l, base + DS_MS_HOLD_LOW_BATTERY), "a reading at the end");
	CHECK(lowbatt_tick(&l, base + DS_MS_HOLD_LOW_BATTERY, false, false, NULL) == LB_WAIT,
	      "no verdict without the reading");
	CHECK(lowbatt_tick(&l, base + DS_MS_HOLD_LOW_BATTERY, false, false, &YES) == LB_POWER_OFF,
	      "still empty: powers off");
	CHECK(lowbatt_tick(&l, base + DS_MS_HOLD_LOW_BATTERY + 5000, false, true, NULL) == LB_POWER_OFF,
	      "a verdict stays");

	/* A first reading that was a stray one */
	lowbatt_start(&l, base);
	CHECK(lowbatt_tick(&l, base + DS_MS_HOLD_LOW_BATTERY + 3, false, false, &NO) == LB_GO_ON,
	      "a fresh reading over the line goes on to the home");

	/* POWER */
	lowbatt_start(&l, base);
	CHECK(lowbatt_tick(&l, base + 100, true, false, NULL) == LB_POWER_OFF, "POWER powers off at once");
	lowbatt_start(&l, base);
	CHECK(lowbatt_tick(&l, base + 100, true, true, NULL) == LB_POWER_OFF,
	      "POWER and the cable in one pass: POWER");

	/* The charger */
	lowbatt_start(&l, base);
	idle(&l, base, base + 2400);
	CHECK(lowbatt_tick(&l, base + 2400, false, true, NULL) == LB_CHARGING, "the cable in: Charging");
	CHECK(lowbatt_tick(&l, base + 2400 + DS_MS_HOLD_SHORT - 1, false, true, NULL) == LB_CHARGING,
	      "Charging is held");
	CHECK(!lowbatt_needs_reading(&l, base + DS_MS_HOLD_LOW_BATTERY + 10), "charging asks no reading");
	CHECK(lowbatt_tick(&l, base + 2400 + DS_MS_HOLD_SHORT, false, true, NULL) == LB_GO_ON,
	      "then the start goes on");
	lowbatt_start(&l, base);
	lowbatt_tick(&l, base + 500, false, true, NULL);
	CHECK(lowbatt_tick(&l, base + 600, false, false, NULL) == LB_CHARGING,
	      "unplugged during the moment: the moment runs on");
	CHECK(lowbatt_tick(&l, base + 500 + DS_MS_HOLD_SHORT, false, false, NULL) == LB_GO_ON,
	      "and goes on; the home's battery_empty takes it from there");
	lowbatt_start(&l, base);
	CHECK(lowbatt_tick(&l, base + DS_MS_HOLD_LOW_BATTERY + 2, false, true, NULL) == LB_CHARGING,
	      "a cable in at the very end wins over the power-off");
	lowbatt_start(&l, base);
	lowbatt_tick(&l, base + 300, false, true, NULL);
	CHECK(lowbatt_tick(&l, base + 400, true, true, NULL) == LB_POWER_OFF, "POWER during Charging");

	/* The bar */
	lowbatt_start(&l, base);
	CHECK(lowbatt_spent(&l, base) == 0.0f, "full at the start");
	CHECK(lowbatt_spent(&l, base + DS_MS_HOLD_LOW_BATTERY / 2) > 0.49f &&
	      lowbatt_spent(&l, base + DS_MS_HOLD_LOW_BATTERY / 2) < 0.51f, "half way");
	CHECK(lowbatt_spent(&l, base + DS_MS_HOLD_LOW_BATTERY + 999) == 1.0f, "empty at the end and after");
}

int main(void)
{
	printf("lowbatt: which start shows it\n");
	CHECK(lowbatt_wanted(true, BATT_EMPTY_PCT, false), "at the line, off the charger");
	CHECK(lowbatt_wanted(true, 0, false), "at 0%");
	CHECK(!lowbatt_wanted(true, BATT_EMPTY_PCT + 1, false), "over the line: a normal start");
	CHECK(!lowbatt_wanted(true, 1, true), "charging: a normal start");
	CHECK(!lowbatt_wanted(false, 1, false), "no reading: a normal start");
	CHECK(!lowbatt_wanted(true, -1, false), "a nonsense reading: a normal start");
	CHECK(BATT_EMPTY_PCT < STATUS_BATT_CRITICAL, "the line is under the critical warning");

	printf("lowbatt: the screen\n");
	at_base(1000);
	printf("lowbatt: across the clock's wrap\n");
	at_base(0xFFFFFFFFu - 3000u);

	if (failures) {
		printf("lowbatt: %d FAILED\n", failures);
		return 1;
	}
	printf("lowbatt: ok\n");
	return 0;
}
