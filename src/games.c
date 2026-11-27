/* SPDX-License-Identifier: GPL-3.0-only */
#include "games.h"
#include "artbadge_draw.h"
#include "zyperdraw.h"
#include "home.h"
#include "keyhints_draw.h"
#include "ui.h"
#include "ds/flex.h"     /* the shelf, laid out by rules */
#include "ds/layout.h"
#include "ds/mark.h"
#include "ds/focus.h"
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
/* THE SHELF, LAID OUT BY RULES (src/ds/flex.h): one column between the
 * page's margins (64), from the system's line to the keys on every screen's
 * foot. Each part's box: */
#define SHELF_MARGIN    DS_MARGIN      /* the page's sides (64) */
#define SHELF_SYS_TOP   132   /* the system's line, its box's top (its middle at 152) */
#define SHELF_SYS_H     DS_LINE_META   /* that line: meta's (40) */
#define SHELF_COVER_GAP 40    /* the covers' band under it */
#define SHELF_TITLE_GAP DS_SECTION     /* the name's first line under the covers' band (24) */
shelf_frame games_frame(void)
{
    static ds_flex F;
    static shelf_frame f;
    static bool done;
    if(done) return f;
    int root=ds_flex_begin(&F,ZYPEROS_SCREEN_W,ZYPEROS_SCREEN_H,
                           &(ds_box){.dir=DS_COL,.pad={SHELF_MARGIN,SHELF_SYS_TOP,SHELF_MARGIN,DS_FOOT_INSET}});
    int sys=ds_flex_add(&F,root,&(ds_box){.h=SHELF_SYS_H});
    int covers=ds_flex_add(&F,root,&(ds_box){.pad={0,SHELF_COVER_GAP,0,0},.h=SHELF_COVER_GAP+GAMES_COVER_H});
    int title=ds_flex_add(&F,root,&(ds_box){.pad={0,SHELF_TITLE_GAP,0,0},.grow=1});
    int keys=ds_flex_add(&F,root,&(ds_box){.h=DS_HINT_GLYPH});
    ds_flex_solve(&F);
    f.sys=ds_flex_rect(&F,sys);
    f.covers=ds_flex_rect(&F,covers);
    f.covers.y+=SHELF_COVER_GAP; f.covers.h-=SHELF_COVER_GAP;
    f.title=ds_flex_rect(&F,title);
    f.title.y+=SHELF_TITLE_GAP; f.title.h-=SHELF_TITLE_GAP;
    f.keys=ds_flex_rect(&F,keys);
    done=true;
    return f;
}
/* THE COVER ROW'S SEATS (DESIGN.md 11.1). Where a cover at `offset` from the
 * focused one sits: its middle's distance from the row's middle, its size (of
 * the focused cover's box), how far it is turned (radians, its near edge
 * outward) and how much of it shows; a cover between two seats is between
 * them in all four. Five covers, the four beside the focused one turned as
 * MP4's album row turns its side cards (src/coverflow.c's tilt, 0.82, and its
 * side alpha, 150), the far pair smaller (2026-11-18: "can we fit more covers
 * instead of three? and make the furthest ones a bit inclined like we do in
 * the mp4 player?"). Three flat covers before, the two beside at 72% and 42%,
 * 390 out. A turned cover is narrower, which is what lets five fit the width
 * with the focused one at its size; the far pair stands partly past the edge
 * for the widest boxes, as MP4's do. The last seat shows nothing: the cover
 * that slides in next comes from it, decoded already. */
