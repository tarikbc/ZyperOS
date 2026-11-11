/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * The Flash Drive page's live numbers, from pictures ZyperOS drew.
 *
 * usbdrive has no fonts, no words and no theme: before it leaves, ZyperOS
 * (src/main.c's usb_drive) draws the page with its rows' first numbers
 * (connected.raw, all it shows without an atlas), and an atlas: one more picture (atlas.raw) with a glyph
 * for every token drivestat.h makes (each digit, the language's decimal
 * mark, the space, the units, "/s") and the three statuses whole, each on
 * the card's own color, and atlas.txt saying where each glyph is and where
 * the page's four slots are (the atlas's ground, at its top-left corner,
 * is the card's color: a slot is cleared to it before its row is drawn):
 *
 *   zyperos-drive-atlas 1
 *   size 640 480
 *   slot status|speed|written|read X0 Y0 X1 Y1 left|right
 *   glyph 0..9|dec|sp|B|KB|MB|GB|ps|st_write|st_read|st_idle X Y W H
 *
 * all in the landscape picture's pixels (the raws themselves are the
 * panel's 480x640, turned: a landscape (x, y) is the panel's row W-1-x,
 * column y). A slot is drawn by clearing it to the card's color, then the
 * glyphs in a row, from its left or up to its right edge, cut at the slot.
 * Anything missing or out of place, and the atlas is not used: the page
 * shows as ZyperOS drew it.
 */
#ifndef USBDRIVE_ATLAS_H
#define USBDRIVE_ATLAS_H

#include <stdbool.h>
#include <stdint.h>

#include "drivestat.h"

enum { AT_SLOT_STATUS, AT_SLOT_SPEED, AT_SLOT_WRITTEN, AT_SLOT_READ, AT_SLOTS };
enum { AT_G_ST_WRITE = DST_TOKENS, AT_G_ST_READ, AT_G_ST_IDLE, AT_GLYPHS };

typedef struct { int x, y, w, h; } at_rect;

typedef struct {
	int w, h;                    /* the landscape picture */
	at_rect slot[AT_SLOTS];
	bool right[AT_SLOTS];        /* drawn up to the right edge, else from the left */
	at_rect glyph[AT_GLYPHS];
} at_layout;

/* atlas.txt read into `l`: true only when every slot and glyph is there,
 * inside the picture, and each glyph no taller than the slots */
bool at_parse(const char *text, at_layout *l);

/* A row of glyphs into slot `s` of `out` (the landscape page being made):
 * cleared to the atlas's ground first, then the glyphs from `atlas`. Pure. */
void at_compose(const at_layout *l, int s, const int *glyphs, int n, const uint32_t *atlas,
                uint32_t *out);

/* A raw picture (the panel's turned rows) read into landscape `w` x `h`;
 * `turned` false for a display that is landscape itself */
bool at_load(const char *path, int w, int h, bool turned, uint32_t *out);

#endif
