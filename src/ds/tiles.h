/* SPDX-License-Identifier: GPL-3.0-only */
/* A question in tiles, or a list (DESIGN.md 11.18, 11.23, 11.36): a panel
 * (11.3) holding a heading, its rule, a tile per choice and the button hints
 * under them. Part of the design system (src/ds/, DESIGN.md "Where each part
 * lives"): it draws, the caller decides. The style is the caller's word, and
 * so is each tile's mark; nothing here reads a name to decide either. */
#ifndef ZYPEROS_DS_TILES_H
#define ZYPEROS_DS_TILES_H

#include <SDL.h>
#include <stdbool.h>

#include "menu.h"        /* menu_row, MENU_NOTE */
#include "menumotion.h"  /* mm_pose */

typedef enum {
	DS_TILES_ROW,    /* tiles side by side: a mark and a name on two lines */
	DS_TILES_GRID,   /* two to a row, a name on one line (Computer Connected) */
	DS_TILES_LIST,   /* rows the panel's width, a line under each name (What did you plug in?) */
} ds_tiles_style;

/* How a panel is drawn at a pose other than rest, scaled or faded as it
 * opens and closes: the caller's pass (src/main.c's panel_pass_begin and
 * panel_pass_end, with panel_scaled). `begin` false: drawn straight to the
 * screen. */
typedef struct {
	SDL_FRect (*scaled)(const SDL_Rect *panel, float scale);
	bool (*begin)(void *ctx);
	void (*end)(void *ctx, const SDL_Rect *panel, mm_pose pose);
	void *ctx;
} ds_pass;

typedef struct {
	ds_tiles_style style;
	const char *heading;
	/* The tiles, a row each - its name the label ("\n" between its two
	 * lines), its small line the value or NULL - up to the first note
	 * (MENU_NOTE). Then one note, the legend; or three: a line under the
	 * rule, a line under the tiles (MENU_NOTE(NULL) for none), the legend.
	 * A legend is packed button hints (keyhints.h) or a line of text. */
	const menu_row *rows;
	int n;
	/* A mark a tile, a ui_glyph (src/ds/glyph.h) in the tiles' order;
	 * UI_GLYPH_COUNT, or `marks` NULL, for none */
	const int *marks;
	int sel;
	const char *legend;   /* when no note gives one */
	bool hints;           /* the hints shown: false while a question that came by itself waits */
} ds_tiles;

void ds_tiles_draw(SDL_Renderer *r, const ds_tiles *t, mm_pose pose, const ds_pass *pass);

#endif
