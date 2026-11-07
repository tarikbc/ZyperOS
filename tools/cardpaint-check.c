/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* The card for a game or an album with no art, drawn on an art worker
 * (src/ui.h's ui_card_paint, src/texload.h's texload_want_or_draw), held to
 * the card the frame draws, on the launcher's own objects.
 *
 * Four things, each of which a handheld shows only as a card that is wrong
 * or a launcher that crashes now and then:
 *
 * - The same pixels. A worker draws in faces of its own, and a card drawn
 *   off the frame that is a pixel off the frame's is another card. Every
 *   kind of title - one line, four, a word too long for the card, a first
 *   letter that is lower case or not a letter at all, the word MENU, and
 *   titles in Japanese and Chinese, in each language's faces - in both
 *   themes, for a game and for an album, with a system's color and with
 *   none, drawn by a worker and by the frame, and compared byte for byte.
 * - The theme. A card drawn for the theme that was, landing after a turn,
 *   is never handed to the shelf, which keeps what it shows until the card
 *   of the theme that is on comes.
 * - The fonts going down. Around a game run the old way the launcher closes
 *   its fonts (ui_quit) and opens them again (ui_init) while the workers go
 *   on: cards asked for then are left to the frame, none is drawn while the
 *   fonts are down, the frame can close them while a worker is in the middle
 *   of one, and the cards after are the frame's pixels again.
 * - Nothing to draw with: no faces, or no worker, and the card is the
 *   frame's to make, as it always was.
 *
 * And one thing the two ways cannot tell apart, since they choose a face the
 * same way: that the choice is the language's. 日本語 is Japanese's own name,
 * kanji alone, which the Japanese and the Chinese faces both have: in English
 * it is set in the Japanese face, as its name, and with Chinese on in the
 * Chinese one, the language's own first - so the two cards differ, and an
 * order drawn after the language changed is still drawn in its own.
 *
 * SDL's software renderer and no window (SDL_VIDEODRIVER=dummy).
 */
#include "config.h"
#include "i18n.h"
#include "texload.h"
#include "theme.h"
#include "ui.h"

#include <SDL_image.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;

static void ck(int cond, const char *what, const char *detail)
{
	if (!cond) {
		printf("  FAIL %s%s%s\n", what, detail ? ": " : "", detail ? detail : "");
		fails++;
	}
}

static const char *const TITLES[] = {
	"Contra",
	"castlevania",                                   /* a lower-case first letter */
	"The Legend of Zelda: A Link to the Past",       /* lines */
	"Super Mario Bros. 3 Super Mario Bros. 3 Super Mario Bros. 3 Super Mario",
	"Supercalifragilisticexpialidociousandthensomemorethanacardcanhold",
	"   Spaced    Out   ",
	"\xc5\x8ckami",                                  /* Okami: no watermark */
	"8 Eyes",
	"MENU Mayhem",                                    /* the key's glyph */
	"",
	"\xe3\x82\xbc\xe3\x83\xab\xe3\x83\x80\xe3\x81\xae\xe4\xbc\x9d\xe8\xaa\xac",  /* Japanese */
	"\xe4\xbb\x99\xe5\x89\x91\xe5\xa5\x87\xe4\xbe\xa0\xe4\xbc\xa0",              /* Chinese */
	"Final Fantasy \xe3\x82\xa4\xe3\x83\xb3\xe3\x82\xbf\xe3\x83\xbc",            /* both */
};
#define NTITLES ((int)(sizeof TITLES / sizeof TITLES[0]))

/* Byte for byte, row by row: the pitch may differ, the pixels may not */
static bool same_pixels(SDL_Surface *a, SDL_Surface *b)
{
	if (!a || !b || a->w != b->w || a->h != b->h ||
	    a->format->format != b->format->format)
		return false;
	for (int y = 0; y < a->h; y++)
		if (memcmp((Uint8 *)a->pixels + (size_t)y * a->pitch,
		           (Uint8 *)b->pixels + (size_t)y * b->pitch, (size_t)a->w * 4))
			return false;
	return true;
}

