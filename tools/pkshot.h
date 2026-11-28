/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PKSHOT_H
#define ZYPEROS_PKSHOT_H

/* A screenshot of your PICO-8, as portkeys (tools/portkeys.c) takes it on
 * the Screenshot hotkey (DESIGN.md 11.61): PICO-8's own 128 x 128 picture,
 * as Diatom writes fake08's, a PNG, and the pop-up with the shot in it.
 * Pure: no device, so tools/pkkeys-check.c feeds it frames of its own. */

#include <stddef.h>
#include <stdint.h>

#include "dmodefit.h"

/* PICO-8's screen, each way, and its picture's bytes as 8-bit RGB */
#define PKSHOT_SIDE  128
#define PKSHOT_BYTES (PKSHOT_SIDE * PKSHOT_SIDE * 3)

/* PICO-8's picture out of the frame on the panel: `px` the frame's rows of
 * XRGB8888, `fw` x `fh` as the panel has it, `pitch` bytes a row; `r` where
 * PICO-8 drew its screen on the landscape window (src/p8draw.h's rect). Each
 * of its pixels read at its middle, turned back to landscape as the bar is
 * turned onto the panel (a landscape pixel (x, y) is the panel's
 * (y, W - 1 - x), W the landscape width) when the panel is taller than it
 * is wide, as the Pixel 2's is; as it is on a landscape one (QEMU's). A
 * middle off the panel (Zoomed, Edges Cut) is black: that much was never on
 * the screen. Sharp at any factor: PICO-8's filtered modes are linear, and a
 * pixel's middle is its own color. Into `out`, rows of RGB. */
void pkshot_pick(const uint8_t *px, int fw, int fh, size_t pitch, const dmode_rect *r,
                 uint8_t out[PKSHOT_BYTES]);

/* A port's whole frame (MENU's screenshot over any port), the same frame
 * turned back to landscape as pkshot_pick reads it: malloc'd rows of RGB,
 * its size in *w and *h; NULL on no memory */
uint8_t *pkshot_frame(const uint8_t *px, int fw, int fh, size_t pitch, int *w, int *h);
/* That frame, w x h, fitted whole into the pop-up's 128 x 128 square, the
 * rest black, for pkshot_lay */
void pkshot_thumb(const uint8_t *rgb, int w, int h, uint8_t out[PKSHOT_BYTES]);

/* `rgb`, w x h, as an 8-bit RGB PNG, every row filter none, as Diatom
 * writes its screenshots; malloc'd, its length in `len`; NULL on no memory */
uint8_t *pkshot_png(const uint8_t *rgb, int w, int h, size_t *len);

/* The shot laid into the pop-up's empty square (notice.h's NOTICE_SLOT):
 * `notice` w x h premultiplied ARGB, `slot` x, y, side and corner radius in
 * its pixels; nearest pixels of the 128, the badge's own rounded corners
 * (src/notice.c's draw_badge) blended over the slab */
void pkshot_lay(uint32_t *notice, int w, int h, const int slot[4], const uint8_t rgb[PKSHOT_BYTES]);

#endif
