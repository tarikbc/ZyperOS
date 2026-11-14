/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GAMEDEL_SCREEN_H
#define ZYPEROS_GAMEDEL_SCREEN_H

/* Delete Game (DESIGN.md 11.31, src/gamedel.h has the plan): the details
 * page's last row, its question and the yes. The screen is
 * src/gamedel_screen.c; the Storage page and the Cover and Video page ask
 * the same question. */
#include "app.h"
#include "menu.h"
#include "panel.h"

#include <stdbool.h>
#include <stddef.h>

/* The most lines the question can take */
#define DEL_ASK_LINES 12

/* How many of the question's lines fit over its two answers before the
 * panel windows; *rule whether a rule goes under them */
int delete_ask_fit(bool *rule);
/* The question's rows for `title`, wrapped into `lines`, its heading */
int delete_ask_rows(app *a, const char *title, char (*lines)[CHV_WRAP_COLS],
                    menu_row *rows, const char **heading);
/* One frame of the question over the shelf, Cancel focused, for a shot */
void delete_ask_preview(app *a);
/* The yes: true when the game went; `said` the message either way, empty
 * for none; `wait` draws the frame that says it is happening */
bool game_delete_now(app *a, int owner, const game_entry *g, bool wait,
                     char *said, size_t n);
/* A on Delete Game: true when the game went */
bool game_delete(app *a, int owner, const game_entry *g, char *said, size_t n);

#endif
