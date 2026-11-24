/* SPDX-License-Identifier: GPL-3.0-only */
#include "games.h"
#include "gamelist.h"
#include "design.h"
#include "home.h"
#include "platform.h"
#include "theme.h"
#include "ui.h"
#include "videoview.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
static SDL_Texture *no_cover(void *ctx, int index, int *w, int *h, float *bottom)
{
    (void)ctx; (void)index; (void)w; (void)h; (void)bottom;
    return NULL;
}

/* The Video view's band (src/videoview.h, DESIGN.md 11.57): a frame each
 * while its covers glide, as the cover row's (games_draw), past the end of
 * the band's own slide; none once they have landed and nothing plays. It
 * asked for none for the glide, so its last frame stayed up with a cover
 * half faded at the band's side until the next picture or the heartbeat
 * (2026-11-19, on the 1.1.0 test build: "a black box stays on the side") */
static void video_view_glide(void)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
    snaprest m = { 0 };
    games_row vr = { 0 };
    vv_info in = { .heading = "SNES", .title = "Bravo", .count = 8, .cursor = 1, .band = -1 };
    uint32_t t, start = 5000;

    assert(r);
    theme_set_file(NULL);
    theme_init(APPEAR_LIGHT);
    assert(ui_init(r, "res/fonts/menu.ttf"));
    assert(page_fonts_init("res/fonts/menu.ttf"));
    games_reset(&vr, 0, 8, 1000);
    snaprest_reset(&m, 1000);
    assert(vv_draw(&m, &vr, r, &in, no_cover, NULL, 4000, 0) == UINT32_MAX);
    games_select(&vr, 1, 8, 1, start);
    snaprest_touch(&m, start);
    for (t = start; t < start + DS_MS_MOVE; t += 4)
        assert(vv_draw(&m, &vr, r, &in, no_cover, NULL, t, 0) == 0);
    assert(vv_draw(&m, &vr, r, &in, no_cover, NULL, start + DS_MS_MOVE + 1, 0) == UINT32_MAX);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surface);
}

/* The rest, the same in both views (src/snaprest.h): the Video view's band
 * and the List view's details stay up while the snap starts, ask the frame
 * at the end of hold-band, go down in move, and a press brings them back;
 * one implementation drives both, so they cannot drift apart */
static const char *list_name(void *ctx, int i)
{
    static const char *const N[] = { "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel" };
    (void)ctx;
    return N[i & 7];
}

static void rest_both_views(void)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 1024, 768, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
    SDL_Texture *snap = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 64, 48);
    snaprest band = { 0 };
    gl_motion lm = { 0 };
    games_row vr = { 0 };
    vv_info vi = { .heading = "SNES", .title = "Bravo", .count = 8, .cursor = 1, .band = -1,
                   .snap = snap, .snap_w = 64, .snap_h = 48, .snap_k = 1, .playing = true };
    gl_info li = { .heading = "SNES", .count = 8, .cursor = 1, .title_of = list_name,
                   .snap = snap, .snap_w = 64, .snap_h = 48, .snap_k = 1, .has_snap = true };
    const uint32_t start = 1000, play = start + 500;

    assert(r && snap);
    games_reset(&vr, 1, 8, start);
    snaprest_reset(&band, start);
    gl_reset(&lm, 1, 8);
    snaprest_reset(&lm.details, start);
    /* the snap's first frame playing: up, the next frame at the end of the rest */
    assert(vv_draw(&band, &vr, r, &vi, no_cover, NULL, play, 0) == DS_MS_HOLD_BAND);
    assert(gl_draw(&lm, r, &li, no_cover, NULL, play, 0) == DS_MS_HOLD_BAND);
    assert(snaprest_at(&band, play) == 0 && snaprest_at(&lm.details, play) == 0);
    assert(vv_draw(&band, &vr, r, &vi, no_cover, NULL, play + DS_MS_HOLD_BAND - 1, 0) == 1);
    assert(gl_draw(&lm, r, &li, no_cover, NULL, play + DS_MS_HOLD_BAND - 1, 0) == 1);
    /* hold-band on: both slide down in move, a frame each */
    assert(vv_draw(&band, &vr, r, &vi, no_cover, NULL, play + DS_MS_HOLD_BAND, 0) == 0);
    assert(gl_draw(&lm, r, &li, no_cover, NULL, play + DS_MS_HOLD_BAND, 0) == 0);
    {
        uint32_t t = play + DS_MS_HOLD_BAND + DS_MS_MOVE / 2;
        float a, b;

        vv_draw(&band, &vr, r, &vi, no_cover, NULL, t, 0);
        gl_draw(&lm, r, &li, no_cover, NULL, t, 0);
        a = snaprest_at(&band, t);
        b = snaprest_at(&lm.details, t);
        assert(a > .5f && a < 1 && fabsf(a - b) < 1e-6f);
    }
    assert(vv_draw(&band, &vr, r, &vi, no_cover, NULL, play + DS_MS_HOLD_BAND + DS_MS_MOVE, 0) == UINT32_MAX);
    assert(gl_draw(&lm, r, &li, no_cover, NULL, play + DS_MS_HOLD_BAND + DS_MS_MOVE, 0) == UINT32_MAX);
    assert(snaprest_at(&band, play + 9000) == 1 && snaprest_at(&lm.details, play + 9000) == 1);
    /* a press: both back up in quick */
    snaprest_touch(&band, play + 10000);
    snaprest_touch(&lm.details, play + 10000);
    assert(snaprest_at(&band, play + 10000 + DS_MS_QUICK) == 0);
    assert(snaprest_at(&lm.details, play + 10000 + DS_MS_QUICK) == 0);
    SDL_DestroyTexture(snap);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surface);
}

