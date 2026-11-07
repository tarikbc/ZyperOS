/* SPDX-License-Identifier: GPL-3.0-only */
#include "home.h"
#include "design.h"
#include "theme.h"
#include "ui.h"
#include "texload.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "systemart_data.h"

static TTF_Font *page_fonts[PAGE_FONTS];
/* The same four in the cuts of Noto Sans SC and JP, for 简体中文 and 日本語
 * (src/ui.h, ui_fonts_for, ui_font_set 1 and 2): each opened the first time
 * its language is on, and kept */
static TTF_Font *page_fonts_cjk[3][PAGE_FONTS];
static bool page_cjk_tried[3];
static void page_fonts_cjk_open(int set)
{
    static const int sizes[] = {DS_TYPE_CHROME,DS_TYPE_ACTION,DS_TYPE_SUPPORT,DS_TYPE_GAME_TITLE};
    char menu[1024], title[1024];
    if(page_cjk_tried[set]) return;
    page_cjk_tried[set]=true;
    ui_cjk_font_path(false,menu,sizeof menu);
    ui_cjk_font_path(true,title,sizeof title);
    for(int i=0;i<PAGE_FONTS;i++) {
        page_fonts_cjk[set][i]=ui_face_open(i==PAGE_GAME_TITLE?title:menu,sizes[i]);
        if(!page_fonts_cjk[set][i]) page_fonts_cjk[set][i]=ui_face_open(menu,sizes[i]);
    }
}
bool page_fonts_init(const char *path)
{
    static const int sizes[] = {DS_TYPE_CHROME,DS_TYPE_ACTION,DS_TYPE_SUPPORT,DS_TYPE_GAME_TITLE};
    char bold[2048];
    snprintf(bold,sizeof bold,"%s",path);
    char *slash=strrchr(bold,'/');
    if(slash) snprintf(slash+1,sizeof bold-(size_t)(slash+1-bold),"title.ttf");
    else snprintf(bold,sizeof bold,"title.ttf");
    for(int i=0;i<PAGE_FONTS;i++) {
        page_fonts[i]=ui_face_open(i==PAGE_GAME_TITLE?bold:path,sizes[i]);
        if(!page_fonts[i]) page_fonts[i]=ui_face_open(path,sizes[i]);
        if(!page_fonts[i]) return false;
    }
    return true;
}
void page_fonts_quit(void)
{
    ui_text_memo_clear();
    for(int i=0;i<PAGE_FONTS;i++) {
        if(page_fonts[i]) { ui_face_close(page_fonts[i]); page_fonts[i]=NULL; }
        for(int k=1;k<3;k++) if(page_fonts_cjk[k][i]) { ui_face_close(page_fonts_cjk[k][i]); page_fonts_cjk[k][i]=NULL; }
    }
    page_cjk_tried[1]=page_cjk_tried[2]=false;
}
TTF_Font *page_font(page_font_role role)
{
    int set=ui_font_set();
    if(set) {
        page_fonts_cjk_open(set);
        if(page_fonts_cjk[set][role]) return page_fonts_cjk[set][role];
    }
    return page_fonts[role] ? page_fonts[role] : ui_font(UI_F_META);
}

