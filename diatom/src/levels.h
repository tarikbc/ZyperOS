/* Volume and brightness while a game runs.
 *
 * The level is the host's: which position each is at, stepping it from the
 * device's level keys with a held key repeating, and setting it from the
 * launcher on its own scale. The port is the hardware under it
 * (include/diatom_port.h).
 *
 * And the boost past the top of each (port/boost.h, ZyperOS's src/boost.h,
 * DESIGN.md 11.6): at the top, the up key held a second more, or pressed three
 * more times, opens cells past it - the volume's +2 dB of gain each in the
 * sound (src/audio.c), the brightness's a lift of the mid-tones in the
 * display's gamma table (the port's). Kept here beside the level, handed over
 * with it on the socket (SETLEVEL and LEVEL's boost=), and drawn by the port
 * from the look worked out here. */
#ifndef DIATOM_LEVELS_H
#define DIATOM_LEVELS_H

#include <stdbool.h>

#include "diatom_port.h"

/* Forget every position, and have the port forget what it last wrote: at each
 * handover, when the launcher may have moved either. The next read asks the
 * hardware. */
void diatom_levels_invalidate(void);

/* Where `kind` is, on this device's scale. False if the device has no control
 * over it, or the hardware cannot be read. */
bool diatom_levels_get(diatom_level_kind kind, int *index, int *count);

/* Put `kind` at `index` of `count` positions on the launcher's scale, rescaled
 * to this device's, with `boost` cells of its boost filled (0 none): open at
 * that many when the index is the top, closed otherwise. False if the device
 * has no control over it. */
bool diatom_levels_set(diatom_level_kind kind, int index, int count, int boost);

/* The boost cells filled past the top of `kind`, 0 when it is closed or open
 * and empty (an empty one closes with the bar anyway), for LEVEL's boost= */
int diatom_levels_boost(diatom_level_kind kind);

/* A count that moves with every level key press and repeat acted on, even
 * one at an end that moved nothing: MENU's chord on the Pixel 2 asks whether
 * one came while MENU was held. */
unsigned diatom_levels_presses(void);

/* The short buzz the boost gives as it opens, and a screenshot as it is taken
 * (main.c's shot_take, DESIGN.md 11.61): the motor at the game's rumble level
 * (diatom_rumble_buzz) for 40 ms, ended by the first frame after; nothing
 * with Rumble Off or on a handheld with no motor Diatom runs. */
void diatom_levels_buzz(void);

/* Once a frame: a level key's press steps its level, and holding it repeats at
 * the launcher's pace; the boost's clock, the bar's look handed to the port, a
 * plug in or out closing the volume boost. */
void diatom_levels_frame(void);

#endif /* DIATOM_LEVELS_H */
