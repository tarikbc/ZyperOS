/* SPDX-License-Identifier: GPL-3.0-only */
/* The plain badge and Zyper, as pixels. See src/noticeart.h. */
#include "noticeart.h"

#include "blur.h"
#include "zyperplace.h"
#include "zypermark.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- shapes by their distance ------------------------------------------- */

/* Signed distances, in the units they are drawn in: negative inside */
static double sd_disc(double x, double y, double cx, double cy, double r)
{
	return sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) - r;
}

static double sd_box(double x, double y, double cx, double cy, double hx, double hy)
{
	double dx = fabs(x - cx) - hx, dy = fabs(y - cy) - hy;
	double ox = dx > 0.0 ? dx : 0.0, oy = dy > 0.0 ? dy : 0.0;
	double in = dx > dy ? dx : dy;

	return sqrt(ox * ox + oy * oy) + (in < 0.0 ? in : 0.0);
}

/* A coverage from a distance in pixels: the edge at 0, a pixel of blend */
static double cover(double d_px)
{
	double a = 0.5 - d_px;

	return a < 0.0 ? 0.0 : a > 1.0 ? 1.0 : a;
}

/* ---- the plain badge ----------------------------------------------------- */

/* The trophy in a 100-unit box, its middle at (50, 50): a cup (a box on a
 * half disc), a ring for a handle each side, a stem and a base. Drawn as one
 * shape, so a handle's inside is the cup's. */
static double trophy(double x, double y)
{
	double cup, ring, stem, base;

	y -= 3.0;   /* the shape runs 24 to 70.5: this puts its middle on 50 */
	cup = fmin(sd_box(x, y, 50.0, 29.0, 20.0, 5.0),
	           fmax(sd_disc(x, y, 50.0, 34.0, 20.0), 34.0 - y));
	ring = fmin(fabs(sd_disc(x, y, 27.0, 34.0, 9.0)),
	            fabs(sd_disc(x, y, 73.0, 34.0, 9.0))) - 2.3;
	stem = sd_box(x, y, 50.0, 58.0, 4.5, 5.5);
	base = sd_box(x, y, 50.0, 67.0, 15.0, 1.5) - 2.0;
	return fmin(fmin(cup, ring), fmin(stem, base));
}

SDL_Surface *noticeart_plain(int size, const ds_theme *t, bool locked)
{
	SDL_Surface *s;
	const ds_rgb tile = locked ? t->track : t->raised;
	const ds_rgb ink = locked ? t->text_dim : t->accent_ink;
	/* Locked is greyed and faded as the badges' own locked look is
	 * (src/rabadge.c): the tile at 70%, the trophy at about 80% of that */
	const double ta = locked ? 0.70 : 1.0, ia = locked ? 0.56 : 1.0;
	const double half = size / 2.0, rad = size / 6.0, k = 100.0 / size;
	int x, y;

	if (size <= 0) return NULL;
	if (!(s = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32, SDL_PIXELFORMAT_ARGB8888)))
		return NULL;
	for (y = 0; y < size; y++) {
		uint8_t *row = (uint8_t *)s->pixels + (size_t)y * s->pitch;

		for (x = 0; x < size; x++) {
			double px = x + 0.5, py = y + 0.5;
			double qx = fabs(px - half) - (half - rad), qy = fabs(py - half) - (half - rad);
			double dt = sqrt((qx > 0 ? qx : 0) * (qx > 0 ? qx : 0) + (qy > 0 ? qy : 0) * (qy > 0 ? qy : 0))
			            + (fmax(qx, qy) < 0.0 ? fmax(qx, qy) : 0.0) - rad;
			double a = cover(dt) * ta;                 /* the tile */
			double g = cover(trophy(px * k, py * k) / k) * ia;   /* the trophy */
			double oa = g + a * (1.0 - g);
			double r, gr, b;

			if (oa <= 0.0) { memset(row + x * 4, 0, 4); continue; }
			r  = (ink.r * g + tile.r * a * (1.0 - g)) / oa;
			gr = (ink.g * g + tile.g * a * (1.0 - g)) / oa;
			b  = (ink.b * g + tile.b * a * (1.0 - g)) / oa;
			row[x * 4 + 0] = (uint8_t)(b + 0.5);
			row[x * 4 + 1] = (uint8_t)(gr + 0.5);
			row[x * 4 + 2] = (uint8_t)(r + 0.5);
			row[x * 4 + 3] = (uint8_t)(oa * 255.0 + 0.5);
		}
	}
	return s;
}

