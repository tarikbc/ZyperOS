/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_STATS_SCREEN_H
#define ZYPEROS_STATS_SCREEN_H

/* MENU > Play Time: the games played, most played first (src/stats.h has
 * the sums). The screen is src/stats_screen.c. */
#include "app.h"

#include <stdbool.h>

/* True when it sent the player to a shelf, so the menu above it closes */
bool stats_screen(app *a);

#endif
