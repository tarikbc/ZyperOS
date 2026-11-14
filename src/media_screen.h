/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MEDIA_SCREEN_H
#define ZYPEROS_MEDIA_SCREEN_H

/* A game's Cover and Video page (DESIGN.md 11.34): the details page's row,
 * the cover on the shelf beside what the sources offer, and the game's
 * video snap. The screen is src/media_screen.c; its rows are
 * src/mediapage.c's. */
#include "app.h"
#include "games.h"

/* The page for game `g` of shelf `owner` */
void media_screen(app *a, int owner, const game_entry *g);

/* --media NAME [TILE] and --media-art DIR, for a shot (media_screen.c says
 * what each is); the shot itself */
extern const char *shot_media;
extern const char *shot_media_art;
extern int shot_media_tile;
void media_shot(app *a);

#endif
