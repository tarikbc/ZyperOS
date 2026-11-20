/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CANVAS_H
#define ZYPEROS_CANVAS_H

/* The canvas: the design space every screen is laid out and drawn in
 * (DESIGN.md 2, "Canvas"), ZYPEROS_SCREEN_W x ZYPEROS_SCREEN_H in
 * src/platform.h.
 *
 * Its height is 768 units on every handheld, so a type size, a row and the
 * vertical rhythm are the same everywhere. Its width follows the shape of the
 * handheld's panel: 1024 on a 4:3 panel (the GKD Pixel 2, the TrimUI Brick),
 * 1368 on a 16:9 one (the TrimUI Smart Pro), on the 8-unit grid. A screen
 * takes its places from the canvas's edges, its middle and its columns, never
 * from a typed 1024, so it fits whatever width the handheld gives it.
 *
 * Set once at the start from the device's profile (canvas_init), before
 * anything is drawn or laid out, and again when the host wears another
 * handheld's (--device). A 4:3 handheld's canvas is the 1024 x 768 it always
 * was, unit for unit. */

#include "device/profile.h"

#define CANVAS_H     768
/* The narrowest and widest a canvas is made: from a 1:1 panel to a 2:1 one.
 * CANVAS_W_MAX sizes what is kept a whole row of the canvas long. */
#define CANVAS_W_MIN 768
#define CANVAS_W_MAX 1536

/* The canvas's width in units, for this run (ZYPEROS_SCREEN_W) */
extern int canvas_w;

/* The width of the canvas for a panel of this shape: CANVAS_H times the
 * aspect, to the nearest 8 units, inside CANVAS_W_MIN..CANVAS_W_MAX. 4:3 is
 * 1024, 16:9 is 1368 (1365.3 to the grid), 3:2 is 1152, 1:1 is 768. A
 * shape it cannot read (a zero) is 4:3's. */
int canvas_w_for(int aspect_w, int aspect_h);

/* The canvas for this handheld, from its profile's panel shape */
void canvas_init(const device_profile *p);

/* Whether this canvas is wider than 4:3: the wide size class, where a
 * screen may lay its parts out side by side */
static inline int canvas_wide(void) { return canvas_w > 1024; }

/* The widest a panel over the screen is: the 4:3 canvas's width. A panel
 * over a game or a page (the in-game menu, MENU's pages) keeps its 4:3
 * width on a wider canvas, in the middle (Tarik's pick, 2026-11-20); a
 * narrower canvas gives it what it has. */
static inline int canvas_panel_room(void) { return canvas_w < 1024 ? canvas_w : 1024; }

/* The 4:3 column in the canvas's middle: its left edge. A composition made
 * as one piece for 1024 units (a page's picture beside its words, a
 * dialog's stage) goes there whole on a wider canvas; 0 on a 4:3 one. */
static inline int canvas_col_x(void) { return (canvas_w - 1024) / 2; }

/* How many cells `cell` wide a row `w` wide holds, `min_gap` at least
 * between them, from 1 to `max`. A wider canvas fills its room with more
 * cells, not with wider gaps: the Scraper's and the Downloads' runs show
 * three covers a row on a 4:3 canvas, five on the Smart Pro's. */
static inline int canvas_fit_cols(int w, int cell, int min_gap, int max)
{
	const int n = (w + min_gap) / (cell + min_gap);

	return n < 1 ? 1 : n > max ? max : n;
}

#endif
