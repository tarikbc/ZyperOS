/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MENUVOL_H
#define ZYPEROS_MENUVOL_H

/* MENU is MENU, and also the modifier that makes the volume keys brightness,
 * on a handheld with no brightness keys (its profile's DEV_BRIGHT_MENU_VOL):
 * the Pixel 2's FUNCTION, the Smart Pro's MENU. MENU plus volume is
 * brightness in games too, so the same chord does the same thing on both
 * sides of a launch.
 *
 * A key that is both cannot report MENU when it goes down, because nothing yet
 * says whether a volume key will follow. So MENU is reported when it comes UP,
 * unless a volume key came in between (the profile's menu_on_release). That
 * costs a tap's length before a menu opens, accepted on 2026-10-01; in games
 * MENU has always worked this way. Holding both volume keys with MENU is
 * holding both brightness keys, which is the Brick's pocket lock, so MP4's
 * lock works unchanged.
 *
 * One copy for every platform layer that has the chord (src/device/pixel2.c,
 * src/device/brick.c for the Smart Pro). Off (`on` false: the Brick, with F1
 * and F2), MENU and the volume keys are plain keys. */

#include <stdbool.h>

#include "../device.h"

typedef struct {
	bool      down;          /* MENU is held */
	bool      chorded;       /* and a volume key was used under it */
	bool      release_next;  /* a MENU tap reported last poll ends this one */
	in_button vol_as[2];     /* what each volume key went down as */
} menuvol;

#define MENUVOL_INIT { false, false, false, { IN_NONE, IN_NONE } }

/* MENU went down or up */
static inline void menuvol_menu(menuvol *m, in_state *st, bool down, bool on)
{
	if (!on) {
		m->down = down;
		plat_set_btn(st, IN_MENU, down);
		return;
	}
	if (down) {
		m->down = true;
		m->chorded = false;
	} else {
		m->down = false;
		if (!m->chorded) {
			plat_set_btn(st, IN_MENU, true);
			m->release_next = true;
		}
	}
}

/* A volume key went down or up: `up` for VOL+ */
static inline void menuvol_volume(menuvol *m, in_state *st, bool up, bool down, bool on)
{
	int k = up ? 0 : 1;

	if (down) {
		if (on && m->down) {
			m->vol_as[k] = up ? IN_BRIGHTUP : IN_BRIGHTDN;
			m->chorded = true;
		} else {
			m->vol_as[k] = up ? IN_VOLUP : IN_VOLDN;
		}
		plat_set_btn(st, m->vol_as[k], true);
	} else if (m->vol_as[k] != IN_NONE) {
		/* Released as whatever it went down as, so letting go of MENU
		 * first does not leave a brightness key stuck down. */
		plat_set_btn(st, m->vol_as[k], false);
		m->vol_as[k] = IN_NONE;
	}
}

/* At each poll's start: the MENU tap of the last one ends */
static inline void menuvol_poll(menuvol *m, in_state *st)
{
	if (m->release_next) {
		plat_set_btn(st, IN_MENU, false);
		m->release_next = false;
	}
}

#endif
