/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_LINK_SCREEN_H
#define ZYPEROS_LINK_SCREEN_H

/* The Link Cable's page over the paused game (DESIGN.md 11.72, src/link.h):
 * Host and Join as tiles, then waiting for the other one, the hosts found,
 * joining, linked; link_ops on the device; and the shots' made-up states.
 * The code is src/link_screen.c, the page's panel src/linkpanel.c. */
#include "app.h"
#include "link.h"
#include "panel.h"

/* What a shot shows (--link KIND), made up as Player 2's (--player2) */
typedef enum {
	LINK_SHOT_TILES, LINK_SHOT_NO_ADAPTER, LINK_SHOT_STARTING, LINK_SHOT_WAITING,
	LINK_SHOT_LOOKING, LINK_SHOT_FOUND, LINK_SHOT_JOINING, LINK_SHOT_LINKED,
	LINK_SHOT_LOST, LINK_SHOT_FAILED, LINK_SHOT_STOPPING,
	/* the other failures' lines under the tiles */
	LINK_SHOT_FAILED_NETWORK, LINK_SHOT_FAILED_JOIN, LINK_SHOT_FAILED_BUSY, LINK_SHOT_FAILED_GAME,
	LINK_SHOT_FAILED_CODE,
	/* the USB-C cable's: Host and Join, waiting, joining, its failure */
	LINK_SHOT_CABLE_TILES, LINK_SHOT_CABLE_WAITING, LINK_SHOT_CABLE_JOINING, LINK_SHOT_FAILED_CABLE,
	LINK_SHOT_KINDS
} link_shot;

extern bool      g_shot_link;
extern link_shot g_shot_link_kind;
/* --link KIND's name to its kind; false for none */
bool link_shot_parse(const char *kind);

/* The page, from the in-game menu's Link Cable row over the paused game
 * `bg`: true when it closed because the link was made, and the game goes on
 * (the menu closes with it) */
bool link_page(app *a, SDL_Texture *bg);

/* What the row says: the state, and whether the link can start: a Wi-Fi
 * adapter in, or another device on the USB-C cable (the row is quiet with
 * neither) */
link_state link_row_state(void);
bool       link_row_adapter(void);

/* The page's panel from its rows, as panel_show and the panel's copy draw
 * it (PANEL_LINK), at `pose` */
void link_panel(app *a, const char *heading, const menu_row *rows, int n, int sel,
                unsigned accent, mm_pose pose);

/* A shot of the page in g_shot_link_kind's state, over a made-up game */
void link_shot_draw(app *a, SDL_Texture *bg);

/* At the game's end, however it ended: the link off, said to the other
 * one, and the Wi-Fi back (said on a panel while it comes) */
void link_game_end(app *a);

/* Once, as the launcher starts */
void link_setup(void);

/* Desk mode as the cable's joiner (ZYPEROS_DESK_LINK_CABLE=1): Auto Off
 * holds, as on the charger. False on a handheld. */
bool link_desk_holds_idle(void);

/* On mGBA, the question's yes (CQ_LINK_GAMBATTE): Gambatte the game's own
 * core (core_game_set), the game stopped and opened again on Gambatte from the
 * shelf's turn, and the page owed over it (link_page_next_take). False,
 * and nothing done, with no game or no Gambatte on the card. */
bool link_restart_on_core(app *a);
/* The page is owed over `file`, the game starting now (asked once: the
 * next start forgets it either way) */
bool link_page_next_take(const char *file);

#endif
