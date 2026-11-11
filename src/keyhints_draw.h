/* SPDX-License-Identifier: GPL-3.0-only */
/* Button hints drawn (keyhints.h): each a button's glyph and its word, as
 * page_hint draws one (src/home.c, DESIGN.md 11.9), in **chrome** words
 * **text-soft** beside a 32-unit glyph, rows KEYHINTS_ROW apart. */
#ifndef KEYHINTS_DRAW_H
#define KEYHINTS_DRAW_H

#include <SDL.h>
#include <stdbool.h>

#include "keyhints.h"

enum {
	KEYHINTS_CENTER,      /* each row centered between x0 and x1: under a panel's tiles */
	KEYHINTS_SPREAD       /* each row from x0 to x1, the room shared: a screen's foot */
};

/* A packed list's width on one row (KEYHINTS_GAP between), 0 for text that
 * is not one */
int keyhints_line_width(const char *packed);

/* A packed list drawn centered between x0 and x1 on one row's middle `cy`
 * (KEYHINTS_CENTER); its rows */
int keyhints_draw_packed(SDL_Renderer *r, const char *packed, int x0, int x1, int cy, Uint8 alpha);

/* How wide one is, and how tall `n` are laid out in `maxw` */
int keyhints_width(const char *key, const char *label);
int keyhints_height(const key_hint *h, int n, int maxw);

/* Drawn between x0 and x1, the first row's middle at `cy`; KEYHINTS_SPREAD
 * with `up` grows its extra rows upward from `cy` instead (a screen's foot,
 * whose last row stays on its line). Returns the rows. */
int keyhints_draw(SDL_Renderer *r, const key_hint *h, int n, int x0, int x1, int cy,
                  int align, bool up, Uint8 alpha);

#endif
