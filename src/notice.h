/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_NOTICE_H
#define ZYPEROS_NOTICE_H

#include <stdbool.h>

#include "design.h"

/* A line of text, rendered to the file Diatom composites over a running game.
 *
 * This exists because the display has exactly one owner at a time. While a
 * game runs, that is Diatom, and this process must not present - so it cannot
 * put anything on screen itself. Diatom can, and already does for the volume
 * bar, but it has no font and is not getting one. So the text is rendered
 * here, to pixels, and carried over.
 *
 * Safe to call mid-game: rendering to a surface touches no display. Only
 * SDL_RenderPresent would, and nothing here presents.
 *
 * Its own file rather than part of cheevos.c because that one is deliberately
 * SDL-free - `make check-cheevos` links it with no SDL at all, and the parsing
 * and filtering it checks should not need a window to be tested.
 */

/* Writes `path` in Diatom's overlay format: "DTOV", uint16 w, uint16 h, then
 * w*h BGRA rows. False if there is no font or the file cannot be written.
 *
 * Drawn in theme `theme_ix`, DS_LIGHT or DS_DARK (DESIGN.md 11.7: a slab of
 * **surface**, its heading **text-dim** and its body **text**), which the
 * caller reads as it asks: a notice is rendered while a game runs, and Auto
 * can have turned the theme at 7:00 or 19:00 since the game started, so the
 * theme is the one on at this moment and never one this file kept. The
 * launcher's theme_index() is that, ticked through the game (on_game_tick),
 * and it is what THEME_FILE says to Diatom's bar beside the notice. */
bool notice_render(const char *heading, const char *body, const char *path,
                   int theme_ix);

/* What an achievement's notice carries besides its words (`look`, below): the
 * plain badge where the card has no art for it, and Zyper. Notice kinds that
 * are not about an achievement - the set's arrival, fast forward, "Getting
 * hot" - carry neither. */
#define NOTICE_ICON  1   /* a trophy on a tile when `badge` is NULL or will
                          * not load, so the slab has the badge's shape
                          * whether the card has the art or not */
#define NOTICE_ZYPER 2   /* Zyper at the right end, DESIGN.md 11.7 */

/* The same slab with an achievement's badge at its left, the lines
 * left-aligned beside it - `badge` a PNG, NULL or one that will not load for
 * none, which is notice_render unless `look` has NOTICE_ICON, which puts the
 * plain badge there (noticeart_plain), locked-looking for a progress notice,
 * whose achievement is not won. With `target` above 0 it is a progress
 * notice: the heading (the achievement's name) over a strip in
 * **accent-ink** on **track**, `value` of `target` filled, and `body` (the
 * count, "74 / 100") under it in **meta** **text**. My picks in the
 * achievements lab, 2026-11-05 (docs/design/mock/cheevos-lab.html). */
bool notice_render_ex(const char *heading, const char *body, const char *badge,
                      unsigned look, int value, int target, const char *path,
                      int theme_ix);

/* A mark the launcher draws itself, where a badge goes */
typedef enum {
	NOTICE_MARK_NONE,
	NOTICE_MARK_HEAT,   /* a thermometer in **warning**: "Getting hot" */
} notice_mark;

/* The slab with `mark` at its left as tall as the two lines, the lines
 * left-aligned beside it, as a badge's (DESIGN.md 11.23: "Getting hot" over
 * a game). NOTICE_MARK_NONE is notice_render. */
bool notice_render_mark(const char *heading, const char *body, notice_mark mark,
                        const char *path, int theme_ix);

/* The thermometer alone, `size` pixels tall and half as wide: its outline in
 * `ink` and its mercury in `fill`, ARGB8888 with straight alpha, for a
 * texture (Zyper's bubble on the home, DESIGN.md 11.23). The caller frees
 * it; NULL when it cannot be made. */
struct SDL_Surface;
struct SDL_Surface *notice_heat_mark(int size, ds_rgb ink, ds_rgb fill);

#endif
