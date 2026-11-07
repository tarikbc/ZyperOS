/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_TEXLOAD_H
#define ZYPEROS_TEXLOAD_H

#include <stdbool.h>
#include <SDL.h>

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
 * was coming. */

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
 * (texload_want_card). A card asked for in another theme
 * and not started yet is taken out and this ask queued in its place as the
 * newest; one being decoded or waiting in another theme is left to land,
 * and this ask queued beside it.
 *
 * False means there is no worker - the thread never started - and the caller
 * must decode it itself. A full queue is not a failure: it returns true and
 * the card is simply asked for again next frame, which is the whole point of
 * asking being cheap. */
bool texload_want(int sys, int idx, const char *first, const char *second);

/* A game's art as texload_want, and when neither file can be read, the card
 * ui_make_card would make for it, drawn by the worker (ui_card_surface): the
 * main thread only uploads it. `title` and `rgb` are the card's title and the
 * system's color; `album` asks for an album's square, square-cornered cover,
 * ui_make_cover's, instead of a game's card. Copied, like the paths.
 *
 * In theme `theme_ix`, DS_LIGHT or DS_DARK, which the caller reads from
 * theme_index() as it asks: the worker draws in what it is handed and never
 * asks theme() itself, and the card comes back saying which theme it is in
 * (texload_take), so a card asked for before a change of theme and landing
 * after it can be told apart from one that is still right.
 *
 * A worker whose faces are not open (texload_fonts_open) draws no card, and
 * the result comes back with no surface, as from texload_want: the caller
 * makes the card itself. A file that is read is art, which is no theme's, and
 * comes back as it does from texload_want. */
bool texload_want_card(int sys, int idx, const char *first, const char *second,
                       const char *title, unsigned rgb, bool album, int theme_ix);

/* The workers' faces for the cards they draw (ui_card_fonts, src/ui.h): a set
 * of its own for each worker, opened and closed here on the main thread only,
 * and drawn with only on the worker that owns it. Open after ui_init, which
 * starts the font library, and closed before ui_quit, which ends it: at boot,
 * and around a game the launcher takes its display and fonts down for.
 *
 * Each set has a lock of its own, which a worker holds while it draws with it,
 * so closing one waits for at most the card that worker is drawing, not for
 * the queue and not for the other worker. Opening a set already open, or
 * closing one already closed, does nothing; with no worker, both do nothing.
 * texload_stop closes them too. */
void texload_fonts_open(void);
void texload_fonts_close(void);

/* One finished decode, or false if none is ready. The caller owns `surf` and
 * must free it; it is NULL when no file could be read and no card was drawn,
 * which means the caller should draw its own placeholder. `theme_ix`, if not
 * NULL, is the theme a card the worker drew is in, and -1 for art, which is
 * no theme's. `generated`, if not NULL, says whether `surf` is a card
 * the worker drew (texload_want_card) rather than a file it read.
 *
 * The surface is already ARGB8888, so the caller only has to make a texture
 * from it. Where the art stops is NOT computed here: content_bottom scans
 * trailing empty rows and stops at the first row with anything in it, so it
 * costs the padding rather than the image, and duplicating it out of main.c
 * to save that would be the worse trade. A card the worker drew fills its
 * surface to the foot, and needs no scan. */
bool texload_take(int *sys, int *idx, SDL_Surface **surf, int *theme_ix,
                  bool *generated);

/* Whether anything is waiting to be taken, without taking it.
 *
 * For a loop that only draws when something changed. Finished decodes are
 * installed while drawing, so a loop sitting idle would never draw and never
 * find them: art that finished while nothing moved would wait for the next
 * press. This lets the loop notice there is something to install. */
bool texload_ready(void);

/* Whether the workers have nothing at all: nothing queued, nothing being
 * decoded, nothing waiting to be taken. For the shelf, which lowers the memory
 * clock only once art has stopped arriving (plat_ram_idle). */
bool texload_idle(void);

#endif
