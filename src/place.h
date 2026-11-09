/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PLACE_H
#define ZYPEROS_PLACE_H

/* Where you were: the shelf and the game left, kept across the restart
 * after every game, and the game playing when the power went, for the next
 * start to go back into. Each is described in src/place.c. */
#include "app.h"

#include <stdbool.h>

void remember_place(app *a);
void restore_place(app *a);
/* The game being played, written as it starts and cleared as it ends */
void playing_set(app *a);
void playing_clear(void);
/* A deliberate power-off (POWER, Auto Off, the battery, the charging
 * screen) marks the record, so the next start continues that game; a crash
 * or a power cut leaves it unmarked, and the next start is the home */
void playing_mark_clean(void);
/* Back into the game a power-off left, when it has a state to resume */
bool playing_restore(app *a);

#endif
