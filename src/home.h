/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HOME_H
#define ZYPEROS_HOME_H
#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>
#include "ds/focus.h"
#include <SDL_ttf.h>
#include "rowmotion.h"
#include "gridrule.h"

/* Every system's still stays resident (2026-11-05: "have the systems
 * pictures pre-loaded on ram so it doesn't have to load while we scroll"):
 * 48 of them, 128 square, are 3 MiB. An atlas is a design's first pose,
 * 256 square, and the moving region of each pose (src/systemart_data.h);
 * they are 35 MiB together decoded, at most 2.1 MiB each, so only the
 * focused system's and its two neighbors' are kept. */
#define HOME_STILLS 64
#define HOME_NEAR 2
#define HOME_LOADER_ATLAS (-3)
#define HOME_LOADER_STILL (-4)
typedef enum { HOME_SPOTLIGHT, HOME_GALLERY, HOME_GRID } home_layout;
#define HOME_LAYOUTS 3
/* A system on the home: its art's key, its name, and its color (systems.cfg's,
 * or CFG_NO_COLOR), for the Grid's focus lift */
typedef struct { const char *key, *name; unsigned accent; } home_item;
typedef struct { bool active, paused; char title[128]; } home_song;
/* `content`: the opaque texels of every pose of an atlas together, in the
 * cell's coordinates (w 0: none, the design does not move, or not an
 * atlas) - a focus frame's ground. An atlas's `tex` is its first pose, the
 * cell square, and `motion` (decoded, in RAM) the moving region of each pose:
 * the one due is written into `tex` over the region, and `shown` is the pose
 * `tex` holds there now. */
typedef struct {
    int asset;
    SDL_Texture *tex;
    bool done;
    SDL_Rect content;
    SDL_Surface *motion;
    int shown;
    SDL_Texture *shade;   /* a still's own soft shadow: its shape blurred (Highlight's lift) */
} home_texture;
/* A tile the Grid drew: its system's key and name, its color, where it was
 * (its cell's top left, its still's side, the cell's width and height) and
 * its name's type (GH_NAME_*), for a glide when the systems change */
typedef struct {
    char key[16], name[32];
    unsigned accent;
    float x, y, s, cw, ch;
    int font;
} home_tile;
typedef struct {
    row_motion motion;
    bool initialized;
    int cursor, count;
    float from, target;
    uint32_t moved, animation;
    home_texture atlas, near[HOME_NEAR], still[HOME_STILLS];
    SDL_Renderer *renderer;
    /* Grid (DESIGN.md 11.1): the cursor it last drew, the first row in sight
     * and the focus's place (units, the window at its first row), each easing
     * from where it was (ms.move, ms.quick), for a grid of grid_n systems,
     * grid_cols across, stills of grid_still; grid_ax the x where Up and Down
     * started (-1: none yet), so Down then Up comes back */
    int grid_cursor, grid_cols, grid_n, grid_still, grid_ax;
    /* The tiles' weights in the focus's glide (src/ds/focus.h) */
    ds_focus_weights grid_w;
    float grid_top_from, grid_top_to, grid_fx, grid_fy;
    uint32_t grid_moved;
    /* The tiles it drew last (grid_live: the last home drawn was a Grid),
     * and a glide from them (grid_from, grid_map: each system's tile there,
     * -1 for one that came) to a new grid when the systems change, held at
     * its start while a menu is over the home */
    home_tile grid_was[HOME_STILLS], grid_from[HOME_STILLS];
    int grid_was_n, grid_from_n;
    signed char grid_map[HOME_STILLS];
    uint32_t grid_glide;
    bool grid_live, grid_gliding, grid_held;
    /* A menu is over the home (the caller's, each frame) */
    bool covered;
} home;

/* The approved 640px previews use these sizes on the 1024px logical canvas. */
typedef enum { PAGE_CHROME, PAGE_ACTION, PAGE_META, PAGE_GAME_TITLE, PAGE_FONTS } page_font_role;
bool page_fonts_init(const char *path);
void page_fonts_quit(void);
TTF_Font *page_font(page_font_role role);
void page_hint(SDL_Renderer *r, const char *key, const char *label,
               int x, int cy, bool focus, bool favorite);
