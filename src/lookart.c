/* SPDX-License-Identifier: GPL-3.0-only */
/* See lookart.h. */
#include "lookart.h"

#include "ds/sdf.h"

#include <math.h>
#include <stdlib.h>

bool la_new(la_canvas *c, int w, int h)
{
	return la_new_scaled(c, w, h, 1.0f, 1.0f);
}

bool la_new_scaled(la_canvas *c, int w, int h, float kx, float ky)
{
	c->w = w;
	c->h = h;
	c->kx = kx;
	c->ky = ky;
	c->p = calloc((size_t)w * h * 4, sizeof *c->p);
	return c->p != NULL;
}

/* `col` over pixel (x, y) at coverage k */
static void put(la_canvas *c, int x, int y, la_rgba col, float k)
{
	float *d = c->p + ((size_t)y * c->w + x) * 4;
	float a = col.a * k, out = a + d[3] * (1 - a);

	if (a <= 0 || out <= 0) return;
	d[0] = (col.r * a + d[0] * d[3] * (1 - a)) / out;
	d[1] = (col.g * a + d[1] * d[3] * (1 - a)) / out;
	d[2] = (col.b * a + d[2] * d[3] * (1 - a)) / out;
	d[3] = out;
}

/* A shape, its signed distance `sd` at a pixel's middle, over the box
 * (x0, y0)-(x1, y1); the color `fn`'s at that pixel when it is given */

void la_paint(la_canvas *c, la_sd sd, la_col fn, const float *q, la_rgba col, float x0, float y0,
                  float x1, float y1)
{
	/* A pixel's distance from the shape: the drawing's, at its scale */
	const float k = (c->kx + c->ky) / 2;
	int ix0 = (int)floorf(x0 * c->kx) - 1, iy0 = (int)floorf(y0 * c->ky) - 1;
	int ix1 = (int)ceilf(x1 * c->kx) + 1, iy1 = (int)ceilf(y1 * c->ky) + 1, x, y;

	if (ix0 < 0) ix0 = 0;
	if (iy0 < 0) iy0 = 0;
	if (ix1 > c->w) ix1 = c->w;
	if (iy1 > c->h) iy1 = c->h;
	for (y = iy0; y < iy1; y++)
		for (x = ix0; x < ix1; x++) {
			float px = ((float)x + 0.5f) / c->kx, py = ((float)y + 0.5f) / c->ky;
			float cov = ds_sd_clamp(0.5f - sd(q, px, py) * k, 0, 1);

			if (cov > 0) put(c, x, y, fn ? fn(q, px, py) : col, cov);
		}
}

/* q: x0 y0 x1 y1 r */
float la_rbox(const float *q, float x, float y)
{
	return ds_sd_rbox(x, y, q[0], q[1], q[2], q[3], q[4]);
}

/* q: x0 y0 x1 y1 r w: the box's outline, w wide, centred on its edge */
float la_rbox_line(const float *q, float x, float y)
{
	return fabsf(ds_sd_rbox(x, y, q[0], q[1], q[2], q[3], q[4])) - q[5] / 2;
}

/* q: cx cy r */
float la_disc(const float *q, float x, float y)
{
	return hypotf(x - q[0], y - q[1]) - q[2];
}

/* q: cx cy r w */
float la_ring(const float *q, float x, float y)
{
	return fabsf(hypotf(x - q[0], y - q[1]) - q[2]) - q[3] / 2;
}

/* q: x0 y0 x1 y1 w: a stroke with round ends */
float la_seg(const float *q, float x, float y)
{
	return ds_sd_segment(x, y, q[0], q[1], q[2], q[3]) - q[4] / 2;
}


/* q: four corners */
float la_poly4(const float *q, float x, float y)
{
	return ds_sd_poly(q, 4, x, y);
}

la_rgba la_mix(la_rgba a, la_rgba b, float t)
{
	t = ds_sd_clamp(t, 0, 1);
	return (la_rgba){ a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
	                  a.a + (b.a - a.a) * t };
}

la_rgba la_role(ds_rgb c)
{
	return (la_rgba){ c.r / 255.f, c.g / 255.f, c.b / 255.f, 1 };
}

la_rgba la_hex(unsigned rgb, float a)
{
	return (la_rgba){ ((rgb >> 16) & 255) / 255.f, ((rgb >> 8) & 255) / 255.f, (rgb & 255) / 255.f, a };
}

SDL_Texture *la_texture(SDL_Renderer *r, la_canvas *c)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, c->w, c->h, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Texture *t = NULL;
	int x, y;

	if (s && SDL_LockSurface(s) == 0) {
		for (y = 0; y < c->h; y++) {
			Uint32 *row = (Uint32 *)((Uint8 *)s->pixels + (size_t)y * s->pitch);

			for (x = 0; x < c->w; x++) {
				const float *d = c->p + ((size_t)y * c->w + x) * 4;

				row[x] = (Uint32)(d[3] * 255 + 0.5f) << 24 | (Uint32)(d[0] * 255 + 0.5f) << 16 |
				         (Uint32)(d[1] * 255 + 0.5f) << 8 | (Uint32)(d[2] * 255 + 0.5f);
			}
		}
		SDL_UnlockSurface(s);
		t = SDL_CreateTextureFromSurface(r, s);
		if (t) {
			SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
		}
	}
	if (s) SDL_FreeSurface(s);
	free(c->p);
	c->p = NULL;
	return t;
}
