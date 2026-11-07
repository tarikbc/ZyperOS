/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "games.h"
#include "home.h"
#include "ui.h"
#include "theme.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

void games_reset(games_row *r,int cursor,int count,uint32_t now)
{
    *r=(games_row){.initialized=true,.cursor=cursor,.count=count,
                   .from=cursor,.target=cursor,.moved=now};
    row_reset(&r->motion,cursor,count,now);
}
float games_position(const games_row *r,uint32_t now)
{ return r->from+(r->target-r->from)*ds_out((float)(now-r->moved)/DS_MS_MOVE); }
bool games_moving(const games_row *r,uint32_t now)
{ return r->initialized && r->from!=r->target && now-r->moved<DS_MS_MOVE; }
void games_select(games_row *r,int cursor,int count,int direction,uint32_t now)
{
    if(!r->initialized || r->count!=count) { games_reset(r,cursor,count,now); return; }
    if(count<=1 || r->cursor==cursor) return;
    row_select(&r->motion,cursor,count,now);
    float pos=games_position(r,now);
    int delta=cursor-r->cursor;
    while(delta>count/2) delta-=count;
    while(delta< -count/2) delta+=count;
    if(count==2 && direction) delta=direction;
    r->from=pos; r->target+=delta; r->direction=delta<0?-1:1;
    /* Letter/page jumps reveal only the destination window. No unrelated
     * intermediate cover races through the center, and no cold row is decoded. */
    if(abs(delta)>1) r->from=r->target-r->direction*.2f;
    r->cursor=cursor; r->moved=now;
    if(fabsf(r->target)>4096) {
        float turns=floorf(r->target/count)*count;
        r->from-=turns; r->target-=turns;
    }
}
int games_slots(const games_row *r,uint32_t now,games_slot out[GAMES_WINDOW])
{
    row_slot samples[ROW_SLOTS]; int n=row_sample(&r->motion,now,samples);
    for(int i=0;i<n;i++) out[i]=(games_slot){samples[i].item,samples[i].offset,samples[i].alpha};
    /* Far covers first, focused art last. Wrap changes representatives at
     * the outside edge, never by interpolating an inactive cover across zero. */
    for(int i=1;i<n;i++) {
        games_slot s=out[i]; int j=i-1;
        while(j>=0 && fabsf(out[j].offset)<fabsf(s.offset)) { out[j+1]=out[j]; j--; }
        out[j+1]=s;
    }
    return n;
}
SDL_FRect games_cover_rect(int width,int height,float offset)
{
    if(width<=0 || height<=0) return (SDL_FRect){0};
    float focus=fmaxf(0,1-fabsf(offset)), scale=.72f+.28f*focus;
    float fit=fminf((float)GAMES_COVER_W/width,(float)GAMES_COVER_H/height)*scale;
    float w=width*fit,h=height*fit;
    return (SDL_FRect){512+offset*390-w/2,376-h/2,w,h};
}
/* See whether balancing retained all words. A longer name gets a full marquee,
 * rather than silently losing the tail to lb_balance's final-line ellipsis. */
