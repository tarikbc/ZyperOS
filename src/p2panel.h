/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_P2PANEL_H
#define ZYPEROS_P2PANEL_H

#include <SDL.h>
#include <stdbool.h>

/* NES Player 2's page (src/p2.h), a panel over the paused game in the
 * in-game menu's style: its heading over a rule, and how a friend joins.
 *
 * On the handheld's own Wi-Fi, laid out as Wi-Fi File Transfer's screen
 * lays out its code (src/hotpanel.c): the QR code a phone's camera joins
 * by, large, at the left, and beside it the line that says the game opens,
 * the network's name and its password. Otherwise one column: the line that
 * says what to do (open zyperos.local/play on the computer, what A will
 * start, what to plug in). Under either, a line for each player a friend can
 * be, and the key legend at the foot. src/main.c builds what it says, opens
 * and closes it as every panel; this draws it. */

#define P2PANEL_STATUS 3          /* players 2 to 4 */

typedef struct {
	const char *heading;
	unsigned    accent;     /* whose menu: the system's color, as the in-game menu's */
	const char *qr;         /* the code's text (captive_wifi_qr); NULL or "": none */
	const char *ssid;       /* beside the code */
	const char *pass;
	const char *how;        /* the line that says what to do */
	/* A line per player: "Player 2" and "waiting" */
	const char *who[P2PANEL_STATUS], *state[P2PANEL_STATUS];
	int         nwho;
	const char *legend;     /* the key legend (src/keyhints.h) */
} p2panel;

/* Where the panel is, at rest, in the launcher's 1024x768 */
SDL_Rect p2panel_rect(const p2panel *p);

/* The panel at rest, its lift included: the caller's pass moves it */
void p2panel_draw(SDL_Renderer *r, const p2panel *p, bool lift);

#endif
