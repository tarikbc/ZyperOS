/* SPDX-License-Identifier: GPL-3.0-only */
/* The art badge, DESIGN.md 11.1: a small mark on a card's art that says one
 * thing about what the card is. A glyph on a disc, at the art's top left
 * corner, inset as the video snap's mark is at the bottom right (11.19), so
 * the two never meet and neither covers the middle of the art, where a
 * cover's subject and its title are.
 *
 *   favorite  a heart in **favorite**, on any game's card that is a favorite
 *             (fav_is): every shelf, Favorites too, and Random Game's pick,
 *             which is the shelf put on it
 *   album     two eighth notes on one beam, the top bar's song mark, in
 *             **accent-ink**: MP4's All, on a music album
 *   book      an open book in **accent-ink**: MP4's All, on an audiobook
 *   video     a screen with a play mark cut out of it, in **accent-ink**:
 *             MP4's All, on a video or a folder of them
 *
 * The disc is **surface**, whole, over a soft shadow of **ink** that lifts it
 * off light art as well as dark, so the glyph has the same ground under it
 * whatever the art is: its contrast is the theme's and not the cover's.
 * AB_D across on a focused card at rest, inset AB_INSET from the art's
 * corner, both scaled with the card as the row scales it.
 *
 * Mine, 2026-11-09: "show a small heart badge when the game is a favorite",
 * and in All "each item's art shows a small badge saying what it is ... in
 * the same badge component and style as the heart."
 *
 * Drawn here as pixels, by signed distance, as the status marks are
 * (src/status.c's way, a copy of the idea and not of the file): plain C, no
 * SDL, so tools/artbadge-check.c links this file alone and looks at every
 * badge, in both themes, at every size a row draws one. src/artbadge_draw.c
 * keeps each (badge, theme, size) as a texture and blits it. */
#ifndef ZYPEROS_ARTBADGE_H
#define ZYPEROS_ARTBADGE_H

#include <stdint.h>

#include "design.h"

typedef enum { AB_FAVORITE, AB_ALBUM, AB_BOOK, AB_VIDEO, AB_KINDS } ab_kind;

/* None: what a caller answers for a card with no badge */
#define AB_NONE (-1)

/* The disc's diameter and its inset from the art's corner on a focused card
 * at rest, in units: the snap mark's (11.19), so the two corners match. */
#define AB_D     40
#define AB_INSET 12
/* The glyph's square, on the 24-unit grid the status marks are drawn on, in
 * the middle of the disc at AB_D */
#define AB_GLYPH 24
/* The shadow under the disc: down AB_SHADOW_Y, softened over AB_SHADOW_BLUR,
 * of ink at these opacities (0-255) in Beard and in Salt & Pepper, and the
 * room it needs round the disc in the texture */
#define AB_SHADOW_Y      2
#define AB_SHADOW_BLUR   6
#define AB_SHADOW_LIGHT 72
#define AB_SHADOW_DARK  120
#define AB_PAD 6
/* The smallest disc drawn: a card further out than this is too small to
 * read one on. MP4's side cards, turned away, draw theirs at about 22. */
#define AB_MIN_D 20

/* The disc's diameter in units for a card drawn at `k` of its focused size,
 * rounded to a whole unit so a size is the same badge every time it comes
 * round; 0 when that is under AB_MIN_D. */
int ab_diameter(float k);

/* Where the disc's center goes for art whose top left corner is at (x, y), drawn
 * at `k` of its focused size: AB_INSET + AB_D / 2 in from both edges, scaled */
void ab_center(float x, float y, float k, float *cx, float *cy);

/* The texture for a disc `d` across: d + AB_PAD * 2 square, in pixels at one
 * pixel a unit, as the launcher draws */
int ab_buf_side(int d);

/* Draws badge `kind` for a disc `d` across into px, side x side from
 * ab_buf_side, `stride` pixels a row, clearing it first: 0xAARRGGBB, straight
 * alpha, as SDL's blending wants. `theme` is DS_LIGHT or DS_DARK; anything
 * else draws DS_LIGHT. The disc's center is the buffer's. */
void ab_render(uint32_t *px, int side, int stride, int d, int theme, ab_kind kind);

/* The glyph's color for `kind` in `theme`, and the disc's: for the checks'
 * contrast, which they owe 3:1 as a mark does (DESIGN.md 1). */
ds_rgb ab_glyph_color(ab_kind kind, int theme);
ds_rgb ab_disc_color(int theme);

#endif
