/* SPDX-License-Identifier: GPL-3.0-only */
/* Button hints: what each button does, as a row of glyphs and words.
 *
 * ZyperOS says what the buttons do in many places. The shelves' feet and Now
 * Playing draw it as a button's glyph and a word (DESIGN.md 11.9 and 11.4,
 * page_hint in src/home.c); the questions in tiles wrote it as a line of
 * text, "Left/Right: move   A: choose   B: computer". This is the one
 * component for it (2026-11-10): a list of {button, word}, laid out in rows
 * that never cut a hint and never run past their room, drawn centered under
 * a panel's tiles or spread across a screen's foot, in the theme that is on.
 *
 * The layout is here, pure, so tools/keyhints-check.c holds it with any
 * widths; the drawing is keyhints_draw.h's. A list can travel inside a
 * panel's rows (a menu_row's label, which a panel's snapshot copies as text):
 * keyhints_pack writes it into one string and keyhints_unpack reads it back.
 *
 * Buttons are named as printed (A, B, X, Y, L1/R1, MENU, SELECT, START); the
 * d-pad's two directions are KEYHINT_LEFT_RIGHT, drawn "< >" as Now Playing's
 * seek is. Words come from res/strings (keyhint.*, or a screen's own). */
#ifndef KEYHINTS_H
#define KEYHINTS_H

#include <stddef.h>

typedef struct {
	const char *key;      /* the button, as printed: "A", "L1/R1", KEYHINT_LEFT_RIGHT */
	const char *label;    /* what it does, one word or two */
} key_hint;

#define KEYHINT_LEFT_RIGHT "< >"
#define KEYHINTS_MAX  8          /* hints in one list, and rows at most */
#define KEYHINTS_GAP  32         /* the room between two hints in a centered row */
#define KEYHINTS_GAP_SPREAD 16   /* the least between two in a spread row (a foot) */
#define KEYHINTS_ROW  44         /* one row's height */

/* How wide a hint is drawn, glyph to the word's end */
typedef int (*keyhint_width_fn)(const char *key, const char *label);

/* The rows of `n` hints in `maxw`: as many in a row as fit, in their order,
 * a hint never split and never alone on a row unless it alone is wider.
 * `start[r]` is row r's first hint, start[rows] == n. Returns the rows. */
int keyhints_rows(const key_hint *h, int n, int maxw, int gap, keyhint_width_fn width,
                  int start[KEYHINTS_MAX + 1]);

/* A list as one string, and back: "\x1b" "keys" then key, \x1f, label, \x1e
 * for each. unpack returns how many, 0 when `text` is no packed list (a
 * legend written as text). `buf` holds the pieces `out` points into. */
#define KEYHINTS_MARK "\x1bkeys"
size_t keyhints_pack(const key_hint *h, int n, char *out, size_t on);
int keyhints_unpack(const char *text, char *buf, size_t bn, key_hint *out, int max);

#endif
