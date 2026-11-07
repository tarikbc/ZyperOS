/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_LINEBREAK_H
#define ZYPEROS_LINEBREAK_H

/* A name too wide for its box, broken into lines, in the part of it that is
 * plain C: where the breaks go, given how wide a string is drawn. The
 * launcher measures with SDL_ttf (src/ui.c's ui_fit_text and
 * ui_balance_lines); tools/linebreak-check.c with a table of its own, so it
 * holds the rule with no font and no window.
 *
 * BALANCED, NOT GREEDY. A greedy break fills each line as far as it goes and
 * leaves what is over for the next, so a long name in a narrow column comes
 * out as full lines with one short word alone under them. A name is set here
 * the way a person would set it: in as few lines as it fits in, and of those
 * breaks the one whose widest line is narrowest, so the lines come out near
 * one length. The port loading screen breaks the game's name this way, in up
 * to three lines (src/main.c, port_loading_screen). */

#include <stddef.h>

/* How wide `s` is drawn, in whatever units the box is given in. */
typedef int (*lb_width_fn)(void *ctx, const char *s);

/* Bytes in one line, its NUL included: a game's title (src/library.h's
 * LIB_NAME) whole. */
#define LB_LINE 256

/* The most lines a break is asked for: a cap over it is held to it. */
#define LB_MAX_LINES 8

/* `src` into `dst`, shortened with an ellipsis until it fits `maxw`: bytes
 * stepped back one at a time and then off any UTF-8 continuation, so a
 * character is never cut in half; nothing at all when not even the ellipsis
 * fits. src/ui.c's ui_fit_text is this, measured by SDL_ttf: one rule for
 * where the dots go, and one copy of it. */
void lb_fit(const char *src, char *dst, size_t dstn, int maxw,
            lb_width_fn width, void *ctx);

/* `text` in at most `cap` lines, none wider than `maxw`, into `lines`;
 * returns how many, 0 for a text with no words or a cap under 1.
 *
 *   One line when it fits.
 *   Otherwise the fewest lines it fits in, up to `cap`, broken between
 *   words, and of those breaks the one whose widest line is narrowest.
 *   A word wider than the box on its own is cut with an ellipsis (lb_fit)
 *   and stands as the cut word.
 *   A text that fits in no `cap` lines even so fills them in order, each with
 *   as many words as it takes, and the last is cut with an ellipsis.
 *
 * So never more than `cap` lines and never a line over `maxw`. Words are what
 * spaces separate: a run of spaces is one break, and spaces at either end are
 * dropped. */
int lb_balance(const char *text, int maxw, int cap, lb_width_fn width,
               void *ctx, char lines[][LB_LINE]);

/* Where an edge `at` units into `text` falls between its characters: into
 * *in the end of the last character that ends at or before `at`, 0 when none
 * does, and into *out the end of the first that ends at or after it, the
 * text's width when `at` is past its end. Both `at` when the edge is already
 * between two. Measured a whole character at a time, never inside the bytes
 * of one, as the text's first characters, so for text set left to right,
 * which is all the launcher sets; 0 and 0 for no text, and both `at` for a
 * text too long for the stack that cannot be copied.
 *
 * For src/ui.c's ui_text_marquee, which stops its window at these when a
 * name rests in it, so the window's end shows whole letters and not a sliver
 * of the next one. */
void lb_edges(const char *text, int at, lb_width_fn width, void *ctx,
              int *in, int *out);

#endif
