/* SPDX-License-Identifier: GPL-3.0-only */
#include "games.h"
#include "artbadge_draw.h"
#include "zyperdraw.h"
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
/* One part of the line under the name at x, or nothing once a part did not
 * fit (x < 0): returns where the next one starts, or -1 */
#define GAMES_META_RIGHT (1024-64)
/* The system's line beside A's hint (page_hint at x 800): 40 short of it */
#define GAMES_ORIGIN_RIGHT 760
/* Where the line under the name sits: a title of one line leaves the second
 * line's place (44 down) to it, whether it names the system (Favorites) or
 * gives the play time (a shelf). Both read this, so they cannot drift apart. A
 * title of two lines leaves the slot below instead. */
#define GAMES_SUBLINE_Y 628
#define GAMES_SUBLINE_Y_LOW 664
static int meta_part(SDL_Renderer *r,TTF_Font *f,const char *s,int x,int y)
{
    if(x<0) return -1;
    int w=ui_text_width(f,s);
    if(x+w>GAMES_META_RIGHT) return -1;
    ui_text(r,f,s,x,ui_cap_y(f,y),-1,UI_TEXT_DIM);
    return x+w+32;
}

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
            SDL_RenderCopyF(r,info->snap,info->snap_src.w>0?&info->snap_src:NULL,&vr);
            SDL_SetTextureAlphaMod(info->snap,255);
            SDL_RenderSetClipRect(r,NULL);
        }
        /* Its badge (src/artbadge.h), at the top left: a heart on a
         * favorite. Scaled with the cover as games_cover_rect scales it, and
         * fading as the snap mark does when the snap takes the cover's place,
         * so it never sits over the video. */
        if(info->badge_of && k<1) {
            int kind=info->badge_of(ctx,slots[i].item);
            if(kind!=AB_NONE)
                ab_draw_at(r,(ab_kind)kind,dst.x,dst.y,.72f+.28f*focus,(Uint8)lroundf(a*(1-k)));
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
    const char *name=info->title && info->title[0]?info->title:T(S_GAMES_NO_GAMES);
    /* A favorite's heart leads its name (2026-11-09: on the cover it was
     * hard to see), in **favorite** as every heart is, centered on the
     * first line's capitals; the name moves over for it. Not on Favorites,
     * where every game is one (info->favorite is false there). */
    int tx=64,tw=656;
    if(info->favorite&&info->count) {
        int cap=ui_cap_height(title);
        float hr=cap*.55f;
        draw_heart(r,64+hr,584,hr,ui_col(theme()->favorite,255));
        tx=64+(int)lroundf(hr*2)+16; tw=656-(tx-64);
    }
    char lines[2][LB_LINE];
    int nlines=ui_balance_lines(title,name,tw,2,lines);
    bool whole=complete_title(name,lines,nlines);
    if(whole) {
        for(int i=0;i<nlines;i++) ui_text(r,title,lines[i],tx,ui_cap_y(title,584+i*44),-1,UI_TEXT);
    } else {
        ui_text_marquee(r,title,name,tx,ui_cap_y(title,584),tw,phase,UI_TEXT);
        unsigned next=ui_pingpong_wait(ui_text_width(title,name)-tw,phase); if(next<wait) wait=next;
    }
    /* The game's own system on Favorites, right under its name (
     * 2026-11-07: "have the system right below the name"): in the second
     * line's place when the name takes one, clear of A's hint; under a name
     * of two lines there is no room above the line below, so it leads that
     * line instead. */
    bool origin=info->origin && *info->origin;
    if(origin && (!whole || nlines<2) && 64+ui_text_width(chrome,info->origin)<=GAMES_ORIGIN_RIGHT) {
        ui_text(r,chrome,info->origin,64,ui_cap_y(chrome,GAMES_SUBLINE_Y),-1,UI_TEXT_DIM);
        origin=false;
    }
    /* The line below: the system when it did not fit above, then Played,
     * then achievements, 32 apart. A part that would pass the right margin
     * is left out with the ones after it, never drawn over the edge. */
    int x=64;
    /* The slot beside A's hint ends at GAMES_ORIGIN_RIGHT; a line that would
     * pass it takes the low slot, where the whole width is free. */
    int my=GAMES_SUBLINE_Y_LOW;
    if((!whole || nlines<2) && !(info->origin && *info->origin && !origin)) {
        char pt[64]=""; int w=0, parts=0;
        if(origin) { w+=ui_text_width(chrome,info->origin); parts++; }
        if(info->install) { w+=ui_text_width(chrome,T(S_PORTMASTER_NOT_INSTALLED)); parts++; }
        if(info->played && *info->played) { snprintf(pt,sizeof pt,T(S_GAMES_PLAYED),info->played); w+=ui_text_width(chrome,pt); parts++; }
        if(info->has_achievements) { char at[64]; snprintf(at,sizeof at,T(S_GAMES_ACHIEVEMENTS),info->earned,info->total); w+=ui_text_width(chrome,at); parts++; }
        if(parts) w+=(parts-1)*32;
        if(64+w<=GAMES_ORIGIN_RIGHT) my=GAMES_SUBLINE_Y;
    }
    if(origin) x=meta_part(r,chrome,info->origin,x,my);
    if(info->install) x=meta_part(r,chrome,T(S_PORTMASTER_NOT_INSTALLED),x,my);
    if(info->played && *info->played) {
        char text[64]; snprintf(text,sizeof text,T(S_GAMES_PLAYED),info->played);
        x=meta_part(r,chrome,text,x,my);
    }
    if(info->has_achievements) {
        char text[64]; snprintf(text,sizeof text,T(S_GAMES_ACHIEVEMENTS),info->earned,info->total);
        meta_part(r,chrome,text,x,my);
    }
    if(info->count) page_hint(r,"A",T(info->install?S_PORTMASTER_INSTALL:
                                      info->resumable?S_GAMES_RESUME:S_GAMES_PLAY),800,600,true,false);
    page_hint(r,"B",T(S_GAMES_SYSTEMS),64,720,false,false);
    if(info->count&&info->resumable&&!info->install) page_hint(r,"Y",T(S_GAMES_FRESH),432,720,false,false);
    page_hint(r,"X",T(S_GAMES_DETAILS),832,720,false,false);
    SDL_SetRenderDrawColor(r,red,green,blue,alpha);
    SDL_SetRenderDrawBlendMode(r,blend);
    return wait;
}
