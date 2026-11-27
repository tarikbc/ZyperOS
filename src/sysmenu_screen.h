/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SYSMENU_SCREEN_H
#define ZYPEROS_SYSMENU_SCREEN_H

/* MENU (DESIGN.md 11.3): its rows built tab by tab, its keys, the top menu
 * and a system's, and MENU > Display's Night Mode and Theme pages. The
 * screens are src/sysmenu_screen.c; the rows' words are src/sys_menu.c's. */
#include "app.h"
#include "linebreak.h"
#include "panel.h"
#include "ds/layout.h"

/* MENU > Display > Night Mode's page: Night Mode, Night Warmth, Night From
 * and Until, each an option row applied at once; a row the mode does not
 * use is quiet (DESIGN.md 11.42) */
enum { NP_MODE, NP_WARMTH, NP_FROM, NP_UNTIL, NP_ROWS };
typedef struct { app *a; char warmth[4], from[24], until[24]; } night_view;

/* MENU > Display > Theme: Accent, Light Background, Dark Background,
 * Folder Color, Corners, Zyper and Reset to Default. Left and Right step a
 * row round and apply it at once; A opens a row's list, and on Reset puts
 * them all back. */
enum { LK_ACCENT, LK_LIGHT, LK_DARK, LK_FOLDERS, LK_CORNERS, LK_HIGHLIGHT, LK_ZYPER, LK_RESET, LK_ROWS };

/* What main.c uses of it; each is described where it is defined */
int usb_auto_lines(app *a, char lines[4][LB_LINE], menu_row rows[4]);
void cheevos_unchecked_message(app *a);
int menu_build(app *a, screen_id screen, int sys,
               menu_row *out, menu_bufs *b, const char **heading);
void look_bar_colors(void);
void look_load(void);
void power_button_load(void);
int night_page_build(void *ctx, menu_row *rows, int max, const char **heading);
/* Its rows' choices by place, as night_page_build last made them (the shots) */
const menu_choice *night_page_choices(void);
/* MENU > Display's Night Mode row at its longest mode, for MENU's width; 0
 * where there is no Night Mode */
int night_row_widest(menu_row *out);
void look_row_mark(app *a, const menu_row *row, int right, int cy, float on_fill);
void look_header(app *a, const SDL_Rect *q);
int look_build(void *ctx, menu_row *rows, int max, const char **heading);
/* Screen Colors' page, for a shot as for the menu: its rows, its column (a
 * color chart, a grey ramp and a hue band, SC_SIDE_W wide), and its Look's
 * list with the chart as the list's picture */
#define SC_SIDE_W DS_SC_SIDE
int sc_build(void *ctx, menu_row *rows, int max, const char **heading);
void sc_side(app *a, const menu_side *at);
void sc_look_choice(menu_choice *out);
/* The page itself: from MENU > Display over the shelf (all NULL), and from
 * the in-game menu's Display tab over the paused game, with that menu's
 * backdrop, power rule and their `ctx` (src/ingame_screen.c, DESIGN.md
 * 11.42). And the look, its row's value in both menus. */
void screen_colors_page(app *a, void (*backdrop)(app *a, void *ctx, float scrim),
                        menu_result (*on_power)(app *a, void *ctx), void *ctx);
const char *screen_colors_value(void);
/* MENU > Sound > Equalizer (src/eq.h): the page, from MENU and MP4's menu;
 * its rows, the presets' list with each one's picture, and that picture,
 * for the shots (--look-page eq, eq-presets) */
void eq_screen(app *a);
int  eq_page_build(void *ctx, menu_row *rows, int max, const char **heading);
void eq_preset_choice(menu_choice *out);
void eq_curve_preview(void *app, int k, int x, int y, int w, int h);
/* --eq-phones: the page and its list as with headphones in (a shot's) */
extern bool g_shot_eq_phones;
menu_result sysmenu_key(app *a, void *ctx, in_button key, int sel);
void zyperos_menu(app *a);

/* Theme's rows' choices by place, as look_build last made them (the shots) */
const menu_choice *theme_choices(void);
/* --rows-report: MENU's rows, the shelf's menu's, Theme's and Night Mode's (menu_rows_print) */
void sysmenu_rows_report(app *a);
/* For check-hover-sweep (--hover-sweep, src/hover_sweep.h): MENU's, a
 * system's, Theme's and Screen Colors' lists through what their cursors
 * show */
void sysmenu_hover_sweep(app *a);

#endif
