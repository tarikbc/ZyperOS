/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_SHELFBUILD_H
#define ZYPEROS_SHELFBUILD_H

/* The shelves, built: the systems with games, Favorites, and Muse's card
 * with its albums; the card read at start and read again (Rescan, File
 * Transfer). Each is described in src/shelfbuild.c. */
#include "app.h"
#include "savedlib.h"

#include <stdbool.h>

/* Every shelf read from the card again, keeping the system */
void rescan_all(app *a);
/* Every shelf built: each folder's listing from `saved` where it has one
 * (taken out of it), else off the card; `save` writes the saved library
 * after. How many came from `saved`. */
int scan_all_from(app *a, saved_lib *saved, bool save);
/* A start's scan: from the saved library when `use_saved` and there is one */
void scan_all_start(app *a, bool use_saved);

/* The card read again after a start, beside the shelf (main.c): begun on the
 * main thread once the shelf is up (NULL: nothing to check), run on any
 * thread - it reads the card and writes the saved library when the card is
 * not what the file says - and applied back on the main thread, which builds
 * the shelves again, keeping the place, when the card is not what they show.
 * True when it did. */
typedef struct lib_check lib_check;
lib_check *lib_check_begin(void);
void lib_check_run(lib_check *c);
bool lib_check_apply(app *a, lib_check *c);
void lib_check_free(lib_check *c);

/* MENU > System Order (2026-11-06, docs/design/mock/controls-lab.html,
 * DESIGN.md 11.29): the home's systems A to Z by name, the default, or in
 * systems.cfg's own order, by US release date. The setting read once at
 * start; the systems sorted once each time the card is read
 * (cfg_sort_systems), never per frame. */
extern bool g_order_release;
void system_order_load(void);
void system_order_apply(systems_cfg *s);
/* The Favorites shelf rebuilt after a favorite changed */
void refresh_favorites_shelf(app *a);
/* A game deleted from the card (src/gamedel.h): off its shelf and Favorites,
 * in place, the shelf stood on moved to the next game or the home */
void shelf_drop_game(app *a, const char *tag, const char *file);
/* Muse's shelf in the order its view is set to */
void muse_order_view(sysview *v);
/* The Singles album's cover, made once there is a Singles */
void singles_cover(void);

/* The albums that are one folder of loose files, filed with the player's
 * answers: a step, from the main loop and the transfer screen's; whether any
 * is still waiting; and whether something moved, for the shelf to be read
 * again */
void muse_file_albums(void);
bool flat_albums_waiting(void);
extern bool g_muse_moved;

#endif
