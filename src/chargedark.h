/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_CHARGEDARK_H
#define ZYPEROS_CHARGEDARK_H

#include <stdbool.h>

#include "idle.h"

/* On the charger, Auto Off's time turns the screen off instead.
 *
 * Auto Off does not power a device down on the cable (the charger HOLDS its
 * clock, src/idle.h), so a Pixel 2 left on the shelf to charge kept its panel
 * lit for as long as it was plugged in: the backlight is the larger part of
 * what the shelf draws (Muse's lock measured 470 mA to 300 with it off,
 * 2026-09-30), and on the cable that is power the battery does not get.
 *
 * So after the same time without a button, on the cable, the backlight goes
 * off and the shelf stops drawing, while the side lights go on showing the
 * charge (plastron's gauge). Any button lights it again, the power button
 * too: a dark screen looks like a device that is off, and the press meant to
 * switch it on must not be the one that switches it off. That press does
 * nothing else. The cable coming out lights it as well, and Auto Off's own
 * countdown starts fresh from there, as it always has on unplugging.
 *
 * The shelf only, where a device is left to charge; Muse's lock already has
 * its own dark, and every other screen still powers down at Auto Off off the
 * cable as before. Music playing holds this clock as it holds Auto Off's.
 * Auto Off at never is never dark.
 *
 * The countdown is idle_check's arithmetic, so a gap in the calls (a game in
 * between) restarts it. All of it is decisions, with no device in it, so
 * tools/idle-check.c reaches it; the shelf's loop does what each answer says
 * (the backlight, a wait, a redraw). */

typedef struct {
	idle_clock clock;
	bool       dark;     /* the backlight is off for this */
} charge_dark;

/* What the shelf's loop does with this pass. */
typedef enum {
	CHARGE_LIT,      /* nothing: carry on with the pass */
	CHARGE_GO_DARK,  /* backlight off, and skip the pass */
	CHARGE_DARK,     /* still dark: wait a little, and skip the pass */
	CHARGE_WAKE      /* backlight on, the press spent: draw again */
} charge_dark_step;

/* One pass of the shelf's loop, after its input poll. `any_input` is any
 * button pressed or held this pass. Dark, every pass is skipped - the power
 * button's with it - until a button or the cable coming out wakes it. */
charge_dark_step charge_dark_poll(charge_dark *c, int seconds, unsigned now_ms,
                                  bool any_input, bool charging, bool held);

#endif
