/* The d-pad as the left analog stick, for a system its games play with a
 * stick (RUN stick=dpad): the Nintendo 64's.
 *
 * Mupen64Plus-Next reads the N64's stick from libretro's left analog stick
 * and its D-pad from the joypad's, and a game like Super Mario 64 or Mario
 * Kart 64 moves only with the stick: on a handheld with no sticks (the GKD
 * Pixel 2) the d-pad reached the N64's D-pad and the game ignored it. So, in
 * this mode, the d-pad is the stick, at full deflection, and the core sees no
 * D-pad. RetroArch's "analog_dpad_mode = left analog" does the same, which is
 * what ROCKNIX and Knulli set for the N64.
 *
 * The N64's own D-pad stays in reach, for the few games that use it:
 *
 *   SELECT held, then the d-pad: the N64's D-pad, until SELECT is let go.
 *   A d-pad direction already held when SELECT goes down stays the stick,
 *   and so does the d-pad after a face button under the same SELECT: SELECT
 *   and a face button are the C buttons (map|N64), and turning the camera
 *   while running must not stop the run.
 *
 * Pure: no clock and no port, so test/stick_test.c drives it poll by poll,
 * as chord.h is driven. */
#ifndef DIATOM_STICK_H
#define DIATOM_STICK_H

#include <stdbool.h>
#include <stdint.h>

#include "diatom_port.h"

#define STICK_DPAD (DIATOM_BIT(DIATOM_BTN_UP) | DIATOM_BIT(DIATOM_BTN_DOWN) | \
                    DIATOM_BIT(DIATOM_BTN_LEFT) | DIATOM_BIT(DIATOM_BTN_RIGHT))

/* Full deflection, libretro's range (-0x8000..0x7fff), and a diagonal's share
 * of it on each axis: 0x7fff / sqrt(2), so a diagonal is as far from the
 * middle as a straight push, as on a round gate. Mupen64Plus-Next turns the
 * stick into polar form and scales the radius (its astick-deadzone and
 * -sensitivity), so a diagonal at 0x7fff on both axes would be 41% longer
 * than a straight push, past the N64's own range. */
#define STICK_FULL 0x7fff
#define STICK_DIAG 23170

typedef struct {
	bool armed;     /* SELECT is down, and nothing but SELECT has gone down */
	bool dpad;      /* the d-pad is the N64's D-pad until SELECT is let go */
} stick;

void stick_reset(stick *s);

/* One poll: `held` is the pad now, `was` at the last poll. True while the
 * d-pad is the game's D-pad (the SELECT rule above), false while it is the
 * stick. MENU is Diatom's and counts as no button. */
bool stick_turn(stick *s, uint32_t held, uint32_t was);

/* The stick for the d-pad in `held`: x right, y down, as libretro has them.
 * Opposite directions held together cancel. */
void stick_axes(uint32_t held, int16_t *x, int16_t *y);

#endif
