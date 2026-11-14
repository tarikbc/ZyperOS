/* SPDX-License-Identifier: GPL-3.0-only */
/* The art badge on screen: each (badge, theme, size) painted once by
 * ab_render into a texture and blitted from then on. See src/artbadge_draw.h.
 *
 * A row in motion draws a card at every size between a side card's and the
 * focused one's, and the badge goes with it a whole unit at a time
 * (ab_diameter): 29 to 40 on a games shelf, a dozen sizes, each painted the
 * first time it comes round, about 2300 pixels, and kept. AB_CACHE holds a
 * shelf's badges at all of them, in both themes, with room; past it the one
 * used longest ago makes way. Kept for the renderer they were made on, as the
 * top bar's marks are (src/topbar.c). */
#include "artbadge_draw.h"

#include "theme.h"

#include <math.h>

#define AB_CACHE 48
#define AB_MAX_SIDE (AB_D * 2 + AB_PAD * 2)

static struct {
	SDL_Texture *tex;
	SDL_Renderer *r;
	int kind, theme, d;
	/* ...in these colors: MENU > Display > Theme changes the accent the
	 * badges are drawn in, and a badge kept from before would not follow */
	uint32_t glyph, disc;
	unsigned used;
} cache[AB_CACHE];

static uint32_t rgb_of(ds_rgb c) { return (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b; }
static unsigned clock_;

static SDL_Texture *badge_tex(SDL_Renderer *r, ab_kind kind, int theme, int d)
{
	static uint32_t px[AB_MAX_SIDE * AB_MAX_SIDE];
	int i, slot = -1, side = ab_buf_side(d);
	uint32_t glyph = rgb_of(ab_glyph_color(kind, theme)), disc = rgb_of(ab_disc_color(theme));

	if (side > AB_MAX_SIDE) return NULL;
	for (i = 0; i < AB_CACHE; i++) {
		if (cache[i].tex && cache[i].r == r && cache[i].kind == (int)kind &&
		    cache[i].theme == theme && cache[i].d == d &&
		    cache[i].glyph == glyph && cache[i].disc == disc) {
			cache[i].used = ++clock_;
			return cache[i].tex;
		}
		/* An empty slot first, else the one used longest ago */
		if (slot < 0 || (cache[slot].tex &&
		    (!cache[i].tex || cache[i].used < cache[slot].used)))
			slot = i;
	}
	if (cache[slot].tex) SDL_DestroyTexture(cache[slot].tex);
	cache[slot].tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888,
	                                    SDL_TEXTUREACCESS_STATIC, side, side);
	if (!cache[slot].tex) return NULL;
	ab_render(px, side, side, d, theme, kind);
	SDL_UpdateTexture(cache[slot].tex, NULL, px, side * 4);
	SDL_SetTextureBlendMode(cache[slot].tex, SDL_BLENDMODE_BLEND);
	cache[slot].r = r;
	cache[slot].kind = (int)kind;
	cache[slot].theme = theme;
	cache[slot].d = d;
	cache[slot].glyph = glyph;
	cache[slot].disc = disc;
	cache[slot].used = ++clock_;
	return cache[slot].tex;
}

void ab_draw(SDL_Renderer *r, ab_kind kind, float cx, float cy, float k, Uint8 alpha)
{
	int d = ab_diameter(k), side, theme = theme_index();
	SDL_Texture *t;

	if (!d || !alpha || kind < 0 || kind >= AB_KINDS) return;
	if (!(t = badge_tex(r, kind, theme, d))) return;
	side = ab_buf_side(d);
	SDL_SetTextureAlphaMod(t, alpha);
	/* On whole units, so the badge is the pixels it was painted as and not
	 * a resampling of them */
	SDL_RenderCopy(r, t, NULL, &(SDL_Rect){ (int)lroundf(cx - side / 2.0f),
	                                        (int)lroundf(cy - side / 2.0f), side, side });
}

void ab_draw_at(SDL_Renderer *r, ab_kind kind, float x, float y, float k, Uint8 alpha)
{
	float cx, cy;

	ab_center(x, y, k, &cx, &cy);
	ab_draw(r, kind, cx, cy, k, alpha);
}
