/* SPDX-License-Identifier: GPL-3.0-only */
/* A stick as the d-pad: the rule is in stickpad.h. */
#include "stickpad.h"

#include <stdlib.h>
#include <string.h>

/* The axis a held direction is on stays the one while it is at least this
 * share of the other, in percent: pushed exactly corner-ways, a stick does
 * not flip between up and right */
#define STICK_KEEP_PCT 80

int stickpad_centred(int value, int min, int max, int flat, int *range)
{
	const long mid = ((long)min + max) / 2, half = ((long)max - min) / 2;
	long v = (long)value - mid;

	if (flat < 0) flat = 0;
	*range = half > flat ? (int)(half - flat) : 0;
	if (*range == 0 || (v > -flat && v < flat)) return 0;
	v = v > 0 ? v - flat : v + flat;
	if (v > *range) v = *range;
	if (v < -*range) v = -*range;
	return (int)v;
}

void stickpad_update(stickpad *s, int x, int y, int max)
{
	int ax = abs(x), ay = abs(y), cur = -1, i, on, off, along, dir;
	bool horizontal;

	for (i = 0; i < STICK_DIRS; i++)
		if (s->held[i]) cur = i;
	memset(s->held, 0, sizeof s->held);
	if (max <= 0) return;
	on = max * STICK_ON_PCT / 100;
	off = max * STICK_OFF_PCT / 100;
	if (cur == STICK_LEFT || cur == STICK_RIGHT)
		horizontal = ax * 100 >= ay * STICK_KEEP_PCT;
	else if (cur == STICK_UP || cur == STICK_DOWN)
		horizontal = ay * 100 < ax * STICK_KEEP_PCT;
	else
		horizontal = ax >= ay;
	along = horizontal ? ax : ay;
	dir = horizontal ? (x < 0 ? STICK_LEFT : STICK_RIGHT) : (y < 0 ? STICK_UP : STICK_DOWN);
	if (along > on || (dir == cur && along >= off))
		s->held[dir] = true;
}
