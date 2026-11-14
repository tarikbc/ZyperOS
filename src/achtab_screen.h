/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_ACHTAB_SCREEN_H
#define ZYPEROS_ACHTAB_SCREEN_H

/* The in-game menu's Achievements tab on screen (src/achtab.h is its words
 * and rows): the line of counts pinned under the tab row, the list with each
 * achievement's badge (src/rabadge.h, the plain one until the card has the
 * art) and its title and points, every row reachable, earned ones in the
 * earned color; or, for a game with no list, the sentence that says why,
 * balanced onto up to AT_LINES lines. main.c hooks it into the in-game menu's
 * build, keys and close (gm_menu_build, gm_key, game_menu).
 *
 * L1 and R1 turn the tabs, so the list has no "eight at a time" paging of
 * its own: it goes round, as every long list does (menu_step_round). A on a
 * row opens the achievement (main.c's cheevo_detail_screen). */

#include <stdbool.h>

#include "achtab.h"
#include "app.h"
#include "linebreak.h"
#include "panel.h"

/* A row of the list from wherever the achievements are: chv_at on a device,
 * a made-up set in a shot */
typedef const cheevo *(*at_source)(int i);

/* The most lines a sentence takes: as many rows as the panel shows */
#define AT_LINES 6

typedef struct {
	at_state state;
	int      n;                    /* rows built */
	bool     mastered;             /* every one earned and sent */
	char     counts[128];
	char     vals[CHV_MAX][32];
	char     lines[AT_LINES][LB_LINE];
	unsigned vcol[CHV_MAX];
	unsigned char sound[CHV_MAX];
} achtab_bufs;

/* The tab's rows, at most `max`, into `rows`; and how the panel draws them,
 * into `st`: the counts over the list (header), the badges (row_icon), the
 * earned color (vcolors), every row reachable (visits_all), or none of that
 * for a sentence. `w` is the width the sentence is balanced to, onto up to
 * AT_LINES lines. The count. */
int achtab_rows(app *a, const at_facts *f, at_source at, achtab_bufs *b,
                menu_row *rows, int max, int w, menu_style *st);

/* The achievement under row `sel` of the rows last built, NULL for a row of
 * a sentence */
const cheevo *achtab_pick(const achtab_bufs *b, at_source at, int sel);

/* Leaving the in-game menu: the badges' textures go (rb_textures_free) */
void achtab_close(void);

/* A made-up set for a shot (--gm-tab 3): eight real-length titles, the
 * first `earned` of them earned (-1: three), and a count moving on one */
const cheevo *achtab_shot_at(int i);
int  achtab_shot_count(void);
void achtab_shot_earned(int earned);

#endif
