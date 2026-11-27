/* SPDX-License-Identifier: GPL-3.0-only */
/* A PICO-8 cart's label as its cover. See src/cartlabel.h. */
#include "cartlabel.h"

#include <SDL.h>
#include <SDL_image.h>

/* On a label's userdata, as ui.c marks a card's ground: what says the
 * surface is pixel art once it is out of here */
static char label_mark;

SDL_Surface *cart_label_load(const char *path)
{
	SDL_Surface *raw, *conv, *label;
	SDL_Rect in = { CART_LABEL_COL, CART_LABEL_LINE, CART_LABEL, CART_LABEL };

	if (!path || !*path || !(raw = IMG_Load(path))) return NULL;
	conv = SDL_ConvertSurfaceFormat(raw, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(raw);
	/* Only a picture of a cart's size is a cart: a .p8.png of any other is
	 * shown whole rather than cut somewhere that is not its label */
	if (!conv || !cart_label_file(path) || conv->w != CART_WIDTH || conv->h != CART_HEIGHT)
		return conv;
	label = SDL_CreateRGBSurfaceWithFormat(0, CART_LABEL, CART_LABEL, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!label) return conv;
	/* Copied, not blended: the label's own alpha, opaque on every cart */
	SDL_SetSurfaceBlendMode(conv, SDL_BLENDMODE_NONE);
	if (SDL_BlitSurface(conv, &in, label, NULL) != 0) {
		SDL_FreeSurface(label);
		return conv;
	}
	SDL_FreeSurface(conv);
	label->userdata = &label_mark;
	return label;
}

bool cart_label_of(const SDL_Surface *s)
{
	return s && s->userdata == &label_mark;
}

void cart_label_sharp(SDL_Texture *t, const SDL_Surface *s)
{
	if (t && cart_label_of(s)) SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
}
