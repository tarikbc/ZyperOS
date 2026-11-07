/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* MENU > Menu Music, as rows: what src/bgm.h's settings look like in a menu.
 * No SDL, so tools/bgm-check.c builds it on a host; main.c's bgm_screen
 * draws it and acts on its keys. */
#ifndef ZYPEROS_BGM_MENU_H
#define ZYPEROS_BGM_MENU_H

#include <stdbool.h>

#include "menu.h"

/* The rows, in order. Music turns it on and off; Volume is how loud under the
 * system's volume; Track is Shuffle or one track over and over; Song Pop-up
 * is whether Zyper says each new song's name on the shelf (DESIGN.md 11.22);
 * Playing is what plays, and A on it is the next track. */
typedef enum {
	BGM_ROW_ON, BGM_ROW_VOLUME, BGM_ROW_TRACK, BGM_ROW_POPUP, BGM_ROW_NOW, BGM_ROWS
} bgm_row;

typedef struct {
	bool        on;
	int         volume;     /* BGM_VOLUME_MIN .. MAX */
	const char *track;      /* the setting: "" or NULL for shuffle */
	const char *now;        /* the file playing or paused, "" or NULL for none */
	int         ntracks;    /* in the folder */
	bool        builtin;    /* ...ZyperOS's own, Roms/BGM having none */
	bool        popup;      /* Song Pop-up */
	const char *now_name;   /* what `now` is called, from its tags as the
	                         * pop-up says it; NULL for its file's name */
} bgm_ui;

/* The text the rows point into, and each row's sound class (menu.h) */
typedef struct {
	char vol[8], track[128], now[128], count[64], put[64];
	unsigned char sound[BGM_ROWS + 3];
} bgm_bufs;

/* Fills rows (max at least BGM_ROWS + 3: the rows, a rule and a note, two
 * notes for ZyperOS's own tracks) and returns how many. */
int bgm_menu_build(const bgm_ui *u, menu_row *rows, int max, bgm_bufs *b,
                   const char **heading);

/* The value of the Menu Music row in the ZyperOS menu: "On", "Off", or
 * "no tracks" for an empty folder - so the row says why it is silent. */
const char *bgm_menu_value(bool on, int ntracks);

#endif
