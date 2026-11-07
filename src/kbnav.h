/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_KBNAV_H
#define ZYPEROS_KBNAV_H

/* The on-screen keyboard's keys: where each one is, where the d-pad goes from
 * it, and what the action keys say (DESIGN.md 11.5).
 *
 * Four rows of ten keys, and under them the action row: shift, symbols,
 * space, delete and done, keys that do what X, Y, B and START do, for a
 * player who has not learned the buttons yet. The d-pad reaches the action
 * row as row 4 and leaves it by the column under a key's center, so where a
 * press goes depends on the layout - which is why the layout is here, beside
 * the moves, and src/keyboard.c draws from these numbers rather than its own.
 *
 * Split out of keyboard.c, the way sys_menu.c is out of main.c: no SDL here,
 * so tools/kbnav-check.c can hold every move without a window. */

#include <stdbool.h>

/* One key's press pop. Zero initialization is at rest. A fresh press starts
 * at 92%; a repeat during its return starts from the current scale. Each
 * press replaces the DS_MS_PRESS return on ds_out, never queues it. Times
 * use unsigned millisecond differences, so a wrapping monotonic clock is
 * valid. Sampling a completed pop clears it to rest. */
typedef struct {
	float from;
	unsigned started;
	bool active;
} kb_pop;

void kb_pop_press(kb_pop *p, unsigned now);
float kb_pop_scale(kb_pop *p, unsigned now);

#define KB_COLS 10
#define KB_ROWS 4                /* the grid: the digits and three more */
#define KB_ROW_ACTIONS KB_ROWS   /* the action row, under the grid */

/* The action row, left to right. Its column (in kb_move's `col`) is this. */
typedef enum {
	KB_ACT_SHIFT,     /* X */
	KB_ACT_SYMBOLS,   /* Y */
	KB_ACT_SPACE,
	KB_ACT_DELETE,    /* B */
	KB_ACT_DONE,      /* START */
	KB_ACTIONS
} kb_action;

/* The layout, in units, from the keyboard lab (docs/design/mock/kit.css,
 * .kb2.wide): ten 78-unit keys with 6 between them, and the action row the
 * same width under them. */
#define KB_KEY_W   78
#define KB_KEY_H   60
#define KB_KEY_GAP 6
#define KB_GRID_W  (KB_COLS * KB_KEY_W + (KB_COLS - 1) * KB_KEY_GAP)

/* Where action key `a` is: `x` from the grid's left edge, `w` wide. */
void kb_action_span(int a, int *x, int *w);

/* The grid column, and the action key, under `x` (from the grid's left
 * edge). A point in a gap belongs to the nearer of the two keys beside it,
 * and one off either end to the key at that end. */
int kb_col_under(int x);
int kb_action_under(int x);

/* (row, col) brought onto a key: a row above the digits or below the action
 * row to the nearer of the two, and a column to the nearest key of its row,
 * which in the action row is one of its five. kb_move does this first, and
 * the shot harness's selection comes through it. */
void kb_clamp(int *row, int *col);

typedef enum { KB_UP, KB_DOWN, KB_LEFT, KB_RIGHT } kb_dir;

/* One press of the d-pad from (row, col). Every edge wraps. In the action row
 * `col` is the kb_action; going up or down out of it lands on the grid column
 * under that key's center, and coming down into it (or up, round the wrap
 * from the digits) lands on the key under the column's center. */
void kb_move(int *row, int *col, kb_dir d);

/* Whether A, held, may repeat the action key it is on (delete, as B
 * repeats): only a hold that began with a press on that key. `armed` is the
 * answer from the frame before, `on` whether the selection is on the key this
 * frame, `pressed` and `down` A's state this frame. Moving off the key or
 * letting go ends it, and a thumb resting on A while the d-pad walks onto
 * delete never starts it. */
bool kb_hold_armed(bool armed, bool on, bool pressed, bool down);

/* Layers: 0 the letters, 1 the capitals, 2 the symbols. The layer X's key
 * (KB_ACT_SHIFT) or Y's (KB_ACT_SYMBOLS) goes to from `layer`; the other
 * actions keep it. */
int kb_layer_after(int layer, kb_action a);

/* What action key `a` says on `layer`: what pressing it does next, so X's is
 * "unshift" on the capitals and Y's "abc" on the symbols. Space says "": it
 * is drawn as the space mark. */
const char *kb_action_label(kb_action a, int layer);

/* The button whose glyph the key carries, as printed on the handheld, or
 * NULL for space, which has none. */
const char *kb_action_button(kb_action a);

#endif
