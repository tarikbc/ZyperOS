/* SPDX-License-Identifier: GPL-3.0-only */
/* Recents' Last Screen card. See src/screencard.h. */
#include "screencard.h"
#include "cartlabel.h"

#include <SDL_image.h>
#include <math.h>

static SDL_Surface *argb(SDL_Surface *s)
{
	return s ? SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0) : NULL;
}

/* `src` scaled up `k` times whole, by the nearest pixel, opaque */
static SDL_Surface *scale_up(SDL_Surface *src, int k)
{
	SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, src->w * k, src->h * k, 32,
	                                                  SDL_PIXELFORMAT_ARGB8888);
	int x, y;

	if (!out) return NULL;
	for (y = 0; y < out->h; y++) {
		const Uint32 *row = (const Uint32 *)((const Uint8 *)src->pixels + (y / k) * src->pitch);
		Uint32 *to = (Uint32 *)((Uint8 *)out->pixels + y * out->pitch);

		for (x = 0; x < out->w; x++) to[x] = row[x / k] | 0xff000000u;
	}
	return out;
}

/* `src` averaged down to `w` x `h` (a box filter: each pixel the mean of the
 * source pixels under it), its alpha kept */
static SDL_Surface *scale_down(SDL_Surface *src, int w, int h)
{
	SDL_Surface *out = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	int x, y;

	if (!out) return NULL;
	for (y = 0; y < h; y++) {
		int y0 = y * src->h / h, y1 = (y + 1) * src->h / h;
		Uint32 *to = (Uint32 *)((Uint8 *)out->pixels + y * out->pitch);

		if (y1 <= y0) y1 = y0 + 1;
		for (x = 0; x < w; x++) {
			int x0 = x * src->w / w, x1 = (x + 1) * src->w / w, sx, sy;
			unsigned long a = 0, r = 0, g = 0, b = 0, n = 0;

			if (x1 <= x0) x1 = x0 + 1;
			for (sy = y0; sy < y1 && sy < src->h; sy++) {
				const Uint32 *row = (const Uint32 *)((const Uint8 *)src->pixels + sy * src->pitch);

				for (sx = x0; sx < x1 && sx < src->w; sx++) {
					Uint32 p = row[sx];
					unsigned pa = p >> 24;

					/* Weighted by alpha, so a clear edge does not darken */
					a += pa;
					r += ((p >> 16) & 0xff) * pa;
					g += ((p >> 8) & 0xff) * pa;
					b += (p & 0xff) * pa;
					n++;
				}
			}
			if (!n || !a) { to[x] = 0; continue; }
			to[x] = (Uint32)(a / n) << 24 | (Uint32)(r / a) << 16 | (Uint32)(g / a) << 8 | (Uint32)(b / a);
		}
	}
	return out;
}

/* `c` over the opaque pixel `d` at `alpha` (0 to 255) */
static Uint32 over(Uint32 d, Uint32 c, unsigned alpha)
{
	unsigned r = ((c >> 16) & 0xff) * alpha + ((d >> 16) & 0xff) * (255 - alpha);
	unsigned g = ((c >> 8) & 0xff) * alpha + ((d >> 8) & 0xff) * (255 - alpha);
	unsigned b = (c & 0xff) * alpha + (d & 0xff) * (255 - alpha);

	return 0xff000000u | (r / 255) << 16 | (g / 255) << 8 | (b / 255);
}

/* How much of the pixel at (x, y) is inside a w x h box with corners round
 * by `rad`, 0 to 1 */
static float inside(int x, int y, int w, int h, float rad)
{
	float px = x + 0.5f, py = y + 0.5f, cx, cy, d;

	if (rad <= 0) return 1.0f;
	cx = px < rad ? rad : px > w - rad ? w - rad : px;
	cy = py < rad ? rad : py > h - rad ? h - rad : py;
	d = sqrtf((px - cx) * (px - cx) + (py - cy) * (py - cy));
	return d <= rad - 0.5f ? 1.0f : d >= rad + 0.5f ? 0.0f : rad + 0.5f - d;
}

SDL_Surface *screen_card_from(SDL_Surface *frame, SDL_Surface *cover)
{
	SDL_Surface *f = argb(frame), *c = NULL, *out;
	int k, cw, ch, inset, sh, x, y, x0, y0;
	float rad;

	if (!f) return NULL;
	/* Whole times toward SC_HEIGHT, never under one: a Game Boy's 144 three
	 * times, a SNES's 224 twice, a DS's 384 once */
	k = (SC_HEIGHT + f->h / 2) / f->h;
	if (k < 1) k = 1;
	if (k > 4) k = 4;
	out = scale_up(f, k);
	SDL_FreeSurface(f);
	if (!out || !cover || !(c = argb(cover)) || c->w <= 0 || c->h <= 0) {
		if (c) SDL_FreeSurface(c);
		return out;
	}
	ch = out->h * SC_COVER_SHARE / 100;
	cw = c->w * ch / c->h;
	/* A wide box (a Genesis cart's, a PSP's) no wider than 45% of the card */
	if (cw > out->w * 45 / 100) {
		cw = out->w * 45 / 100;
		ch = c->h * cw / c->w;
	}
	if (cw < 4 || ch < 4) { SDL_FreeSurface(c); return out; }
	{
		SDL_Surface *small = scale_down(c, cw, ch);

		SDL_FreeSurface(c);
		if (!(c = small)) return out;
	}
	inset = out->h * SC_INSET_SHARE / 100;
	sh = ch / 40 > 2 ? ch / 40 : 2;
	rad = ch * 0.05f;
	x0 = inset;
	y0 = out->h - inset - ch;
	/* Its shadow: the ink at 40%, down and right, its corners as round */
	for (y = 0; y < ch; y++) {
		Uint32 *to = (Uint32 *)((Uint8 *)out->pixels + (y0 + sh + y) * out->pitch);

		if (y0 + sh + y >= out->h) break;
		for (x = 0; x < cw && x0 + sh + x < out->w; x++) {
			unsigned a = (unsigned)(102 * inside(x, y, cw, ch, rad));

			if (a) to[x0 + sh + x] = over(to[x0 + sh + x], 0x101010u, a);
		}
	}
	/* The cover, by its own alpha and the round corners */
	for (y = 0; y < ch; y++) {
		const Uint32 *row = (const Uint32 *)((const Uint8 *)c->pixels + y * c->pitch);
		Uint32 *to = (Uint32 *)((Uint8 *)out->pixels + (y0 + y) * out->pitch);

		for (x = 0; x < cw; x++) {
			unsigned a = (unsigned)((row[x] >> 24) * inside(x, y, cw, ch, rad));

			if (a) to[x0 + x] = over(to[x0 + x], row[x], a);
		}
	}
	SDL_FreeSurface(c);
	return out;
}

SDL_Surface *screen_card_make(const char *frame, const char *cover, const ui_card_order *card,
                              bool away)
{
	SDL_Surface *f, *c = NULL, *out;
	bool painted = false;

	if (!frame || !*frame || !(f = IMG_Load(frame))) return NULL;
	if (cover && *cover) c = cart_label_load(cover);   /* a PICO-8 cart as its label */
	if (!c && card && (c = ui_card_paint(card, away))) painted = true;
	out = screen_card_from(f, c);
	SDL_FreeSurface(f);
	if (c && painted) ui_card_free(c);
	else if (c) SDL_FreeSurface(c);
	return out;
}
