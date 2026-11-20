/* SPDX-License-Identifier: GPL-3.0-only */
/* Core Settings: every (or whitelisted) option of a system's core, cycled and
 * kept as coreopt.<TAG>.<key> in the library. The shelf and the in-game menu
 * both open it as a page (coreopts_page); in-game A on Core Settings passes
 * the paused frame and asks Diatom for live OPTIONS. Otherwise definitions
 * come from the userdata cache alone. */
#ifndef ZYPEROS_COREOPTS_PAGE_H
#define ZYPEROS_COREOPTS_PAGE_H

#include <SDL.h>
#include <stdbool.h>

#include "app.h"
#include "coreopt_defs.h"
#include "menu.h"

/* The list's state: gather once, then build rows / handle Left-Right. */
typedef struct {
	char            tag[8];
	char            core[40];
	bool            live;
	codefs          defs;
	int             sel;
	unsigned char   sound[CODEFS_MAX + 4];
	menu_choice     choice[CODEFS_MAX + 4];
	const char     *labels[CODEFS_MAX][CODEFS_VALUES_MAX];
	char            cur[CODEFS_MAX][CODEFS_VAL_MAX];
	bool            empty;
} coreopts_ui;

/* Load definitions for `tag` / `core`. `live` asks Diatom for OPTIONS. */
void coreopts_ui_init(coreopts_ui *ui, const char *tag, const char *core,
                      bool live);

/* Free the defs blob heap held by `ui`. */
void coreopts_ui_finish(coreopts_ui *ui);

/* Fill `rows` (option lines, or the empty-state note). The count. */
int coreopts_ui_rows(coreopts_ui *ui, menu_row *rows, int max);

/* How many option rows the cursor can rest on (0 when empty). */
int coreopts_ui_count(const coreopts_ui *ui);

/* Left / Right cycles the value at `sel` and saves. True if handled. */
bool coreopts_ui_key(coreopts_ui *ui, in_button key, int sel);

/* Open as a full page (shelf menu). `bg` is the paused game when in-game
 * (NULL from the shelf). `live` asks Diatom when a game is paused. */
void coreopts_page(app *a, const char *tag, const char *core,
                   SDL_Texture *bg, bool live);

/* Host shot: the shelf's Core Settings page for the system under the
 * cursor, the focus on `row` (0 when empty). */
void coreopts_shot_draw(app *a, int row);

#endif
