/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MEDIATEX_H
#define ZYPEROS_MEDIATEX_H

#include <stdbool.h>
#include <SDL.h>

/* The Cover and Video page's pictures, decoded off the frame (DESIGN.md
 * 11.34), as texload.c decodes the shelf's: IMG_Load and the shrink on a
 * thread of its own, the upload on the main one, which alone may touch the
 * renderer. A picture is shrunk to `px` at its longer side as it is read
 * (box averaging, artshrink.c's), so a tile's texture is small.
 *
 * Asked by key - the caller's number for a tile, the cover, the big look -
 * and an ask for a key not yet started replaces the one waiting. */
bool mt_start(void);
void mt_stop(void);
void mt_ask(int key, const char *path, int px);
/* A decoded picture: true with its key and the surface, which the caller
 * uploads and frees. NULL surface: the file was no picture. */
bool mt_take(int *key, SDL_Surface **s);
/* Drop every ask and every picture not taken: the page closed */
void mt_forget(void);
/* Decode now, on this thread: for a shot, which draws one frame */
SDL_Surface *mt_load(const char *path, int px);

#endif