typedef struct { float x,scale,turn,alpha; } cover_seat;
#define GAMES_SEATS 4
static const cover_seat SEATS[GAMES_SEATS]={
    {0,1,0,1},{290,.72f,.82f,.59f},{458,.56f,.82f,.48f},{560,.5f,.82f,0},
};
static cover_seat seat_at(float offset)
{
    float d=fminf(fabsf(offset),GAMES_SEATS-1);
    int i=(int)d; if(i>=GAMES_SEATS-1) i=GAMES_SEATS-2;
    float k=d-i; const cover_seat *a=&SEATS[i],*b=&SEATS[i+1];
    return (cover_seat){a->x+(b->x-a->x)*k,a->scale+(b->scale-a->scale)*k,
                        a->turn+(b->turn-a->turn)*k,a->alpha+(b->alpha-a->alpha)*k};
}
/* A cover's place: its middle, its half size unturned, its turn (signed: a
 * cover right of the middle turns its right edge toward the eye) */
typedef struct { float cx,cy,hw,hh,ang,scale,alpha; } cover_pose;
static cover_pose cover_pose_at(int width,int height,float offset)
{
    cover_seat st=seat_at(offset);
    float fit=fminf((float)GAMES_COVER_W/width,(float)GAMES_COVER_H/height)*st.scale;
    const shelf_frame sf=games_frame();
    return (cover_pose){sf.covers.x+sf.covers.w/2.0f+(offset<0?-st.x:st.x),sf.covers.y+sf.covers.h/2.0f,
                        width*fit/2,height*fit/2,offset<0?st.turn:-st.turn,st.scale,st.alpha};
}
SDL_FRect games_cover_rect(int width,int height,float offset)
{
    if(width<=0 || height<=0) return (SDL_FRect){0};
    cover_pose p=cover_pose_at(width,height,offset);
    return (SDL_FRect){p.cx-p.hw,p.cy-p.hh,p.hw*2,p.hh*2};
}
/* A turned cover as MP4's album row draws one (src/coverflow.c): yawed about
 * its upright middle and seen through a weak perspective, the far edge
 * shorter, in COVER_STRIPS upright strips so the picture follows the turn.
 * Every texture coordinate is a strip's edge, 0 to 1: SDL 2.32.10, the
 * handhelds', draws nothing for one outside that (#297). */
#define COVER_STRIPS 8
static float cover_focal(const cover_pose *p) { return GAMES_COVER_W*p->scale*3+1; }
static void cover_point(const cover_pose *p,float lx,float ly,float *sx,float *sy,float *k)
{
    float f=cover_focal(p),z=lx*sinf(p->ang),s=f/(f+z);
    *sx=p->cx+lx*cosf(p->ang)*s; *sy=p->cy+ly*s; if(k) *k=s;
}
void games_cover_corners(int width,int height,float offset,SDL_FPoint q[4],float *alpha)
{
    cover_pose p=cover_pose_at(width,height,offset);
    for(int k=0;k<4;k++)
        cover_point(&p,k==1||k==2?p.hw:-p.hw,k<2?-p.hh:p.hh,&q[k].x,&q[k].y,NULL);
    if(alpha) *alpha=p.alpha;
}
static void turned_cover(SDL_Renderer *r,SDL_Texture *t,const cover_pose *p,Uint8 a)
{
    SDL_Vertex v[2*(COVER_STRIPS+1)];
    int idx[6*COVER_STRIPS],n=0;
    for(int i=0;i<=COVER_STRIPS;i++) {
        float u=(float)i/COVER_STRIPS,lx=-p->hw+2*p->hw*u,sx,sy0,sy1;
        cover_point(p,lx,-p->hh,&sx,&sy0,NULL); cover_point(p,lx,p->hh,&sx,&sy1,NULL);
        v[2*i]=(SDL_Vertex){{sx,sy0},{255,255,255,a},{u,0}};
        v[2*i+1]=(SDL_Vertex){{sx,sy1},{255,255,255,a},{u,1}};
    }
    for(int i=0;i<COVER_STRIPS;i++) {
        int tl=2*i,bl=tl+1,tr=tl+2,br=tl+3;
        idx[n++]=tl; idx[n++]=tr; idx[n++]=br; idx[n++]=tl; idx[n++]=br; idx[n++]=bl;
    }
    SDL_RenderGeometry(r,t,v,2*(COVER_STRIPS+1),idx,n);
}
/* See whether balancing retained all words. A longer name gets a full marquee,
 * rather than silently losing the tail to lb_balance's final-line ellipsis. */