/* A worker's answer for (7, idx), within two seconds */
static bool wait_for(int idx, texload_got *out)
{
	Uint32 until = SDL_GetTicks() + 2000;

	while (SDL_GetTicks() < until) {
		texload_got g;

		while (texload_take(&g)) {
			if (g.sys == 7 && g.idx == idx) { *out = g; return true; }
			if (g.surf) SDL_FreeSurface(g.surf);
		}
		SDL_Delay(1);
	}
	return false;
}

/* Every title in this theme and language, a game's and an album's, with a
 * color and without: drawn by the frame, here, and by a worker, through
 * texload_want_or_draw with a file that is not there, as a shelf asks. */
static void same_both_ways(const char *where)
{
	static int idx;
	const unsigned colors[2] = { 0xE6312E, CFG_NO_COLOR };
	char what[512];

	for (int t = 0; t < NTITLES; t++)
		for (int album = 0; album < 2; album++)
			for (int c = 0; c < 2; c++) {
				ui_card_order o;
				SDL_Surface *frame, *here;
				texload_got g = { 0 };

				ui_card_order_set(&o, TITLES[t], colors[c], album);
				snprintf(what, sizeof what, "%s, %s \"%s\"%s", where,
				         album ? "an album's" : "a game's", TITLES[t],
				         c ? ", no color" : "");
				frame = ui_card_paint(&o, false);
				here = ui_card_paint(&o, true);
				ck(frame && frame->w == 512 && frame->h == (album ? 512 : 656),
				   "the frame's card is the card's size", what);
				ck(same_pixels(frame, here), "drawn away from the frame, the frame's pixels", what);

				idx++;
				ck(texload_want_or_draw(7, idx, "/no/such/.media/card.png", NULL, &o),
				   "a worker takes the ask", what);
				if (!wait_for(idx, &g)) {
					ck(0, "a worker answers", what);
				} else {
					ck(g.card && g.theme == o.theme_ix,
					   "the answer is a card, in the theme it was asked in", what);
					ck(same_pixels(frame, g.surf), "a worker's card is the frame's pixels", what);
				}
				if (g.surf) SDL_FreeSurface(g.surf);
				SDL_FreeSurface(frame);
				if (here) SDL_FreeSurface(here);
			}
}

static int delivered, delivered_theme;
static SDL_Surface *delivered_surf;

static void count_put(void *ctx, texload_got *g)
{
	(void)ctx;
	delivered++;
	delivered_theme = g->theme;
	if (delivered_surf) SDL_FreeSurface(delivered_surf);
	delivered_surf = g->surf;
}

/* A card asked for in Beard that lands after the turn to Salt & Pepper, and
 * the same slot asked for again in Salt & Pepper: the shelf is handed the
 * second and never the first. */
