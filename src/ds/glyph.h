/* SPDX-License-Identifier: GPL-3.0-only */
/* The marks drawn from strokes: a few lines and arrowheads in a unit square,
 * rendered once a (mark, size) and tinted when drawn. Part of the design
 * system (src/ds/, DESIGN.md "Where each part lives"); ui.h includes it, so
 * every screen that draws a mark has it. */
#ifndef ZYPEROS_DS_GLYPH_H
#define ZYPEROS_DS_GLYPH_H

#include <SDL.h>
#include <stdbool.h>

/* The play modes' marks, in the shapes everybody already reads: two arrows
 * chasing round a loop for repeat, the same with a 1 in it for repeat one, and
 * two crossing arrows for shuffle. Drawn here from a few lines of geometry
 * rather than taken from a font or an icon set. Centered on (cx, cy), `size`
 * pixels square, in `col`; each shape and size is rendered once and kept. */
/* And the three answers when a computer is plugged in (DESIGN.md 11.18): a
 * bolt for Charge Only, a folder with an arrow up out of it for File
 * Transfer, a terminal's prompt for Developer (SSH). And the empty home's two
 * ways (11.19): the USB trident, and three arcs and a dot for Wi-Fi. And the
 * answers while File Transfer's files move (11.14): an arrow up and one down
 * for keep going, a page with a tick for stop after this file, a ring crossed
 * for cancel now, a play triangle for play after this file. And a tick for
 * the choice that is on in a list of choices (MENU > Language, 11.30). And
 * the two answers when something on the Pixel 2's port wants power (11.36):
 * headphones for a USB accessory, a laptop for a computer. And Flash
 * Drive's two ways (11.18): an arrow down into a tray, to the card, and up
 * out of it, from the card. And MENU > Scraper's (11.19): a box with a
 * picture on it for covers, a screen with a play triangle for video snaps,
 * its three sources' own marks, never their logos - a cloud for
 * ScreenScraper, an open book for libretro's catalog, a grid for
 * SteamGridDB - and a heart for the favorites. */
typedef enum {
	UI_GLYPH_REPEAT, UI_GLYPH_REPEAT_ONE, UI_GLYPH_SHUFFLE,
	UI_GLYPH_CHARGE, UI_GLYPH_TRANSFER, UI_GLYPH_TERMINAL,
	UI_GLYPH_USB, UI_GLYPH_WIFI,
	UI_GLYPH_ARROWS, UI_GLYPH_FINISH, UI_GLYPH_CANCEL, UI_GLYPH_PLAY,
	UI_GLYPH_CHECK,
	UI_GLYPH_HEADPHONES, UI_GLYPH_LAPTOP, UI_GLYPH_PHONE,
	UI_GLYPH_TO_CARD, UI_GLYPH_FROM_CARD,
	UI_GLYPH_COVER, UI_GLYPH_FILM,
	UI_GLYPH_CLOUD, UI_GLYPH_BOOK, UI_GLYPH_GRID, UI_GLYPH_HEART, UI_GLYPH_CLOCK,
	UI_GLYPH_COUNT
} ui_glyph;
void ui_glyph_draw(SDL_Renderer *r, ui_glyph g, int cx, int cy, int size,
                   SDL_Color col);

/* The limits of one mark: strokes (a curve is several, g_arc six) and
 * arrowheads. A mark past them would lose what is past them, so
 * tools/glyph-check.c holds every mark to them: `strokes` and `heads` are
 * what the mark asks for, whether or not they fit. */
#define DS_GLYPH_STROKES 32
#define DS_GLYPH_HEADS   4
bool ds_glyph_fits(ui_glyph g, int *strokes, int *heads);

/* Every rendered mark freed (ui_quit). */
void ds_glyphs_free(void);

#endif