home_layout home_parse(const char *id)
{ return id && !strcmp(id, "gallery") ? HOME_GALLERY : HOME_SPOTLIGHT; }
const char *home_id(home_layout l) { return l == HOME_GALLERY ? "gallery" : "spotlight"; }
const char *home_name(home_layout l) { return T(l == HOME_GALLERY ? S_HOME_GALLERY : S_HOME_SPOTLIGHT); }
const char *home_label(const char *key, const char *canonical)
{
    static const struct { const char *key, *name; } labels[] = {
        {"A2600","Atari 2600"}, {"GW","G&W"}, {"A5200","Atari 5200"},
        {"VECTREX","Vectrex"}, {"NES","NES"}, {"MSX2","MSX2"},
        {"A7800","Atari 7800"}, {"SMS","SMS"}, {"GB","Game Boy"},
        {"MD","Genesis"}, {"PCE","TG-16"}, {"LYNX","Lynx"},
        {"GG","Game Gear"}, {"NEOGEO","Neo Geo"}, {"SFC","SNES"},
        {"SEGACD","Sega CD"}, {"3DO","3DO"}, {"32X","32X"},
        {"VB","Virtual Boy"}, {"PS1","PS1"}, {"NEOCD","Neo Geo CD"},
        {"N64","N64"}, {"NGP","NGP"}, {"GBC","GBC"}, {"WS","WS"},
        {"NGPC","NGPC"}, {"DC","Dreamcast"}, {"WSC","WSC"}, {"GBA","GBA"},
        {"PKMINI","PokéMini"}, {"NDS","NDS"}, {"PICO8","PICO-8"},
        {"ARCADE","Arcade"}, {"MAME","MAME"}, {"NAOMI","NAOMI"},
        {"AW","Atomiswave"}, {"PRT","Ports"}, {"MUSE","MP4"}
    };
    /* Favorites is the launcher's own shelf and named in the language that is on */
    if (key && !strcmp(key, "FAV")) return T(S_HOME_FAVORITES);
    for (unsigned i = 0; key && i < sizeof labels / sizeof *labels; i++)
        if (!strcmp(key, labels[i].key)) return labels[i].name;
    /* Unknown long names use their tag at the same readable size. */
    if (canonical && canonical[0] && strlen(canonical) <= 12) return canonical;
    return key && key[0] ? key : T(S_HOME_SYSTEM);
}

float home_position(const home *h, uint32_t now)
{ return h->from + (h->target-h->from)*ds_out((float)(now-h->moved)/DS_MS_MOVE); }
bool home_moving(const home *h, uint32_t now)
{ return h->initialized && h->from != h->target && now-h->moved < DS_MS_MOVE; }
void home_reset(home *h, int cursor, int count, uint32_t now)
{
    h->initialized = true; h->cursor = cursor; h->count = count;
    h->from = h->target = (float)cursor; h->moved = h->animation = now;
    row_reset(&h->motion,cursor,count,now);
}
void home_select(home *h, int cursor, int count, int direction, uint32_t now)
{
    if (!h->initialized || h->count != count) { home_reset(h,cursor,count,now); return; }
    if (cursor == h->cursor || count <= 1) return;
    row_select(&h->motion,cursor,count,now);
    h->from = home_position(h,now);
    float delta = (float)(cursor-h->cursor);
    if (direction > 0 && delta < 0) delta += count;
    else if (direction < 0 && delta > 0) delta -= count;
    else if (!direction) {
        if (delta > count/2.0f) delta -= count;
        if (delta < -count/2.0f) delta += count;
    }
    h->target += delta; h->cursor = cursor; h->moved = h->animation = now;
    /* Bound long-running navigation without changing any rendered distance. */
    if (fabsf(h->target) > 4096) {
        float turns = floorf(h->target/count)*count;
        h->target -= turns; h->from -= turns;
    }
}
int home_jump(int cursor, int count, int dir)
{
    int k = HOME_JUMP < count - 1 ? HOME_JUMP : count - 1;
    if (count <= 0) return 0;
    if (k < 0) k = 0;
    return ((cursor + (dir < 0 ? -k : k)) % count + count) % count;
}
unsigned char home_group_key(const char *tag, const char *core, const char *name)
{
    if (!strcmp(tag, "FAV")) return 1;
    if (!strcmp(core, "port")) return 2;
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 'a' && c <= 'z') return (unsigned char)(c - 32);
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
    }
    return '#';
}
int home_letter_jump(const unsigned char *keys, int count, int cursor, int dir)
{
    /* A group is a run of equal keys, so the shelf's order is all it needs:
     * Favorites and Ports have keys of their own and are runs of one. */
    if (count <= 1 || cursor < 0 || cursor >= count) return cursor < 0 ? 0 : cursor;
    unsigned char key = keys[cursor];
    int i;
    if (dir > 0) {
        for (i = 1; i < count; i++) {
            int k = (cursor + i) % count;
            if (keys[k] != key) return k;
        }
        return cursor;                          /* one group, the whole row */
    }
    /* A group is a run in the row's order, so its first system is found
     * walking back to the row's start and never past it */
    int start = cursor;
    while (start > 0 && keys[start - 1] == key) start--;
    if (start != cursor) return start;          /* up goes to its own first */
    int p = (cursor - 1 + count) % count;
    if (keys[p] == key) return cursor;          /* one group, the whole row */
    start = p;
    while (start > 0 && keys[start - 1] == keys[p]) start--;
    return start;
}
bool home_song_update(home_song *song, bool active, bool paused, const char *title)
{
    home_song next = { .active = active, .paused = active && paused };
    if (active) snprintf(next.title,sizeof next.title,"%s",title && title[0] ? title : T(S_HOME_AUDIO));
    bool changed = song->active != next.active || song->paused != next.paused ||
                   strcmp(song->title,next.title);
    *song = next;
    return changed;
}

