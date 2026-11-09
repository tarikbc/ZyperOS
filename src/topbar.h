/* SPDX-License-Identifier: GPL-3.0-only */
/* The top bar, DESIGN.md 11.12: the one header every screen that has one
 * draws, in one layout. The clock at the left end, from x 64, in **action**
 * **text** with tabular digits (2026-11-05: "remove zyperOS from the top left
 * and put the time there instead"); at the right end, ending at x 960, the
 * marks (src/status.h) and the battery's words left of the battery - its
 * percentage, or its time left - in **chrome** with tabular digits. One row 24
 * units tall, centered on y 64.
 *
 * Who draws it: the home and the games shelves, and the menus over them; every
 * screen over a paused game (#40); and every MP4 screen - its shelf, an
 * album's tracks, Now Playing, a video with its controls, and the lock screen
 * over them (2026-11-09: "the same top bar component as the home screen and
 * the in-game menu"). Each in its own theme and with its own colour for the
 * battery's words: the theme's text-soft, or text over a paused game, where a
 * white frame under scrim.game leaves text-soft at 4.4:1.
 *
 * There was a second layout until 2026-11-09, the clock beside the marks at
 * the right, which Now Playing, a video's controls and MP4's shelf used: two
 * top bars a screen apart. It is gone, and this file holds the one there is.
 *
 * What it shows is gathered by the caller (src/main.c's draw_topbar: the
 * radios, the battery, the clock's setting, a shot's fixed values); this file
 * places and draws it, and says where it drew. */
#ifndef ZYPEROS_TOPBAR_H
#define ZYPEROS_TOPBAR_H

#include <SDL.h>
#include <stdbool.h>

#include "status.h"

#define TOPBAR_CLOCK_X 64                       /* the clock's left edge */
#define TOPBAR_RIGHT   960                      /* the battery's right edge */
#define TOPBAR_DY      28                       /* the row under status.h's */
#define TOPBAR_TOP     (STATUS_TOP + TOPBAR_DY) /* 52 */
#define TOPBAR_MID     (STATUS_MID + TOPBAR_DY) /* 64: the capitals' middle */

typedef struct {
	int          theme;     /* an index into DS_THEME, DS_LIGHT or DS_DARK */
	SDL_Color    soft;      /* the battery's words, when it is not low */
	status_marks marks;     /* what the marks show; pct_w is set here */
	const char  *clock;     /* "10:08", or NULL for none (a clock nobody set) */
	/* The battery's words, "82%" or "3 h 20 min", or NULL for none: MENU >
	 * Battery Icon on Bars, no reading, or marks.calc's dots in their place */
	const char  *batt;
} topbar;

/* Where it drew, for the screens that place a line in its row
 * (topbar_half) and for a shot's checks */
typedef struct {
	SDL_Rect clock;         /* the clock's text; zero width for none */
	SDL_Rect marks;         /* the marks and the battery's words */
	int      marks_w;       /* status_marks_width's */
	int      batt_natural;  /* the battery's words as wide as they are, before
	                         * STATUS_PCT_MAX held them to their room */
} topbar_drawn;

void topbar_draw(SDL_Renderer *r, topbar *t, topbar_drawn *out);

/* How far either side of the screen's middle a line in the bar's row can
 * reach, clear of the clock and the marks by DS_GRID * 3 as they were drawn:
 * MP4's album title on its shelf and a video's title over its frame. The
 * screen's half where neither was drawn. */
int topbar_half(const topbar_drawn *d);

#endif
