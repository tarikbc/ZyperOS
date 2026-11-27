/* SPDX-License-Identifier: GPL-3.0-only */
/* The list an option row opens on A (src/menu.h's menu_choice, DESIGN.md
 * 9): its choices under the row's name, a tick on the one that is on and
 * the cursor on it, as MENU > Language is. A chooses one, B leaves it as it
 * was. In the menu it opens from's accent and width, over its backdrop,
 * with its power rule (the in-game menu's paused game and its stop). */
#ifndef ZYPEROS_MENU_PICK_H
#define ZYPEROS_MENU_PICK_H

#include "panel.h"

#define MENU_PICK_POWER (-2)

/* The choice picked, or -1 for B (or the menu closing under it), or
 * MENU_PICK_POWER when the menu it opens from's power rule closed it (the
 * in-game menu's stop), so that menu closes too. `ticked`
 * is the one that is on (-1: none), `at` the one the cursor starts on. */
int menu_pick(app *a, const menu_style *from, void *from_ctx, const char *heading,
              const char *const *labels, const char *const *notes, int n, int ticked, int at);

/* The same with what the cursor shows (menu_choice's hover, src/ds/hover.h):
 * the choice under it shown from the frame the cursor reaches it and put
 * back when the list closes, whichever way, before a choice is applied; a
 * hover with draw has a picture column, the choice under the cursor drawn
 * in a 4:3 box at its top and its note wrapped under it, so the list holds
 * every choice and the note never pushes it into a scroll (the column is
 * src/ds/preview.h's). NULL hover: menu_pick. */
int menu_pick_hover(app *a, const menu_style *from, void *from_ctx, const char *heading,
                    const char *const *labels, const char *const *notes, const struct ds_hover *hover,
                    int n, int ticked, int at);
/* The list's width: `base`, the menu it opens from's, or wider with a
 * picture column, to hold the column and the longest choice beside it
 * (`ticked` with its tick); and the column's width into `side_w` (0
 * without one), ds_preview_col_w's */
int menu_pick_width(app *a, int base, const char *const *labels, int n, int ticked,
                    bool preview, int *side_w);

/* Where the picture column's box and its note's lines go, for `side_x` and
 * `top` the column's left and the rows' top: what menu_pick_preview draws
 * and the shots draw */
void menu_pick_side_draw(app *a, int side_x, int top, int side_w, int note_lines, const char *note,
                         void (*preview)(void *app, int k, int x, int y, int w, int h), int k);
/* The note's room for a set of `notes` in a column `side_w` wide: the
 * tallest's lines (ds_preview_note_lines) */
int menu_pick_note_room(const char *const *notes, int n, int side_w);

/* What a choice does, wrapped to the list's two lines under it (`notes`),
 * as the Display row's own line is; the lines it took. The shots draw it
 * too. */
int menu_pick_note(const char *note, char out[2][CHV_WRAP_COLS]);

/* A choice's words as its list shows them: the row's value as it is, which
 * begins with a capital where it is written (res/strings/GLOSSARY.md), a
 * name in its own case. The shots draw a list with it too. */
int menu_pick_word(const char *label, char *out, int n);

/* For tools/menu-rows-check.py (--rows-report): each row of `page` on a
 * line, "rows: page, label, value, live, sound class, choices, cur, none,
 * own, the word at cur, every choice", the words as a list shows them and
 * the choices apart by 0x1f */
void menu_rows_print(const char *page, const menu_row *rows, int n, const unsigned char *sound,
                     const menu_choice *choices);

#endif