static int asset_for(const char *key)
{
    /* Asset keys use the existing art basename; identity stays the ROM tag. */
    if (key && !strcmp(key,"SFC")) key="SNES";
    else if (key && !strcmp(key,"MD")) key="GENESIS";
    else if (key && !strcmp(key,"PRT")) key="PORTS";
    else if (key && !strcmp(key,"FAV")) key="FAVORITES";
    for (int i=0; i<(int)SA_ASSET_COUNT; i++)
        if (key && !strcmp(key,SA_ASSETS[i].key)) return i;
    return -1;
}
const char *home_art_key(const char *tag)
{
    int i = asset_for(tag);

    return i >= 0 ? SA_ASSETS[i].key : NULL;
}

static void release(home_texture *t)
{ if(t->tex) SDL_DestroyTexture(t->tex); *t = (home_texture){ .asset = -1 }; }
void home_free(home *h)
{
    release(&h->atlas);
    for(int i=0;i<HOME_NEAR;i++) release(&h->near[i]);
    for(int i=0;i<HOME_STILLS;i++) release(&h->still[i]);
    h->renderer = NULL;
}
static bool valid(int i)
{ return i>=0 && i<(int)SA_ASSET_COUNT; }
/* An atlas's opaque texels, all its cells together, in a cell's coordinates */
static SDL_Rect cells_content(SDL_Surface *s, int cell)
{
    SDL_Surface *c = s->format->format==SDL_PIXELFORMAT_ARGB8888 ? s
                   : SDL_ConvertSurfaceFormat(s,SDL_PIXELFORMAT_ARGB8888,0);
    int x0=cell, y0=cell, x1=-1, y1=-1;
    if (!c) return (SDL_Rect){0};
    if (SDL_LockSurface(c)==0) {
        for (int y=0, cy=0; y<c->h; y++, cy = cy+1==cell ? 0 : cy+1) {
            const Uint32 *row=(const Uint32 *)((const Uint8 *)c->pixels + (size_t)y*c->pitch);
            for (int x=0, cx=0; x<c->w; x++, cx = cx+1==cell ? 0 : cx+1) {
                if (!(row[x]>>24)) continue;
                if (cx<x0) x0=cx;
                if (cx>x1) x1=cx;
                if (cy<y0) y0=cy;
                if (cy>y1) y1=cy;
            }
        }
        SDL_UnlockSurface(c);
    }
    if (c!=s) SDL_FreeSurface(c);
    return x1<0 ? (SDL_Rect){0} : (SDL_Rect){x0,y0,x1-x0+1,y1-y0+1};
}
static bool install(home_texture *t, SDL_Renderer *r, SDL_Surface *s, bool atlas)
{
    const sa_asset *a = valid(t->asset) ? &SA_ASSETS[t->asset] : NULL;
    t->done = true;
    if (!s || !a || !a->cell || !a->cols || !a->rows) return false;
    int w = atlas ? a->cols*a->cell : a->cell;
    int ht = atlas ? a->rows*a->cell : a->cell;
    if (s->w != w || s->h != ht) return false;
    if (atlas) t->content = cells_content(s,a->cell);
    t->tex = SDL_CreateTextureFromSurface(r,s);
    if (!t->tex) return false;
    SDL_SetTextureBlendMode(t->tex,SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t->tex,SDL_ScaleModeNearest);
    return true;
}
bool home_accept(home *h, SDL_Renderer *r, int shelf, int asset, SDL_Surface *s)
{
    if (shelf != HOME_LOADER_ATLAS && shelf != HOME_LOADER_STILL) return false;
    if (shelf == HOME_LOADER_ATLAS) {
        if (h->atlas.asset == asset && !h->atlas.done) install(&h->atlas,r,s,true);
        for(int i=0;i<HOME_NEAR;i++)
            if (h->near[i].asset == asset && !h->near[i].done) install(&h->near[i],r,s,true);
    } else for(int i=0;i<HOME_STILLS;i++)
        if(h->still[i].asset == asset && !h->still[i].done) install(&h->still[i],r,s,false);
    return true;
}
static void request(home_texture *t, SDL_Renderer *r, const char *root, bool atlas)
{
    if(t->done || !valid(t->asset)) return;
    const sa_asset *a = &SA_ASSETS[t->asset];
    const char *relative = atlas ? a->atlas : a->still;
    if (!relative || !*relative) { t->done = true; return; }
    char path[2048]; snprintf(path,sizeof path,"%s/%s",root,relative);
    if(texload_want(atlas ? HOME_LOADER_ATLAS : HOME_LOADER_STILL,t->asset,path,NULL)) return;
    /* --shot has no workers. Also log the exceptional worker-start failure path. */
    uint64_t start = SDL_GetPerformanceCounter();
    SDL_Surface *s = IMG_Load(path);
    install(t,r,s,atlas);
    if(s) SDL_FreeSurface(s);
    fprintf(stderr,"home: cold %s %.2f ms\n",relative,
            (SDL_GetPerformanceCounter()-start)*1000.0/SDL_GetPerformanceFrequency());
}
/* The atlas slot that should hold `asset`, taken from whichever slot holds
 * it now (a neighbor becoming the focus keeps its decoded atlas), else an
 * empty one. */
