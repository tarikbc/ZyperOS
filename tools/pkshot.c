/* SPDX-License-Identifier: GPL-3.0-only */
/* Your PICO-8's screenshot: see pkshot.h. */
#include "pkshot.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

void pkshot_pick(const uint8_t *px, int fw, int fh, size_t pitch, const dmode_rect *r,
                 uint8_t out[PKSHOT_BYTES])
{
	const int turned = fh > fw, lw = turned ? fh : fw, lh = turned ? fw : fh;
	int i, j;

	for (j = 0; j < PKSHOT_SIDE; j++)
		for (i = 0; i < PKSHOT_SIDE; i++) {
			const int lx = r->x + (int)((i + 0.5) * r->w / PKSHOT_SIDE);
			const int ly = r->y + (int)((j + 0.5) * r->h / PKSHOT_SIDE);
			uint8_t *d = out + ((size_t)j * PKSHOT_SIDE + i) * 3;
			uint32_t v = 0;

			if (r->w > 0 && r->h > 0 && lx >= 0 && lx < lw && ly >= 0 && ly < lh) {
				const int x = turned ? ly : lx, y = turned ? lw - 1 - lx : ly;

				memcpy(&v, px + (size_t)y * pitch + (size_t)x * 4, 4);
			}
			d[0] = (uint8_t)(v >> 16);
			d[1] = (uint8_t)(v >> 8);
			d[2] = (uint8_t)v;
		}
}

uint8_t *pkshot_frame(const uint8_t *px, int fw, int fh, size_t pitch, int *w, int *h)
{
	const int turned = fh > fw, lw = turned ? fh : fw, lh = turned ? fw : fh;
	uint8_t *out;
	int lx, ly;

	if (fw <= 0 || fh <= 0 || !(out = malloc((size_t)lw * lh * 3))) return NULL;
	for (ly = 0; ly < lh; ly++)
		for (lx = 0; lx < lw; lx++) {
			const int x = turned ? ly : lx, y = turned ? lw - 1 - lx : ly;
			uint8_t *d = out + ((size_t)ly * lw + lx) * 3;
			uint32_t v;

			memcpy(&v, px + (size_t)y * pitch + (size_t)x * 4, 4);
			d[0] = (uint8_t)(v >> 16);
			d[1] = (uint8_t)(v >> 8);
			d[2] = (uint8_t)v;
		}
	*w = lw;
	*h = lh;
	return out;
}

void pkshot_thumb(const uint8_t *rgb, int w, int h, uint8_t out[PKSHOT_BYTES])
{
	/* Fitted whole, the rest black: the frame's own shape in the square */
	const int big = w > h ? w : h;
	const int tw = big > 0 ? PKSHOT_SIDE * w / big : 0, th = big > 0 ? PKSHOT_SIDE * h / big : 0;
	const int ox = (PKSHOT_SIDE - tw) / 2, oy = (PKSHOT_SIDE - th) / 2;
	int i, j;

	memset(out, 0, PKSHOT_BYTES);
	for (j = 0; j < th; j++)
		for (i = 0; i < tw; i++) {
			const int sx = (int)((i + 0.5) * w / tw), sy = (int)((j + 0.5) * h / th);

			memcpy(out + ((size_t)(oy + j) * PKSHOT_SIDE + ox + i) * 3, rgb + ((size_t)sy * w + sx) * 3, 3);
		}
}

static void png_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* A chunk at out: its length, type, data and CRC; the new end */
static uint8_t *png_chunk(uint8_t *out, const char *type, const uint8_t *data, uint32_t n)
{
	png_u32(out, n);
	memcpy(out + 4, type, 4);
	if (n) memcpy(out + 8, data, n);
	png_u32(out + 8 + n, (uint32_t)crc32(0L, out + 4, 4 + n));
	return out + 12 + n;
}

uint8_t *pkshot_png(const uint8_t *rgb, int w, int h, size_t *len)
{
	static const uint8_t SIG[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
	const size_t row = (size_t)w * 3 + 1, raw_n = row * (size_t)h;
	uint8_t *raw, *out, *o, ihdr[13];
	uLongf zn;
	int y;

	*len = 0;
	if (w <= 0 || h <= 0 || !(raw = malloc(raw_n))) return NULL;
	for (y = 0; y < h; y++) {
		raw[(size_t)y * row] = 0;
		memcpy(raw + (size_t)y * row + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
	}
	zn = compressBound((uLong)raw_n);
	if (!(out = malloc(8 + 25 + 12 + zn + 12))) {
		free(raw);
		return NULL;
	}
	memcpy(out, SIG, 8);
	png_u32(ihdr, (uint32_t)w);
	png_u32(ihdr + 4, (uint32_t)h);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;   /* 8-bit RGB */
	o = png_chunk(out + 8, "IHDR", ihdr, 13);
	if (compress2(o + 8, &zn, raw, (uLong)raw_n, 6) != Z_OK) {
		free(raw);
		free(out);
		return NULL;
	}
	free(raw);
	/* the IDAT written in place: its data is already at o + 8 */
	png_u32(o, (uint32_t)zn);
	memcpy(o + 4, "IDAT", 4);
	png_u32(o + 8 + zn, (uint32_t)crc32(0L, o + 4, (uInt)(4 + zn)));
	o += 12 + zn;
	o = png_chunk(o, "IEND", NULL, 0);
	*len = (size_t)(o - out);
	return out;
}

void pkshot_lay(uint32_t *notice, int w, int h, const int slot[4], const uint8_t rgb[PKSHOT_BYTES])
{
	const int sx = slot[0], sy = slot[1], side = slot[2], corner = slot[3];
	int x, y;

	if (side <= 0 || sx < 0 || sy < 0 || sx + side > w || sy + side > h) return;
	for (y = 0; y < side; y++)
		for (x = 0; x < side; x++) {
			const int dx = x < corner ? corner - x : (x >= side - corner ? x - (side - corner - 1) : 0);
			const int dy = y < corner ? corner - y : (y >= side - corner ? y - (side - corner - 1) : 0);
			const uint8_t *c = rgb + ((size_t)(y * PKSHOT_SIDE / side) * PKSHOT_SIDE + x * PKSHOT_SIDE / side) * 3;
			uint32_t *d = notice + (size_t)(sy + y) * w + sx + x, under = *d, a = 255, o = 0;
			int k;

			if (dx || dy) {
				const double dd = sqrt((double)dx * dx + (double)dy * dy) - corner;

				if (dd > 0.5) continue;
				if (dd > -0.5) a = (uint32_t)(255 * (0.5 - dd));
			}
			/* Over the slab, premultiplied: the shot's color at `a` */
			for (k = 0; k < 32; k += 8) {
				const uint32_t src = k == 24 ? 255 : c[2 - k / 8];

				o |= ((src * a + ((under >> k) & 255) * (255 - a) + 127) / 255) << k;
			}
			*d = o;
		}
}
