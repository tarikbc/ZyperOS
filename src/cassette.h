/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CASSETTE_H
#define ZYPEROS_CASSETTE_H

/* MP4's Cassette look drawn (DESIGN.md 11.64): a C-cassette at its real
 * proportions, 100.4 by 63.8 mm, in a clear smoke-grey shell (my pick,
 * 2026-11-19), 592 by 376 units - 74 and 47 grid steps, the nearest pair of
 * whole steps to its 1.5737 (1.5745). Its parts are a drawing, in units of
 * that size, each one's millimetres beside it; its rectangles' edges on the
 * grid from its corner, so on the screen they are whole pixels too.
 *
 * The shell, the label and the window's glass are drawn here once, into
 * textures with soft edges, for a theme's colors; so are a reel's hub and a
 * tape pack. Each frame only copies them: the packs scaled to their radius,
 * the hubs turned (SDL_RenderCopyEx), the reels clipped to the window. The
 * label's words, the cover's stripe and the bars are drawn by the look
 * (src/nowplaying_looks.c) over them. */

#include <SDL.h>
#include <stdbool.h>

#include "design.h"

#define CAS_W 592
#define CAS_H 376

/* The label, the paper stuck on the shell: 87 x 35 mm from 6.8 and 5.4 */
#define CAS_LABEL_X0 40
#define CAS_LABEL_Y0 32
#define CAS_LABEL_X1 552
#define CAS_LABEL_Y1 240
/* The cover's stripe across it, over the window: 1.4 mm */
#define CAS_STRIPE_Y0 96
#define CAS_STRIPE_Y1 104
/* The side's letter in its box, the song beside it, its place at the
 * label's right: their row's middle, 11.5 mm down */
#define CAS_SIDE_X0  56
#define CAS_SIDE_Y0  48
#define CAS_SIDE     40
#define CAS_TITLE_X0 112
#define CAS_ROW_MID  68
#define CAS_LABEL_PAD 16
/* The full-screen tape's label (my picks in the Tape Styles lab,
 * 2026-11-23): a small cover first, at the song's row, 52 across; the
 * side's letter after it; the song after that */
#define CAS_COVER_X0 52
#define CAS_COVER    52
#define CAS_SIDE_X1  116
#define CAS_TITLE_X1 172
/* Under the window, the artist and the album: their line's middle */
#define CAS_UNDER_MID 224
/* The window, a pill 54.3 x 16.3 mm, in the middle */
#define CAS_WIN_X0 136
#define CAS_WIN_Y0 112
#define CAS_WIN_X1 456
#define CAS_WIN_Y1 208
/* The reels: 42.5 mm apart, on the window's middle; the hub 21.5 mm
 * across, a full pack of a C60 or a C90 50.2 */
#define CAS_REEL_DX  125.3f
#define CAS_REEL_Y   160.0f
#define CAS_HUB_R    63.4f
#define CAS_PACK_R   148.0f
/* The tape's speed, 4.76 cm a second, in units */
#define CAS_TAPE_SPEED (47.6f * CAS_W / 100.4f)

typedef struct {
	SDL_Texture *shell;     /* CAS_W x CAS_H: the shell, the label, the window's inside */
	SDL_Texture *glass;     /* the window's box: the label's paper round the pill, its edge, the glass */
	SDL_Texture *hub;       /* a reel's hub, 2 x CAS_HUB_R across, turned */
	SDL_Texture *pack;      /* a full pack, 2 x CAS_PACK_R across, scaled to its radius */
	ds_rgb paper, edge, ground;   /* what they were drawn for */
	bool dark;
	int  w, h;              /* the tape's size, units: the drawing at its scale */
} cas_tex;

/* The textures for the theme's colors (`paper` the label: **surface**), for
 * a tape `w` x `h` units, the drawing scaled to it and drawn at that size,
 * a texel a unit: on the Pixel 2 the screen is drawn 1024 x 768 and made
 * 640 x 480 whole, so a texture at the panel's pixels would be made larger
 * and smaller again. The hub and the pack are at the tape's scale down
 * (`h` over CAS_H), so they stay round. Made again only when those change.
 * False when one would not make. */
bool cas_ready(SDL_Renderer *r, cas_tex *t, const ds_theme *th, bool dark, int w, int h);
void cas_free(cas_tex *t);

#endif
