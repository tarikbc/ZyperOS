/* SPDX-License-Identifier: GPL-3.0-only */
#include "folderart.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* Digits 5 wide and 7 high, a row a byte, the high bit of five the left */
static const unsigned char DIGITS[10][7] = {
	{ 0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e }, { 0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e },
	{ 0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f }, { 0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e },
	{ 0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02 }, { 0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e },
	{ 0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e }, { 0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 },
	{ 0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e }, { 0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c },
};
/* The label's paper and ink, and a sheet's: part of the picture, the same in
 * both themes, as the dyed folder is. Ink is ZyperOS's charcoal (**ink**). */
#define PAPER   0xf5f4eeu
#define PAPER2  0xdcd9ccu
#define INK     0x111310u

static unsigned shade(unsigned rgb, float f)
{
	int r = (int)lroundf(((rgb >> 16) & 255) * f), g = (int)lroundf(((rgb >> 8) & 255) * f),
	    b = (int)lroundf((rgb & 255) * f);

	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	return (unsigned)(r << 16 | g << 8 | b);
}

static unsigned mix(unsigned a, unsigned b, float t)
{
	int r = (int)lroundf(((a >> 16) & 255) * (1 - t) + ((b >> 16) & 255) * t);
	int g = (int)lroundf(((a >> 8) & 255) * (1 - t) + ((b >> 8) & 255) * t);
	int c = (int)lroundf((a & 255) * (1 - t) + (b & 255) * t);

	return (unsigned)(r << 16 | g << 8 | c);
}

static float luma(unsigned rgb)
{
	return .299f * ((rgb >> 16) & 255) + .587f * ((rgb >> 8) & 255) + .114f * (rgb & 255);
}

static void rect(SDL_Surface *s, int x, int y, int w, int h, unsigned rgb)
{
	if (w <= 0 || h <= 0) return;
	SDL_FillRect(s, &(SDL_Rect){ x, y, w, h },
	             SDL_MapRGBA(s->format, (Uint8)(rgb >> 16), (Uint8)(rgb >> 8), (Uint8)rgb, 255));
}

/* A box with a 1-cell edge */
static void box(SDL_Surface *s, int x, int y, int w, int h, unsigned fill, unsigned edge)
{
	rect(s, x, y, w, h, edge);
	rect(s, x + 1, y + 1, w - 2, h - 2, fill);
}

/* The count on a paper label, its right bottom corner at (rx, by) */
static void label(SDL_Surface *s, int rx, int by, int games, unsigned edge)
{
	char num[8];
	int n = snprintf(num, sizeof num, "%d", games < 0 ? 0 : games > 9999 ? 9999 : games);
	int w = n * 6 - 1 + 6, h = 7 + 6, x = rx - w, y = by - h;

	rect(s, x + 1, y + 1, w, h, shade(edge, .8f));      /* its shadow, down and right */
	box(s, x, y, w, h, PAPER, edge);
	for (int i = 0; i < n; i++)
		for (int r = 0; r < 7; r++)
			for (int c = 0; c < 5; c++)
				if (DIGITS[num[i] - '0'][r] & (0x10 >> c))
					rect(s, x + 3 + i * 6 + c, y + 3 + r, 1, 1, INK);
}

/* A cell of the design grid of 128 x 112 on a grid `s` wide and high */
#define GX(v) ((int)lroundf((v) * (float)s->w / FA_CARD_W))
#define GY(v) ((int)lroundf((v) * (float)s->h / FA_CARD_H))

/* The folder: its back with a tab at its top left, two sheets of paper
 * standing out of it, then its front, lower, so the back and the sheets show
 * above it; light along the front's top edge and shade along its foot */
static void folder(SDL_Surface *s, unsigned rgb)
{
	unsigned out = shade(rgb, .42f), back = shade(rgb, .74f), front = rgb;
	unsigned hi = mix(rgb, 0xffffffu, .35f), lo = shade(rgb, .86f);
	int tab_h = GY(8), left = GX(4), right = GX(124), top = GY(14), bot = GY(106);
	int ftop = GY(30), fbot = GY(108);

	/* The tab: wider toward the back, its right side a slope */
	for (int y = 0; y < tab_h; y++) {
		int x1 = GX(40) + y * GX(8) / (tab_h ? tab_h : 1);

		rect(s, left, top - tab_h + y, x1 - left, 1, y ? back : out);
		rect(s, left, top - tab_h + y, 1, 1, out);
		rect(s, x1 - 1, top - tab_h + y, 1, 1, out);
	}
	box(s, left, top, right - left, bot - top, back, out);
	box(s, GX(14), GY(19), GX(96), ftop - GY(19) + 1, PAPER2, out);
	box(s, GX(22), GY(23), GX(94), ftop - GY(23) + 1, PAPER, out);
	/* A line or two of writing on the front sheet */
	if (s->w >= FA_CARD_W) {
		rect(s, GX(30), GY(26), GX(40), 1, PAPER2);
		rect(s, GX(74), GY(26), GX(28), 1, PAPER2);
	}
	box(s, left, ftop, right - left, fbot - ftop, front, out);
	rect(s, left + 1, ftop + 1, right - left - 2, s->w >= FA_CARD_W ? 2 : 1, hi);
	rect(s, left + 1, fbot - 1 - GY(4), right - left - 2, GY(4), lo);
	/* Its foot's corners and the back's top right, a pixel in, as a pixel
	 * artist rounds them */
	if (s->w >= FA_CARD_W) {
		rect(s, left + 1, fbot - 2, 1, 1, out); rect(s, right - 2, fbot - 2, 1, 1, out);
		SDL_FillRect(s, &(SDL_Rect){ left, fbot - 1, 1, 1 }, 0);
		SDL_FillRect(s, &(SDL_Rect){ right - 1, fbot - 1, 1, 1 }, 0);
		SDL_FillRect(s, &(SDL_Rect){ right - 1, top, 1, 1 }, 0);
		rect(s, right - 2, top + 1, 1, 1, out);
	}
}

void fa_paint(SDL_Surface *s, unsigned rgb, int games, bool with_label)
{
	SDL_FillRect(s, NULL, 0);
	SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
	folder(s, rgb);
	if (with_label && s->w >= FA_CARD_W) label(s, 116, 102, games, shade(rgb, .42f));
}

void fa_name_box(unsigned rgb, SDL_Rect *b, unsigned *ink)
{
	*b = (SDL_Rect){ 10, 36, 106, 48 };
	*ink = luma(rgb) > 150 ? INK : PAPER;
}