/* One part of the line under the name at x, or nothing once a part did not
 * fit (x < 0): returns where the next one starts, or -1 */
#define GAMES_META_RIGHT (ZYPEROS_SCREEN_W-SHELF_MARGIN)
/* A's hint beside the name: its left, 224 in from the canvas's right edge,
 * and its middle 16 under the name's first line's (600) */
#define SHELF_HINT_X  (ZYPEROS_SCREEN_W-224)
#define SHELF_HINT_DY 16
/* The name's room ends 80 short of A's hint (656 from the margin), and the
 * system's line beside the hint 40 short of it */
#define SHELF_TITLE_END 80
#define GAMES_ORIGIN_RIGHT (SHELF_HINT_X-40)
/* The name's lines: the first's middle, and the game title's line, 40 (44
 * until 1.0.6) */
#define GAMES_TITLE_Y    (games_frame().title.y+GAMES_TITLE_STEP/2)   /* 584 */
#define GAMES_TITLE_STEP DS_LINE_META   /* the game title is meta's size: its line is meta's (40) */
/* Where the line under the name sits: a title of one line leaves the second
 * line's place (a title line down) to it, whether it names the system
 * (Favorites) or gives the play time (a shelf). Both read this, so they cannot
 * drift apart. A title of two lines leaves the slot below instead. */
