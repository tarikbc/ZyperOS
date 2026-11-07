/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TOURDRAW_H
#define ZYPEROS_TOURDRAW_H

/* The tutorial on the screen (DESIGN.md 11.10): a pure function of
 * src/tour.h's state and the clock, drawn again every frame, as the tutorial
 * lab draws it. Every size is in the launcher's 1024x768 units and every
 * color is a role of the theme that is on.
 *
 * Zyper and the heart are main.c's, handed in, so there is one of each:
 * Zyper's moves and his shadow on Beard are draw_zyper's, and the heart is
 * the shelf's own. The volume and brightness bar is the launcher's real one
 * (plat_draw_osd), which the caller draws over this. */

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "tour.h"

/* Zyper as the tutorial draws him: posed by the rig (src/zyperrig.h's
 * zr_pose: tilt, tongue, ears, blink, the hop and breath as `dy`, which
 * leaves his shadow on Beard where it was, and a hop's squash), toward the
 * ground by `dim`; `alpha` is his opacity, and `shadow` how strong that
 * shadow is, 0 for none: his shadow is baked for the height he stands at,
 * so none while he pops in by scale. */
typedef struct {
	zr_pose pose;
	float   dim, shadow;
	Uint8   alpha;
} tour_zyper;

typedef struct {
	/* Zyper `h` tall about (cx, cy), the middle of his box where he
	 * stands. */
	void (*zyper)(void *ctx, float cx, float cy, float h, const tour_zyper *z);
	/* The shelf's heart, `rad` half its width, about (cx, cy). */
	void (*heart)(void *ctx, float cx, float cy, float rad, SDL_Color c);
	/* The power-off's Zyper, `t` ms into it, `h` tall, standing at (cx, cy)
	 * once in, in from `from_x` (the launcher's own frames, poweroff_frame's
	 * without the ground), at `alpha`: the power step's stage plays it. */
	void (*poweroff)(void *ctx, unsigned t, float cx, float cy, float h, float from_x,
	                 Uint8 alpha);
	void *ctx;
	/* Where ZyperOS is (P_ROOT): the choices' home screen previews are its
	 * res/tour/home-LAYOUT-THEME.png. NULL draws them as empty frames. */
	const char *root;
} tour_hooks;

/* The whole screen for `t` at `now`: the welcome, a step, or the finish.
 * It clears to the ground itself. Nothing for a tour that is over. */
void tour_draw(SDL_Renderer *r, const tour *t, uint32_t now, const tour_hooks *h);

/* A one-line message, DESIGN.md 11.8, at `op` of the way in: a menu panel
 * holding one row over **scrim.menu**, from 96% of its size and clear, as a
 * panel opens (8.2). For "Show Tutorial is in MENU." after a skip, over the
 * shelf. */
void tour_message(SDL_Renderer *r, const char *s, float op);

/* What the drawing made for itself - the details panel and the stand-in bar
 * - goes, before its renderer does. Made again the next time it is drawn. */
void tour_draw_free(void);

#endif
