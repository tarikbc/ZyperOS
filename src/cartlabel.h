/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CARTLABEL_H
#define ZYPEROS_CARTLABEL_H

#include <stdbool.h>
#include <string.h>
#include <strings.h>

/* A PICO-8 cart as its own cover (Discord, moroboshi69: "Set the default
 * boxart for PICO-8 games to use the game's png").
 *
 * A .p8.png cart is a picture: the cart Lexaloffle draws, 160 x 205, with the
 * game's 128 x 128 label at (16, 24) and the game itself in the low two bits
 * of every pixel. A game with no cover of its own or EmulationStation's has
 * its cart for one (game_media, src/esmedia.h), and the cover is the label
 * alone, as PICO-8 shows it, drawn nearest so its pixels stay pixels.
 *
 * The test on a name has no SDL, for src/esmedia.c; the rest reads the
 * picture. */

#define CART_WIDTH      160
#define CART_HEIGHT     205
#define CART_LABEL_COL   16    /* where the label starts, in the cart's pixels */
#define CART_LABEL_LINE  24
#define CART_LABEL      128    /* its side */

/* Whether `path` names a cart: it ends in .p8.png, in any case */
static inline bool cart_label_file(const char *path)
{
	size_t n = path ? strlen(path) : 0;

	return n > 7 && !strcasecmp(path + n - 7, ".p8.png");
}

struct SDL_Surface;
struct SDL_Texture;

/* A cover off the card as ARGB8888, the one form the launcher reads pixels
 * in, NULL when it is not there or will not decode: a cart's label alone,
 * any other picture whole. The one loader for a cover's file, so every place
 * that shows a game's cover shows a cart's the same. */
struct SDL_Surface *cart_label_load(const char *path);

/* Whether `s` is a cart's label from cart_label_load, which is pixel art */
bool cart_label_of(const struct SDL_Surface *s);

/* A texture made from `s` drawn nearest when `s` is a label: 128 pixels
 * drawn larger stay sharp, where the scaling every device sets would blur
 * them. Nothing for any other picture. */
void cart_label_sharp(struct SDL_Texture *t, const struct SDL_Surface *s);

#endif
