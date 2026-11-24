/* SPDX-License-Identifier: GPL-3.0-only */
/* Core Settings (in-game only): every (or whitelisted) option of the running
 * core, from Diatom's live OPTIONS. A on the Game-tab row opens the page over
 * the paused frame; values are kept as coreopt.<TAG>.<key> in the library and
 * sent to the resident with SETOPT. There is no shelf row: without a running
 * core there is nothing to ask. */
#ifndef ZYPEROS_COREOPTS_PAGE_H
#define ZYPEROS_COREOPTS_PAGE_H

#include <SDL.h>
#include <stdbool.h>

#include "app.h"
#include "coreopt_defs.h"
#include "menu.h"

/* The list's state: gather once from the resident, then build rows / handle
 * Left-Right. */
typedef struct {
	char            tag[8];
	char            core[40];
	codefs          defs;
	int             sel;
	unsigned char   sound[CODEFS_MAX + 4];
	menu_choice     choice[CODEFS_MAX + 4];
	const char     *labels[CODEFS_MAX][CODEFS_VALUES_MAX];
	char            cur[CODEFS_MAX][CODEFS_VAL_MAX];
	bool            empty;
} coreopts_ui;

/* Load definitions for `tag` / `core` from the resident's OPTIONS. */
void coreopts_ui_init(coreopts_ui *ui, const char *tag, const char *core);

/* Free the defs blob heap held by `ui`. */
void coreopts_ui_finish(coreopts_ui *ui);

/* Fill `rows` (option lines, or the empty-state note). The count. */
int coreopts_ui_rows(coreopts_ui *ui, menu_row *rows, int max);

/* How many option rows the cursor can rest on (0 when empty). */
int coreopts_ui_count(const coreopts_ui *ui);

/* Left / Right cycles the value at `sel` and saves. True if handled. */
bool coreopts_ui_key(coreopts_ui *ui, in_button key, int sel);

/* Open as a full page over the paused game (`bg`). */
void coreopts_page(app *a, const char *tag, const char *core, SDL_Texture *bg);

/* Host shot: Core Settings drawn as the in-game page, definitions from the
 * userdata cache fixture (tools/i18n-shots.py), focus on `row`. */
void coreopts_shot_draw(app *a, int row);

#endif
