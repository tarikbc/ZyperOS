/* SPDX-License-Identifier: GPL-3.0-only */
#include "games.h"
#include "design.h"
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
    puts("ok: game-row aspect, wrap, tiny lists, rapid reversals, and bounded jumps");
    return 0;
}
