/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CONTROLS_H
#define ZYPEROS_CONTROLS_H

#include "menu.h"

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

/* Five pages, because six rows is what a panel holds under its heading and
 * the shelf's seven buttons do not fit on one: the pad and the shoulders move
 * you, the face buttons act on what you moved to. Which page of the five is
 * said at the heading's right, "2 / 5", as every panel of pages says it
 * (DESIGN.md 11.3); there is no footer. */
typedef enum {
	CTL_MOVING, CTL_SHELF, CTL_GAME, CTL_MUSE, CTL_ANYWHERE, CTL_PAGES
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

#define CTL_GAME_PAGES 4      /* the system's two at most, its own, In a game */

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
} ctl_game;

/* What a page's rows point into: its heading and the values a pair of rows
 * became ("Turbo A/B"), in the language that is on. The caller's, as
 * menu_row holds pointers. */
typedef struct {
	char head[48];
	char key[CTL_MAX_ROWS][16];
	char val[CTL_MAX_ROWS][64];
} ctl_buf;

/* How many pages this game has: always In a game, after the system's. */
int ctl_game_pages(const ctl_game *g);

/* Page `page`'s rows and its heading in b->head ("Controls: Genesis"); the
 * caller says which page of how many at the heading's right ("1 / 3", as
 * every panel of pages does, DESIGN.md 11.3). Out of range is the first
 * page. */
int ctl_game_rows(const ctl_game *g, int page, menu_row *out, ctl_buf *b);

/* Whether a system has a page of its buttons here: false for a system run by
 * an emulator of its own ("run:"), which has no in-game menu, and for one
 * this table does not know. */
bool ctl_game_known(const char *tag);

/* Two values said as one, the way the pages pair buttons: "Turbo A" and
 * "Turbo B" are "Turbo A/B", "L trigger" and "R trigger" "L/R trigger", the
 * same twice is itself, and anything else "C/B". Into out (cap bytes);
 * false when it does not fit. Pure, for the check. */
bool ctl_pair_value(const char *a, const char *b, char *out, int cap);

#endif