/* The same, in alpha `alpha` (a screen that fades its keys in and out), and
 * how wide one is, glyph to the label's end (to lay a row of them out) */
void page_hint_fade(SDL_Renderer *r, const char *key, const char *label,
                    int x, int cy, bool focus, bool favorite, Uint8 alpha);
int page_hint_width(const char *key, const char *label, bool focus);

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
/* The same jump for a direction held down a list: it never goes round, and
 * stays at the end it reached, as a held step stops at a menu's end
 * (menu_step_index). MENU > Storage's list in A to Z (DESIGN.md 11.32). */
int home_letter_held(const unsigned char *keys, int count, int cursor, int dir);
/* One repeat of Up or Down held down MENU > Storage's list, `held_ms` since
 * it went down (DESIGN.md 11.32). With `keys` (A to Z) it is home_letter_held,
 * a letter; with none (Size, Last Played, not in letter order)
 * HOME_HELD_ROWS rows. Further the longer it is held, as MP4's seek is
 * (repeat_stage, src/keyrepeat.h): twice from a second, four times from
 * three. Never round an end: it stops there, as a held step does. */
#define HOME_HELD_ROWS 5
int home_held_step(const unsigned char *keys, int count, int cursor, int dir, unsigned held_ms);
float home_position(const home *h, uint32_t now);
bool home_moving(const home *h, uint32_t now);
bool home_song_update(home_song *song, bool active, bool paused, const char *title);
/* Art requests use their own loader identities. No forget/bump of game jobs. */
void home_prepare(home *h, SDL_Renderer *r, const char *root,
                  const home_item *items, int count, int cursor, uint32_t now);
/* A finished picture for the home (`shelf` a HOME_LOADER_*): true when it
 * was the home's. An atlas in the texture's format is kept as it is, and
 * *surface is then NULL; the caller frees whatever is left. */
bool home_accept(home *h, SDL_Renderer *r, int shelf, int asset, SDL_Surface **surface);
/* Highlight's list picture (MENU > Display > Theme, DESIGN.md 11.42): three
 * of the home's systems in a row in `box`, the middle one in focus as
 * Highlight's choice `focus` (LOOK_FOCUS_*) marks it, the others stepped
 * back, the stills only */
void home_focus_preview(home *h, SDL_Renderer *r, const home_item *items, SDL_Rect box, int focus);
void home_free(home *h);
/* Spotlight's focused sprite alone, for a frame where nothing else on the
 * home has changed (main.c, focus frames): whether that is so - Spotlight,
 * the row still - the part of its square any of its frames can touch, which
 * the caller clips to and lays ground on, and every layer home_draw lays
 * there drawn at `now` as home_draw would (the sprite, then the row as
 * Highlight marks it). Returns the delay to its next frame, as home_draw
 * does. */
#define HOME_FOCUS_GROUNDS 8
bool home_focus_alone(const home *h, home_layout layout, int count, int cursor, uint32_t now);
int home_focus_ground(const home *h, int count, int cursor, uint32_t now,
                      SDL_Rect out[HOME_FOCUS_GROUNDS]);
uint32_t home_draw_focus(home *h, SDL_Renderer *r, const home_item *items, int count,
                         int cursor, uint32_t now, bool animate);
/* Where the Grid puts `count` systems on the canvas (gh_layout, in the
 * home's room): Up and Down move a row (gh_row_step), L1 and R1 a screenful
 * (src/shelf_keys.c) */
extern int home_asks;
gh_geom home_grid_geom(int count);
/* Returns the delay to the next changed frame, UINT32_MAX when still/hidden. */
uint32_t home_draw(home *h, SDL_Renderer *r, home_layout layout,
                    const home_item *items, int count, int cursor,
                    const char *count_label, const char *open_label,
                    uint32_t now, bool animate);
/* status_left is measured by the existing status corner before this call.
 * `phase` is ms into the song's name sliding (ui_text_marquee): a name wider
 * than its room slides, one that fits stands still. Returns the ms until the
 * picture next changes by itself, 0 while the name moves, UINT32_MAX when it
 * never will (the caller's redraw_at). */
uint32_t home_header(SDL_Renderer *r, const home_song *song, int status_left, unsigned phase);
#endif
