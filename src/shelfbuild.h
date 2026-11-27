/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SHELFBUILD_H
#define ZYPEROS_SHELFBUILD_H

/* The shelves, built: the systems with games, Favorites, and MP4's card
 * with its albums; the card read at start and read again (Rescan, File
 * Transfer). Each is described in src/shelfbuild.c. */
#include "app.h"
#include "esmedia.h"
#include "savedlib.h"

#include <stdbool.h>

/* Every shelf read from the card again, keeping the system */
void rescan_all(app *a);
/* MP4's card alone, at the end of `a`'s systems: for a process with no other
 * shelf (src/mp4hand.h) */
void mp4_shelf_alone(app *a);

/* EmulationStation's folders (config/systems.cfg, es lines): each system's
 * folder set to the one its shelf reads, after a load of
 * systems.cfg: what the last read of the card chose (lib_folder_get) */
void shelf_folders_apply(systems_cfg *s);
/* The same choice made from the card, for a process with no shelves: the
 * computer's --scrape, --meta and --titles, whose folders are systems.cfg's.
 * Reads Roms/ and the folders of each system with EmulationStation names. */
void shelf_folders_from_card(systems_cfg *s);
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
/* After lib_check_run, on its thread: each folder's gamelist.xml, where
 * EmulationStation left one, into the card's library for its games with no
 * row there (es_import, src/esmedia.h). And after lib_check_apply, on the
 * frame's: `fn` once for each folder something was written for, with what,
 * for the shelves to show the names and favorites at once. */
void lib_check_import(lib_check *c);
void lib_check_imported(lib_check *c,
                        void (*fn)(const char *folder, const es_import_list *l, void *ctx), void *ctx);
void lib_check_free(lib_check *c);

/* MENU > System Order (2026-11-06, docs/design/mock/controls-lab.html,
 * DESIGN.md 11.29): the home's systems A to Z by name, the default, or in
 * systems.cfg's own order, by US release date. The setting read once at
 * start; the systems sorted once each time the card is read
 * (cfg_sort_systems), never per frame. */
extern bool g_order_release;
void system_order_load(void);
/* Game Names, Short or File Name, from the device's database into the
 * library (lib_names_set), kept under GAME_NAMES_KEY: "short" or "file" */
#define GAME_NAMES_KEY "names"
void game_names_load(void);
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
/* PortMaster's own cover (res/portmaster, DESIGN.md 11.33): whether
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
/* Splore on the PICO-8 shelf (src/pico8.h). p8_on_card: your PICO-8's files
 * on the card, as its shelf `s` asks them, now. splore_is_entry: whether `g`,
 * a game of system `sys`, is Splore's entry; splore_is_offer: and your
 * PICO-8 is not on the card, so A opens the page that says how to add it;
 * splore_cover_path: its own art for theme `theme_ix`, as pm_cover_path.
 * own_cover_path: either entry's (PortMaster's, Splore's), false for any
 * other game, which keeps its box art. pm_shelf_offers counts Splore's entry
 * too: it is never a game. */
bool p8_on_card(const system_cfg *s);
bool splore_is_entry(const app *a, int sys, const game_entry *g);
bool splore_is_offer(const app *a, int sys, const game_entry *g);
bool splore_cover_path(const app *a, int sys, const game_entry *g, int theme_ix,
                       char *out, size_t n);
bool own_cover_path(const app *a, int sys, const game_entry *g, int theme_ix,
                    char *out, size_t n);
/* Folders on a shelf (src/shelfdir.h). shelf_dirs_init: after a scan, the
 * system's games moved to `all` when any is in a folder, and the shelf on
 * the top's folders and games; shelf_dirs_free: `all` let go, the shelf on
 * the top. shelf_show_dir: the shelf on folder `dir`, or the nearest one up
 * that holds a game, its textures let go and its arrays made again; not
 * sorted (sort_shelf), its cursor kept in range. False with no folders, or
 * no memory, which leaves an empty shelf. */
void shelf_dirs_init(sysview *v);
void shelf_dirs_free(sysview *v);
bool shelf_show_dir(sysview *v, const char *dir);
/* The Favorites shelf rebuilt after a favorite changed */
void refresh_favorites_shelf(app *a);
/* Recents, the games played last (src/homesys.h), built again after a game
 * ended, keeping the shelf stood on */
void refresh_recents_shelf(app *a);
/* Collections (src/collect.h, DESIGN.md 11.56), the group after them, made
 * again after a collection or a game in one changed, keeping the shelf stood
 * on and its place in Collections */
void refresh_collections_shelf(app *a);
/* Screenshots (src/shotname.h, DESIGN.md 11.68), the group after them,
 * read again after a game that took a shot or a shot deleted, keeping the
 * shelf stood on and the shot it was on */
void refresh_shots_shelf(app *a);
/* Its index, or -1 when the home has none; and whether the card had a shot
 * at the last read, on the home or turned off it (Home Systems' row) */
int shelf_shots_at(const app *a);
bool shelf_shots_have(void);
/* The Collections shelf's index, or -1 when the home has none */
int shelf_coll_at(const app *a);
/* Recents' (src/homesys.h), the same */
int shelf_recents_at(const app *a);
/* How many games its collections hold on the shelves, each once: the home's
 * count under it */
int shelf_coll_games(void);
/* Shelf `s`, when it is Collections, on collection `dir`, or on its top
 * ("", or a collection with no game here any more): its list, owners and
 * textures made again, its cursor kept in range, not sorted (sort_shelf).
 * False when `s` is not Collections or there was no memory. */
bool shelf_coll_show(app *a, int s, const char *dir);
/* The groups' names, Favorites, Recents, Collections and Screenshots, in
 * the language that is on */
void shelf_groups_rename(app *a);
/* Every shelf built again from the saved library, keeping the place: MENU >
 * Display > Home Systems, after a change */
void shelves_reload(app *a);
/* The systems the last scan found games for, shown or not, in the home's
 * order: Home Systems's rows. Returns how many; *out the list. */
typedef struct {
	char tag[16];
	char name[CFG_STR];
} shelf_have;
int shelf_have_list(const shelf_have **out);
/* A game deleted from the card (src/gamedel.h): off its shelf and Favorites,
 * in place, the shelf stood on moved to the next game or the home */
void shelf_drop_game(app *a, const char *tag, const char *file);

/* A game renamed (src/gamern.h): its entry on its shelf under its new file
 * and name, in place, its shelf in order again and Favorites made again.
 * False when no shelf has it. */
bool shelf_rename_game(app *a, const char *tag, const char *file, const char *new_file,
                       const char *new_name);
/* MP4's shelf in the order its view is set to */
void mp4_order_view(sysview *v);
/* The Singles album's cover, made once there is a Singles */
void singles_cover(void);

/* The albums that are one folder of loose files, filed with the player's
 * answers: a step, from the main loop and the transfer screen's; whether any
 * is still waiting; and whether something moved, for the shelf to be read
 * again */
void mp4_file_albums(void);
bool flat_albums_waiting(void);
extern bool g_mp4_moved;

#endif
