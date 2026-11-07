/* SPDX-License-Identifier: GPL-3.0-only */
/* See chargedark.h. */
#include "chargedark.h"

#include <stdio.h>

/* True when the screen should go dark now: on the charger, Auto Off set, and
 * `seconds` without input. Off the charger the countdown is held at zero, so
 * plugging in starts it from the plug. `held` holds it too (music playing). */
static bool charge_dark_check(charge_dark *c, int seconds, unsigned now_ms,
                              bool input, bool charging, bool held)
{
	c->clock.seconds = seconds;
	/* Off the cable counts as input: nothing to darken, and the countdown
	 * starts from the plug rather than from the last button. */
	return idle_check(&c->clock, now_ms, input || !charging, held);
}

charge_dark_step charge_dark_poll(charge_dark *c, int seconds, unsigned now_ms,
                                  bool any_input, bool charging, bool held)
{
	if (c->dark) {
		if (!any_input && charging) return CHARGE_DARK;
		c->dark = false;
		/* The clock starts again from here, so the screen stays lit a whole
		 * Auto Off before it can go dark again. */
		charge_dark_check(c, seconds, now_ms, true, charging, held);
		fprintf(stderr, "charging: screen on (%s)\n",
		        any_input ? "a button" : "the cable came out");
		return CHARGE_WAKE;
	}
	if (!charge_dark_check(c, seconds, now_ms, any_input, charging, held))
		return CHARGE_LIT;
	c->dark = true;
	fprintf(stderr, "charging: screen off after %d s without a button\n", seconds);
	return CHARGE_GO_DARK;
}
