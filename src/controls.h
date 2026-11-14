/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CONTROLS_H
#define ZYPEROS_CONTROLS_H

#include "menu.h"
#include "portdoc.h"
#include "remap.h"

/* Every button, and what it does where: the one place ON THE DEVICE that says
 * so. Eric's, 2026-09-18.
 *
 * The controls that need it most are the ones no screen announces. SELECT
 * opens Muse from anywhere, the volume keys and F1/F2 work everywhere, and Y
 * sets the play mode on one Muse screen of four. Each of those is learned, or
 * read in a README that is not on the Brick. This page is where a player who
 * has only the handheld can look them up.
 *
 * The device is the source. The README's Controls section is a summary and
 * docs/guide.md is the long version, but this is what the hardware in the
 * player's hands says, and it is the copy that cannot be out of date by being
 * on a different device.
 *
 * Split out of main.c, the way sys_menu.c is: no SDL here, so
 * tools/controls-check.c can link it and read what these pages contain
 * without a renderer. */

/* Six pages, because six rows is what a panel holds under its heading and
 * the shelf's seven buttons do not fit on one: the pad and the shoulders move
 * you, the face buttons act on what you moved to. In a game has two, the
 * game's own keys and the hotkeys (SELECT and a shoulder, 2026-11-07). Which
 * page of the six is said at the heading's right, "2 / 6", as every panel of
 * pages says it (DESIGN.md 11.3); there is no footer. */
typedef enum {
	CTL_MOVING, CTL_SHELF, CTL_GAME, CTL_HOTKEYS, CTL_MUSE, CTL_ANYWHERE, CTL_PAGES
} ctl_page;

/* Room for the longest page and its footer, with a little spare. */
#define CTL_MAX_ROWS 10

/* What a page is called, under the heading, in the language that is on
 * (src/i18n.h); S_CONTROLS_HEADING is the heading's "Controls: %s". */
const char *ctl_page_name(ctl_page p);

/* One page's rows, any note included, and how many there are.
 * Every button the Brick has is named on some page, and no page names one
 * twice - what tools/controls-check.c holds this to. A button can appear on
 * two pages when it does two things: A opens a game on the shelf and pauses a
 * track in Muse. */
int ctl_rows(ctl_page p, menu_row *out);

/* Whether brightness is FUNCTION with the volume keys, on a device with no
 * brightness keys of its own (the Pixel 2, plat_brightness_on_volume): the
 * pages then name those where the Brick's say F1/F2, and Muse's lock is
 * FUNCTION with both. The Brick's names until it is set. */
void ctl_set_fn_volume(bool on);

/* Whether the device shows the battery on lights of its own and L2+R2 flashes
 * the level there (the Pixel 2, plat_battery_lights): the shelf's page then
 * says so. Not shown until it is set. */
void ctl_set_battery_lights(bool on);

/* ---------- in a game: the in-game menu's Controls row ---------------------
 *
 * Every control that works in the game being played, on its system and its
 * core (2026-11-06, picked in docs/design/mock/controls-lab.html): the
 * system's buttons first, a page of its own for what a system adds (the
 * Nintendo 64's C buttons, the DS's stylus), then In a game, the hotkeys.
 * Nothing that does not work right now. By button, as the home's pages are:
 * the handheld's button at the left, what it does at the right.
 *
 * The buttons are the core's own names for the RetroPad's, measured from
 * the pinned cores on 2026-11-06, put through the system's map| line and its
 * turbo exactly as Diatom is (src/resident.c): the map first, then the
 * turbo, a later pair winning. So the page cannot say what the pad does
 * not: a Neo Geo Pocket's X is a turbo B, because its core reads the A cap
 * as its B.
 *
 * Built when the page opens, never in a game. */

/* the system's two at most, its own, Hotkeys, In a game */
#define CTL_GAME_PAGES 5

typedef struct {
	const char *tag;          /* the system's, e.g. "MD" */
	const char *name;         /* its name, for the heading */
	const char *short_name;   /* the home's short label, for a long name */
	const char *map;          /* its map| spec, "" for none */
	const char *turbo;        /* its turbo map, NULL or "" for none */
	const char *ext;          /* the game file's extension, without the dot */
	/* Whether a heading fits beside the page count, measured with the font
	 * by the launcher; NULL counts characters instead (19 at most) */
	bool      (*fits)(void *ctx, const char *head);
	void       *ctx;
	/* A program of its own, not a game Diatom runs (OpenBOR,
	 * src/portmenu.h): its In a game page has no hotkeys, and POWER ends it */
	bool        port;
	/* What a Nintendo 64 game's controller holds, "Rumble Pak" or
	 * "Controller Pak", as the in-game menu's Pak row says it: the C
	 * buttons page says which, and where to change it. NULL is the Rumble
	 * Pak, ZyperOS's default. */
	const char *accessory;
	/* A port from the Ports shelf, PortMaster's own app among them: what its
	 * files say (src/portdoc.h). Its pages are then the port's and not a
	 * system's (below); NULL for anything else. The caller's, held for as
	 * long as the pages are. */
	const port_doc *doc;
	/* The pages the in-game menu's Controls tab opens from its Hotkeys row,
	 * beside the handheld: none of the system's buttons (the tab is where
	 * they are), and SELECT alone a row of Hotkeys ("SELECT | The game's")
	 * rather than its note, so the page fits beside the handheld */
	bool        no_buttons;
} ctl_game;