static home_texture take_atlas(home_texture *pool, int n, int asset)
{
    for(int i=0;i<n;i++) if(pool[i].asset==asset && valid(asset)) {
        home_texture t=pool[i]; pool[i]=(home_texture){ .asset=-1 }; return t;
    }
    return (home_texture){ .asset=asset };
}
void home_prepare(home *h, SDL_Renderer *r, const char *root,
                  const home_item *items, int count, int cursor, uint32_t now)
{
    if (!r || count<=0 || cursor<0 || cursor>=count) return;
    if(h->renderer != r) { home_free(h); h->renderer = r; }
    home_select(h,cursor,count,0,now);
    /* Every system's still, nearest the cursor first. Unique: a one or two
     * item library, or Favorites beside its system, shares an asset. */
    int wanted[HOME_STILLS], n=0;
    for(int k=0;k<count && n<HOME_STILLS;k++) {
        int step=(k+1)/2*(k%2?1:-1), ix=((cursor+step)%count+count)%count;
        int id=asset_for(items[ix].key);
        if(!valid(id)) continue;
        bool found=false;
        for(int j=0;j<n;j++) if(wanted[j]==id) found=true;
        if(!found) wanted[n++]=id;
    }
    for(int j=0;j<HOME_STILLS;j++) {
        bool keep=false;
        for(int k=0;k<n;k++) if(h->still[j].asset == wanted[k]) keep=true;
        if(!keep) release(&h->still[j]);
    }
    /* At most eight asks a pass, the nearest of those not yet in: the
     * loader's queue (24) is shared with the game covers and drops its
     * oldest when full. The home asks again every frame it draws, so the
     * rest follow within a few frames of the launcher starting. */
    home_texture *ask[8]; int asks=0;
    for(int k=0;k<n;k++) {
        home_texture *slot=NULL;
        for(int j=0;j<HOME_STILLS;j++) if(h->still[j].asset==wanted[k]) slot=&h->still[j];
        if(!slot) for(int j=0;j<HOME_STILLS;j++) if(h->still[j].asset<0) {
            slot=&h->still[j]; slot->asset=wanted[k]; break;
        }
        if(slot && !slot->done && asks<8) ask[asks++]=slot;
    }
    /* Newest request is decoded first: the farthest of these first. */
    for(int k=asks-1;k>=0;k--) request(ask[k],r,root,false);
    /* Atlases: the focus and its two neighbors, so a step lands on one
     * already decoded. A slot keeps its texture when the focus moves. */
    int focus=asset_for(items[cursor].key);
    int side[HOME_NEAR]={ asset_for(items[(cursor+count-1)%count].key),
                          asset_for(items[(cursor+1)%count].key) };
    home_texture pool[1+HOME_NEAR]={ h->atlas, h->near[0], h->near[1] };
    h->atlas=take_atlas(pool,1+HOME_NEAR,focus);
    for(int i=0;i<HOME_NEAR;i++)
        h->near[i]= side[i]==focus || (i && side[i]==side[0])
                  ? (home_texture){ .asset=-1 } : take_atlas(pool,1+HOME_NEAR,side[i]);
    for(int i=0;i<1+HOME_NEAR;i++) release(&pool[i]);
    for(int i=0;i<HOME_NEAR;i++) request(&h->near[i],r,root,true);
    /* Keep the active atlas last, so it is decoded first. */
    request(&h->atlas,r,root,true);
}
static SDL_Texture *still(const home *h, int asset)
{ for(int i=0;i<HOME_STILLS;i++) if(h->still[i].asset==asset) return h->still[i].tex; return NULL; }
static unsigned frame_at(const sa_asset *a, uint32_t elapsed, uint32_t *wait)
{
    *wait=UINT32_MAX;
    if(!a->frames || !a->nframes) return 0;
    if(a->nframes==1 || !a->duration_ms) return a->frames[0].tile;
    uint32_t phase=elapsed%a->duration_ms;
    for(unsigned i=0;i<a->nframes;i++) {
        if(phase<a->frames[i].ms) { *wait=a->frames[i].ms-phase; return a->frames[i].tile; }
        phase-=a->frames[i].ms;
    }
    return a->frames[0].tile;
}
static void sprite(home *h, SDL_Renderer *r, const home_item *item,
                   SDL_FRect dst, bool active, uint32_t phase, uint32_t *wait, float opacity)
{
    int id=asset_for(item->key);
    SDL_Texture *t=still(h,id);
    SDL_Rect src={0};
    if(valid(id)) {
        const sa_asset *a=&SA_ASSETS[id]; src.w=src.h=a->cell;
        if(active && h->atlas.asset==id && h->atlas.tex) {
            uint32_t w; unsigned tile=frame_at(a,phase,&w);
            if(tile<(unsigned)a->cols*a->rows) {
                t=h->atlas.tex; src.x=(tile%a->cols)*a->cell; src.y=(tile/a->cols)*a->cell;
                if(w<*wait) *wait=w;
            }
        }
    }
    if(t) {
        Uint8 alpha; SDL_GetTextureAlphaMod(t,&alpha);
        SDL_SetTextureAlphaMod(t,(Uint8)lroundf(alpha*opacity));
        SDL_RenderCopyF(r,t,&src,&dst);
        SDL_SetTextureAlphaMod(t,alpha);
    }
    else {
        /* An explicit absence, never a drawing of the wrong machine. */
        int cx=(int)(dst.x+dst.w/2), cy=(int)(dst.y+dst.h/2);
        SDL_Color c=UI_TEXT_DIM; c.a=(Uint8)(255*opacity);
        SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
        SDL_Rect dash={cx-12,cy-2,24,4}; SDL_RenderFillRect(r,&dash);
        if(dst.w>=256) ui_text(r,ui_font(UI_F_META),T(S_HOME_NO_ARTWORK),cx,ui_cap_y(ui_font(UI_F_META),cy+48),0,c);
    }
}
void page_hint(SDL_Renderer *r, const char *key, const char *label,
               int x, int cy, bool focus, bool favorite)
{
    int size=focus?40:32, w=ui_button_width(key,size);
    TTF_Font *font=page_font(focus?PAGE_ACTION:PAGE_CHROME);
    ui_button(r,key,x+w/2,cy,size,focus?UI_BUTTON_ON_ACCENT:UI_BUTTON_PLAIN);
    ui_text(r,font,label,x+w+12,ui_cap_y(font,cy),-1,
            favorite?ui_col(theme()->favorite,255):UI_TEXT_SOFT);
}
/* These draws alter only the draw color/blend. Targets, scale, viewport and
 * clipping stay untouched, including when a menu draws the home into a target. */
