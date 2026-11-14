/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CTLTAB_SCREEN_H
#define ZYPEROS_CTLTAB_SCREEN_H

/* The in-game menu's Controls tab on screen (src/ctltab.h is its rows): its
 * keys, a button's choices, Reset Buttons' question, the Hotkeys pages, and
 * the tutorial's handheld beside the rows (and a button's choices), lit for
 * the row under the cursor and moving as the design says. main.c hooks it into the in-game menu's
 * build, keys and panel (gm_menu_build, gm_key, menu_style's side).
 * Design: docs/superpowers/specs/2026-11-13-button-remap-design.md,
 * sections 2 to 8. */

#include <stdbool.h>
#include <stddef.h>

#include "app.h"
#include "config.h"
#include "ctltab.h"
#include "panel.h"

/* The column the handheld stands in: the rows and the focus fill start this
 * far in from the content's left edge (the mock's, 2026-11-13) */
#define CTLSCR_SIDE_W 280

/* What the tab is about: the game the menu is over, and where its choices
 * are kept and sent */
typedef struct {
	const system_cfg *s;     /* the game's own system (shelf_owner) */
	const char *file;        /* the game's file, as the shelf names it */
	const char *title;       /* its name, for Reset Buttons' question */
	const char *pak;         /* the Pak row's value (gm_pak_name), NULL for no row */
	/* Applies To says the system (every game of it): kept with the in-game
	 * menu's memory, so it stays with the tab and the row until the game ends */
	bool       *scope_all;
	bool        fixed_game;  /* Applies To fixed on This Game */
	bool        running;     /* a game is paused under it: changes are sent */
	/* The in-game menu's own style and context, whose backdrop and power
	 * rule the pages this tab opens are drawn over and keep; NULL in a shot */
	const menu_style *over;
	void       *ctx;
} ctlscr_game;

/* The in-game menu opened over a new MENU press: the handheld lit as it
 * is, and the tab's view of the card read again */
void ctlscr_open(void);

/* The tab turned to (L1, R1): the handheld lit for its row from the first
 * frame it is drawn, and the card read again */
void ctlscr_enter(void);

/* The in-game menu closed: the handheld's kept texture goes */
void ctlscr_close(void);

/* The tab's rows, their sound classes (MENU_SND_*) and value colors by
 * place, and their identities (ct_row) into `ids` when not NULL; each array
 * CT_MAX_ROWS long. The count. */
int ctlscr_rows(app *a, const ctlscr_game *g, menu_row *rows, unsigned *vcol,
                unsigned char *sound, int *ids);

/* A key on the row `id` (ct_row, -1 for none): Left, Right and A as the
 * design says; the rest nothing. The Pak row is main.c's. MENU_STAY. */
menu_result ctlscr_key(app *a, const ctlscr_game *g, in_button key, int id);

/* The tab's card changed from outside (the Pak row): read it again */
void ctlscr_changed(void);

/* The handheld, menu_style's `side` for the in-game menu's tabs and a
 * button's choices */
void ctlscr_side(app *a, const menu_side *at);

/* Reset Buttons' question for the scope shown, into `q` */
void ctlscr_question(const ctlscr_game *g, char *q, size_t n);

/* A shot's (--gm-pick): the choices of the button on row `id` (ct_row) as A
 * there opens them, over what is drawn already */
void ctlscr_shot_pick(app *a, const ctlscr_game *g, int id);

#endif
