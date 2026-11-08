/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_NOTICEART_H
#define ZYPEROS_NOTICEART_H

/* Two pictures the launcher draws itself where an achievement's own art
 * cannot be: the plain badge, and Zyper.
 *
 * The plain badge stands in for an achievement's badge until the card has
 * it (src/rabadge.h): a handheld with no network has none, and a set fetched
 * during a game has them only after it. A trophy on a tile, in the theme's
 * own colors, the size and corners of a badge, so a list or a notice has the
 * same shape whether the card has the art or not and nothing moves when it
 * lands.
 *
 * Zyper is on the notice an unlock makes over a game (DESIGN.md 11.7),
 * as the badge's partner at the right end of the slab. The notice is a
 * file of pixels that Diatom lays over the game, so there is no renderer to
 * hand his triangles to: they are filled here, from the same table and the
 * same placement as the menus' (src/zyperplace.c).
 *
 * Both are SDL surfaces and nothing more - no renderer, no window - so a
 * game that is running is never touched, and tools/noticeart-check.c holds
 * them on a build machine. ARGB8888, straight alpha; the caller frees. */

#include <SDL.h>
#include <stdbool.h>

#include "design.h"

/* A trophy on a tile, `size` pixels square, in theme `t`: earned, the trophy
 * in **accent-ink** on **raised**; or `locked`, as the list draws the
 * badges of achievements not yet won, **text-dim** on **track** and faded.
 * The corners a sixth of the side, as the badges' are. NULL when `size` is
 * not positive or there is no memory. */
SDL_Surface *noticeart_plain(int size, const ds_theme *t, bool locked);

/* Zyper, `h` tall, cocked and panting as he rests in the menus, in his own
 * colors. With `shadow`, as on Beard (DESIGN.md 10), the **focus-small**
 * shadow under him in the theme's **ink**; the surface is then larger than
 * he is, by `*pad` pixels on each side (the shadow's reach), and he is
 * `h` tall at its middle. Without it `*pad` is 0 and the surface is his
 * extent. `pad` may be NULL. NULL when `h` is not positive or there is no
 * memory. */
SDL_Surface *noticeart_zyper(int h, bool shadow, const ds_theme *t, int *pad);

#endif