typedef struct { Uint8 r,g,b,a; SDL_BlendMode blend; } draw_state;
static draw_state save_state(SDL_Renderer *r)
{ draw_state s; SDL_GetRenderDrawColor(r,&s.r,&s.g,&s.b,&s.a); SDL_GetRenderDrawBlendMode(r,&s.blend); return s; }
static void restore_state(SDL_Renderer *r,draw_state s)
{ SDL_SetRenderDrawColor(r,s.r,s.g,s.b,s.a); SDL_SetRenderDrawBlendMode(r,s.blend); }
/* Spotlight's focused sprite, the one place on its home that changes by
 * itself: the atlas frames of the system's machine (home_draw_focus) */
static const SDL_FRect FOCUS={536,112,448,448};

/* The row's sprites as home_draw draws them, in its order: the others, then
 * the cursor's over them */
typedef struct { int item; SDL_FRect dst; bool active; float alpha; } row_draw;

static int row_draws(const home *h,bool gallery,int count,int cursor,uint32_t now,row_draw out[ROW_SLOTS])
{
    row_slot slots[ROW_SLOTS]; int ns=row_sample(&h->motion,now,slots), n=0;
    /* Inactive wrap sprites leave the screen before changing sides. */
    for(int pass=0;pass<2;pass++) for(int k=0;k<ns;k++) {
        int i=slots[k].item;
        if((i==cursor)!=(pass==1) || i<0 || i>=count) continue;
        float d=slots[k].offset;
        if(fabsf(d)>4.5f) continue;
        float focus=fmaxf(0,1-fabsf(d));
        float size=gallery ? 208+176*focus : 112+16*focus;
        float x=512+d*(gallery?328:152);
        /* Gallery's focused sprite is 384 (3x its 128 cell) at 120..504: its
         * underline sits 8 under it and the title 22 under that (review P2). */
        float y=gallery?312:600-8*focus;
        if(x+size/2<0 || x-size/2>1024) continue;
        out[n++]=(row_draw){ i, {roundf(x-size/2),roundf(y-size/2),size,size},
                             gallery && i==cursor, slots[k].alpha };
    }
    return n;
}

