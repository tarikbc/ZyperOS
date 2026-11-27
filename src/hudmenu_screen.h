/* SPDX-License-Identifier: GPL-3.0-only */
/* The On-Screen Stats page over the paused game (src/hudmenu.h): A on the
 * in-game menu's On-Screen Stats row opens it. Its rows under a band at its
 * top, the band the screen's own strip where the line goes, at its own
 * size: the paused game there, and the line in it as the rows make it, so
 * each change shows as it is made. The Shows row opens the items' list,
 * over the same band. Every change is kept and sent to Diatom at once
 * (SETHUD), so B leaves with it already on the game. */
#ifndef ZYPEROS_HUDMENU_SCREEN_H
#define ZYPEROS_HUDMENU_SCREEN_H

#include <SDL.h>

#include "app.h"
#include "hudmenu.h"
#include "ds/layout.h"

/* The band's height in units, the hud-band token: the top strip of the
 * screen holds the line at Far (48) dropped under the level bar (94 + 49),
 * and the bottom one at Far */
#define HM_BAND_H DS_HUD_BAND

/* The player's On-Screen Stats, from the device's database */
void hm_load(hm_state *s);
/* Diatom told the whole of it (SETHUD) */
void hm_send(const hm_state *s);

/* The page, over the paused game `bg` */
void hud_page(app *a, SDL_Texture *bg);

/* --hud-page ROW: the page as it draws, the cursor on ROW, over `bg`, for
 * a shot; with `items`, the items' list instead; with `pick` (a row, -1
 * none), that row's list of choices, as A opens it; `shows` (NULL: the
 * kept ones) the items, "fps,speed,temp" */
void hud_page_shot(app *a, SDL_Texture *bg, int sel, bool items, int pick, const char *shows);

/* For check-hover-sweep (src/hover_sweep.h): On-Screen Stats' lists */
void hud_hover_sweep(app *a);

/* --rows-report: the page's rows and its items' list's, as kept
 * (menu_rows_print, tools/menu-rows-check.py) */
void hud_rows_report(app *a);

#endif
