/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PIXTAPE_H
#define ZYPEROS_PIXTAPE_H

/* MP4's Cassette in pixel art (DESIGN.md 11.64, my picks in the Pixel
 * Tapes lab, 2026-11-23): all seven styles, drawn by code
 * on a grid of cells from src/cassette.h's drawing in millimetres, as the
 * home's own placeholders are (res/system-art/original/placeholders): one
 * dark outline cell round each part, a light row along the top and left
 * edges and a dark one along the bottom and right, flat tones, an ordered
 * dither where a smooth drawing has a gradient, no soft edge.
 *
 * A cell is mp4t_cell units, whole pixels on the panel: 8 on the Pixel 2
 * (5 pixels) and the Brick (8), 16 on the Smart Pro (15). The tape is a
 * whole number of cells each way, 120 x 76 at its largest with 8-unit cells.
 *
 * Made once, never a frame: the still parts as two textures a cell a texel
 * (the back - the shell, its screws, the head end, the window's inside; the
 * front - the label and its print, the window's frame and glass), drawn
 * again only when the style, the grid or the album changes (Cover Band's
 * band is the album's color; Worn's wear is seeded by the album's name, so
 * an album keeps the same ring and scuffs); and what turns as sprite
 * sheets: a pack of tape for every half cell of radius, a hub for every
 * MP4T_HUB_STEP degrees. Every copy is scaled up with nearest sampling
 * (SDL_SetTextureScaleMode), so a cell stays a block. A frame is six copies:
 * the back, two packs, two hubs, the front. Nothing is
 * drawn on the CPU in a frame and no texture is sent to the GPU. */

#include <SDL.h>
#include <stdbool.h>

#include "mp4look.h"

/* The grid: the tape `w` x `h` units in cells of `u` */
typedef struct {
	int   u, w, h;          /* units */
	int   cw, ch;           /* cells */
	float cx, cy;           /* the drawing's units a cell (src/cassette.h's 592 x 376) */
} pt_grid;

void pt_grid_make(pt_grid *g, int w, int h, int u);

/* A box of the drawing, (x0, y0)-(x1, y1) in its units, on the grid's
 * cells: in units from the tape's corner. Square boxes stay square. */
SDL_Rect pt_snap(const pt_grid *g, float x0, float y0, float x1, float y1);

/* The label's boxes the words go in or round, in units from the tape's
 * corner, the same the front's cells are drawn for: the cover, before the
 * side's letter, at the song's row; the side's letter's box; the label's
 * paper (Clear's card) */
typedef struct {
	SDL_Rect cover, side, paper;
	float    row_mid, under_mid;   /* the song's row's middle, the line under it's: units */
} pt_boxes;

void pt_boxes_of(const pt_grid *g, mp4t_id style, pt_boxes *b);

/* A style's materials and its print: the shell, the paper and the ink the
 * words are in (0xRRGGBB) */
typedef struct {
	unsigned shell, rim, inside, paper, hub, ink, dim, mark, print, outline;
	unsigned deco, deco2;   /* its stripes or bands; 0 for none or the cover's */
	float    shell_a;       /* Clear's see-through shell */
	bool     light_shell, dark_paper, worn;
} pt_mat;

const pt_mat *pt_material(mp4t_id style);

/* A sprite in a sheet: where, and its top-left from its reel's middle cell */
typedef struct {
	SDL_Rect src;           /* cells */
	int ox, oy;
} pt_sprite;

#define PT_PACKS_MAX 64

typedef struct {
	SDL_Texture *back, *front, *packs[2], *hubs;
	pt_sprite    pack[2][PT_PACKS_MAX];    /* [reel][half cells - first] */
	int          pack_first, pack_n;
	pt_sprite    hub[MP4T_HUB_FRAMES];
	SDL_Rect     clip;                     /* cells: the window, or Clear's inside */
	int          reel_ci[2], reel_cj;      /* the reels' middle cells */
	/* What they were drawn for */
	mp4t_id      style;
	int          cw, ch, u;
	unsigned     album, band_rgb;
	bool         made;
} pt_tex;

/* The textures for `style` on grid `g`, for the album named `album` and
 * whose cover's main color is `band_rgb`: made again only when one of them
 * changes. False when one would not make. */
bool pt_ready(SDL_Renderer *r, pt_tex *t, mp4t_id style, const pt_grid *g, const char *album,
              unsigned band_rgb);
void pt_free(pt_tex *t);

/* One frame of the tape at (x, y): the back, the packs with the tape at `f`
 * (mp4l_tape's), the hubs at `ang`, the front */
void pt_draw(SDL_Renderer *r, const pt_tex *t, const pt_grid *g, int x, int y, float f,
             const float ang[2]);

/* The cells drawn, for a check: `which` 0 the back, 1 the front, into
 * `out` (cw x ch RGBA bytes), for the style and album */
bool pt_cells(mp4t_id style, const pt_grid *g, const char *album, unsigned band_rgb, int which,
              unsigned char *out);

#endif
