/* The d-pad as the left analog stick: the rule is in stick.h. */
#include "stick.h"

#include <string.h>

#define SEL  DIATOM_BIT(DIATOM_BTN_SELECT)
#define MENU DIATOM_BIT(DIATOM_BTN_MENU)

void stick_reset(stick *s)
{
	memset(s, 0, sizeof *s);
}

bool stick_turn(stick *s, uint32_t held, uint32_t was)
{
	const uint32_t pressed = held & ~was;
	/* What else went down: a face button (a C button under SELECT), a
	 * shoulder (a hotkey under SELECT), START */
	const uint32_t others = pressed & ~(SEL | MENU | STICK_DPAD);

	if (!(held & SEL)) {
		s->armed = s->dpad = false;
		return false;
	}
	if (pressed & SEL) {
		/* A direction held from before SELECT is a run, not a D-pad press */
		s->armed = !(was & STICK_DPAD);
		s->dpad = false;
	}
	if (s->armed) {
		if (others) s->armed = false;
		else if (pressed & STICK_DPAD) {
			s->dpad = true;
			s->armed = false;
		}
	}
	return s->dpad;
}

void stick_axes(uint32_t held, int16_t *x, int16_t *y)
{
	int dx = ((held & DIATOM_BIT(DIATOM_BTN_RIGHT)) ? 1 : 0) -
	         ((held & DIATOM_BIT(DIATOM_BTN_LEFT))  ? 1 : 0);
	int dy = ((held & DIATOM_BIT(DIATOM_BTN_DOWN))  ? 1 : 0) -
	         ((held & DIATOM_BIT(DIATOM_BTN_UP))    ? 1 : 0);
	int16_t m = (dx && dy) ? STICK_DIAG : STICK_FULL;

	*x = (int16_t)(dx * m);
	*y = (int16_t)(dy * m);
}
