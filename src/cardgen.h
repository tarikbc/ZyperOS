/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_CARDGEN_H
#define ZYPEROS_CARDGEN_H

/* The card drawn where there is no art - a game with no box art, an album
 * with no cover, a system with no card file (src/ui.c's card_surface) - in
 * the part of it that is plain C: the colors it is drawn in, in each theme,
 * and whether a card already made may still be shown.
 *
 * A card is pixels, not a role. Its slab and its title are baked into a
 * texture when it is made, so a card made in Beard would stay Beard's after
 * Auto turns the shelf to Salt & Pepper at 19:00. So each card is made for
 * one theme, named by its index (DS_LIGHT or DS_DARK), and shown only while
 * that theme is on: a change of theme drops the others.
 *
 * No SDL, so tools/cardgen-check.c holds both rules without a window: the
 * title reads on the slab at every row in both themes, Beard's light slab
 * included, and a card is kept in its own theme and in no other. */

#include <stdbool.h>

#include "design.h"

/* The slab `k` of the way down the card, 0 at the top and 1 at the foot: the
 * theme's **surface** at the top (DESIGN.md 3.2: surface is a card's slab),
 * darkening toward its **ground** and stopping halfway there. Darker at the
 * foot so a wall of cards keeps some depth, and short of the ground so the
 * foot of a card still stands off what it stands on. `theme_ix` is DS_LIGHT
 * or DS_DARK, and anything else is DS_LIGHT; `k` is held to 0-1. */
ds_rgb cardgen_slab(int theme_ix, float k);

/* The title's color: the theme's **text**, which is charcoal on Beard's
 * light slab and beard white on Salt & Pepper's dark one. */
ds_rgb cardgen_title(int theme_ix);

/* Whether a texture made for theme `made_for` may be shown while theme `on`
 * is: a card, made for DS_LIGHT or DS_DARK, only in its own theme; art, which
 * is no theme's and is -1, in either. */
bool cardgen_kept(int made_for, int on);

#endif
