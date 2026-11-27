/* SPDX-License-Identifier: GPL-3.0-only */
/* A PICO-8 cart as its own cover (src/cartlabel.h):
 *
 *     make check-cartlabel
 *
 * A 160 x 205 .p8.png comes back as its 128 x 128 label, cut at (16, 24),
 * marked as pixel art, and a texture of it is drawn nearest; any other
 * picture, and a .p8.png of another size, comes back whole and unmarked.
 * Writes its own pictures, offline, no display. */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_image.h>

#include "cartlabel.h"

static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); \
	printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static Uint32 px(const SDL_Surface *s, int x, int y)
{
	return ((const Uint32 *)((const Uint8 *)s->pixels + y * s->pitch))[x];
}

/* A cart: the frame one color, the label another, its corners marked */
static bool write_cart(const char *path, int w, int h)
{
	SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	SDL_Rect label = { CART_LABEL_COL, CART_LABEL_LINE, CART_LABEL, CART_LABEL };
	SDL_Rect dot = { CART_LABEL_COL, CART_LABEL_LINE, 1, 1 };
	bool ok;

	if (!s) return false;
	SDL_FillRect(s, NULL, 0xff202020);
	SDL_FillRect(s, &label, 0xff29adff);
	SDL_FillRect(s, &dot, 0xffff004d);
	dot.x = CART_LABEL_COL + CART_LABEL - 1;
	dot.y = CART_LABEL_LINE + CART_LABEL - 1;
	SDL_FillRect(s, &dot, 0xff00e436);
	ok = IMG_SavePNG(s, path) == 0;
	SDL_FreeSurface(s);
	return ok;
}

int main(void)
{
	char cart[96], plain[96], odd[96];
	SDL_Surface *s, *target;
	SDL_Renderer *r;
	SDL_Texture *t;
	SDL_ScaleMode mode = SDL_ScaleModeLinear;

	printf("cartlabel: a PICO-8 cart as its cover\n");
	snprintf(cart, sizeof cart, "/tmp/zyperos-cartlabel-%ld.p8.png", (long)getpid());
	snprintf(plain, sizeof plain, "/tmp/zyperos-cartlabel-%ld.png", (long)getpid());
	snprintf(odd, sizeof odd, "/tmp/zyperos-cartlabel-%ld-odd.P8.PNG", (long)getpid());
	CHECK(write_cart(cart, CART_WIDTH, CART_HEIGHT) && write_cart(plain, CART_WIDTH, CART_HEIGHT) &&
	      write_cart(odd, 200, 200), "cannot write the pictures");

	CHECK(cart_label_file("Roms/PICO-8/celeste.p8.png") && cart_label_file("A.P8.PNG"),
	      "a cart's name");
	CHECK(!cart_label_file("jelpi.p8") && !cart_label_file("shot.png") &&
	      !cart_label_file(".p8.png") && !cart_label_file(NULL), "a name that is no cart");

	s = cart_label_load(cart);
	CHECK(s && s->w == CART_LABEL && s->h == CART_LABEL, "the label is %dx%d, not 128x128",
	      s ? s->w : 0, s ? s->h : 0);
	if (s && s->w == CART_LABEL && s->h == CART_LABEL) {
		CHECK(px(s, 0, 0) == 0xffff004d, "the label's corner is %08x, cut in the wrong place",
		      (unsigned)px(s, 0, 0));
		CHECK(px(s, 127, 127) == 0xff00e436, "the label's far corner is %08x",
		      (unsigned)px(s, 127, 127));
		CHECK(px(s, 64, 64) == 0xff29adff, "the label's middle is %08x", (unsigned)px(s, 64, 64));
	}
	CHECK(cart_label_of(s), "the label is not marked as pixel art");

	/* Drawn nearest, on a renderer with linear scaling set */
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	target = SDL_CreateRGBSurfaceWithFormat(0, 64, 64, 32, SDL_PIXELFORMAT_ARGB8888);
	r = target ? SDL_CreateSoftwareRenderer(target) : NULL;
	t = r && s ? SDL_CreateTextureFromSurface(r, s) : NULL;
	cart_label_sharp(t, s);
	CHECK(t && SDL_GetTextureScaleMode(t, &mode) == 0 && mode == SDL_ScaleModeNearest,
	      "the label's texture is not drawn nearest");
	if (t) SDL_DestroyTexture(t);
	if (s) SDL_FreeSurface(s);

	/* Any other picture whole, and left to the device's scaling */
	s = cart_label_load(plain);
	CHECK(s && s->w == CART_WIDTH && s->h == CART_HEIGHT && !cart_label_of(s), "a plain .png was cut");
	t = r && s ? SDL_CreateTextureFromSurface(r, s) : NULL;
	cart_label_sharp(t, s);
	CHECK(t && SDL_GetTextureScaleMode(t, &mode) == 0 && mode == SDL_ScaleModeLinear,
	      "a plain picture was made nearest");
	if (t) SDL_DestroyTexture(t);
	if (s) SDL_FreeSurface(s);
	s = cart_label_load(odd);
	CHECK(s && s->w == 200 && s->h == 200 && !cart_label_of(s),
	      "a .p8.png of another size was cut");
	if (s) SDL_FreeSurface(s);
	CHECK(!cart_label_load("/tmp/zyperos-cartlabel-none.p8.png") && !cart_label_load(NULL),
	      "a picture that is not there");

	if (r) SDL_DestroyRenderer(r);
	if (target) SDL_FreeSurface(target);
	unlink(cart); unlink(plain); unlink(odd);
	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
