/* SPDX-License-Identifier: GPL-3.0-only */
#include "games.h"
#include "design.h"
#include "platform.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
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
    puts("ok: game-row aspect, wrap, tiny lists, rapid reversals, and bounded jumps");
    puts("ok: five covers, the focused one flat, the near pair clear of it for every shape");
    return 0;
}
