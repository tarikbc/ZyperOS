/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_LINKPANEL_H
#define ZYPEROS_LINKPANEL_H

#include <SDL.h>
#include <stdbool.h>

/* The Link Cable's page while it hosts, looks for a host, joins one or is
 * linked (DESIGN.md 11.72), a panel over the paused game as the in-game
 * menu's pages are (11.3) and as its mock draws it: the heading over a rule,
 * one large line in the middle ("Waiting for the other Pixel 2"), the host's
 * code under it while it hosts (the other one types it to join), the lines
 * under it that say what to do, three dots while it waits, the hosts found
 * as rows to choose from, and the key legend at the foot. The Host and Join
 * question before it is the tiles' (src/ds/tiles.h). src/link_screen.c says
 * what it shows, opens and closes it as every panel; this draws it. */

#define LINKPANEL_LINES 3
#define LINKPANEL_HOSTS 6         /* LINK_HOSTS_MAX */

typedef struct {
	const char *heading;
	const char *big;                       /* the large line */
	const char *code;                      /* under it, the host's code; NULL for none */
	const char *lines[LINKPANEL_LINES];    /* under it, NULL for none */
	bool        dots;                      /* waiting: three dots, going round */
	const char *hosts[LINKPANEL_HOSTS];    /* the hosts found, as rows */
	int         nhosts;
	int         sel;                       /* the host under the cursor */
	const char *legend;                    /* the key legend (src/keyhints.h) */
} linkpanel;

/* Where the panel is, at rest, in the launcher's 1024x768: the same for
 * every page, so it does not jump as the link goes from one to the next */
SDL_Rect linkpanel_rect(const linkpanel *p);

/* The panel at rest, its lift included (the caller's pass moves it); the
 * dots at `now_ms` */
void linkpanel_draw(SDL_Renderer *r, const linkpanel *p, bool lift, unsigned now_ms);

#endif