#define GAMES_SUBLINE_Y     (GAMES_TITLE_Y + GAMES_TITLE_STEP)
#define GAMES_SUBLINE_Y_LOW (GAMES_TITLE_Y + 2 * GAMES_TITLE_STEP)
static int meta_part_in(SDL_Renderer *r,TTF_Font *f,const char *s,int x,int y,SDL_Color c)
{
    if(x<0) return -1;
    int w=ui_text_width(f,s);
    if(x+w>GAMES_META_RIGHT) return -1;
    ui_text(r,f,s,x,ui_cap_y(f,y),-1,c);
    return x+w+32;
}
static int meta_part(SDL_Renderer *r,TTF_Font *f,const char *s,int x,int y)
{
    return meta_part_in(r,f,s,x,y,UI_TEXT_DIM);
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
/* One light across a card (src/focusfx.h): a band leaning as the card's name
 * does not, white at its middle and clear at both edges, `t` of the way from
 * off its left to off its right. Clipped to the card's sides; from one of its
 * corners' radii in from its top to one in from its foot, faded in and out
 * over two more, so it ends softly and its round corners are never lit. */
static void sheen(SDL_Renderer *r,SDL_FRect c,float t,Uint8 alpha)
{
    const float lean=.45f, half=c.w*.27f, rad=c.w*DS_RADIUS_CARD/512.f;
    const float cx=c.x-.35f*c.w+t*1.7f*c.w, mid=c.y+c.h*.5f;
    const float ys[4]={c.y+rad,c.y+3*rad,c.y+c.h-3*rad,c.y+c.h-rad}, on[4]={0,1,1,0};
    const Uint8 peak=(Uint8)lroundf(alpha*.30f);
    SDL_Vertex v[12];
    int idx[36],n=0;
    for(int row=0;row<4;row++) {
        float sx=(ys[row]-mid)*lean;
        for(int col=0;col<3;col++) {
            Uint8 al=col==1?(Uint8)lroundf(peak*on[row]):0;
            v[row*3+col]=(SDL_Vertex){{cx+(col-1)*half+sx,ys[row]},{255,255,255,al},{0,0}};
        }
    }
    for(int row=0;row<3;row++) for(int col=0;col<2;col++) {
        int a=row*3+col,b=a+1,c2=a+3,d=a+4;
        idx[n++]=a; idx[n++]=b; idx[n++]=d; idx[n++]=a; idx[n++]=d; idx[n++]=c2;
    }
    SDL_Rect clip={(int)ceilf(c.x),(int)ceilf(c.y),(int)floorf(c.w),(int)floorf(c.h)};
    SDL_RenderSetClipRect(r,&clip);
    SDL_RenderGeometry(r,NULL,v,12,idx,n);
    SDL_RenderSetClipRect(r,NULL);
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
    const shelf_frame sf=games_frame();
    const int sys_cy=sf.sys.y+sf.sys.h/2;
    int sw=ui_text_width(meta,info->system),room=sf.sys.w-countw-32;
    if(sw>room) {
        ui_text_marquee(r,meta,info->system,sf.sys.x,ui_cap_y(meta,sys_cy),room,phase,UI_TEXT_SOFT);
        unsigned next=ui_pingpong_wait(sw-room,phase); if(next<wait) wait=next;
    } else ui_text(r,meta,info->system,sf.sys.x,ui_cap_y(meta,sys_cy),-1,UI_TEXT_SOFT);
    ui_text_tabular(r,chrome,count,sf.sys.x+sf.sys.w,ui_cap_y(chrome,sys_cy),1,UI_TEXT_DIM);

    const ds_focus_marks marks=ds_focus_marks_of(DS_FOCUS_COVER);
    games_slot slots[GAMES_WINDOW]; int n=games_slots(row,now,slots);
    for(int i=0;i<n;i++) {
        /* Asked out to the last seat, where a cover shows nothing: so the
         * one that slides in next is decoded before it is seen */
        if(fabsf(slots[i].offset)>GAMES_SEATS-1) continue;
        int w=0,h=0; float cb=1;
        SDL_Texture *t=texture(ctx,slots[i].item,&w,&h,&cb);
        if(!t || w<=0 || h<=0) continue;
        cover_pose pose=cover_pose_at(w,h,slots[i].offset);
        if(pose.alpha<=0) continue;
        SDL_FRect dst={pose.cx-pose.hw,pose.cy-pose.hh,pose.hw*2,pose.hh*2};
        bool turned=fabsf(pose.ang)>.001f;
        float focus=fmaxf(0,1-fabsf(slots[i].offset)),rise=0;
        /* Highlight's Lift (src/ds/focus.h): the cover coming to the middle
         * rises 12 on a deep lift as it comes, and a snap taking its place
         * with it */
        if(marks.lift==DS_FOCUS_LIFT_HIGH && !turned && focus>.02f) {
            rise=12*focus;
            dst.y-=rise;
            ui_lift_focus(r,&(SDL_Rect){(int)lroundf(dst.x),(int)lroundf(dst.y),(int)lroundf(dst.w),(int)lroundf(dst.h)},
                          true,focus*pose.alpha*slots[i].alpha);
        }
        Uint8 was; SDL_BlendMode texblend;
        SDL_GetTextureAlphaMod(t,&was); SDL_GetTextureBlendMode(t,&texblend);
        SDL_SetTextureBlendMode(t,SDL_BLENDMODE_BLEND);
        Uint8 a=(Uint8)lroundf(was*pose.alpha*slots[i].alpha);
        float k=info->snap && info->snap_k>0 && slots[i].item==info->cursor &&
                fabsf(slots[i].offset)<.001f ? fminf(info->snap_k,1) : 0;
        if(turned) {
            /* The vertices carry its alpha; the texture's own is left out */
            SDL_SetTextureAlphaMod(t,255);
            turned_cover(r,t,&pose,a);
        } else {
            SDL_SetTextureAlphaMod(t,(Uint8)lroundf(a*(1-k)));
            if(k<1) SDL_RenderCopyF(r,t,NULL,&dst);
        }
        SDL_SetTextureAlphaMod(t,was); SDL_SetTextureBlendMode(t,texblend);
        /* The focused card with no art as the row stops on it (src/focusfx.h):
         * its frame of play turning sharp over the blurred one, and the one
         * light across it. Both under a snap that takes its place. */
        bool here=slots[i].item==info->cursor && fabsf(slots[i].offset)<.001f;
        if(here && k<1 && info->sharp && info->sharp_k>0) {
            Uint8 sw; SDL_GetTextureAlphaMod(info->sharp,&sw);
            SDL_SetTextureBlendMode(info->sharp,SDL_BLENDMODE_BLEND);
            SDL_SetTextureAlphaMod(info->sharp,(Uint8)lroundf(a*(1-k)*fminf(info->sharp_k,1)));
            SDL_RenderCopyF(r,info->sharp,NULL,&dst);
            SDL_SetTextureAlphaMod(info->sharp,sw);
        }
        if(here && k<1 && info->sheen>0 && info->sheen<1) sheen(r,dst,info->sheen,(Uint8)lroundf(a*(1-k)));
        if(k>0) {
            /* The cover turns wide (My pick in the Scraper lab): the
             * snap, whole and at its own shape in the cover's box, shown
             * through a frame that grows from the cover's rect to its own,
             * as the cover fades. Never stretched on the way. */
            SDL_FRect vr=games_cover_rect(info->snap_w,info->snap_h,0);
            vr.y-=rise;
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
            if(kind!=AB_NONE && turned) {
                /* On a turned cover at its turned corner, as large as the
                 * cover is there (coverflow.c's draw_badge) */
                float in=(AB_INSET+AB_D/2.0f)*pose.scale,bx,by,bk;
                cover_point(&pose,-pose.hw+in,-pose.hh+in,&bx,&by,&bk);
                ab_draw(r,(ab_kind)kind,bx,by,pose.scale*bk,a);
            } else if(kind!=AB_NONE)
                ab_draw_at(r,(ab_kind)kind,dst.x,dst.y,pose.scale,(Uint8)lroundf(a*(1-k)));
        }
        /* A cover with a snap says so: a play mark in an accent disc at its
         * bottom right, fading with the cover as the snap takes over (
         * 2026-11-06). Two sizes, the focused card's and the others', so the
         * glyph's cache holds both rather than drawing it anew each frame. */
        if(info->snap_of && k<1 && info->snap_of(ctx,slots[i].item)) {
            int d=focus>.5f?DS_BADGE:DS_BADGE_SMALL, inset=focus>.5f?DS_BADGE_INSET:DS_BADGE_INSET_SMALL;
            if(turned) {
                float px,py;
                cover_point(&pose,pose.hw-inset-d/2.0f,pose.hh-inset-d/2.0f,&px,&py,NULL);
                ds_play_disc(r,(int)lroundf(px-d/2.0f),(int)lroundf(py-d/2.0f),d,a);
            } else
                ds_play_disc(r,(int)lroundf(dst.x+dst.w)-inset-d,(int)lroundf(dst.y+dst.h)-inset-d,d,
                             (Uint8)lroundf(a*(1-k)));
        }
        /* The wait for the snap: a bar under the focused cover, the cover's
         * width, filling over hold-snap, held full while the first picture
         * comes, and fading with the cover as it turns (2026-11-06) */
        if(info->snap_wait>0 && slots[i].item==info->cursor && fabsf(slots[i].offset)<.001f && k<1) {
            int bw=(int)lroundf(dst.w),bx=(int)lroundf(dst.x),by=(int)lroundf(dst.y+dst.h)+12;
            float fade=(1-k)*slots[i].alpha;
            ds_bar(r,bx,by,bw,(int)lroundf(bw*fminf(info->snap_wait,1)),(Uint8)lroundf(255*fade));
        }
    }
    /* Highlight's marks round the cover in the middle, holding still as the
     * row slides under them, as Spotlight's do: Border's ring (its corners
     * the cover's own, square), Bar's bar right under the cover; both gone
     * as a snap takes the cover's place. The bar gives way to the snap's
     * wait bar while that fills, in the same place: both mark this cover.
     * It sat under the wait bar's place until 1.1.0, 16 lower, and a name
     * long enough to reach the middle ran into it (Tony Hawk's Pro Skater 2:
     * the bar on its "2") */
    if((marks.ring || marks.bar) && info->count>0) {
        int w=0,h=0; float cb=1;
        SDL_Texture *t=texture(ctx,info->cursor,&w,&h,&cb);
        float k=info->snap && info->snap_k>0 ? fminf(info->snap_k,1) : 0;
        if(t && w>0 && h>0 && k<1) {
            SDL_FRect c=games_cover_rect(w,h,0);
            Uint8 al=(Uint8)lroundf(255*(1-k));
            if(marks.ring) ds_focus_ring(r,c,0,al);
            if(marks.bar && !(info->snap_wait>0)) ds_focus_bar(r,c,al);
        }
    }
    TTF_Font *title=page_font(PAGE_GAME_TITLE);
    const char *name=info->title && info->title[0]?info->title:T(S_GAMES_NO_GAMES);
    /* A favorite's heart leads its name (2026-11-09: on the cover it was
     * hard to see), in **favorite** as every heart is, centered on the
     * first line's capitals; the name moves over for it. Not on Favorites,
     * where every game is one (info->favorite is false there). */
    int tx=sf.title.x,tw=SHELF_HINT_X-SHELF_TITLE_END-sf.title.x;
    if(info->favorite&&info->count) {
        int cap=ui_cap_height(title);
        float hr=cap*.55f;
        draw_heart(r,sf.title.x+hr,GAMES_TITLE_Y,hr,ui_col(theme()->favorite,255));
        tx=sf.title.x+(int)lroundf(hr*2)+16; tw=SHELF_HINT_X-SHELF_TITLE_END-tx;
    }
    char lines[2][LB_LINE];
    int nlines=ui_balance_lines(title,name,tw,2,lines);
    bool whole=complete_title(name,lines,nlines);
    if(whole) {
        for(int i=0;i<nlines;i++) ui_text(r,title,lines[i],tx,ui_cap_y(title,GAMES_TITLE_Y+i*GAMES_TITLE_STEP),-1,UI_TEXT);
    } else {
        ui_text_marquee(r,title,name,tx,ui_cap_y(title,GAMES_TITLE_Y),tw,phase,UI_TEXT);
        unsigned next=ui_pingpong_wait(ui_text_width(title,name)-tw,phase); if(next<wait) wait=next;
    }
    /* The game's own system on Favorites, right under its name (
     * 2026-11-07: "have the system right below the name"): in the second
     * line's place when the name takes one, clear of A's hint; under a name
     * of two lines there is no room above the line below, so it leads that
     * line instead. */
    bool origin=info->origin && *info->origin;
    if(origin && (!whole || nlines<2) && sf.title.x+ui_text_width(chrome,info->origin)<=GAMES_ORIGIN_RIGHT) {
        ui_text(r,chrome,info->origin,sf.title.x,ui_cap_y(chrome,GAMES_SUBLINE_Y),-1,UI_TEXT_DIM);
        origin=false;
    }
    /* The line below: the system when it did not fit above, then Played,
     * then achievements, 32 apart. A part that would pass the right margin
     * is left out with the ones after it, never drawn over the edge. */
    int x=sf.title.x;
    /* The slot beside A's hint ends at GAMES_ORIGIN_RIGHT; a line that would
     * pass it takes the low slot, where the whole width is free. */
    int my=GAMES_SUBLINE_Y_LOW;
    /* A folder's line is its count of games, in the second line's place */
    char games_line[64]="";
    if(info->folder>0) snprintf(games_line,sizeof games_line,TN(S_SHELF_GAMES,info->folder),info->folder);
    if((!whole || nlines<2) && !(info->origin && *info->origin && !origin)) {
        char pt[64]=""; int w=0, parts=0;
        if(games_line[0]) { w+=ui_text_width(chrome,games_line); parts++; }
        if(origin) { w+=ui_text_width(chrome,info->origin); parts++; }
        if(info->install||info->howto) { w+=ui_text_width(chrome,T(S_PORTMASTER_NOT_INSTALLED)); parts++; }
        if(info->needs && *info->needs) { w+=ui_text_width(chrome,info->needs); parts++; }
        if(info->played && *info->played) { snprintf(pt,sizeof pt,T(S_GAMES_PLAYED),info->played); w+=ui_text_width(chrome,pt); parts++; }
        if(info->has_achievements) { char at[64]; snprintf(at,sizeof at,T(S_GAMES_ACHIEVEMENTS),info->earned,info->total); w+=ui_text_width(chrome,at); parts++; }
        if(info->cheats && *info->cheats) { w+=ui_text_width(chrome,info->cheats); parts++; }
        if(parts) w+=(parts-1)*32;
        if(sf.title.x+w<=GAMES_ORIGIN_RIGHT) my=GAMES_SUBLINE_Y;
    }
    if(origin) x=meta_part(r,chrome,info->origin,x,my);
    if(games_line[0]) x=meta_part(r,chrome,games_line,x,my);
    if(info->install||info->howto) x=meta_part(r,chrome,T(S_PORTMASTER_NOT_INSTALLED),x,my);
    /* Firmware it cannot start without, in **warning**: what A would say */
    if(info->needs && *info->needs) x=meta_part_in(r,chrome,info->needs,x,my,ui_col(theme()->warning,255));
    if(info->played && *info->played) {
        char text[64]; snprintf(text,sizeof text,T(S_GAMES_PLAYED),info->played);
        x=meta_part(r,chrome,text,x,my);
    }
    if(info->has_achievements) {
        char text[64]; snprintf(text,sizeof text,T(S_GAMES_ACHIEVEMENTS),info->earned,info->total);
        x=meta_part(r,chrome,text,x,my);
    }
    /* and how many of its cheats are on (DESIGN.md 11.66), last */
    if(info->cheats && *info->cheats) meta_part(r,chrome,info->cheats,x,my);
    if(info->count) page_hint(r,"A",info->a_word?info->a_word:
                              T(info->folder>0||info->opens?S_GAMES_OPEN:info->howto?S_PICO8_HOW_TO_ADD:info->install?S_PORTMASTER_INSTALL:
                                info->resumable?S_GAMES_RESUME:S_GAMES_PLAY),SHELF_HINT_X,GAMES_TITLE_Y+SHELF_HINT_DY,true,false);
    /* The foot, spread from B at the left to X at the right (DESIGN.md
     * 11.4): START turns the shelf into the grid, the next View (11.47) */
    key_hint foot[4]; int nf=0;
    foot[nf++]=(key_hint){"B",T(info->inside?S_GAMES_BACK:S_GAMES_SYSTEMS)};
    if(info->count) foot[nf++]=(key_hint){"START",T(S_GAMES_TO_GRID)};
    if(info->count&&info->resumable&&!info->install&&!info->howto&&!info->opens&&!info->folder) foot[nf++]=(key_hint){"Y",T(S_GAMES_FRESH)};
    if(!info->folder) foot[nf++]=(key_hint){"X",T(S_GAMES_DETAILS)};
    /* DS_FOOT_Y, every screen's: a two-line name's system line (cap 664) stays 30 from the keys */
    keyhints_draw(r,info->foot?info->foot:foot,info->foot?info->nfoot:nf,sf.keys.x,sf.keys.x+sf.keys.w,
                  sf.keys.y+sf.keys.h/2,KEYHINTS_SPREAD,true,255);
    SDL_SetRenderDrawColor(r,red,green,blue,alpha);
    SDL_SetRenderDrawBlendMode(r,blend);
    return wait;
}
