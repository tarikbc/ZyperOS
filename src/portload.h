/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_PORTLOAD_H
#define ZYPEROS_PORTLOAD_H

/* What the panel shows while a port loads (DESIGN.md 11.11), saved for
 * plastron's splash. Each is described in src/portload.c. */
#include "app.h"
#include "config.h"

#include <stdbool.h>

/* The screen for game `i` of the shelf, drawn; true when every line held
 * its contrast, false (said on stderr) fails --portload's shot */
bool port_loading_draw(app *a, const system_cfg *s, int i);
/* The still for the game under the cursor, written for splash */
void port_loading_screen(app *a, const system_cfg *s);

#endif
