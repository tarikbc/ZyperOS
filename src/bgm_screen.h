/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_BGM_SCREEN_H
#define ZYPEROS_BGM_SCREEN_H

/* MENU > Menu Music: its rows are src/bgm_menu.c's, its player src/bgm_out.c.
 * Every change is heard as it is made - the music plays behind this screen -
 * and kept in the device database beside UI Sounds. The folder is read here
 * as the screen opens, and the player told to read it again, so tracks
 * copied over Wi-Fi or USB since the launcher started are in the list.
 * The screen is src/bgm_screen.c. */
#include "app.h"
#include "bgm.h"
#include "bgm_menu.h"
#include "menu.h"

#include <stddef.h>

/* The page's view: its rows' words, the list, and what plays */
typedef struct {
	bgm_bufs b;
	bgm_list list;
	char     now[512];
	char     now_name[BGM_TAG_MAX];   /* ...as the song pop-up names it */
} bgm_view;

/* The two folders menu music plays from (src/bgm.h): Menu Music at the top
 * of the card, and ZyperOS's own */
void bgm_dirs(char *dir, char *builtin, size_t n);
/* What plays, for the Menu Music page: as the player reads it */
void bgm_scan_now(bgm_list *l);
/* The page's rows, the screen's and a shot's (--music-page) */
int bgm_build(void *ctx, menu_row *rows, int max, const char **heading);
/* MENU > Menu Music */
void bgm_screen(app *a);

#endif
