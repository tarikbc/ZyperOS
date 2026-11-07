/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_TOURDEVICE_H
#define ZYPEROS_TOURDEVICE_H

/* The handheld beside each lesson of the tutorial (DESIGN.md 11.10, "The
 * handheld"): the GKD Pixel 2 the tutorial is running on, drawn from the
 * landing page's model (tools/build-pixel2-model.py's millimetres), turned
 * to the side its lesson's key is on, with that key lit and pressing with
 * the glyph. Drawn in code from the theme's roles, from the tutorial's
 * state and the clock alone, into a small texture kept while it does not
 * change (tourdev_make), where the renderer can.
 *
 * My pick in the tutorial-buttons lab, 2026-11-05: "Turns to the key"
 * (docs/design/mock/tutorial-buttons-lab.html). */

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "tour.h"

/* How far right the lesson block - progress, stage, check, glyph, title and
 * lines - moves to make room for the handheld, in units. */
#define TOURDEV_SHIFT 104

/* The handheld's middle, the column's, in the launcher's 1024x768 units. */
#define TOURDEV_X 138
#define TOURDEV_Y 262

/* Whether `t`'s lessons show the handheld: on the Pixel 2, whose brightness
 * is MENU+VOL (tour.fn_volume, from plat_brightness_on_volume), and in a
 * lesson. The Brick shows none and keeps its block in the middle. */
bool tourdev_shown(const tour *t);

/* Draws the handheld for `t` at `now`, at `op`, into its kept texture if it
 * changed since. Call it before anything is drawn to the screen in a frame:
 * it draws into a render target and gives the screen back after. */
void tourdev_make(SDL_Renderer *r, const tour *t, uint32_t now, float op);

/* The handheld for `t` at `now`, at `op` of its opacity (the first lesson's
 * fade; it stays on through the others): the kept texture if tourdev_make
 * made it for this view, else drawn. Nothing outside a lesson. */
void tourdev_draw(SDL_Renderer *r, const tour *t, uint32_t now, float op);

/* Lets the kept texture go, with the tutorial's (tour_draw_free) */
void tourdev_free(void);

#endif
