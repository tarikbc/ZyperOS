/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_STICKPAD_H
#define ZYPEROS_STICKPAD_H

/* A stick as the d-pad, on the launcher's screens (the Smart Pro's left
 * stick, from tools/sppad.c): a direction is held once the stick is half way
 * to its end, and let go under a third of the way, so a stick resting near
 * the line does not flutter. Along one axis at a time, the one the stick is
 * further along: the launcher's lists and grids move one way per press, and
 * a stick pushed up and a little right is up. Held, a direction repeats as
 * the d-pad's does, since it is the same button held.
 *
 * x and y as evdev has them: + is right and down, -max to max. No SDL, so
 * tools/stickpad-check.c holds it. */

#include <stdbool.h>

/* The order of src/inbutton.h's first four, IN_LEFT to IN_DOWN */
enum { STICK_LEFT, STICK_RIGHT, STICK_UP, STICK_DOWN, STICK_DIRS };

#define STICK_ON_PCT  50   /* held from half the way */
#define STICK_OFF_PCT 33   /* let go under a third */

typedef struct {
	bool held[STICK_DIRS];
} stickpad;

/* The stick at (x, y) of -max..max: which directions are held now, in
 * s->held. The one held stays held while its axis is still the further one
 * and past the let-go line. */
void stickpad_update(stickpad *s, int x, int y, int max);

#endif
