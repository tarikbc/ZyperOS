/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TEXLOAD_H
#define ZYPEROS_TEXLOAD_H

#include <stdbool.h>
#include <SDL.h>

#include "ui.h"   /* ui_card_order: the card a worker draws where there is no file */

/* Decoding card art off the render thread.
 *
 * cf_draw asks for every visible card every frame and the decode used to
 * happen right there, inside the frame: 14-60ms against a 16.7ms budget,
 * measured on this device. That is why a letter jump showed two still
 * pictures - the tween was running correctly and the device could not produce
 * frames for it.
 *
 * Time-slicing cannot fix it. prime_toward already slices at one card per
 * frame and one card still overruns the frame by itself; the cost is inside
 * IMG_Load and does not subdivide. So it runs on a thread.
 *
 * The split is by what needs the renderer. IMG_Load and the format conversion
 * are pure CPU and happen on the worker; SDL_CreateTextureFromSurface needs
 * the renderer and stays on the main thread.
 *
 * The visible consequence is that a cold card is blank for a few frames rather
 * than freezing the shelf. It is not slower - the art lands at the same moment
 * either way, and what changes is whether everything else kept moving while it
 * was coming.
 *
 * Where there is no file, the card the launcher draws in its place is drawn
 * here as well (texload_want_or_draw), and finished pictures are handed to
 * the frame a few at a time (texload_deliver), so neither the drawing nor
 * the uploads of a whole shelf land on one frame. */

bool texload_start(void);
void texload_stop(void);

/* Everything in flight is stale from here on: the shelf it was for has been
 * rebuilt and its indices no longer mean what they meant. Results already
 * decoded are dropped rather than installed against the wrong game. */
void texload_bump(void);

/* Drop every request not started yet. Unlike texload_bump nothing is disowned:
 * a decode already under way still lands and is installed, because the shelf
 * it was for has not changed shape - only where anyone is looking. For a cursor
 * that has moved on, so the next requests are not served behind a place nobody
 * is looking at any more. */
void texload_forget(void);

/* Decode for (sys, idx), trying `first` then `second`. Either may be NULL.
 * Ignored if that card is already queued, in flight, or waiting to be taken,
 * so a caller may ask every frame without piling work up - in the same theme
 * (texload_want_art). A card asked for in another theme
 * and not started yet is taken out and this ask queued in its place as the
 * newest; one being decoded or waiting in another theme is left to land,
 * and this ask queued beside it.
 *
 * False means there is no worker - the thread never started - and the caller
 * must decode it itself. A full queue is not a failure: it returns true and
 * the card is simply asked for again next frame, which is the whole point of
 * asking being cheap. */
bool texload_want(int sys, int idx, const char *first, const char *second);


/* System art: try the light card first, then the dark fallback. Tag the
 * result with the requested theme so a later theme change replaces it. */
bool texload_want_art(int sys, int idx, const char *first, const char *second,
                      int theme_ix);

/* texload_want, and where neither file is a picture, the generated card
 * `card` describes, drawn on the worker (ui_card_paint) rather than left for
 * the frame. In the order's theme: asked again in another, it is another ask,
 * as texload_want_art's are, and a card that lands in a theme no longer on is
 * never handed over (texload_deliver). `card` NULL is texload_want.
 *
 * False, as texload_want's, means no worker: the caller reads the file and
 * makes the card itself. */
bool texload_want_or_draw(int sys, int idx, const char *first,
                          const char *second, const ui_card_order *card);

/* One finished picture. `surf` is the caller's to free; NULL when no file
 * could be read and no card was drawn, which means the caller should draw its
 * own placeholder - a card the worker could not draw (ui_card_paint) is that.
 * `card` says `surf` is the generated card, drawn in theme `theme`, to be
 * made a texture with ui_card_upload. Otherwise `theme` is the theme art
 * asked for with texload_want_art is for, and -1 for art that is no theme's.
 *
 * The surface is already ARGB8888, so the caller only has to make a texture
 * from it. Where the art stops is NOT computed here: content_bottom scans
 * trailing empty rows and stops at the first row with anything in it, so it
 * costs the padding rather than the image, and duplicating it out of main.c
 * to save that would be the worse trade. */
typedef struct {
	int          sys, idx;
	SDL_Surface *surf;
	int          theme;
	bool         card;
} texload_got;

/* The oldest finished picture, or false if none is ready. */
bool texload_take(texload_got *g);

/* The share of a frame texload_deliver may spend, in ms: a quarter of the
 * Pixel 2's 16.7 ms at 60 Hz, the rest the frame's own to draw. */
#define TEXLOAD_SHARE_MS 4.0f

/* For the frame: hand finished pictures to `put`, oldest first, while they
 * fit in this frame's share (src/putpace.h), and at least one when any is
 * waiting, so a slow device still fills its shelf; the rest stay waiting for
 * the next frame, and texload_ready says so. `put` owns `g->surf`. A card
 * drawn in a theme other than `theme_on` is freed here and never handed
 * over: its slot keeps whatever it shows until the card of the theme that is
 * on comes. Returns how many were handed over; one thrown away for its theme
 * is not one, though it was taken. */
int texload_deliver(int theme_on, void (*put)(void *ctx, texload_got *g),
                    void *ctx);

/* The one finished picture for (sys, idx), handed to `put` now if it is
 * waiting, wherever it is in line and whatever this frame has spent, under
 * the same rule of theme: for a screen that is waiting for that one picture
 * and no other, the launch zoom's focused card. True if it was handed over. */
bool texload_deliver_slot(int sys, int idx, int theme_on,
                          void (*put)(void *ctx, texload_got *g), void *ctx);

/* Whether anything is waiting to be taken, without taking it.
 *
 * For a loop that only draws when something changed. Finished decodes are
 * installed while drawing, so a loop sitting idle would never draw and never
 * find them: art that finished while nothing moved would wait for the next
 * press. This lets the loop notice there is something to install. */
bool texload_ready(void);

/* How many finished pictures are waiting to be taken: for a check, and for
 * a measurement that wants a shelf's worth finished before a frame takes
 * them. */
int texload_waiting(void);

/* Whether the workers have nothing at all: nothing queued, nothing being
 * decoded, nothing waiting to be taken. For the shelf, which lowers the memory
 * clock only once art has stopped arriving (plat_ram_idle). */
bool texload_idle(void);

#endif
