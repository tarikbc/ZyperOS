/* SPDX-License-Identifier: GPL-3.0-only */
/* Achievement badges: RetroAchievements' 64-pixel art for each achievement,
 * kept on the card beside the set it belongs to,
 *
 *     Roms/<System>/.cheevos/badges/<name>.png
 *
 * from media.retroachievements.org/Badge/<name>.png. Only the earned look is
 * fetched; the locked one is drawn from it here (greyed), never fetched.
 *
 * Fetched on the shelf, never in a game: after a game with a set ends, and
 * when a set is opened from the game's details - all of a set's missing
 * badges in one curl on a thread of its own (net_get_pngs), so nothing
 * waits on it and a handheld with no network just has none. tools/ra-sets.py
 * puts them on the card from a computer with the sets, for a handheld that is
 * never online. A badge that is not there is drawn as nothing: the pop-up and
 * the card are what they were before badges.
 *
 * The paths and the URL are pure; the fetching is a thread and a curl, and
 * the textures are the list's (main.c's g_row_icon). */
#ifndef ZYPEROS_RABADGE_H
#define ZYPEROS_RABADGE_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL.h>

#include "design.h"

#define RB_URL "https://media.retroachievements.org/Badge/"

/* Where badge `name` of the set at `set_path` is kept. False for no name. */
bool rb_path(const char *set_path, const char *name, char *out, size_t n);
/* It is on the card */
bool rb_have(const char *set_path, const char *name);

/* Badge `name` of the set at `set_path` as a texture, earned or `locked` -
 * the locked one greyed and faded here, never fetched - or NULL when the card
 * does not have it. Kept: the last RB_TEXTURES asked for stay, so a list
 * scrolled up and down reads each file once. */
#define RB_TEXTURES 48
SDL_Texture *rb_texture(SDL_Renderer *r, const char *set_path, const char *name,
                        bool locked);
/* The same, or where the card has no art for it - none yet, or never, on a
 * handheld with no network - the plain badge in theme `t` (src/noticeart.h),
 * earned or `locked`, so every row of a list has an icon of the one shape and
 * nothing moves when the art lands. Never NULL but for no renderer or no
 * memory. Kept as the badges are; the plain badge is made once a theme and
 * look. A frame never waits on the network for either: files are read from
 * the card, and the fetch is on its own thread. */
SDL_Texture *rb_icon(SDL_Renderer *r, const char *set_path, const char *name,
                     bool locked, const ds_theme *t);
/* Let them all go: a theme's renderer change, the screen closed */
void rb_textures_free(void);

/* The loaded set's (src/cheevos.h) missing badges, fetched behind whatever
 * is on screen. Does nothing while a fetch runs, with no network, or with
 * none missing. Main thread. */
void rb_fetch_loaded(void);
/* How many landed since the last call - a screen showing badges asks, to
 * draw again. Main thread. */
int  rb_landed(void);

#endif
