/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_STORAGE_SCREEN_H
#define ZYPEROS_STORAGE_SCREEN_H

/* MENU > Storage (DESIGN.md 11.32): how the card is used, its
 * recommendations and a page per system, measured by src/storage.h. The
 * screen is src/storage_screen.c; its rows are src/storage_page.c's. */
#include "app.h"
#include "panel.h"

#include <SDL.h>
#include <stdbool.h>

/* The row's value on MENU: the card's free room, kept a while */
const char *storage_menu_value(void);
/* MENU > Storage */
void storage_screen(app *a);

/* What the Cover and Video page draws with too: Zyper as the page draws him */
void store_zyper(SDL_Renderer *r, float cx, float cy, float h, float tilt, bool shadow);
/* The page as a base, as MP4's shelf is: the screen's, and a shot's */
int store_base_open(void);

/* --storage NAME [ROW] and --storage-sort, for a shot (storage_screen.c
 * says what each is); the shot itself */
extern const char *shot_storage;
extern int shot_storage_sel;
extern const char *shot_storage_row;
extern int g_store_sort_shot;
void storage_shot(app *a);

#endif
