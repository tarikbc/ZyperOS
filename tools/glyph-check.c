/* SPDX-License-Identifier: GPL-3.0-only */
/* The marks drawn from strokes (src/ds/glyph.c), held to their limits and to
 * their cache.
 *
 * A mark is at most DS_GLYPH_STROKES strokes and DS_GLYPH_HEADS arrowheads;
 * past them the rest of it is not drawn, with no word said (PHONE asks for 29
 * of 32: one more arc and it loses its foot). So every mark is built here and
 * what it asks for is counted, kept or not.
 *
 * The cache keeps 48 (mark, size) textures and, full, lets the one drawn
 * longest ago go. A mark drawn, then 100 others, then drawn again must give
 * the same pixels: the slot that went was not the one in use. */
#include <SDL.h>
#include <stdio.h>
#include <string.h>

#include "ds/glyph.h"

static int failures;

/* In ui_glyph's order; a mark added there is named here too */
static const char *const NAME[] = {
	"REPEAT", "REPEAT_ONE", "SHUFFLE", "CHARGE", "TRANSFER", "TERMINAL", "USB", "WIFI",
	"ARROWS", "FINISH", "CANCEL", "PLAY", "CHECK", "HEADPHONES", "LAPTOP", "PHONE",
	"TO_CARD", "FROM_CARD", "COVER", "FILM", "CLOUD", "BOOK", "GRID", "HEART", "CLOCK",
};
_Static_assert(sizeof NAME / sizeof NAME[0] == UI_GLYPH_COUNT, "a ui_glyph with no name in tools/glyph-check.c");

static void fail(const char *what)
{
	printf("FAIL %s\n", what);
	failures++;
}

/* The mark at (32, 32), 48 across, white, read back */
static void shot(SDL_Renderer *r, SDL_Surface *s, ui_glyph g, int size, Uint32 *out)
{
	SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
	SDL_RenderClear(r);
	ui_glyph_draw(r, g, 32, 32, size, (SDL_Color){ 255, 255, 255, 255 });
	SDL_RenderPresent(r);
	memcpy(out, s->pixels, (size_t)s->pitch * s->h);
}

int main(void)
{
	static Uint32 before[64 * 64], after[64 * 64];
	SDL_Surface *s;
	SDL_Renderer *r;
	int g, strokes, heads, lit = 0, i;

	for (g = 0; g < UI_GLYPH_COUNT; g++) {
		char what[96];

		if (!ds_glyph_fits((ui_glyph)g, &strokes, &heads)) {
			snprintf(what, sizeof what, "%s asks for %d strokes and %d heads, past %d and %d", NAME[g],
			         strokes, heads, DS_GLYPH_STROKES, DS_GLYPH_HEADS);
			fail(what);
		}
		if (strokes + heads == 0) {
			snprintf(what, sizeof what, "%s has no stroke", NAME[g]);
			fail(what);
		}
	}

	if (SDL_Init(0) != 0) {
		printf("FAIL SDL: %s\n", SDL_GetError());
		return 1;
	}
	s = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888);
	r = s ? SDL_CreateSoftwareRenderer(s) : NULL;
	if (!r) {
		printf("FAIL a software renderer: %s\n", SDL_GetError());
		return 1;
	}
	shot(r, s, UI_GLYPH_PHONE, 48, before);
	for (i = 0; i < 64 * 64; i++) lit += (before[i] & 0xff) > 128;
	if (lit < 100) fail("PHONE at 48 draws almost nothing");
	/* 100 other (mark, size) pairs, more than the cache holds, PHONE at 48
	 * drawn again among them now and then so it stays */
	for (i = 0; i < 100; i++) {
		ui_glyph_draw(r, (ui_glyph)(i % UI_GLYPH_COUNT), 32, 32, 20 + i, (SDL_Color){ 255, 255, 255, 255 });
		if (i % 30 == 29) shot(r, s, UI_GLYPH_PHONE, 48, after);
	}
	shot(r, s, UI_GLYPH_PHONE, 48, after);
	if (memcmp(before, after, sizeof before)) fail("PHONE at 48 differs after the cache was full");
	/* and one long gone: drawn again, the same */
	shot(r, s, UI_GLYPH_CLOCK, 21, before);
	for (i = 0; i < 100; i++)
		ui_glyph_draw(r, (ui_glyph)(i % UI_GLYPH_COUNT), 32, 32, 200 + i, (SDL_Color){ 255, 255, 255, 255 });
	shot(r, s, UI_GLYPH_CLOCK, 21, after);
	if (memcmp(before, after, sizeof before)) fail("CLOCK at 21 differs when drawn again");
	ds_glyphs_free();
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(s);
	SDL_Quit();
	if (failures) return 1;
	printf("ok glyphs: %d marks within %d strokes and %d heads; the cache gives the same pixels\n",
	       UI_GLYPH_COUNT, DS_GLYPH_STROKES, DS_GLYPH_HEADS);
	return 0;
}
