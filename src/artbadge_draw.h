/* SPDX-License-Identifier: GPL-3.0-only */
/* The art badge (src/artbadge.h) on screen, in the theme the launcher is in. */
#ifndef ZYPEROS_ARTBADGE_DRAW_H
#define ZYPEROS_ARTBADGE_DRAW_H

#include <SDL.h>

#include "artbadge.h"

/* Badge `kind` centered on (cx, cy), for a card drawn at `k` of its focused
 * size (ab_diameter), at `alpha` as the card is. Nothing for AB_NONE, or a
 * card too small for one. */
void ab_draw(SDL_Renderer *r, ab_kind kind, float cx, float cy, float k, Uint8 alpha);

/* The same at the top left corner of art whose own top left is (x, y), inset
 * as ab_center says */
void ab_draw_at(SDL_Renderer *r, ab_kind kind, float x, float y, float k, Uint8 alpha);

#endif
