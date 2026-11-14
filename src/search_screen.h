/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SEARCH_SCREEN_H
#define ZYPEROS_SEARCH_SCREEN_H

/* MENU > Search Games: the query on the keyboard, then what it finds
 * (src/search.h). The screen is src/search_screen.c. */
#include "app.h"

#include <stdbool.h>

/* True with the game picked in *shelf and *game: the menu is to close */
bool search_screen(app *a, int *shelf, int *game);

#endif