static void the_turn(void)
{
	ui_card_order beard, pepper;
	SDL_Surface *want;
	Uint32 until;

	theme_init(APPEAR_LIGHT);
	ui_card_order_set(&beard, "Metroid", 0x5A3FD8, false);
	texload_want_or_draw(7, 900, "/no/such/.media/Metroid.png", NULL, &beard);
	until = SDL_GetTicks() + 2000;
	while (texload_waiting() < 1 && SDL_GetTicks() < until) SDL_Delay(1);
	ck(texload_waiting() == 1, "the card of the theme that was has landed", NULL);

	theme_init(APPEAR_DARK);
	delivered = 0;
	texload_deliver(theme_index(), count_put, NULL);
	ck(delivered == 0, "a card of the theme that was is not handed over", NULL);
	ck(texload_waiting() == 0, "and is not left waiting either", NULL);

	ui_card_order_set(&pepper, "Metroid", 0x5A3FD8, false);
	ck(pepper.theme_ix == DS_DARK, "an order is in the theme that is on", NULL);
	want = ui_card_paint(&pepper, false);
	texload_want_or_draw(7, 900, "/no/such/.media/Metroid.png", NULL, &pepper);
	until = SDL_GetTicks() + 2000;
	while (!delivered && SDL_GetTicks() < until) {
		texload_deliver(theme_index(), count_put, NULL);
		SDL_Delay(1);
	}
	ck(delivered == 1 && delivered_theme == DS_DARK,
	   "the card of the theme that is on is handed over", NULL);
	ck(same_pixels(want, delivered_surf), "in that theme's pixels", NULL);

	/* Asked in Beard and in Salt & Pepper before either is drawn, as a turn
	 * between two frames does: whichever lands, Salt & Pepper's alone is
	 * handed over. */
	delivered = 0;
	theme_init(APPEAR_LIGHT);
	ui_card_order_set(&beard, "Kirby", 0xF080A0, false);
	theme_init(APPEAR_DARK);
	ui_card_order_set(&pepper, "Kirby", 0xF080A0, false);
	texload_want_or_draw(7, 901, NULL, NULL, &beard);
	texload_want_or_draw(7, 901, NULL, NULL, &pepper);
	until = SDL_GetTicks() + 300;
	while (SDL_GetTicks() < until) {
		texload_deliver(theme_index(), count_put, NULL);
		SDL_Delay(1);
	}
	ck(delivered == 1 && delivered_theme == DS_DARK,
	   "asked in both themes at once, only the one on is handed over", NULL);
	SDL_FreeSurface(want);
	if (delivered_surf) SDL_FreeSurface(delivered_surf);
	delivered_surf = NULL;
	theme_init(APPEAR_LIGHT);
}

/* What a worker answers for asks made while the fonts are down, and that it
 * answers at all */
static int drawn_while_down, declined_while_down;

static void down_put(void *ctx, texload_got *g)
{
	(void)ctx;
	if (g->card) drawn_while_down++;
	else declined_while_down++;
	if (g->surf) SDL_FreeSurface(g->surf);
}

/* The fallback game's way round the fonts, a hundred times, with the workers
 * asked for a shelf of cards before every ui_quit, and a shelf more while the
 * fonts are down. Every other round leaves out the texload_bump that
 * free_all_textures makes first, so the frame closes the faces while a worker
 * is drawing with them: it waits for that card. */
static void the_fallback(SDL_Renderer *r)
{
	int asked = 0;

	drawn_while_down = declined_while_down = 0;
	for (int round = 0; round < 100; round++) {
		ui_card_order o;
		char title[64];
		Uint32 until;

		for (int k = 0; k < 11; k++) {
			snprintf(title, sizeof title, "Round %d Game %d", round, k);
			ui_card_order_set(&o, title, 0x2E8B57, k & 1);
			texload_want_or_draw(7, 1000 + k, NULL, NULL, &o);
		}
		if (round & 1) texload_bump();
		ui_quit();
		/* The asks of the shelf before are thrown away when they land, if
		 * the bump came; without it they are drawn, and the frame did not
		 * close a face under any of them. What is asked now is answered
		 * with no card at all. */
		texload_bump();
		for (int k = 0; k < 11; k++) {
			snprintf(title, sizeof title, "Down %d Game %d", round, k);
			ui_card_order_set(&o, title, 0x2E8B57, false);
			texload_want_or_draw(7, 2000 + k, NULL, NULL, &o);
			asked++;
		}
		until = SDL_GetTicks() + 2000;
		while (!texload_idle() && SDL_GetTicks() < until)
			if (!texload_deliver(theme_index(), down_put, NULL)) SDL_Delay(1);
		ck(texload_idle(), "every ask while the fonts were down was answered", NULL);
		if (!ui_init(r, "res/fonts/menu.ttf")) { ck(0, "ui_init again", NULL); return; }
	}
	ck(drawn_while_down == 0, "no card was drawn while the fonts were down", NULL);
	ck(declined_while_down == asked, "each was left to the frame", NULL);
	same_both_ways("after a hundred fallback games");
}