/* ---- Zyper --------------------------------------------------------------- */

/* A triangle's inside, at the centers of the samples of a hi-res buffer.
 * Painted in the table's order, so what is drawn later covers what is
 * drawn before it, as SDL_RenderGeometry paints him in the menus. */
static void fill_tri(uint32_t *buf, int w, int h, const float *p, uint32_t col)
{
	float minx = fminf(p[0], fminf(p[2], p[4])), maxx = fmaxf(p[0], fmaxf(p[2], p[4]));
	float miny = fminf(p[1], fminf(p[3], p[5])), maxy = fmaxf(p[1], fmaxf(p[3], p[5]));
	int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx);
	int y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
	int x, y;

	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > w - 1) x1 = w - 1;
	if (y1 > h - 1) y1 = h - 1;
	for (y = y0; y <= y1; y++) {
		float py = y + 0.5f;

		for (x = x0; x <= x1; x++) {
			float px = x + 0.5f;
			float e0 = (p[2] - p[0]) * (py - p[1]) - (p[3] - p[1]) * (px - p[0]);
			float e1 = (p[4] - p[2]) * (py - p[3]) - (p[5] - p[3]) * (px - p[2]);
			float e2 = (p[0] - p[4]) * (py - p[5]) - (p[1] - p[5]) * (px - p[4]);

			if ((e0 >= 0.0f && e1 >= 0.0f && e2 >= 0.0f) ||
			    (e0 <= 0.0f && e1 <= 0.0f && e2 <= 0.0f))
				buf[(size_t)y * w + x] = col;
		}
	}
}

/* Samples to a side of a pixel: the hi-res buffer is drawn at this times
 * the size and averaged down, which is what keeps an edge two triangles
 * share from showing as a seam, as filling each with a coverage would */
#define Z_SS 4

