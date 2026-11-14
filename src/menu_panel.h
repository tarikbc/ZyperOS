/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MENU_PANEL_H
#define ZYPEROS_MENU_PANEL_H

/* A menu's panel drawn: its rows, notes, tabs, marquees and Zyper beside the
 * heading, and the state the runner, the screens and the shots set around a
 * draw. The drawing is src/menu_panel.c, which says what each is; the
 * runner and the panel pass are main.c's. */
#include "app.h"
#include "menu.h"
#include "menumotion.h"
#include "panel.h"
#include "sys_menu.h"
#include "zyperrig.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <stdbool.h>

/* MENU's tabs (src/sys_menu.h, DESIGN.md 11.3): the tab on, the one it came
 * from and which way (+1 for R1, -1 for L1), and when on the animation clock,
 * for the move between them; t0 0 is at rest. One for the launcher, as MENU
 * is one, kept while the handheld is on: MENU opens where it was closed.
 * `g_menu_tabs` is set around MENU's own draw, as g_menu_zyper is, and NULL
 * everywhere else, so the screens it opens draw their own headings. */
struct menu_tabs {
	int at, from, dir;
	unsigned t0;
	/* how many tabs there are, and the name of each, so the row and the
	 * turn serve any set of tabs, not MENU's alone */
	int count;
	const char *(*name)(int i);
};
typedef struct menu_tabs menu_tabs;

/* Set around a draw, and NULL or -1 everywhere else: Zyper beside the
 * heading and his moment, MENU's tabs and the row each tab was left on, the
 * runner's cursor moved by a key handler, the row marks, the header
 * picture, the picture at the rows' left, a copy being drawn (snap_draw),
 * the rows' badges, About's scrolled body, and a new list's epoch */
extern const zr_rig *g_menu_zyper;
extern unsigned      g_menu_zyper_at;
extern menu_tabs        g_tabs;
extern const menu_tabs *g_menu_tabs;
extern pm_row           g_tab_row[PT_TABS];
extern int g_menu_sel_to;
extern void (*g_row_mark)(app *a, const menu_row *row, int right, int cy, float on_fill);
extern void (*g_menu_header)(app *a, const SDL_Rect *r);
extern int    g_menu_header_h;
extern void (*g_menu_side)(app *a, const menu_side *at);
extern int    g_menu_side_w;
extern bool   g_snap_drawing;
extern SDL_Texture *(*g_row_icon)(int row);
extern int g_menu_scroll_hold;
extern int g_menu_scroll_span, g_menu_scroll_line;
extern unsigned g_menu_epoch;

/* What a row's value takes at the right of the rows, for the measures */
int menu_value_w(TTF_Font *fm, const char *v);
/* A note's height, and how many notes a panel with no rows can show */
int menu_note_h(void);
int menu_notes_fit(void);
/* Turn the tabs one way, +1 for R1 and -1 for L1, round the end */
void tabs_turn(menu_tabs *t, int dir);
/* The panel itself, at `pose` in its open or close; menu_draw_ex and
 * menu_draw_caption are how a screen asks for one */
void menu_panel(app *a, const char *heading, const menu_row *rows,
                int n, int sel, int fixed_w, unsigned accent,
                const unsigned *vcolors, bool visits_all, int loop_at,
                const char *heading_note, mm_pose pose);

#endif