/* What a page's rows point into: its heading and the values a pair of rows
 * became ("Turbo A/B"), in the language that is on. The caller's, as
 * menu_row holds pointers. */
typedef struct {
	char head[48];
	char key[CTL_MAX_ROWS][16];
	char val[CTL_MAX_ROWS][64];
	char note[96];
} ctl_buf;

/* How many pages this game has: always In a game, after the system's.
 *
 * A port (ctl_game.doc) has its own, in this order, each only when its
 * files say it: what its README says the buttons do ("Controls: Wordle"),
 * the keys its gptokeyb keymap types for them ("Keys: Wordle"), then In a
 * game - MENU, SELECT+START when gptokeyb runs (PortMaster's quit chord),
 * the volume, the brightness, POWER - and last About, what port.json says
 * to copy where (inst) and what the port is (desc), as prose. PortMaster's
 * own app has its buttons ("Controls: PortMaster") and Anywhere's. */
int ctl_game_pages(const ctl_game *g);

/* Whether page `page` is prose (a port's About): its rows are MENU_BODY
 * rows of whole paragraphs, which the launcher wraps to the panel and lets
 * scroll, as a synopsis does, where every other page is a table that must
 * sit still */
bool ctl_game_prose(const ctl_game *g, int page);

/* Page `page`'s rows and its heading in b->head ("Controls: Genesis"); the
 * caller says which page of how many at the heading's right ("1 / 3", as
 * every panel of pages does, DESIGN.md 11.3). Out of range is the first
 * page. */
int ctl_game_rows(const ctl_game *g, int page, menu_row *out, ctl_buf *b);

/* Whether a system has a page of its buttons here: false for one this table
 * does not know, and for a system run by an emulator of its own ("run:") with
 * no in-game menu over it - every one but OpenBOR, which has the menu
 * (src/portmenu.h). */
bool ctl_game_known(const char *tag);

/* Whether a system's core keeps save states, so its in-game menu has a quick
 * slot and SELECT+R1 and SELECT+L1 save and load it: every system Diatom
 * runs but ScummVM and J2ME, whose cores keep none (FreeJ2ME-Plus's
 * retro_serialize_size is 0, read 2026-11-07). Diatom asks the core too, and
 * under SELECT gives a core with none its R1 and L1 (diatom/src/chord.h). */
bool ctl_has_states(const char *tag);

/* Two values said as one, the way the pages pair buttons: "Turbo A" and
 * "Turbo B" are "Turbo A/B", "L trigger" and "R trigger" "L/R trigger", the
 * same twice is itself, and anything else "C/B". Into out (cap bytes);
 * false when it does not fit. Pure, for the check. */
bool ctl_pair_value(const char *a, const char *b, char *out, int cap);


/* ---------- remapping: what a system offers (src/remap.h) --------------------
 *
 * The choices a button can be given on a system, from the same table the
 * pages are: the system's labelled targets, faces first (ordered by the
 * handheld cap that gives each under `map`, the system's map| spec, NULL or
 * "" for none, so the Dreamcast reads A, B, X, Y and the PlayStation circle,
 * cross, triangle, square), then the shoulders, Start and Select in the
 * order of the table, then Turbo of each face in the same order, then
 * Nothing. Into out, at most max; the count. 0 for a system this does not
 * know. */
int ctl_remap_choices(const char *tag, const char *ext, const char *map,
                      rm_choice *out, int max);

/* A choice as the core names it, in the language that is on: its name (shapes
 * for the PlayStation and the PSP), "Turbo %s", or the Nothing string. */
void ctl_remap_say(const char *tag, const char *ext, rm_choice c, char *out, size_t n);

/* The layout where each face cap gives the button it names, in letters, for
 * a system whose faces are lettered A, B, X, Y in the table and carry at
 * least two of them out of place: the Dreamcast, the Saturn, the Neo Geo
 * Pocket and its Color. letters is the identity with the faces written.
 * False for the rest (shapes, numbers, letters already in place). */
bool ctl_letter_layout(const char *tag, rm_table *letters);

#endif