int main(void)
{
    games_row row={0}; games_slot slots[GAMES_WINDOW];
    games_reset(&row,0,3,0);
    games_select(&row,1,3,1,0);
    float midway=games_position(&row,40);
    assert(midway>0 && midway<1);
    /* The third, inactive cover changes edges; it never crosses the center. */
    for(unsigned t=0;t<=DS_MS_MOVE;t++) {
        int n=games_slots(&row,t,slots);
        assert(n>=2 && n<=3);
        for(int i=0;i<n;i++) if(slots[i].item==2) assert(fabsf(slots[i].offset)>=1);
    }
    games_select(&row,0,3,-1,40);
    assert(fabsf(games_position(&row,40)-midway)<.0001f);
    assert(games_position(&row,200)==0);
    games_reset(&row,0,2,0);
    games_select(&row,1,2,-1,0);
    assert(games_position(&row,160)==-1);
    assert(games_slots(&row,160,slots)==2);
    games_select(&row,0,2,-1,160);
    assert(games_position(&row,320)==-2);
    games_reset(&row,0,1,0);
    assert(games_slots(&row,0,slots)==1 && slots[0].offset==0);
    games_reset(&row,0,0,0);
    assert(games_slots(&row,0,slots)==0);
    games_reset(&row,0,100,0);
    games_select(&row,50,100,0,0);
    assert(games_slots(&row,0,slots)<=GAMES_WINDOW);
    assert(fabsf(games_position(&row,0)-games_position(&row,160))<1);
    for(int i=0;i<3;i++) {
        int w[]={200,600,300},h[]={600,200,300};
        SDL_FRect box=games_cover_rect(w[i],h[i],0);
        assert(box.w<=GAMES_COVER_W && box.h<=GAMES_COVER_H);
        assert(fabsf(box.w/box.h-(float)w[i]/h[i])<.001f);
    }
    /* The five covers (DESIGN.md 11.1): the focused one flat; the near pair
     * turned, its near edge outward and taller, clear of the focused cover
     * for every shape of box; the far pair's inner edge on the screen; the
     * last seat, where a cover slides in from, unseen */
    for(int r=0;r<=30;r++) {
        float aspect=.5f+r*.05f; int w=(int)lroundf(aspect*600),h=600;
        SDL_FPoint q[4]; float al;
        SDL_FRect focus=games_cover_rect(w,h,0);
        games_cover_corners(w,h,0,q,&al);
        assert(al==1 && fabsf(q[0].y-q[1].y)<.01f && fabsf(q[0].x-focus.x)<.01f);
        for(int side=-1;side<=1;side+=2) {
            games_cover_corners(w,h,(float)side,q,&al);
            float inner=side>0?q[0].x:q[1].x;
            float outer_h=side>0?q[2].y-q[1].y:q[3].y-q[0].y, inner_h=side>0?q[3].y-q[0].y:q[2].y-q[1].y;
            assert(al>0 && al<1 && outer_h>inner_h);
            assert(side>0?inner>focus.x+focus.w:inner<focus.x);
            games_cover_corners(w,h,2.0f*side,q,&al);
            inner=side>0?q[0].x:q[1].x;
            assert(al>0 && inner>0 && inner<ZYPEROS_SCREEN_W);
            games_cover_corners(w,h,3.0f*side,q,&al);
            assert(al==0);
        }
    }
    video_view_glide();
    rest_both_views();
    puts("ok: game-row aspect, wrap, tiny lists, rapid reversals, and bounded jumps");
    puts("ok: the Video view asks a frame for every step of its covers' glide");
    puts("ok: the Video view's band and the List view's details rest on one clock: up as the snap starts, down hold-band on in move, up at a press");
    puts("ok: five covers, the focused one flat, the near pair clear of it for every shape");
    return 0;
}
