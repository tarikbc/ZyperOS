/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_FOLDERART_H
#define ZYPEROS_FOLDERART_H

/* A folder of games, as pixel art in its system's color (src/shelfdir.h,
 * DESIGN.md 11.39): the picture of a folder's card on the shelf and of its
 * row in the list. Drawn on a small grid, a cell a pixel, which the caller
 * scales up whole (nearest), so it reads as the home's pixel consoles do.
 *
 * A folder's back with its tab, two sheets of paper standing out of it, and
 * its front, in the system's color, with the count of its games on a paper
 * label in pixel digits. My pick of three looks drawn for it (2026-11-13:
 * "can the folder actually look like a folder in the system? maybe a pixel
 * art folder", then "B, with papers").
 *
 * No TTF and no renderer: surfaces only, so the art workers draw it. */
#include <SDL.h>
#include <stdbool.h>

/* The grid a card is drawn on, in cells, and a list row's; a card's is
 * scaled by FA_CARD_SCALE to its 512 units across */
#define FA_CARD_W     128
#define FA_CARD_H     112
#define FA_CARD_SCALE 4
#define FA_ICON_W     32
#define FA_ICON_H     28

/* A folder in `rgb` (0xRRGGBB) into `s`, an ARGB8888 surface of FA_CARD_W
 * x FA_CARD_H or FA_ICON_W x FA_ICON_H cells, cleared to nothing first;
 * `games` on its label when `label` (a row's count is beside it, not on it) */
void fa_paint(SDL_Surface *s, unsigned rgb, int games, bool label);

/* Where a card's name goes on its front, in cells of the card's grid, and
 * the color it is written in there: ink on a light folder, paper on a dark
 * one */
void fa_name_box(unsigned rgb, SDL_Rect *box, unsigned *ink);

#endif
