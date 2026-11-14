/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GAMERN_SCREEN_H
#define ZYPEROS_GAMERN_SCREEN_H

/* Rename (DESIGN.md 11.41, src/gamern.h has the plan): the details page's
 * row above Delete Game. The screen is src/gamern_screen.c. */
#include "app.h"

#include <stdbool.h>
#include <stddef.h>

/* The keyboard on the game's name, then the rename: true when it was
 * renamed; `said` the message either way, empty for none */
bool game_rename(app *a, int owner, const game_entry *g, char *said, size_t n);

#endif
