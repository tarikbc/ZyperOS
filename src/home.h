/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HOME_H
#define ZYPEROS_HOME_H
#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include "rowmotion.h"

/* Every system's still stays resident (2026-11-05: "have the systems
 * pictures pre-loaded on ram so it doesn't have to load while we scroll"):
 * 39 of them are 2.4 MiB. Animated atlases are 23 MiB together, so only the
 * focused system's and its two neighbors' are kept. */
#define HOME_STILLS 64
#define HOME_NEAR 2
#define HOME_LOADER_ATLAS (-3)
#define HOME_LOADER_STILL (-4)
typedef enum { HOME_SPOTLIGHT, HOME_GALLERY } home_layout;
typedef struct { const char *key, *name; } home_item;
typedef struct { bool active, paused; char title[128]; } home_song;
/* `content`: an atlas's opaque texels, every cell's together, in a cell's
 * coordinates (w 0: none, or not an atlas) - a focus frame's ground */
typedef struct { int asset; SDL_Texture *tex; bool done; SDL_Rect content; } home_texture;
typedef struct {
    row_motion motion;
    bool initialized;
    int cursor, count;
    float from, target;
    uint32_t moved, animation;
    home_texture atlas, near[HOME_NEAR], still[HOME_STILLS];
    SDL_Renderer *renderer;
} home;

/* The approved 640px previews use these sizes on the 1024px logical canvas. */
typedef enum { PAGE_CHROME, PAGE_ACTION, PAGE_META, PAGE_GAME_TITLE, PAGE_FONTS } page_font_role;
bool page_fonts_init(const char *path);
void page_fonts_quit(void);
TTF_Font *page_font(page_font_role role);
void page_hint(SDL_Renderer *r, const char *key, const char *label,
               int x, int cy, bool focus, bool favorite);

home_layout home_parse(const char *id);
const char *home_id(home_layout layout);
const char *home_name(home_layout layout);
const char *home_label(const char *key, const char *canonical);
/* The art's key for a system's tag ("SNES" for SFC), as the shelf draws it,
 * or NULL when the tag has none: the File Transfer page shows the same
 * picture beside the system's folder (hare_set_folder_icon). */
const char *home_art_key(const char *tag);
void home_reset(home *h, int cursor, int count, uint32_t now);
void home_select(home *h, int cursor, int count, int direction, uint32_t now);
/* L1/R1 on the home (2026-11-06: "r1/l1 jumps 3 systems"): the system
 * HOME_JUMP along from `cursor`, `dir` -1 or 1, coming round past either end
 * as a step does - home_select moves the row the whole way, in the press's
 * direction. On a row too short for the whole jump to land somewhere new it
 * goes one less than the row: two systems are a step apart, one is none. */
#define HOME_JUMP 3
int home_jump(int cursor, int count, int dir);
/* A system's group on the A to Z home: the first letter or digit of its name,
 * upper-cased, and a group of its own for Favorites (tag FAV) and for Ports
 * (core "port"), which the order puts first and last whatever they are called
 * (cfg_sort_systems). */
/* Up and down on the home, with MENU > System Order on A to Z: `keys` is each
 * system's group (its name's first letter, or a key of its own for Favorites
 * and Ports), in the row's order. Down is the first system of the next group;
 * up is the first of the cursor's own group, or of the previous one when it is
 * already there. Round the ends as home_jump goes, and the cursor stays put
 * when the whole row is one group. Pure: the caller builds `keys` on the press. */
unsigned char home_group_key(const char *tag, const char *core, const char *name);
int home_letter_jump(const unsigned char *keys, int count, int cursor, int dir);
float home_position(const home *h, uint32_t now);
bool home_moving(const home *h, uint32_t now);
bool home_song_update(home_song *song, bool active, bool paused, const char *title);
/* Art requests use their own loader identities. No forget/bump of game jobs. */
void home_prepare(home *h, SDL_Renderer *r, const char *root,
                  const home_item *items, int count, int cursor, uint32_t now);
bool home_accept(home *h, SDL_Renderer *r, int shelf, int asset, SDL_Surface *surface);
void home_free(home *h);
/* Spotlight's focused sprite alone, for a frame where nothing else on the
 * home has changed (main.c, focus frames): whether that is so - Spotlight,
 * the row still, and the sprite's atlas clear of the row in every frame -
 * the part of its square any of its frames can touch, which the caller lays
 * ground on, and the sprite drawn there at `now` as home_draw would. Returns
 * the delay to its next frame, as home_draw does. */
#define HOME_FOCUS_GROUNDS 8
bool home_focus_alone(const home *h, home_layout layout, int count, int cursor, uint32_t now);
int home_focus_ground(const home *h, int count, int cursor, uint32_t now,
                      SDL_Rect out[HOME_FOCUS_GROUNDS]);
uint32_t home_draw_focus(home *h, SDL_Renderer *r, const home_item *items, int count,
                         int cursor, uint32_t now, bool animate);
/* Returns the delay to the next changed frame, UINT32_MAX when still/hidden. */
uint32_t home_draw(home *h, SDL_Renderer *r, home_layout layout,
                    const home_item *items, int count, int cursor,
                    const char *count_label, const char *open_label,
                    uint32_t now, bool animate);
/* status_left is measured by the existing status corner before this call. */
void home_header(SDL_Renderer *r, const home_song *song, int status_left);
#endif
