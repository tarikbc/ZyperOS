/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MENUSNAP_H
#define ZYPEROS_MENUSNAP_H

/* A menu's panel kept whole: what it was drawn from, every row's text
 * copied, so it can be drawn again after the screen that drew it has rebuilt
 * its rows or gone. src/main.c keeps the panel on screen this way, and the
 * screen that comes next draws it leaving (DESIGN.md 8.2, src/menumotion.h).
 * Plain C, over src/menu.h and src/zyperrig.h; tools/menusnap-check.c holds
 * it without a screen. */

#include "menu.h"
#include "zyperrig.h"

#include <stdbool.h>
#include <stddef.h>

/* The most a copy keeps: rows, and bytes of text, their ends included. A
 * panel past either is not kept, and is cut where it would have faded. */
#define MENUSNAP_ROWS 512
#define MENUSNAP_TEXT 65536

typedef struct {
	bool        ok;           /* holds a panel */
	/* The caller's, which menusnap_take leaves alone: whose screen drew
	 * it, the frame it was last on screen in, and which drawer it is for. */
	int         level;
	unsigned    frame;
	int         kind;
	/* What menu_draw_ex was given, its text in `text` */
	const char *heading;
	/* menu_draw_caption's caption at the heading's right, or NULL */
	const char *note;
	menu_row    rows[MENUSNAP_ROWS];
	unsigned    vcolors[MENUSNAP_ROWS];
	bool        has_vcolors, visits_all;
	int         n, sel, fixed_w, loop_at;
	unsigned    accent;
	/* Zyper beside the heading, when he was: his rig by value, and his clock
	 * less the caller's as he was drawn */
	bool        zyper;
	zr_rig      zy;
	unsigned    zy_ahead;
	/* MENU's tab row, when it was drawn in place of the heading: the tab
	 * that was on, or -1; and a screen's row marks and header picture, as
	 * menu_style's, by pointer (set by the caller, as `level` is) */
	int         tab;
	void       *row_mark, *header;
	int         header_h;
	size_t      used;         /* of `text` */
	char        text[MENUSNAP_TEXT];
} menu_snap;

/* A panel's arguments copied into `s`: the heading's and every row's text,
 * the row marks (src/menu.h) and NULL as they are, `vcolors` when there are
 * any, and `zy`'s rig when there is one. False, and `s->ok` false, when they
 * do not fit. */
bool menusnap_take(menu_snap *s, const char *heading, const menu_row *rows,
                   int n, int sel, int fixed_w, unsigned accent,
                   const unsigned *vcolors, bool visits_all, int loop_at,
                   const zr_rig *zy, unsigned zy_ahead);

/* The caption at the heading's right (menu_draw_caption) copied into `s`
 * after menusnap_take, NULL as it is. False, and `s->ok` false, when it does
 * not fit or `s` holds no panel. */
bool menusnap_note(menu_snap *s, const char *note);

#endif