SDL_Surface *noticeart_zyper(int h, bool shadow, const ds_theme *t, int *pad)
{
	const zr_pose pose = { ZYPER_TILT_DEG, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
	float xy[ZYPER_NVERT * 2], xy1[ZYPER_NVERT * 2];
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	blur_shadow bs = { 0 };
	SDL_Surface *s = NULL;
	uint32_t *hi = NULL;
	float *sh = NULL, *line = NULL;
	int i, x, y, zw, zh, m = 0, sw, sht;

	if (pad) *pad = 0;
	if (h <= 0) return NULL;
	/* Placed at the sample scale, about the origin, then moved to the corner
	 * of his extent: the same pose and brow as the menus' rest */
	zyper_place(0.0f, 0.0f, (float)h * Z_SS, &pose, zyper_brow_lift(ZYPER_TILT_DEG), xy);
	for (i = 0; i < ZYPER_NVERT; i++) {
		x0 = fminf(x0, xy[i * 2]);     x1 = fmaxf(x1, xy[i * 2]);
		y0 = fminf(y0, xy[i * 2 + 1]); y1 = fmaxf(y1, xy[i * 2 + 1]);
	}
	zw = (int)ceilf((x1 - x0) / Z_SS);
	zh = (int)ceilf((y1 - y0) / Z_SS);
	for (i = 0; i < ZYPER_NVERT; i++) {
		xy[i * 2]     -= x0;
		xy[i * 2 + 1] -= y0;
	}
	if (shadow) {
		/* As the menus' (src/zyperdraw.c): the blur alone, at a texel a unit */
		bs = blur_shadow_size(DS_SHADOW_FOCUS_SMALL_BLUR, 0, 1);
		m = bs.m;
	}
	sw = zw + 2 * m;
	sht = zh + 2 * m;

	hi = calloc((size_t)zw * Z_SS * (size_t)zh * Z_SS, sizeof *hi);
	s = SDL_CreateRGBSurfaceWithFormat(0, sw, sht, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!hi || !s) goto fail;
	for (i = 0; i + 2 < ZYPER_NVERT; i += 3)
		fill_tri(hi, zw * Z_SS, zh * Z_SS, &xy[i * 2],
		         0xFF000000u | (uint32_t)zyper_verts[i].r << 16 |
		         (uint32_t)zyper_verts[i].g << 8 | zyper_verts[i].b);

	if (shadow) {
		sh = malloc((size_t)sw * sht * sizeof *sh);
		line = malloc((size_t)(sw > sht ? sw : sht) * sizeof *line);
		if (!sh || !line) goto fail;
		for (i = 0; i < ZYPER_NVERT; i++) {
			xy1[i * 2]     = xy[i * 2] / Z_SS + m;
			xy1[i * 2 + 1] = xy[i * 2 + 1] / Z_SS + m;
		}
		if (!blur_fill_tris(sh, sw, sht, xy1, ZYPER_NVERT)) goto fail;
		blur_box3_by(sh, sw, sht, bs.rad, line);
	}

	for (y = 0; y < sht; y++) {
		uint8_t *row = (uint8_t *)s->pixels + (size_t)y * s->pitch;

		for (x = 0; x < sw; x++) {
			double za = 0.0, zr = 0.0, zg = 0.0, zb = 0.0, sa = 0.0, oa;
			int zx = x - m, zy = y - m;

			if (zx >= 0 && zx < zw && zy >= 0 && zy < zh) {
				int n = 0, u, v;
				unsigned r = 0, g = 0, b = 0;

				for (v = 0; v < Z_SS; v++)
					for (u = 0; u < Z_SS; u++) {
						uint32_t c = hi[(size_t)(zy * Z_SS + v) * (zw * Z_SS) + zx * Z_SS + u];

						if (!(c >> 24)) continue;
						n++;
						r += (c >> 16) & 255u; g += (c >> 8) & 255u; b += c & 255u;
					}
				if (n) {
					za = (double)n / (Z_SS * Z_SS);
					zr = (double)r / n; zg = (double)g / n; zb = (double)b / n;
				}
			}
			if (shadow && y - DS_SHADOW_FOCUS_SMALL_Y >= 0) {
				double v = sh[(size_t)(y - DS_SHADOW_FOCUS_SMALL_Y) * sw + x];

				sa = (v > 1.0 ? 1.0 : v) * DS_SHADOW_FOCUS_SMALL_A / 255.0;
			}
			/* Him over his shadow */
			oa = za + sa * (1.0 - za);
			if (oa <= 0.0) { memset(row + x * 4, 0, 4); continue; }
			row[x * 4 + 0] = (uint8_t)((zb * za + t->ink.b * sa * (1.0 - za)) / oa + 0.5);
			row[x * 4 + 1] = (uint8_t)((zg * za + t->ink.g * sa * (1.0 - za)) / oa + 0.5);
			row[x * 4 + 2] = (uint8_t)((zr * za + t->ink.r * sa * (1.0 - za)) / oa + 0.5);
			row[x * 4 + 3] = (uint8_t)(oa * 255.0 + 0.5);
		}
	}
	free(hi);
	free(sh);
	free(line);
	if (pad) *pad = m;
	return s;
fail:
	free(hi);
	free(sh);
	free(line);
	if (s) SDL_FreeSurface(s);
	return NULL;
}