static bool complete_title(const char *title,char lines[][LB_LINE],int n)
{
    const unsigned char *p=(const unsigned char *)title;
    for(int i=0;i<n;i++) for(const unsigned char *q=(unsigned char *)lines[i];*q;q++) {
        if(*q==' ') continue;
        while(*p==' ') p++;
        if(*p++!=*q) return false;
    }
    while(*p==' ') p++;
    return !*p;
}
uint32_t games_draw(games_row *row,SDL_Renderer *r,const games_info *info,
                    cf_tex_fn texture,void *ctx,uint32_t now,uint32_t phase)
{
    SDL_BlendMode blend; Uint8 red,green,blue,alpha;
    SDL_GetRenderDrawBlendMode(r,&blend);
    SDL_GetRenderDrawColor(r,&red,&green,&blue,&alpha);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    games_select(row,info->cursor,info->count,0,now);
    uint32_t wait=row_moving(&row->motion,now)?0:UINT32_MAX;
    TTF_Font *meta=page_font(PAGE_META),*chrome=page_font(PAGE_CHROME);
    char count[40]; snprintf(count,sizeof count,"%d / %d",info->count?info->cursor+1:0,info->count);
    int countw=ui_text_tabular_width(chrome,count);
    int sw=ui_text_width(meta,info->system),room=960-64-countw-32;
    if(sw>room) {
        ui_text_marquee(r,meta,info->system,64,ui_cap_y(meta,152),room,phase,UI_TEXT_SOFT);
        unsigned next=ui_pingpong_wait(sw-room,phase); if(next<wait) wait=next;
    } else ui_text(r,meta,info->system,64,ui_cap_y(meta,152),-1,UI_TEXT_SOFT);
    ui_text_tabular(r,chrome,count,960,ui_cap_y(chrome,152),1,UI_TEXT_DIM);

    games_slot slots[GAMES_WINDOW]; int n=games_slots(row,now,slots);
    for(int i=0;i<n;i++) {
        if(fabsf(slots[i].offset)>1.8f) continue;
        int w=0,h=0; float cb=1;
        SDL_Texture *t=texture(ctx,slots[i].item,&w,&h,&cb);
        if(!t || w<=0 || h<=0) continue;
        SDL_FRect dst=games_cover_rect(w,h,slots[i].offset);
        float focus=fmaxf(0,1-fabsf(slots[i].offset));
        Uint8 was; SDL_BlendMode texblend;
        SDL_GetTextureAlphaMod(t,&was); SDL_GetTextureBlendMode(t,&texblend);
        SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
        Uint8 a=(Uint8)lroundf(was*(.42f+.58f*focus)*slots[i].alpha);
        float k=info->snap && info->snap_k>0 && slots[i].item==info->cursor &&
                fabsf(slots[i].offset)<.001f ? fminf(info->snap_k,1) : 0;
        SDL_SetTextureAlphaMod(t,(Uint8)lroundf(a*(1-k)));
        if(k<1) SDL_RenderCopyF(r,t,NULL,&dst);
        SDL_SetTextureAlphaMod(t,was); SDL_SetTextureBlendMode(t,texblend);
        if(k>0) {
            /* The cover turns wide (My pick in the Scraper lab): the
             * snap, whole and at its own shape in the cover's box, shown
             * through a frame that grows from the cover's rect to its own,
             * as the cover fades. Never stretched on the way. */
            SDL_FRect vr=games_cover_rect(info->snap_w,info->snap_h,0);
            SDL_Rect clip={(int)floorf(dst.x+(vr.x-dst.x)*k),(int)floorf(dst.y+(vr.y-dst.y)*k),
                           (int)ceilf(dst.w+(vr.w-dst.w)*k)+1,(int)ceilf(dst.h+(vr.h-dst.h)*k)+1};
            SDL_RenderSetClipRect(r,&clip);
            SDL_SetTextureAlphaMod(info->snap,(Uint8)lroundf(a*k));
            SDL_RenderCopyF(r,info->snap,NULL,&vr);
            SDL_SetTextureAlphaMod(info->snap,255);
            SDL_RenderSetClipRect(r,NULL);
        }
        /* A cover with a snap says so: a play mark in an accent disc at its
         * bottom right, fading with the cover as the snap takes over (
         * 2026-11-06). Two sizes, the focused card's and the others', so the
         * glyph's cache holds both rather than drawing it anew each frame. */
        if(info->snap_of && k<1 && info->snap_of(ctx,slots[i].item)) {
            int d=focus>.5f?40:30, inset=focus>.5f?12:9;
            Uint8 ba=(Uint8)lroundf(a*(1-k));
            SDL_Rect disc={(int)lroundf(dst.x+dst.w)-inset-d,(int)lroundf(dst.y+dst.h)-inset-d,d,d};
            ui_round_rect(r,&disc,d/2,ui_col(theme()->accent,ba));
            ui_glyph_draw(r,UI_GLYPH_PLAY,disc.x+d/2+d/16,disc.y+d/2,d/2,ui_col(theme()->on_accent,ba));
        }
        /* The wait for the snap: a bar under the focused cover, the cover's
         * width, filling over hold-snap, held full while the first picture
         * comes, and fading with the cover as it turns (2026-11-06) */
        if(info->snap_wait>0 && slots[i].item==info->cursor && fabsf(slots[i].offset)<.001f && k<1) {
            int bw=(int)lroundf(dst.w),bx=(int)lroundf(dst.x),by=(int)lroundf(dst.y+dst.h)+12;
            float fade=(1-k)*slots[i].alpha;
            SDL_Color track=ui_col(theme()->track,(Uint8)lroundf(255*fade));
            SDL_Color fill=ui_col(theme()->accent_ink,(Uint8)lroundf(255*fade));
            int fw=(int)lroundf(bw*fminf(info->snap_wait,1));
            ui_round_rect(r,&(SDL_Rect){bx,by,bw,UI_BAR_H},UI_BAR_H/2,track);
            if(fw>0) ui_round_rect(r,&(SDL_Rect){bx,by,fw,UI_BAR_H},UI_BAR_H/2,fill);
        }
    }
    TTF_Font *title=page_font(PAGE_GAME_TITLE);
    const char *name=info->title && info->title[0]?info->title:"No games";
    char lines[2][LB_LINE];
    int nlines=ui_balance_lines(title,name,656,2,lines);
    if(complete_title(name,lines,nlines)) {
        for(int i=0;i<nlines;i++) ui_text(r,title,lines[i],64,ui_cap_y(title,584+i*44),-1,UI_TEXT);
    } else {
        ui_text_marquee(r,title,name,64,ui_cap_y(title,584),656,phase,UI_TEXT);
        unsigned next=ui_pingpong_wait(ui_text_width(title,name)-656,phase); if(next<wait) wait=next;
    }
    int x=64;
    if(info->played && *info->played) {
        char text[64]; snprintf(text,sizeof text,"Played %s",info->played);
        x+=ui_text(r,chrome,text,x,ui_cap_y(chrome,664),-1,UI_TEXT_DIM)+32;
    }
    if(info->has_achievements) {
        char text[64]; snprintf(text,sizeof text,"Achievements %d / %d",info->earned,info->total);
        ui_text(r,chrome,text,x,ui_cap_y(chrome,664),-1,UI_TEXT_DIM);
    }
    if(info->count) page_hint(r,"A",info->resumable?"Resume":"Play",800,600,true,false);
    page_hint(r,"B","Systems",64,720,false,false);
    page_hint(r,"Y",info->favorite?"Favorited":"Favorite",432,720,false,info->favorite);
    page_hint(r,"X","Details",832,720,false,false);
    SDL_SetRenderDrawColor(r,red,green,blue,alpha);
    SDL_SetRenderDrawBlendMode(r,blend);
    return wait;
}
