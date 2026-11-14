/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_UPDATE_SCREEN_H
#define ZYPEROS_UPDATE_SCREEN_H

/* MENU > System Update's page (src/sysupdate.h has what it says, DESIGN.md
 * 11.17), which Install PortMaster's page is drawn as too. The page is
 * src/update_screen.c. */
#include "app.h"
#include "sysupdate.h"

#include <SDL.h>

/* What's New: as many of the release's points as the page reads, each at
 * most this long */
#define UP_NOTES   12
#define UP_NOTE_W  512

/* What's New, read from the release's list; how many */
int up_notes(char out[UP_NOTES][UP_NOTE_W]);
/* What's New's first line on screen (a shot's --update sets it) */
extern int g_up_scroll;

/* The page: the heading is the caller's, `notes` are the points under the
 * line, and `icon` stands where Zyper does, NULL for him */
void update_page_draw(app *a, const char *heading, const su_page *p,
                      const char *const *notes, int nnotes, SDL_Texture *icon);
/* The button's action on this page, or SUA_NONE */
su_action update_page_key(const su_page *p, const char *button);
/* MENU > System Update */
void update_screen(app *a);

#endif
