/* SPDX-License-Identifier: GPL-3.0-only */
#include "rowmotion.h"
#include "design.h"
#include <math.h>
#include <stdlib.h>
#define EDGE 4.5f
static row_slot sample(const row_track *t,float u)
{
    float e=ds_out(u),pos=t->from+(t->to-t->from)*e;
    float alpha=t->alpha+(t->to_alpha-t->alpha)*e;
    if(t->wrap && u<1) {
        float edge=t->from<0?-EDGE:EDGE;
        if(u<.5f) { e=ds_out(u*2); pos=t->from+(edge-t->from)*e; alpha=t->alpha*(1-e); }
        else { e=ds_out((u-.5f)*2); pos=-edge+(t->to+edge)*e; alpha=t->to_alpha*e; }
    }
    return (row_slot){t->item,pos,alpha};
}
static int targets(int cursor,int count,row_slot out[ROW_WINDOW])
{
    int n=0;
    if(count<=0) return 0;
    for(int d=-3;d<=3;d++) {
        int item=((cursor+d)%count+count)%count,j;
        float offset=d;
        /* Two visible items slide between their real seats. They do not
         * jump to the other edge just to honor a ring's direction. */
        if(count==2) offset=item-cursor;
        for(j=0;j<n;j++) if(out[j].item==item) break;
        if(j<n) { if(fabsf(offset)<fabsf(out[j].offset)) out[j].offset=offset; }
        else out[n++]=(row_slot){item,offset,1};
    }
    return n;
}
void row_reset(row_motion *r,int cursor,int count,uint32_t now)
{
    *r=(row_motion){.initialized=true,.cursor=cursor,.count=count,.moved=now};
    row_slot slots[ROW_WINDOW]; int n=targets(cursor,count,slots);
    for(int i=0;i<n;i++) r->tracks[r->n++]=(row_track){slots[i].item,slots[i].offset,slots[i].offset,1,1,false};
}
void row_select(row_motion *r,int cursor,int count,uint32_t now)
{
    if(!r->initialized || r->count!=count) { row_reset(r,cursor,count,now); return; }
    if(cursor==r->cursor) return;
    row_slot old[ROW_SLOTS],want[ROW_WINDOW];
    int nold=row_sample(r,now,old),n=targets(cursor,count,want),previous=r->cursor;
    int delta=abs(cursor-previous); if(delta>count/2) delta=count-delta;
    r->n=0; r->cursor=cursor; r->moved=now;
    for(int i=0;i<n;i++) {
        row_slot from={want[i].item,want[i].offset,0};
        for(int j=0;j<nold;j++) if(old[j].item==want[i].item) { from=old[j]; break; }
        bool wrap=from.offset*want[i].offset<0 && want[i].item!=previous && want[i].item!=cursor;
        /* Page/letter jumps cut to the destination window, then fade in. */
        if(delta>1) { from.offset=want[i].offset; from.alpha=0; wrap=false; }
        r->tracks[r->n++]=(row_track){want[i].item,from.offset,want[i].offset,from.alpha,1,wrap};
    }
    for(int j=0;delta<=1 && j<nold && r->n<ROW_SLOTS;j++) {
        bool found=false; for(int i=0;i<n;i++) if(want[i].item==old[j].item) found=true;
        if(!found && old[j].alpha>0) r->tracks[r->n++]=(row_track){old[j].item,old[j].offset,
            old[j].offset<0?-EDGE:EDGE,old[j].alpha,0,false};
    }
}
int row_sample(const row_motion *r,uint32_t now,row_slot out[ROW_SLOTS])
{
    int n=0; float u=(float)(now-r->moved)/DS_MS_MOVE;
    for(int i=0;i<r->n;i++) {
        row_slot s=sample(&r->tracks[i],u);
        if(s.alpha>.00001f) out[n++]=s;
    }
    return n;
}
bool row_moving(const row_motion *r,uint32_t now)
{
    if(now-r->moved>=DS_MS_MOVE) return false;
    for(int i=0;i<r->n;i++) if(r->tracks[i].from!=r->tracks[i].to ||
        r->tracks[i].alpha!=r->tracks[i].to_alpha || r->tracks[i].wrap) return true;
    return false;
}
