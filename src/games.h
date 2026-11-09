/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GAMES_H
#define ZYPEROS_GAMES_H
#include "coverflow.h"
#include "rowmotion.h"
#include <stdint.h>
#define GAMES_WINDOW ROW_SLOTS
#define GAMES_COVER_W 384
#define GAMES_COVER_H 328

typedef struct {
    row_motion motion;
    bool initialized;
    int count,cursor,direction;
    float from,target;
    uint32_t moved;
} games_row;
typedef struct { int item; float offset,alpha; } games_slot;
typedef struct {
    const char *system,*title,*played;
    /* The focused game's own system, on a shelf of many systems' games
     * (Favorites): first on the line under its name (2026-11-07: "In
     * the favorites screen, let's have the system written below the game
     * name"). NULL on a console's own shelf, where the heading says it. */
    const char *origin;
    /* The game under the cursor is a favorite: a heart before its name.
     * False on Favorites, where every game is one. */
    bool favorite;
    int count,cursor,earned,total;
    bool has_achievements,resumable;
    /* The focused entry is PortMaster while it is not on the card
     * (src/pminstall.h): A says Install, and the line under its name
     * that it is not installed */
    bool install;
    /* The focused game's video snap (src/snapshelf.h), or NULL: its picture,
     * its shape, and how far the cover has turned into it, 0 to 1. */
    SDL_Texture *snap;
    int snap_w,snap_h;
    /* The part of its texture the picture is in (snapv_texture's src); none
     * drawn from the whole */
    SDL_Rect snap_src;
    float snap_k;
    /* How far the wait before that snap has gone, 0 to 1, drawn as a bar
     * under the focused cover (DESIGN.md 11.19). 0 or less draws none, so a
     * shelf that never asks about snaps leaves it out by saying nothing. */
    float snap_wait;
    /* Whether item `i` has a video snap, for the mark on its cover; NULL for
     * none at all. Asked with games_draw's ctx. */
    bool (*snap_of)(void *ctx,int i);
    /* The badge on item `i`'s cover (src/artbadge.h), or AB_NONE; NULL for
     * none on any. Asked with games_draw's ctx, for every cover drawn, every
     * frame, so a favorite set on the details page shows the next frame. */
    int (*badge_of)(void *ctx,int i);
} games_info;
void games_reset(games_row *row,int cursor,int count,uint32_t now);
void games_select(games_row *row,int cursor,int count,int direction,uint32_t now);
float games_position(const games_row *row,uint32_t now);
bool games_moving(const games_row *row,uint32_t now);
int games_slots(const games_row *row,uint32_t now,games_slot slots[GAMES_WINDOW]);
SDL_FRect games_cover_rect(int width,int height,float offset);
/* No I/O: the caller supplies cached facts and its existing worker-backed textures. */
uint32_t games_draw(games_row *row,SDL_Renderer *r,const games_info *info,
                    cf_tex_fn texture,void *ctx,uint32_t now,uint32_t title_phase);
#endif
