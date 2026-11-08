/* SPDX-License-Identifier: GPL-3.0-only */
/* The buttons that are shapes and no letters: the PlayStation's cross,
 * circle, triangle and square.
 *
 * Written in a string as code points of Unicode's private use area, which no
 * face here has a glyph for: ui.c draws each one as its shape wherever text
 * is drawn or measured, as it draws the word MENU as the Pixel 2's key, in
 * the text's own color, at the text's size, so it reads in either theme and
 * is as crisp as the letters beside it. A pair of them is PAD_CIRCLE "/"
 * PAD_CROSS to the pages, and the same in every language: a shape needs no
 * translating.
 *
 * Here alone and with no SDL, so src/controls.c and the checks can name them
 * (2026-11-07). */
#ifndef ZYPEROS_PADGLYPH_H
#define ZYPEROS_PADGLYPH_H

#define PAD_CROSS    "\xEE\x80\x80"   /* U+E000 */
#define PAD_CIRCLE   "\xEE\x80\x81"   /* U+E001 */
#define PAD_TRIANGLE "\xEE\x80\x82"   /* U+E002 */
#define PAD_SQUARE   "\xEE\x80\x83"   /* U+E003 */

/* The shape at the start of `p` (its first byte 0xEE): 0 cross, 1 circle,
 * 2 triangle, 3 square, or -1 for none. Three bytes each. */
static inline int pad_glyph_at(const char *p)
{
	const unsigned char *u = (const unsigned char *)p;

	if (u[0] == 0xEE && u[1] == 0x80 && u[2] >= 0x80 && u[2] <= 0x83) return u[2] - 0x80;
	return -1;
}

#endif