/* The language an order was asked in is the one it is drawn in */
static void the_language(void)
{
	static const char nihongo[] = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
	ui_card_order en, zh;
	SDL_Surface *a, *b, *c;

	ui_fonts_for(LANG_EN);
	ui_card_order_set(&en, nihongo, 0xE6312E, false);
	ui_fonts_for(LANG_ZH_HANS);
	ui_card_order_set(&zh, nihongo, 0xE6312E, false);
	a = ui_card_paint(&en, true);
	b = ui_card_paint(&zh, true);
	ck(a && b && !same_pixels(a, b),
	   "a kanji title is in the faces of the language it was asked in", NULL);
	ui_fonts_for(LANG_EN);
	c = ui_card_paint(&zh, false);
	ck(same_pixels(b, c), "and still is once the language has changed", NULL);
	if (a) SDL_FreeSurface(a);
	if (b) SDL_FreeSurface(b);
	if (c) SDL_FreeSurface(c);
}

int main(void)
{
	SDL_Surface *screen;
	SDL_Renderer *r;
	ui_card_order o;
	SDL_Surface *s;

	/* Said as it goes: a check that crashes says how far it got */
	setvbuf(stdout, NULL, _IONBF, 0);
	if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
	IMG_Init(IMG_INIT_PNG);
	screen = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
	r = SDL_CreateSoftwareRenderer(screen);
	theme_set_file(NULL);
	theme_init(APPEAR_LIGHT);
	if (!ui_init(r, "res/fonts/menu.ttf")) { printf("FAIL ui_init\n"); return 1; }

	printf("no worker:\n");
	ui_card_order_set(&o, "Contra", 0xE6312E, false);
	ck(!texload_want_or_draw(7, 0, NULL, NULL, &o),
	   "with no worker the ask says so, and the caller makes the card", NULL);

	if (!texload_start()) { printf("FAIL texload_start\n"); return 1; }
	printf("the same pixels, both ways:\n");
	ui_fonts_for(LANG_EN);
	same_both_ways("Beard, English");
	ui_fonts_for(LANG_JA);
	same_both_ways("Beard, Japanese faces first");
	ui_fonts_for(LANG_ZH_HANS);
	same_both_ways("Beard, Chinese faces first");
	ui_fonts_for(LANG_EN);
	theme_init(APPEAR_DARK);
	same_both_ways("Salt & Pepper, English");
	ui_fonts_for(LANG_JA);
	same_both_ways("Salt & Pepper, Japanese faces first");
	ui_fonts_for(LANG_EN);
	theme_init(APPEAR_LIGHT);

	printf("the language's faces:\n");
	the_language();

	printf("a turn of the theme:\n");
	the_turn();

	printf("the fonts down and up around a fallback game:\n");
	the_fallback(r);

	printf("no faces to draw with:\n");
	texload_bump();
	ui_quit();
	ui_init(r, "/no/such/folder/menu.ttf");
	ui_card_order_set(&o, "Contra", 0xE6312E, false);
	ck(!ui_card_paint(&o, true), "with no faces a worker draws nothing", NULL);
	s = ui_card_paint(&o, false);
	ck(s != NULL, "and the frame still makes the card, slab and band", NULL);
	if (s) SDL_FreeSurface(s);
	{
		texload_got g = { 0 };

		texload_want_or_draw(7, 3000, NULL, NULL, &o);
		ck(wait_for(3000, &g) && !g.card && !g.surf,
		   "a worker's answer is no card, for the frame to make", NULL);
	}
	ui_quit();

	texload_stop();
	SDL_DestroyRenderer(r);
	SDL_FreeSurface(screen);
	IMG_Quit();
	SDL_Quit();
	if (fails) { printf("cardpaint: %d FAILED\n", fails); return 1; }
	printf("ok: the card with no art drawn on a worker is the frame's, pixel for pixel, "
	       "in both themes; never shown after a turn; safe across the fonts going down\n");
	return 0;
}
