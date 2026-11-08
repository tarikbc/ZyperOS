/* SPDX-License-Identifier: GPL-3.0-only */
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
/* PortMaster on the Ports shelf (src/pminstall.h). g_pm_offer: the card's
 * ZyperOS has the installer and PortMaster is not on the card, as the last
 * scan or pm_offer_refresh found, so A on the shelf's PortMaster opens the
 * page that installs it. pm_on_card: PortMaster whole in that Ports folder,
 * its program and its script, asked of the card now. pm_is_offer: whether
 * `g`, a game of system `sys`, is that entry while it is offered;
 * pm_shelf_offers: 1 when shelf `sys` holds it so, else 0 (for a count of
 * games, which it is not); pm_only_offer: whether it is all the shelf holds,
 * a shelf with no ports on it yet, which the empty home does not count. */
extern bool g_pm_offer;
bool pm_on_card(const char *folder);
bool pm_offer_refresh(const app *a);
bool pm_is_offer(const app *a, int sys, const game_entry *g);
int  pm_shelf_offers(const app *a, int sys);
bool pm_only_offer(const app *a, int sys);
/* PortMaster's own cover (tools/genportmaster.py, DESIGN.md 11.33): whether
 * `g`, a game of system `sys`, is the Ports shelf's PortMaster entry, on the
 * card or not; and that entry's cover for theme `theme_ix` (DS_LIGHT or
 * DS_DARK), in the card's ZyperOS folder, into `out`, false when `g` is no
 * such entry or the file is not there (a card with no installer, the
 * Brick's). The shelf, Favorites, the port's loading screen and its details
 * all draw it, in place of any box art: no scrape fetches one for it. */
bool pm_is_entry(const app *a, int sys, const game_entry *g);
bool pm_cover_path(const app *a, int sys, const game_entry *g, int theme_ix,
                   char *out, size_t n);
/* The icon the Install PortMaster page draws where System Update's draws
 * Zyper, into `out`; false when the card has none */
bool pm_icon_path(char *out, size_t n);
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
