/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_LOOKART_H
#define ZYPEROS_LOOKART_H

/* The Player Looks' drawings made once (src/cassette.c, src/vinyl.c): a
 * canvas of straight-alpha floats on the CPU, each shape painted over the
 * last as a signed distance (src/ds/sdf.h) with a one-unit soft edge, then
 * made a texture. Drawn when a theme or an album changes, never a frame. */

#include <SDL.h>
#include <stdbool.h>

#include "design.h"

typedef struct { float r, g, b, a; } la_rgba;
/* w x h of r, g, b, a; a drawing's unit `kx` x `ky` of its pixels, so a
 * drawing in its own units is painted at any size (1 by la_new) */
typedef struct { int w, h; float *p; float kx, ky; } la_canvas;

/* A shape's signed distance at (x, y), from its numbers `q`; a color at
 * (x, y) where it is not one color */
typedef float (*la_sd)(const float *q, float x, float y);
typedef la_rgba (*la_col)(const float *q, float x, float y);

bool la_new(la_canvas *c, int w, int h);
/* A canvas w x h pixels for a drawing scaled kx across and ky down: the
 * shapes' numbers, boxes and colors' places stay the drawing's units */
bool la_new_scaled(la_canvas *c, int w, int h, float kx, float ky);
/* `sd`'s shape over the box (x0, y0)-(x1, y1), in `col` or `fn`'s colors */
void la_paint(la_canvas *c, la_sd sd, la_col fn, const float *q, la_rgba col, float x0, float y0,
              float x1, float y1);
/* The canvas as a texture, blended and linearly sampled; the canvas freed */
SDL_Texture *la_texture(SDL_Renderer *r, la_canvas *c);

/* The shapes. q: x0 y0 x1 y1 r (a box with round corners); the same and w,
 * its outline w wide; cx cy r (a disc); cx cy r w (a ring); x0 y0 x1 y1 w (a
 * stroke with round ends); four corners (a quadrilateral) */
float la_rbox(const float *q, float x, float y);
float la_rbox_line(const float *q, float x, float y);
float la_disc(const float *q, float x, float y);
float la_ring(const float *q, float x, float y);
float la_seg(const float *q, float x, float y);
float la_poly4(const float *q, float x, float y);

la_rgba la_mix(la_rgba a, la_rgba b, float t);
la_rgba la_role(ds_rgb c);
la_rgba la_hex(unsigned rgb, float a);

#endif