/* Whole pixels a sprite at `f` can touch */
static SDL_Rect cover(SDL_FRect f)
{
    int x0=(int)floorf(f.x), y0=(int)floorf(f.y);
    return (SDL_Rect){x0,y0,(int)ceilf(f.x+f.w)-x0,(int)ceilf(f.y+f.h)-y0};
}

static SDL_Rect content_rect(const home *h)
{
    const sa_asset *a = valid(h->atlas.asset) ? &SA_ASSETS[h->atlas.asset] : NULL;
    const SDL_Rect c = h->atlas.content;
    float k;
    int m, x0, y0, x1, y1;

    if (!a || !a->cell || !h->atlas.tex || c.w<=0 || c.h<=0) return (SDL_Rect){0};
    /* The texels' screen pixels, nearest-scaled (install), and one texel
     * more each way for any renderer's rounding of where a texel lands */
    k = FOCUS.w/a->cell;
    m = (int)ceilf(k);
    x0 = (int)floorf(FOCUS.x + c.x*k) - m;
    y0 = (int)floorf(FOCUS.y + c.y*k) - m;
    x1 = (int)ceilf(FOCUS.x + (c.x+c.w)*k) + m;
    y1 = (int)ceilf(FOCUS.y + (c.y+c.h)*k) + m;
    if (x0<(int)FOCUS.x) x0=(int)FOCUS.x;
    if (y0<(int)FOCUS.y) y0=(int)FOCUS.y;
    if (x1>(int)(FOCUS.x+FOCUS.w)) x1=(int)(FOCUS.x+FOCUS.w);
    if (y1>(int)(FOCUS.y+FOCUS.h)) y1=(int)(FOCUS.y+FOCUS.h);
    return (SDL_Rect){x0,y0,x1-x0,y1-y0};
}

