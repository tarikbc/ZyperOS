/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Menu Music, as rows: what src/bgm.h's settings look like in a menu.
 * No SDL, so tools/bgm-check.c builds it on a host; main.c's bgm_screen
 * draws it and acts on its keys. */
#ifndef ZYPEROS_BGM_MENU_H
#define ZYPEROS_BGM_MENU_H

#include <stdbool.h>

#include "menu.h"

/* The rows, in order. Music turns it on and off; Volume is how loud under the
 * system's volume; ZyperOS Tracks is whether ZyperOS's own play with the
 * card's (src/bgm.h, bgm_scan_all), there only while the card has some of
 * its own, since without them ZyperOS's play whatever it says; Track is
 * Shuffle or one track over and over; Song Pop-up is whether Zyper says
 * each new song's name on the shelf (DESIGN.md 11.22); Playing is what
 * plays, and A on it is the next track. */
typedef enum {
	BGM_ROW_ON, BGM_ROW_VOLUME, BGM_ROW_BUILTIN, BGM_ROW_TRACK, BGM_ROW_POPUP,
	BGM_ROW_NOW, BGM_ROWS
} bgm_row;

typedef struct {
	bool        on;
	int         volume;     /* BGM_VOLUME_MIN .. MAX */
	const char *track;      /* the setting: "" or NULL for shuffle */
	const char *now;        /* the track playing or paused, "" or NULL for none */
	int         ntracks;    /* in the list, all of them */
	int         own;        /* ...the card's: Menu Music's */
	bool        with_builtin; /* ZyperOS Tracks */
	bool        popup;      /* Song Pop-up */
	const char *now_name;   /* what `now` is called, from its tags as the
	                         * pop-up says it; NULL for its file's name */
} bgm_ui;

/* The text the rows point into, each row's sound class (menu.h), and which
 * row each one is (bgm_row; BGM_ROWS for the rule and the note) */
typedef struct {
	char vol[8], track[128], now[128], count[96];
	unsigned char sound[BGM_ROWS + 3];
	unsigned char id[BGM_ROWS + 3];
} bgm_bufs;

/* Fills rows (max at least BGM_ROWS + 3) and returns how many: the rows,
 * a rule, and a note of how many tracks there are, whose, or with none of
 * the card's own where they go (Menu Music). */
int bgm_menu_build(const bgm_ui *u, menu_row *rows, int max, bgm_bufs *b,
                   const char **heading);

/* The value of the Menu Music row in the ZyperOS menu: "On", "Off", or
 * "no tracks" for an empty folder - so the row says why it is silent. */
const char *bgm_menu_value(bool on, int ntracks);

#endif
