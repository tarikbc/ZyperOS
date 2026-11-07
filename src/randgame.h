/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* Random Game: one game picked from the shelves, every game as likely as
 * every other - not every shelf, or a system with three games would come up
 * as often as one with three hundred.
 *
 * MENU > Random Game on the systems row picks from every system; the same
 * row in a system's own menu picks from that system. What is not on a shelf
 * is never picked: an empty system is not on the row (hide_empty_systems),
 * and a file the scan leaves out is on no shelf. Muse and Favorites are
 * left out of the systems row's pick - Muse's cards are albums, and a
 * favorite is already on its own system's shelf, where it would otherwise
 * count twice.
 *
 * Pure, so tools/randgame-check.c holds it on a build machine; main.c's
 * random_game puts the shelf on the game and starts it. */
#ifndef ZYPEROS_RANDGAME_H
#define ZYPEROS_RANDGAME_H

#include <stdbool.h>
#include <stdint.h>

/* `count[i]` games on shelf i, which counts only where `use[i]`. `r` is any
 * random number. The pick is never (avoid_shelf, avoid_game) - the game
 * picked last time - while there is another game to give. False when no
 * shelf in use has a game. */
bool rg_pick(const int *count, const bool *use, int n, uint32_t r,
             int avoid_shelf, int avoid_game, int *shelf, int *game);

/* A random number, from the clock and a counter: no seed to keep, and two
 * presses in the same second still differ. */
uint32_t rg_random(void);

#endif