/* The row's sprites over the content: true for each in `hit`. False when one
 * of those overlaps another of the row, which would have to be drawn again
 * too (only while the row moves, which is no focus frame's). */
static bool row_over(const row_draw *row,int nr,SDL_Rect content,bool hit[ROW_SLOTS])
{
    for(int k=0;k<nr;k++) {
        SDL_Rect c=cover(row[k].dst);
        hit[k]=SDL_HasIntersection(&c,&content);
    }
    for(int k=0;k<nr;k++) for(int j=0;j<nr;j++) {
        SDL_Rect a=cover(row[k].dst), b=cover(row[j].dst);
        if(hit[k] && j!=k && SDL_HasIntersection(&a,&b)) return false;
    }
    return true;
}

bool home_focus_alone(const home *h,home_layout layout,int count,int cursor,uint32_t now)
{
    row_draw row[ROW_SLOTS];
    bool hit[ROW_SLOTS];
    SDL_Rect c;

    if (layout==HOME_GALLERY || count<=0 || cursor<0 || cursor>=count ||
        row_moving(&h->motion,now))
        return false;
    c=content_rect(h);
    return c.w>0 && c.h>0 && row_over(row,row_draws(h,false,count,cursor,now,row),c,hit);
}

int home_focus_ground(const home *h,int count,int cursor,uint32_t now,SDL_Rect out[HOME_FOCUS_GROUNDS])
{
    row_draw row[ROW_SLOTS];
    bool hit[ROW_SLOTS];
    SDL_Rect c=content_rect(h);
    int nr=row_draws(h,false,count,cursor,now,row), n=0;

    if (c.w<=0 || c.h<=0 || !row_over(row,nr,c,hit)) return 0;
    out[n++]=c;
    for(int k=0;k<nr && n<HOME_FOCUS_GROUNDS;k++) if(hit[k]) out[n++]=cover(row[k].dst);
    return n;
}

uint32_t home_draw_focus(home *h,SDL_Renderer *r,const home_item *items,int count,int cursor,
                         uint32_t now,bool animate)
{
    draw_state saved=save_state(r);
    uint32_t wait=UINT32_MAX, phase=animate?now-h->animation:0, row_wait=UINT32_MAX;
    row_draw row[ROW_SLOTS];
    bool hit[ROW_SLOTS];
    int nr=row_draws(h,false,count,cursor,now,row);

    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    sprite(h,r,&items[cursor],FOCUS,true,phase,&wait,1);
    /* Then the row's sprites the content reaches, over it, in home_draw's order */
    if (row_over(row,nr,content_rect(h),hit))
        for(int k=0;k<nr;k++) if(hit[k])
            sprite(h,r,&items[row[k].item],row[k].dst,row[k].active,phase,&row_wait,row[k].alpha);
    if(!animate) wait=UINT32_MAX;
    restore_state(r,saved);
    return wait;
}

