/* SPDX-License-Identifier: GPL-3.0-only */
/* The Display list's picture (a hover's draw, menu_pick_hover, DESIGN.md 11.3): the
 * handheld's screen in small, and a game's frame on it as display mode `k`
 * would put it (src/dmodefit.h) - bars where the mode leaves the screen
 * bare, the edges gone where it crops, the shape it gives the picture - so
 * a mode's words have a picture beside them before it is chosen.
 *
 * In a game, the paused frame the in-game menu is over, at the size and
 * shape Diatom said (plat_resident_source). On a shelf, the game under the
 * cursor's last frame (its Auto picture) with the shape its system's games
 * last said, kept for it (dmode_note_shape); a game never played has none,
 * and a frame drawn for the purpose stands in. */
#ifndef ZYPEROS_DMODEPREVIEW_H
#define ZYPEROS_DMODEPREVIEW_H

#include <SDL.h>
#include <stdbool.h>

#include "app.h"

/* The picture for mode `k` in the box x, y, w x h (units): `frame` is the
 * game's picture, `src_w` x `src_h` as its core makes it with the shape
 * `aspect` (0 for square pixels), on Diatom's surface `surf_w` x `surf_h`;
 * NULL draws the stand-in */
void dmode_preview_draw(app *a, SDL_Texture *frame, int src_w, int src_h, double aspect,
                        int surf_w, int surf_h, int k, int x, int y, int w, int h);

/* The Super Game Boy list's picture (src/sgbmode.h) for choice `k` (Off,
 * Its Games), at display mode `dmode` on this handheld's screen, sized as
 * Diatom would size it: a Game Boy game's 160x144 alone for Off, the Super
 * Game Boy's 256x224 for Its Games, the game in its middle and around it a
 * stand-in of a game's own border. Drawn frames, ours: no game's border and
 * no Nintendo art. */
void dmode_sgb_preview(app *a, int dmode, int k, int x, int y, int w, int h);

/* The Border list's picture (src/border.h): the game's picture where display
 * mode `dmode` puts it, as dmode_preview_draw draws it, with border `kind`
 * around it: black (Off), the picture itself made small, stretched back and
 * darkened, as Diatom's blur is (Blur), or the player's own picture made
 * the surface's shape (`own`, a surface border_own_surface made, for Your
 * Own; `own_key` its file, whose texture is kept while it is the same), or
 * a handheld's frame made the surface's size (`own` too, `own_key` naming
 * the frame) with the game at `game_at` in it, in the surface's pixels,
 * rather than where the mode puts it. `frame` NULL draws the stand-in, as
 * there. */
void dmode_border_preview(app *a, SDL_Texture *frame, int src_w, int src_h, double aspect,
                          int dmode, int kind, SDL_Surface *own, const char *own_key,
                          const SDL_Rect *game_at, int x, int y, int w, int h);

/* The surface Diatom draws on, as the last DISPLAY said, or the device's
 * panel before one did */
void dmode_surface(int *w, int *h);

/* Keeps the shape the game running says (plat_resident_source) for its
 * system `tag`, for the shelf's list; and reads it back, 0 when none was
 * kept (square pixels) and `known` false */
void dmode_note_shape(const char *tag);
double dmode_shape_of(const char *tag, bool *known);
/* And the picture's size its games last said, kept with its shape: false,
 * 0 x 0, for a system never played */
bool dmode_source_of(const char *tag, int *w, int *h);

#endif
