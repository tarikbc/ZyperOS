/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The Flash Drive page's live numbers, from pictures ZyperOS drew.
 *
 * usbdrive has no fonts, no words and no theme: before it leaves, ZyperOS
 * (src/main.c's usb_drive) draws the page with its tiles' first numbers
 * (connected.raw, all it shows without an atlas), and an atlas: one more
 * picture (atlas.raw) with a glyph for every token drivestat.h makes (each
 * digit, the language's decimal mark, the space, the units, "/s") in two
 * sizes, the speeds' (big) and the totals' (small), and each way's mark
 * moving and idle, on the tiles' color; and atlas.txt saying where each
 * glyph is and where the page's slots are (the atlas's ground, at its
 * top-left corner, is the tiles' color: a slot is cleared to it before its
 * row is drawn):
 *
 *   zyperos-drive-atlas 2
 *   size 640 480
 *   slot speed_in|speed_out|total_in|total_out|mark_in|mark_out X0 Y0 X1 Y1 left|right
 *   glyph b0..b9|bdec|bsp|bB|bKB|bMB|bGB|bps X Y W H     (the speeds')
 *   glyph s0..s9|sdec|ssp|sB|sKB|sMB|sGB|sps X Y W H     (the totals')
 *   glyph in_on|in_off|out_on|out_off X Y W H            (the marks)
 *
 * all in the landscape picture's pixels (the raws themselves are the
 * panel's 480x640, turned: a landscape (x, y) is the panel's row W-1-x,
 * column y). A slot is drawn by clearing it to the tiles' color, then the
 * glyphs in a row, from its left or up to its right edge, cut at the slot.
 * The file is read as untrusted: anything missing, out of the picture or
 * taller than the slots its family goes in, and the atlas is not used; the
 * page shows as ZyperOS drew it.
 */
#ifndef USBDRIVE_ATLAS_H
#define USBDRIVE_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#include "drivestat.h"

enum { AT_SLOT_SPEED_IN, AT_SLOT_SPEED_OUT, AT_SLOT_TOTAL_IN, AT_SLOT_TOTAL_OUT, AT_SLOT_MARK_IN,
       AT_SLOT_MARK_OUT, AT_SLOTS };
/* The glyphs: a family of tokens for the speeds (AT_BIG + token), one for
 * the totals (AT_SMALL + token), then the marks */
enum { AT_BIG = 0, AT_SMALL = DST_TOKENS, AT_MARK = 2 * DST_TOKENS,
       AT_G_IN_ON = AT_MARK, AT_G_IN_OFF, AT_G_OUT_ON, AT_G_OUT_OFF, AT_GLYPHS };

typedef struct { int x, y, w, h; } at_rect;

typedef struct {
	int w, h;                    /* the landscape picture */
	at_rect slot[AT_SLOTS];
	bool right[AT_SLOTS];        /* drawn up to the right edge, else from the left */
	at_rect glyph[AT_GLYPHS];
} at_layout;

/* atlas.txt read into `l`: true only when every slot and glyph is there,
 * inside the picture, and each glyph no taller than every slot its family
 * is drawn in */
bool at_parse(const char *text, at_layout *l);

/* A row of glyphs into slot `s` of `out` (the landscape page being made):
 * cleared to the atlas's ground first, then the glyphs from `atlas`, each
 * clipped to the slot whatever the layout says. Pure. */
void at_compose(const at_layout *l, int s, const int *glyphs, int n, const uint32_t *atlas,
                uint32_t *out);

/* A raw picture (the panel's turned rows) read into landscape `w` x `h`;
 * `turned` false for a display that is landscape itself */
bool at_load(const char *path, int w, int h, bool turned, uint32_t *out);

#endif