uint32_t home_draw(home *h,SDL_Renderer *r,home_layout layout,
                    const home_item *items,int count,int cursor,
                    const char *count_label,const char *open_label,uint32_t now,bool animate)
{
    if(count<=0 || cursor<0 || cursor>=count) return UINT32_MAX;
    draw_state saved=save_state(r);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    home_select(h,cursor,count,0,now);
    uint32_t wait=UINT32_MAX, phase=animate?now-h->animation:0;
    
    bool gallery=layout==HOME_GALLERY;
    if(!gallery) sprite(h,r,&items[cursor],FOCUS,true,phase,&wait,1);
    row_draw row[ROW_SLOTS]; int nr=row_draws(h,gallery,count,cursor,now,row);
    for(int k=0;k<nr;k++)
        sprite(h,r,&items[row[k].item],row[k].dst,row[k].active,phase,&wait,row[k].alpha);
    const char *label=home_label(items[cursor].key,items[cursor].name);
    if(gallery && items[cursor].name && items[cursor].name[0] &&
       ui_text_width(ui_font(UI_F_TITLE),items[cursor].name)<=880) label=items[cursor].name;
    int title_x=gallery?512:76, title_y=gallery?562:272;
    /* Unknown tags are fitted only as a last resort; configured labels use title size. */
    char fitted[128]; ui_fit_text(ui_font(UI_F_TITLE),label,fitted,sizeof fitted,gallery?880:440);
    ui_text(r,ui_font(UI_F_TITLE),fitted,title_x,ui_cap_y(ui_font(UI_F_TITLE),title_y),gallery?0:-1,UI_TEXT);
    ui_text(r,page_font(PAGE_META),count_label?count_label:"",title_x,
            ui_cap_y(page_font(PAGE_META),title_y+64),gallery?0:-1,UI_TEXT_DIM);
    ds_rgb accent=theme()->accent_ink;
    SDL_SetRenderDrawColor(r,accent.r,accent.g,accent.b,255);
    SDL_Rect underline={492,gallery?512:672,40,DS_BAR}; SDL_RenderFillRect(r,&underline);
    char index[32]; snprintf(index,sizeof index,"%d / %d",cursor+1,count);
    ui_text(r,page_font(PAGE_CHROME),index,512,ui_cap_y(page_font(PAGE_CHROME),704),0,UI_TEXT_DIM);
    page_hint(r,"MENU",T(S_HOME_SETTINGS),64,704,false,false);
    if(!gallery) page_hint(r,"SELECT","MP4",784,704,false,false);
    page_hint(r,"A",open_label?open_label:T(S_HOME_OPEN_GAMES),gallery?768:76,gallery?704:416,true,false);
    if(animate && row_moving(&h->motion,now)) wait=0;
    if(!animate) wait=UINT32_MAX;
    restore_state(r,saved);
    return wait;
}

/* A filled disc, for the music mark's note heads. */
static void disc(SDL_Renderer *r,float x,float y,float radius,SDL_Color c)
{
    SDL_Vertex v[36]; const int n=12;
    for(int k=0;k<n;k++) {
        float a=k*6.2831853f/n, b=(k+1)*6.2831853f/n;
        v[k*3]=(SDL_Vertex){{x,y},c,{0,0}};
        v[k*3+1]=(SDL_Vertex){{x+cosf(a)*radius,y+sinf(a)*radius},c,{0,0}};
        v[k*3+2]=(SDL_Vertex){{x+cosf(b)*radius,y+sinf(b)*radius},c,{0,0}};
    }
    SDL_RenderGeometry(r,NULL,v,n*3,NULL,0);
}
void home_header(SDL_Renderer *r,const home_song *song,int status_left)
{
    draw_state saved=save_state(r);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    /* The clock stands at the left end now (draw_corner_at), not the
     * wordmark; the song keeps the middle, clear of both. */
    if(song && song->active) {
        const int left=272, right=status_left-24, mark=24, gap=12;
        if(right-left>=mark+gap+32) {
            char fitted[128]; TTF_Font *font=page_font(PAGE_CHROME);
            int maxw=right-left-mark-gap; if(maxw>256) maxw=256;
            ui_fit_text(font,song->title,fitted,sizeof fitted,maxw);
            int width=ui_text_width(font,fitted), x=left+(right-left-width-mark-gap)/2;
            SDL_Color c=ui_col(theme()->accent_ink,255);
            SDL_SetRenderDrawColor(r,c.r,c.g,c.b,c.a);
            if(song->paused) {
                SDL_Rect bars[]={{x,52,6,24},{x+13,52,6,24}};
                SDL_RenderFillRects(r,bars,2);
            } else {
                SDL_Rect stems[]={{x+8,52,4,20},{x+8,52,16,4},{x+20,52,4,16}};
                SDL_RenderFillRects(r,stems,3);
                disc(r,x+5,72,6,c); disc(r,x+17,68,6,c);
            }
            ui_text(r,font,fitted,x+mark+gap,ui_cap_y(font,64),-1,UI_TEXT_SOFT);
        }
    }
    restore_state(r,saved);
}
