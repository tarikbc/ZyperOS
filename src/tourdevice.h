/* SPDX-License-Identifier: GPL-3.0-only */
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

#include "tdkey.h"   /* td_key: the keys of the handheld a pose can light */
#include "tour.h"

/* The handheld as anyone can draw it: where (the middle, in screen units),
 * how big (screen units a millimetre), how far turned (degrees, 0 front, 52
 * the side), at `op` of its opacity, with each key lit `on` and pressed
 * `press` from 0 to 1, the d-pad's arms pressed (up, down, left, right),
 * whether the side's tabs stand out of the edge, and their ring. Clear it
 * whole before filling it, so two poses compare as bytes. */
typedef struct {
	float cx, cy, scale, deg, op;
	float on[TD_KEYS], press[TD_KEYS];
	float arm[4];
	bool tabs;
	float tab_ring;
} td_pose;

/* A texture one place keeps the handheld in while its pose does not change.
 * Zero it before first use, then set `box`, the screen rectangle it covers
 * (its size is the texture's). `can` is 0 not tried yet, 1 the renderer can
 * keep, -1 it cannot. `look` is the theme's colors the texture holds. */
typedef struct {
	SDL_Texture *tex;
	SDL_Rect box;
	td_pose kept;
	bool ok;
	int can;
	unsigned look[15];
} td_keep;

/* Draws the handheld in pose `p`: copies `k`'s texture if it holds `p` in
 * the theme in force, else draws `p` into the texture (the render target,
 * color and blend come back after) and copies that, or, where the renderer
 * cannot keep, draws it straight to the screen. */
void td_draw(SDL_Renderer *r, td_keep *k, const td_pose *p);

/* Draws `p` into `k`'s texture if it holds something else, and nothing on
 * the screen: the render target's switch, without the copy. A caller about
 * to clip calls it first, so the td_draw after it only copies and the clip
 * holds (a switch of the render target drops the clip). */
void td_make(SDL_Renderer *r, td_keep *k, const td_pose *p);

/* Lets `k`'s texture go; `k` can be used again after */
void td_keep_free(td_keep *k);

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
