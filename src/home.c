/* SPDX-License-Identifier: GPL-3.0-only */
#include "home.h"
#include "blur.h"
#include "design.h"
#include "ds/focus.h"
#include "focusgeo.h"
#include "look.h"
#include "ds/layout.h"   /* DS_HINT_*: a hint's glyph and the gap to its word */
#include "platform.h"    /* ZYPEROS_SCREEN_W: the stage's middle and right */
#include "theme.h"
#include "ui.h"
#include "texload.h"
#include "topbar.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "systemart_data.h"

/* Stills asked for a pass: eight, as the loader's queue is shared with the
 * covers; a shot, which has no loader, takes every one at once, for the
 * Grid home shows eighteen (main.c) */
int home_asks = 8;
static TTF_Font *page_fonts[PAGE_FONTS];
/* The same four in the cuts of Noto Sans SC and JP, for 简体中文 and 日本語
 * (src/ui.h, ui_fonts_for, ui_font_set 1 and 2): each opened the first time
 * its language is on, and kept */
static TTF_Font *page_fonts_cjk[3][PAGE_FONTS];
static bool page_cjk_tried[3];
static void page_fonts_cjk_open(int set)
{
    static const int sizes[] = {DS_TYPE_CHROME,DS_TYPE_SUPPORT,DS_TYPE_SUPPORT,DS_TYPE_GAME_TITLE};
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
    static const int sizes[] = {DS_TYPE_CHROME,DS_TYPE_SUPPORT,DS_TYPE_SUPPORT,DS_TYPE_GAME_TITLE};
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
{
    if (id && !strcmp(id, "gallery")) return HOME_GALLERY;
    if (id && !strcmp(id, "grid")) return HOME_GRID;
    return HOME_SPOTLIGHT;
}
const char *home_id(home_layout l)
{ return l == HOME_GALLERY ? "gallery" : l == HOME_GRID ? "grid" : "spotlight"; }
const char *home_name(home_layout l)
{ return T(l == HOME_GALLERY ? S_HOME_GALLERY : l == HOME_GRID ? S_HOME_GRID : S_HOME_SPOTLIGHT); }
const char *home_label(const char *key, const char *canonical)
{
    static const struct { const char *key, *name; } labels[] = {
        {"A2600","Atari 2600"}, {"GW","G&W"}, {"A5200","Atari 5200"},
        {"VECTREX","Vectrex"}, {"SG1000","SG-1000"}, {"NES","NES"}, {"MSX2","MSX2"},
        {"A7800","Atari 7800"}, {"SMS","SMS"}, {"GB","Game Boy"},
        {"MD","Genesis"}, {"PCE","TG-16"}, {"LYNX","Lynx"},
        {"GG","Game Gear"}, {"NEOGEO","Neo Geo"}, {"SFC","SNES"},
        {"SEGACD","Sega CD"}, {"3DO","3DO"}, {"32X","32X"},
        {"VB","Virtual Boy"}, {"PS1","PS1"}, {"NEOCD","Neo Geo CD"},
        {"N64","N64"}, {"NGP","NGP"}, {"GBC","GBC"}, {"WS","WS"},
        {"NGPC","NGPC"}, {"DC","Dreamcast"}, {"WSC","WSC"}, {"GBA","GBA"},
        {"PKMINI","PokéMini"}, {"NDS","NDS"}, {"PSP","PSP"}, {"PICO8","PICO-8"},
        {"TIC80","TIC-80"}, {"COLECO","Coleco"}, {"DOOM","Doom"},
        {"ARCADE","Arcade"}, {"MAME","MAME"}, {"NAOMI","NAOMI"},
        {"AW","Atomiswave"}, {"PRT","Ports"}, {"MUSE","MP4"}
    };
    /* Favorites is the launcher's own shelf and named in the language that is on */
    if (key && !strcmp(key, "FAV")) return T(S_HOME_FAVORITES);
    if (key && !strcmp(key, "RECENT")) return T(S_HOME_RECENTS);
    if (key && !strcmp(key, "COLL")) return T(S_HOME_COLLECTIONS);
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
    h->grid_cols = 0;              /* the grid placed again on its next frame */
    h->grid_ax = -1;
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
    if (!strcmp(tag, "RECENT")) return 3;
    if (!strcmp(tag, "COLL")) return 4;
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
    else if (key && !strcmp(key,"RECENT")) key="RECENTS";
    else if (key && !strcmp(key,"COLL")) key="COLLECTIONS";
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
{
    if(t->tex) SDL_DestroyTexture(t->tex);
    if(t->shade) SDL_DestroyTexture(t->shade);
    if(t->motion) SDL_FreeSurface(t->motion);
    *t = (home_texture){ .asset = -1 };
}
void home_free(home *h)
{
    release(&h->atlas);
    for(int i=0;i<HOME_NEAR;i++) release(&h->near[i]);
    for(int i=0;i<HOME_STILLS;i++) release(&h->still[i]);
    h->renderer = NULL;
}
static bool valid(int i)
{ return i>=0 && i<(int)SA_ASSET_COUNT; }
/* A still's own soft shadow (Highlight's lift on the home, DESIGN.md 11.1):
 * its opaque texels at half size, padded by the blur's reach and blurred
 * three passes (src/blur.h), white with that alpha, to be tinted where it
 * is drawn. Made once as the still arrives: 82 x 82 texels, a few
 * thousand sums, and nothing per frame. */
#define SHADE_RAD 3
#define SHADE_PAD (3 * SHADE_RAD)
static SDL_Texture *silhouette(SDL_Renderer *r, SDL_Surface *s)
{
    SDL_Surface *c = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0), *o = NULL;
    SDL_Texture *t = NULL;
    int w = s->w / 2, hh = s->h / 2, W = w + 2 * SHADE_PAD, H = hh + 2 * SHADE_PAD;
    float *a = c ? calloc((size_t)W * H, sizeof *a) : NULL, *line = a ? malloc(sizeof *line * (size_t)(W > H ? W : H)) : NULL;

    if (!c || !a || !line) goto done;
    for (int y = 0; y < hh; y++)
        for (int x = 0; x < w; x++) {
            const Uint32 *p0 = (const Uint32 *)((const Uint8 *)c->pixels + (size_t)(2 * y) * c->pitch) + 2 * x;
            const Uint32 *p1 = (const Uint32 *)((const Uint8 *)c->pixels + (size_t)(2 * y + 1) * c->pitch) + 2 * x;

            a[(y + SHADE_PAD) * W + x + SHADE_PAD] = ((p0[0] >> 24) + (p0[1] >> 24) + (p1[0] >> 24) + (p1[1] >> 24)) / 1020.0f;
        }
    blur_box3(a, W, H, SHADE_RAD, line);
    o = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!o) goto done;
    for (int y = 0; y < H; y++) {
        Uint32 *row = (Uint32 *)((Uint8 *)o->pixels + (size_t)y * o->pitch);

        for (int x = 0; x < W; x++) {
            float v = a[y * W + x];

            row[x] = (Uint32)lroundf((v < 0 ? 0 : v > 1 ? 1 : v) * 255) << 24 | 0xFFFFFFu;
        }
    }
    t = SDL_CreateTextureFromSurface(r, o);
    if (t) {
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
    }
done:
    if (o) SDL_FreeSurface(o);
    if (c) SDL_FreeSurface(c);
    free(a);
    free(line);
    return t;
}
/* Every pose's opaque texels together, in the cell's coordinates: the first
 * pose's (the top cell of `c`, ARGB8888) and the region where the others
 * differ from it */
static SDL_Rect poses_content(SDL_Surface *c, const sa_asset *a)
{
    int cell=a->cell, x0=cell, y0=cell, x1=-1, y1=-1;
    for (int y=0; y<cell; y++) {
        const Uint32 *row=(const Uint32 *)((const Uint8 *)c->pixels + (size_t)y*c->pitch);
        for (int x=0; x<cell; x++) {
            if (!(row[x]>>24)) continue;
            if (x<x0) x0=x;
            if (x>x1) x1=x;
            if (y<y0) y0=y;
            if (y>y1) y1=y;
        }
    }
    SDL_Rect base = x1<0 ? (SDL_Rect){0} : (SDL_Rect){x0,y0,x1-x0+1,y1-y0+1};
    SDL_Rect moving = { a->rx, a->ry, a->rw, a->rh }, both;
    if (base.w<=0) return moving;
    SDL_UnionRect(&base,&moving,&both);
    return both;
}
/* An atlas is the first pose, the cell square, with each pose's moving
 * region packed under it (src/systemart_data.h); a design that does not
 * move is the first pose alone */
static bool atlas_size(const sa_asset *a, int *w, int *h)
{
    *w = *h = a->cell;
    if (!a->rw) return a->nframes==1;
    if (!a->rh || !a->cols || !a->rows) return false;
    if (a->cols*a->rw > *w) *w = a->cols*a->rw;
    *h += a->rows*a->rh;
    return true;
}
static bool install(home_texture *t, SDL_Renderer *r, SDL_Surface **sp, bool atlas)
{
    SDL_Surface *s = *sp;
    const sa_asset *a = valid(t->asset) ? &SA_ASSETS[t->asset] : NULL;
    int w, ht;
    t->done = true;
    if (!s || !a || !a->cell) return false;
    if (!atlas) {
        if (s->w != (int)SA_STILL || s->h != (int)SA_STILL) return false;
        t->tex = SDL_CreateTextureFromSurface(r,s);
        if (t->tex) t->shade = silhouette(r, s);
    } else {
        if (!atlas_size(a,&w,&ht) || s->w != w || s->h != ht) return false;
        /* The regions are kept in the texture's format, so a pose is one
         * SDL_UpdateTexture. The workers decode to it (src/texload.c), and
         * such a surface is taken as it is, *sp left NULL for the caller:
         * an atlas is up to a megapixel, and copying it on the frame it
         * landed on was a hitch of its own (2026-11-19). Another format (a
         * shot's own decode) is converted, as before. */
        SDL_Surface *c = s->format->format == SDL_PIXELFORMAT_ARGB8888 ? s
                       : SDL_ConvertSurfaceFormat(s,SDL_PIXELFORMAT_ARGB8888,0);
        if (!c) return false;
        if (c == s) *sp = NULL;
        t->tex = SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,a->cell,a->cell);
        if (!t->tex || SDL_UpdateTexture(t->tex,&(SDL_Rect){0,0,a->cell,a->cell},c->pixels,c->pitch)) {
            if (t->tex) SDL_DestroyTexture(t->tex);
            t->tex = NULL;
            SDL_FreeSurface(c);
            return false;
        }
        t->shown = 0;
        if (a->rw) {
            t->content = poses_content(c,a);
            t->motion = c;
        } else SDL_FreeSurface(c);
    }
    if (!t->tex) return false;
    SDL_SetTextureBlendMode(t->tex,SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t->tex,SDL_ScaleModeNearest);
    return true;
}
/* Pose `tile` into an atlas's texture: its region over the first pose's,
 * which is the texture outside the region whichever pose is shown */
static void show_pose(home_texture *t, const sa_asset *a, unsigned tile)
{
    if (!t->motion || !a->rw || !a->cols || (int)tile == t->shown || tile >= (unsigned)a->cols*a->rows) return;
    const Uint8 *p = (const Uint8 *)t->motion->pixels
                   + (size_t)(a->cell + tile/a->cols*a->rh)*t->motion->pitch
                   + (size_t)(tile%a->cols*a->rw)*4;
    if (SDL_UpdateTexture(t->tex,&(SDL_Rect){a->rx,a->ry,a->rw,a->rh},p,t->motion->pitch)==0)
        t->shown = (int)tile;
}
bool home_accept(home *h, SDL_Renderer *r, int shelf, int asset, SDL_Surface **s)
{
    if (shelf != HOME_LOADER_ATLAS && shelf != HOME_LOADER_STILL) return false;
    if (shelf == HOME_LOADER_ATLAS) {
        /* One slot holds an atlas (take_atlas), so one takes the surface */
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
    install(t,r,&s,atlas);
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
    /* And the Grid's last tiles and those of a glide on its way: a system
     * that left the home fades out as its own still (DESIGN.md 11.1) */
    for(int k=0;k<h->grid_was_n+h->grid_from_n && n<HOME_STILLS;k++) {
        const home_tile *t=k<h->grid_was_n?&h->grid_was[k]:&h->grid_from[k-h->grid_was_n];
        int id=asset_for(t->key);
        bool found=false;
        if(!valid(id) || (k>=h->grid_was_n && !h->grid_gliding)) continue;
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
    home_texture *ask[HOME_STILLS]; int asks=0;
    for(int k=0;k<n;k++) {
        home_texture *slot=NULL;
        for(int j=0;j<HOME_STILLS;j++) if(h->still[j].asset==wanted[k]) slot=&h->still[j];
        if(!slot) for(int j=0;j<HOME_STILLS;j++) if(h->still[j].asset<0) {
            slot=&h->still[j]; slot->asset=wanted[k]; break;
        }
        if(slot && !slot->done && asks<home_asks) ask[asks++]=slot;
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
    if(valid(id)) {
        const sa_asset *a=&SA_ASSETS[id];
        /* The focus draws its atlas, the pose due written into it; any
         * other sprite its still, smaller (SA_STILL), the whole texture */
        if(active && h->atlas.asset==id && h->atlas.tex) {
            uint32_t w; unsigned tile=frame_at(a,phase,&w);
            show_pose(&h->atlas,a,tile);
            t=h->atlas.tex;
            if(w<*wait) *wait=w;
        }
    }
    if(t) {
        Uint8 alpha; SDL_GetTextureAlphaMod(t,&alpha);
        SDL_SetTextureAlphaMod(t,(Uint8)lroundf(alpha*opacity));
        SDL_RenderCopyF(r,t,NULL,&dst);
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
/* A system's still lifted (Highlight, src/ds/focus.h): its own soft shadow
 * under `dst`, `lift` deep, at `k` of its strength. Beard: leaning a third
 * of the way to the accent's ink, a little under the machine; Salt &
 * Pepper: a light in the accent round it. */
static void sprite_shade(const home *h, SDL_Renderer *r, const home_item *item, SDL_FRect dst,
                         int lift, float k)
{
    const int id = asset_for(item->key);
    const bool high = lift == DS_FOCUS_LIFT_HIGH, dark = theme_index() == DS_DARK;
    SDL_Texture *t = NULL;
    float u, pad, dy, grow;

    if (lift == DS_FOCUS_LIFT_NONE || !(k > 0.02f) || !valid(id)) return;
    for (int i = 0; i < HOME_STILLS; i++) if (h->still[i].asset == id) t = h->still[i].shade;
    if (!t) return;
    u = dst.w / (float)SA_STILL;
    pad = SHADE_PAD * 2 * u;
    dy = dark ? dst.h * (high ? .03f : .015f) : dst.h * (high ? .075f : .04f);
    grow = high ? dst.w * .03f : 0;
    if (dark) {
        ds_rgb c = theme()->accent;

        SDL_SetTextureColorMod(t, c.r, c.g, c.b);
        SDL_SetTextureAlphaMod(t, (Uint8)lroundf((high ? 140 : 105) * (k > 1 ? 1 : k)));
    } else {
        ds_rgb a = theme()->ink, b = theme()->accent_ink;

        SDL_SetTextureColorMod(t, (Uint8)(a.r + (b.r - a.r) * 2 / 5), (Uint8)(a.g + (b.g - a.g) * 2 / 5),
                               (Uint8)(a.b + (b.b - a.b) * 2 / 5));
        SDL_SetTextureAlphaMod(t, (Uint8)lroundf((high ? 110 : 88) * (k > 1 ? 1 : k)));
    }
    SDL_RenderCopyF(r, t, NULL, &(SDL_FRect){ dst.x - pad - grow, dst.y - pad - grow + dy,
                                              dst.w + 2 * (pad + grow), dst.h + 2 * (pad + grow) });
}
void page_hint_fade(SDL_Renderer *r, const char *key, const char *label,
                    int x, int cy, bool focus, bool favorite, Uint8 alpha)
{
    int size=focus?DS_HINT_GLYPH_FOCUS:DS_HINT_GLYPH, w=ui_button_width(key,size);
    TTF_Font *font=page_font(focus?PAGE_ACTION:PAGE_CHROME);
    SDL_Color c=favorite?ui_col(theme()->favorite,255):UI_TEXT_SOFT;
    c.a=(Uint8)(c.a*alpha/255);
    ui_button_pressed(r,key,x+w/2,cy,size,focus?UI_BUTTON_ON_ACCENT:UI_BUTTON_PLAIN,
                      0.0f,1.0f,alpha/255.0f);
    ui_text(r,font,label,x+w+DS_HINT_GAP,ui_cap_y(font,cy),-1,c);
}
void page_hint(SDL_Renderer *r, const char *key, const char *label,
               int x, int cy, bool focus, bool favorite)
{
    page_hint_fade(r,key,label,x,cy,focus,favorite,255);
}
int page_hint_width(const char *key, const char *label, bool focus)
{
    return ui_button_width(key,focus?DS_HINT_GLYPH_FOCUS:DS_HINT_GLYPH)+DS_HINT_GAP+
           ui_text_width(page_font(focus?PAGE_ACTION:PAGE_CHROME),label);
}
/* The foot's left end, on every layout: MENU's Settings, then START and the
 * layout it turns to, 32 on (keyhints' gap), as a shelf's foot names the
 * View its START turns to (2026-11-17). Returns where START's word ends. */
static int home_foot_keys(SDL_Renderer *r, home_layout layout, int x)
{
    const char *settings=T(S_HOME_SETTINGS);
    const char *next=home_name((home_layout)(((int)layout+1)%HOME_LAYOUTS));
    int sx=x+page_hint_width("MENU",settings,false)+DS_HINTS_GAP;

    page_hint(r,"MENU",settings,x,DS_FOOT_Y,false,false);
    page_hint(r,"START",next,sx,DS_FOOT_Y,false,false);
    return sx+page_hint_width("START",next,false);
}
/* The place ("7 / 9", w wide) in the middle of the foot, or keyhints' gap
 * right of the keys where a long word would meet it there (Italian's "In
 * evidenza" by the Grid's "12 / 21") */
static int home_foot_mid(int keys_end, int w)
{
    int mid=ZYPEROS_SCREEN_W/2;

    return mid-w/2<keys_end+DS_HINTS_GAP ? keys_end+DS_HINTS_GAP+(w+1)/2 : mid;
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
/* THE HOME'S STAGE, placed from two anchors rather than typed: a stage of
 * layers (the focused still, the row of the others, the words) is not a
 * column of boxes, so its places are relations to the anchor's box.
 *
 * Spotlight: the focused system's still, HOME_STILL square, HOME_STILL_IN
 * from the screen's right and HOME_STILL_DOWN under the content's top; its
 * name, count and A's hint on the still's middle (the count there, the name
 * HOME_LINE above it, A's hint HOME_A_DOWN under it), HOME_TEXT_IN in from
 * the page's margin; the row's middle HOME_ROW_UNDER under the still.
 *
 * Gallery: the focused still HOME_GALLERY_STILL square, HOME_GALLERY_DOWN
 * under the content's top, in the middle; its underline 8 under it and the
 * name HOME_GALLERY_NAME under the underline's top. */
#define HOME_MARGIN        DS_MARGIN                  /* 64 */
#define HOME_TOP           (DS_HEADER + DS_SECTION)   /* the content under the top bar (96) */
#define HOME_STILL         448
#define HOME_STILL_IN      40
#define HOME_STILL_DOWN    16
#define HOME_TEXT_IN       12
#define HOME_LINE          64     /* the name's middle above the count's */
#define HOME_A_DOWN        80     /* A's hint's middle under the count's */
#define HOME_ROW_UNDER     40
#define HOME_ROW_PITCH     152    /* the row's sprites apart, and the gallery's */
#define HOME_GALLERY_PITCH 328
#define HOME_GALLERY_STILL 384    /* 1.5 times its 256 cell */
#define HOME_GALLERY_DOWN  24
#define HOME_GALLERY_NAME  50
#define HOME_SELECT_X      (ZYPEROS_SCREEN_W-240) /* SELECT's hint on the foot (Spotlight) */
#define HOME_GALLERY_A_X   (ZYPEROS_SCREEN_W-256) /* the gallery's A, kept there while it ends by the inset */
/* The cover's still, by the canvas's right edge (src/canvas.h) */
static inline SDL_FRect home_focus(void)
{
	return (SDL_FRect){ ZYPEROS_SCREEN_W - HOME_STILL_IN - HOME_STILL, HOME_TOP + HOME_STILL_DOWN,
	                    HOME_STILL, HOME_STILL };
}
#define FOCUS (home_focus())
#define HOME_MID_X       (ZYPEROS_SCREEN_W/2)
#define HOME_STILL_MID_Y ((int)(FOCUS.y+FOCUS.h/2))                       /* 336 */
#define HOME_ROW_Y       ((int)(FOCUS.y+FOCUS.h)+HOME_ROW_UNDER)          /* 600 */
#define HOME_GALLERY_Y   (HOME_TOP+HOME_GALLERY_DOWN+HOME_GALLERY_STILL/2) /* 312 */

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
        float x=HOME_MID_X+d*(gallery?HOME_GALLERY_PITCH:HOME_ROW_PITCH);
        /* Gallery's focused sprite is 384 (1.5x its 256 cell) at 120..504: its
         * underline sits 8 under it and the title 22 under that (review P2). */
        float y=gallery?HOME_GALLERY_Y:HOME_ROW_Y-8*focus;
        if(x+size/2<0 || x-size/2>ZYPEROS_SCREEN_W) continue;
        out[n++]=(row_draw){ i, {roundf(x-size/2),roundf(y-size/2),size,size},
                             gallery && i==cursor, slots[k].alpha };
    }
    return n;
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

/* The row as Highlight marks it (src/ds/focus.h): Border's ring round the
 * focused still, holding still as the row slides under it; Lift raises the
 * focused still on its own shadow; None leaves the others a step back;
 * Bar's bar is under it (home_draw). One painter for the whole frame and
 * the focus frame, so the two lay the same pixels. */
static void row_paint(home *h,SDL_Renderer *r,const home_item *items,const row_draw *row,int nr,
                      bool gallery,ds_focus_marks marks,uint32_t phase,uint32_t *wait,const bool *only)
{
    const float fs=gallery?HOME_GALLERY_STILL:128.0f, fy0=gallery?HOME_GALLERY_Y:HOME_ROW_Y-8;

    /* `only`: the layers to lay (focus_layers' slots), or NULL for all */
    if(marks.ring && (!only || only[0]))
        ds_focus_ring(r,(SDL_FRect){HOME_MID_X-fs/2,fy0-fs/2,fs,fs},DS_RADIUS_CELL,255);
    for(int k=0;k<nr;k++) {
        SDL_FRect d=row[k].dst;
        if(only && !only[1+k]) continue;
        float f=gallery?(d.w-208)/176.0f:(d.w-112)/16.0f, a=row[k].alpha;
        if(f<0) f=0;
        if(f>1) f=1;
        if(marks.lift==DS_FOCUS_LIFT_HIGH) { d.y-=8*f; sprite_shade(h,r,&items[row[k].item],d,marks.lift,f); }
        if(marks.focus==LOOK_FOCUS_NONE) a*=.6f+.4f*f;
        sprite(h,r,&items[row[k].item],d,row[k].active,phase,wait,a);
    }
}

/* The layers of Spotlight's stage over the ground besides the focused
 * sprite, as row_paint lays them: Border's ring, then each of the row's
 * sprites (lifted, its shade under it). `ext` is the whole pixels each can
 * touch, empty for a layer not drawn. Slot 0 is the ring, 1 + k row[k]. */
#define FOCUS_LAYERS (1 + ROW_SLOTS)
#define RING_REACH   10   /* units: the ring's gap and line round its tile, on any panel, rounded out */

static SDL_Rect px_cover(float x, float y, float w, float h)
{
    int x0=(int)floorf(x), y0=(int)floorf(y);
    return (SDL_Rect){x0,y0,(int)ceilf(x+w)-x0,(int)ceilf(y+h)-y0};
}

static int focus_layers(const home *h,int count,int cursor,uint32_t now,ds_focus_marks marks,
                        row_draw row[ROW_SLOTS],SDL_Rect ext[FOCUS_LAYERS])
{
    int nr=row_draws(h,false,count,cursor,now,row);

    memset(ext,0,sizeof(SDL_Rect)*FOCUS_LAYERS);
    if(marks.ring)
        ext[0]=px_cover(HOME_MID_X-64-RING_REACH,HOME_ROW_Y-8-64-RING_REACH,128+2*RING_REACH,128+2*RING_REACH);
    for(int k=0;k<nr;k++) {
        SDL_FRect d=row[k].dst;
        float f=(d.w-112)/16.0f;
        SDL_Rect e;

        if(f<0) f=0;
        if(f>1) f=1;
        if(marks.lift==DS_FOCUS_LIFT_HIGH) d.y-=8*f;
        e=px_cover(d.x,d.y,d.w,d.h);
        if(marks.lift==DS_FOCUS_LIFT_HIGH && f>0.02f) {
            /* sprite_shade's rect, its deepest: pad, grow and drop */
            float u=d.w/(float)SA_STILL, pad=SHADE_PAD*2*u, grow=d.w*.03f, dy=d.h*.075f;
            SDL_Rect sh=px_cover(d.x-pad-grow,d.y-pad-grow,d.w+2*(pad+grow),d.h+2*(pad+grow)+dy), u2;

            SDL_UnionRect(&e,&sh,&u2);
            e=u2;
        }
        ext[1+k]=e;
    }
    return nr;
}

/* The ground a focus frame lays and the layers it draws again whole over
 * it: the sprite's frames' part of its square; every layer over the
 * square, which the sprite drawn again whole would cover; and every layer
 * reaching ground already laid, round until none more does. False when
 * that runs to more than HOME_FOCUS_GROUNDS rects or reaches Bar's bar,
 * which a focus frame does not draw. */
static bool focus_plan(const home *h,int count,int cursor,uint32_t now,
                       SDL_Rect ground[HOME_FOCUS_GROUNDS],int *ng,bool again[FOCUS_LAYERS],
                       row_draw row[ROW_SLOTS],int *nr)
{
    const ds_focus_marks marks=ds_focus_marks_of(DS_FOCUS_STILL);
    const SDL_Rect square=px_cover(FOCUS.x,FOCUS.y,FOCUS.w,FOCUS.h);
    SDL_Rect ext[FOCUS_LAYERS], c=content_rect(h);
    bool grew=true;
    int n=0;

    if (c.w<=0 || c.h<=0) return false;
    *nr=focus_layers(h,count,cursor,now,marks,row,ext);
    ground[n++]=c;
    for(int i=0;i<FOCUS_LAYERS;i++) again[i]=ext[i].w>0 && SDL_HasIntersection(&ext[i],&square);
    for(int i=0;i<FOCUS_LAYERS;i++) if(again[i]) {
        if(n==HOME_FOCUS_GROUNDS) return false;
        ground[n++]=ext[i];
    }
    while(grew) {
        grew=false;
        for(int i=0;i<FOCUS_LAYERS;i++) {
            if(again[i] || ext[i].w<=0) continue;
            for(int j=0;j<n && !again[i];j++) again[i]=SDL_HasIntersection(&ext[i],&ground[j]);
            if(!again[i]) continue;
            if(n==HOME_FOCUS_GROUNDS) return false;
            ground[n++]=ext[i];
            grew=true;
        }
    }
    if(marks.bar) {
        const int under_y=HOME_ROW_Y-8+64+16;
        SDL_Rect bar={HOME_MID_X-64,under_y-8,128,32};   /* its pill and round it */

        for(int j=0;j<n;j++) if(SDL_HasIntersection(&ground[j],&bar)) return false;
    }
    *ng=n;
    return true;
}

bool home_focus_alone(const home *h,home_layout layout,int count,int cursor,uint32_t now)
{
    SDL_Rect ground[HOME_FOCUS_GROUNDS];
    bool again[FOCUS_LAYERS];
    row_draw row[ROW_SLOTS];
    int ng, nr;

    if (layout!=HOME_SPOTLIGHT || count<=0 || cursor<0 || cursor>=count ||
        row_moving(&h->motion,now))
        return false;
    return focus_plan(h,count,cursor,now,ground,&ng,again,row,&nr);
}

int home_focus_ground(const home *h,int count,int cursor,uint32_t now,SDL_Rect out[HOME_FOCUS_GROUNDS])
{
    bool again[FOCUS_LAYERS];
    row_draw row[ROW_SLOTS];
    int ng, nr;

    return focus_plan(h,count,cursor,now,out,&ng,again,row,&nr) ? ng : 0;
}

uint32_t home_draw_focus(home *h,SDL_Renderer *r,const home_item *items,int count,int cursor,
                         uint32_t now,bool animate)
{
    draw_state saved=save_state(r);
    uint32_t wait=UINT32_MAX, phase=animate?now-h->animation:0, row_wait=UINT32_MAX;
    SDL_Rect ground[HOME_FOCUS_GROUNDS];
    bool again[FOCUS_LAYERS];
    row_draw row[ROW_SLOTS];
    int ng, nr;

    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    sprite(h,r,&items[cursor],FOCUS,true,phase,&wait,1);
    /* Then the layers on the ground laid, whole, in home_draw's order:
     * Border's ring, the row's sprites with Highlight's lift, shade and
     * step back (row_paint) */
    if (focus_plan(h,count,cursor,now,ground,&ng,again,row,&nr))
        row_paint(h,r,items,row,nr,false,ds_focus_marks_of(DS_FOCUS_STILL),phase,&row_wait,again);
    if(!animate) wait=UINT32_MAX;
    restore_state(r,saved);
    return wait;
}

/* ---- Grid (DESIGN.md 11.1) ---------------------------------------------- */

/* The systems in a grid: each its still and its short name under it, the
 * focus its moving sprite on a **surface** tile with a thumbnail's lift, its
 * name in **text** with the accent's underline under it, the others in
 * **text-soft**. The grid sizes itself to how many systems there are
 * (2026-11-19: Man in Boots on the Discord, "a little too much white space"
 * on a card of few systems; my picks from the home grid lab): the still in
 * four steps, 256 for up to three, 176 for up to eight, 128 for up to ten,
 * 104 for up to twelve in two rows, and today's grid of 104s, six across and
 * three rows in sight, for more (gh_layout, src/gridrule.h); the name grows
 * with its still. The grid's room is the shelf grid's (src/gamegrid.c): from
 * the top bar's band and a section (96) to where the band under it starts,
 * the grid in its middle, a short last row in the middle under the full
 * ones. The band, the shelf grid's: the system's whole name, its count, A. */
#define HG_MARGIN   HOME_MARGIN
#define HG_TOP      HOME_TOP
#define HG_FOOT     568      /* the room's foot: the band's top less DS_GRID_BAND_GAP */
#define HG_BAND_TOP 592
#define HG_TILE_PAD 8        /* the focus's tile round its cell and its underline */

gh_geom home_grid_geom(int count)
{
    return gh_layout(count, HG_MARGIN, HG_TOP, ZYPEROS_SCREEN_W - 2 * HG_MARGIN,
                     HG_FOOT - HG_TOP, DS_GRID_GAP);
}

/* A name's face by its type (GH_NAME_*): chrome, support or meta */
static TTF_Font *hg_name_font(int name)
{
    return name == GH_NAME_META ? ui_font(UI_F_META)
         : name == GH_NAME_SUPPORT ? page_font(PAGE_META) : page_font(PAGE_CHROME);
}

static int hg_window(const gh_geom *g, int cursor, int top)
{
    int row = cursor / g->cols, last = g->rows - g->win;

    if (last < 0) last = 0;
    if (top < 0) top = row - g->win / 2;
    if (row < top) top = row;
    if (row > top + g->win - 1) top = row - g->win + 1;
    if (top > last) top = last;
    if (top < 0) top = 0;
    return top;
}

static float hg_eased(float from, float to, uint32_t since, int ms)
{
    float k = (float)since / ms;

    return k >= 1 ? to : from + (to - from) * ds_out(k);
}

/* The cursor moved: the window glides in ms.move, the focus in ms.quick,
 * from where they are; a jump of more than a row shows where it lands with a
 * short glide, as the shelf's do. A grid of another shape (the systems
 * changed) places them again. The focus's place is in units, the window at
 * its first row. */
static void hg_follow(home *h, const gh_geom *g, int cursor, uint32_t now)
{
    int x, y, px, py;
    float top, fx, fy;

    gh_cell(g, cursor, &x, &y);
    if (h->grid_cols != g->cols || h->grid_n != g->n || h->grid_still != g->still ||
        h->grid_cursor < 0 || h->grid_cursor >= g->n) {
        int t = hg_window(g, cursor, -1);

        h->grid_cols = g->cols;
        h->grid_n = g->n;
        h->grid_still = g->still;
        h->grid_cursor = cursor;
        h->grid_top_from = h->grid_top_to = (float)t;
        h->grid_fx = (float)x;
        h->grid_fy = (float)y;
        h->grid_moved = now - DS_MS_MOVE;
        h->grid_w.n = 0;
        return;
    }
    if (h->grid_cursor == cursor) return;
    ds_focus_weights_move(&h->grid_w, h->grid_cursor, now - h->grid_moved);
    gh_cell(g, h->grid_cursor, &px, &py);
    top = hg_eased(h->grid_top_from, h->grid_top_to, now - h->grid_moved, DS_MS_MOVE);
    fx = hg_eased(h->grid_fx, (float)px, now - h->grid_moved, DS_MS_QUICK);
    fy = hg_eased(h->grid_fy, (float)py, now - h->grid_moved, DS_MS_QUICK);
    h->grid_top_to = (float)hg_window(g, cursor, (int)lroundf(h->grid_top_to));
    h->grid_top_from = top;
    h->grid_fx = fx;
    h->grid_fy = fy;
    if (abs(cursor / g->cols - h->grid_cursor / g->cols) > 1) {
        int dir = cursor > h->grid_cursor ? 1 : -1;

        h->grid_top_from = h->grid_top_to - (h->grid_top_to != top ? dir * .2f : 0);
        h->grid_fx = (float)x;
        h->grid_fy = (float)y - dir * .2f * (g->ch + g->gap);
    }
    h->grid_cursor = cursor;
    h->grid_moved = now;
}

/* The systems changed under the grid (one turned off or on in MENU > Display
 * > Home Systems, Favorites or Recents made or emptied, a system's last game
 * deleted, System Order turned): a glide from the tiles it drew last, each to
 * its new place and size, in ms.turn on the turn curve; a system that left
 * fades out where it was in ms.fade-out, one that came fades in at its place.
 * Held at its start while a menu is over the home (`covered`), drawing no
 * more frames than the home would, and run from the frame the menu starts to
 * leave, so it is seen. Only from a grid drawn last: never on the first
 * frame, nor coming from Spotlight or Gallery. */
static void hg_glide_note(home *h, const home_item *items, int count, uint32_t now)
{
    bool same = h->grid_was_n == count;

    for (int i = 0; same && i < count; i++)
        same = !strcmp(h->grid_was[i].key, items[i].key ? items[i].key : "");
    if (same) return;
    if (h->grid_live && h->grid_was_n > 0) {
        memcpy(h->grid_from, h->grid_was, sizeof h->grid_from);
        h->grid_from_n = h->grid_was_n;
        for (int i = 0; i < count && i < HOME_STILLS; i++) {
            h->grid_map[i] = -1;
            for (int k = 0; k < h->grid_from_n; k++)
                if (!strcmp(h->grid_from[k].key, items[i].key ? items[i].key : "")) { h->grid_map[i] = (signed char)k; break; }
        }
        h->grid_gliding = true;
        h->grid_held = true;
        h->grid_glide = now;
    }
    h->grid_was_n = 0;
}

/* The glide's turn and fade now (0 to 1), and whether it goes on */
static bool hg_glide_at(home *h, uint32_t now, float *k, float *fade)
{
    uint32_t since;

    if (!h->grid_gliding) return false;
    if (h->grid_held && !h->covered) { h->grid_held = false; h->grid_glide = now; }
    since = h->grid_held ? 0 : now - h->grid_glide;
    if (since >= (DS_MS_TURN > DS_MS_FADE_OUT ? DS_MS_TURN : DS_MS_FADE_OUT)) {
        h->grid_gliding = false;
        return false;
    }
    *k = ds_turn((float)since / DS_MS_TURN);
    *fade = since >= DS_MS_FADE_OUT ? 1 : (float)since / DS_MS_FADE_OUT;
    return true;
}

/* The grid's focus, in one place: Highlight's marks (src/ds/focus.h,
 * DESIGN.md 11.1; my picks in the focus lab, 2026-11-19). Behind the tiles,
 * Border's: a **surface** card 8 round the cell, grown a little, on a soft
 * lift, the ring round it. In front, Bar's: the bar 8 under the focused
 * name's box. Lift and None mark nothing: the still itself grows and rises
 * (hg_tile). `cell` is the cell, its still and its name down, where the
 * focus is now (gliding); `still` the still's side and `box` the name's
 * line; `window` the clip of the rows in sight, NULL when every row is.
 * *(2026-11-15: a name and an underline alone were hard to find at arm's
 * length among 21 systems; the step back of the others and a mark of the
 * player's choosing answer that now.)* */
static void hg_focus(home *h, SDL_Renderer *r, const SDL_FRect *cell, float still, int box,
                     const ds_focus_marks *m, bool front, const SDL_Rect *window)
{
    (void)h;
    if (!front) {
        SDL_FRect card;
        SDL_Rect q;

        if (!m->ring) {
            /* the tiles cut to the window of rows, when there is one */
            if (window) SDL_RenderSetClipRect(r, window);
            return;
        }
        card = ds_focus_tile((SDL_FRect){ cell->x - HG_TILE_PAD, cell->y - HG_TILE_PAD,
                                          cell->w + 2 * HG_TILE_PAD, cell->h + 2 * HG_TILE_PAD }, m, 1);
        q = (SDL_Rect){ (int)lroundf(card.x), (int)lroundf(card.y), (int)lroundf(card.w), (int)lroundf(card.h) };
        /* the lift in the room the caller gave it, then the card, its ring
         * and the tiles cut to the window of rows, when there is one */
        ui_lift_focus(r, &q, false, 1);
        if (window) SDL_RenderSetClipRect(r, window);
        ui_round_rect(r, &q, DS_RADIUS_CARD, ui_col(theme()->surface, 255));
        ds_focus_ring(r, card, DS_RADIUS_CARD, 255);
        return;
    }
    if (m->bar) {
        ds_focus_marks sm = *m;
        SDL_FRect st;

        sm.ring = sm.bar = false;
        st = ds_focus_tile((SDL_FRect){ roundf(cell->x + cell->w / 2 - still / 2), cell->y, still, still }, &sm, 1);
        ds_focus_bar(r, (SDL_FRect){ cell->x, st.y + st.h + GH_NAME_GAP, cell->w, (float)box }, 255);
    }
}

/* One tile: its still `s` square in the middle of its cell, `cw` wide at
 * (x, y), and its name under it in `font`, `alpha` of it. `w` is its weight
 * in the focus's glide: the still grows to Highlight's size on whole pixels
 * and rises on its own shadow with it, the name follows the still's foot
 * and turns from **text-soft** to **text**, and a tile out of focus steps
 * back (src/ds/focus.h). */
static void hg_tile(home *h, SDL_Renderer *r, const home_item *item, const char *name,
                    TTF_Font *font, int box, float x, float y, float s, float cw, bool focus,
                    float w, const ds_focus_marks *m, float alpha, uint32_t phase, uint32_t *wait)
{
    char fit[64];
    SDL_Color c = UI_TEXT, soft = UI_TEXT_SOFT;
    float cx = x + cw / 2.0f;
    ds_focus_marks sm = *m;
    SDL_FRect st;

    if (alpha <= 0) return;
    sm.ring = sm.bar = false;
    st = ds_focus_tile((SDL_FRect){ roundf(cx - s / 2.0f), y, s, s }, &sm, w);
    alpha *= fg_dim_still(w);
    sprite_shade(h, r, item, st, m->lift, w * alpha);
    sprite(h, r, item, st, focus, phase, wait, alpha);
    ui_fit_text(font, name, fit, sizeof fit, (int)cw + DS_GRID_GAP - 8);
    c.r = (Uint8)lroundf(soft.r + (c.r - soft.r) * w);
    c.g = (Uint8)lroundf(soft.g + (c.g - soft.g) * w);
    c.b = (Uint8)lroundf(soft.b + (c.b - soft.b) * w);
    c.a = (Uint8)lroundf(255 * alpha);
    ui_text(r, font, fit, (int)lroundf(cx),
            ui_cap_y(font, (int)lroundf(st.y + st.h) + GH_NAME_GAP + box / 2), 0, c);
}

static uint32_t home_grid_draw(home *h, SDL_Renderer *r, const home_item *items, int count,
                               int cursor, const char *count_label, const char *open_label,
                               uint32_t now, uint32_t phase, uint32_t *wait)
{
    gh_geom g = home_grid_geom(count);
    const ds_focus_marks marks = ds_focus_marks_of(DS_FOCUS_STILL);
    TTF_Font *label = hg_name_font(g.name), *title = page_font(PAGE_GAME_TITLE);
    TTF_Font *meta = page_font(PAGE_META);
    int pitch = g.ch + g.gap, n = count < HOME_STILLS ? count : HOME_STILLS;

    if (cursor >= n) cursor = n - 1;
    float top, fx, fy, k = 1, fade = 1;
    bool gliding;
    SDL_FRect focus;
    SDL_Rect clip;
    struct { float x, y, s, cw, ch; } at[HOME_STILLS];

    hg_glide_note(h, items, n, now);
    hg_follow(h, &g, cursor, now);
    gliding = hg_glide_at(h, now, &k, &fade);
    top = hg_eased(h->grid_top_from, h->grid_top_to, now - h->grid_moved, DS_MS_MOVE);
    {
        int x, y;

        gh_cell(&g, cursor, &x, &y);
        fx = hg_eased(h->grid_fx, (float)x, now - h->grid_moved, DS_MS_QUICK);
        fy = hg_eased(h->grid_fy, (float)y, now - h->grid_moved, DS_MS_QUICK);
    }
    if (now - h->grid_moved < DS_MS_MOVE || (gliding && !h->grid_held)) *wait = 0;

    /* Each tile where it is now: its cell, or on its way there from the
     * grid drawn before */
    for (int i = 0; i < n; i++) {
        int x, y;

        gh_cell(&g, i, &x, &y);
        at[i].x = (float)x; at[i].y = y - top * pitch; at[i].s = (float)g.still;
        at[i].cw = (float)g.cw; at[i].ch = (float)g.ch;
        if (gliding && h->grid_map[i] >= 0) {
            const home_tile *f = &h->grid_from[(int)h->grid_map[i]];

            at[i].x = f->x + (at[i].x - f->x) * k;
            at[i].y = f->y + (at[i].y - f->y) * k;
            at[i].s = f->s + (at[i].s - f->s) * k;
            at[i].cw = f->cw + (at[i].cw - f->cw) * k;
            at[i].ch = f->ch + (at[i].ch - f->ch) * k;
        }
    }
    focus = gliding ? (SDL_FRect){ at[cursor].x, at[cursor].y, at[cursor].cw, at[cursor].ch }
                    : (SDL_FRect){ fx, fy - top * pitch, (float)g.cw, (float)g.ch };

    /* The window of rows: a gap round the rows in sight, from the foot of
     * the row above to the top of the row below, so the focus's marks have
     * room (Border's ring, Bar's bar under the name) and a row scrolling in
     * or out is cut there and the next is not seen. Border's card lift gets
     * another gap round it for its blur. A grid with every row in sight is
     * not cut. */
    clip = (SDL_Rect){ 0, g.y - g.gap, ZYPEROS_SCREEN_W, (g.win - 1) * pitch + g.ch + 2 * g.gap };
    if (g.rows > g.win)
        SDL_RenderSetClipRect(r, &(SDL_Rect){ clip.x, clip.y - g.gap, clip.w, clip.h + 2 * g.gap });
    hg_focus(h, r, &focus, (float)g.still, g.box, &marks, false, g.rows > g.win ? &clip : NULL);
    /* A system that left fades out where it was */
    if (gliding)
        for (int j = 0; j < h->grid_from_n; j++) {
            const home_tile *f = &h->grid_from[j];
            bool kept = false;

            for (int i = 0; i < n && !kept; i++) kept = h->grid_map[i] == j;
            if (kept) continue;
            hg_tile(h, r, &(home_item){ f->key, f->name, f->accent }, f->name, hg_name_font(f->font),
                    f->font == GH_NAME_META ? 40 : f->font == GH_NAME_SUPPORT ? 36 : 28,
                    f->x, f->y, f->s, f->cw, false, 0, &marks, 1 - fade, phase, wait);
        }
    for (int i = 0; i < n; i++) {
        const char *name = home_label(items[i].key, items[i].name);
        float alpha = gliding && h->grid_map[i] < 0 ? k : 1;

        if (at[i].y >= clip.y + clip.h || at[i].y + at[i].ch <= clip.y) continue;
        hg_tile(h, r, &items[i], name, label, g.box, at[i].x, at[i].y, at[i].s, at[i].cw,
                i == cursor, gliding ? (i == cursor ? 1.0f : 0.0f) : ds_focus_weight(&h->grid_w, i, cursor, now - h->grid_moved),
                &marks, alpha, phase, wait);
    }
    hg_focus(h, r, &focus, (float)g.still, g.box, &marks, true, NULL);
    SDL_RenderSetClipRect(r, NULL);
    /* What it drew, for a glide when the systems change */
    for (int i = 0; i < n; i++) {
        home_tile *w = &h->grid_was[i];

        snprintf(w->key, sizeof w->key, "%s", items[i].key ? items[i].key : "");
        snprintf(w->name, sizeof w->name, "%s", home_label(items[i].key, items[i].name));
        w->accent = items[i].accent;
        w->x = at[i].x; w->y = at[i].y; w->s = at[i].s; w->cw = at[i].cw; w->ch = at[i].ch;
        w->font = g.name;
    }
    h->grid_was_n = n;
    h->grid_live = true;
    /* The rail, when the systems are more than the window */
    if (g.rows > g.win) {
        int x = ZYPEROS_SCREEN_W - DS_RAIL_INSET - DS_BAR, hh = g.win * g.ch + (g.win - 1) * g.gap;
        int mh = hh * g.win / g.rows, my;

        if (mh < DS_RAIL_MIN) mh = DS_RAIL_MIN;
        my = g.y + (int)lroundf((hh - mh) * top / (g.rows - g.win));
        ui_round_rect(r, &(SDL_Rect){ x, g.y, DS_BAR, hh }, DS_BAR / 2, ui_col(theme()->track, 255));
        ui_round_rect(r, &(SDL_Rect){ x, my, DS_BAR, mh }, DS_BAR / 2, ui_col(theme()->accent_ink, 255));
    }
    /* The band: the system's whole name (Favorites in the language that is
     * on), A at its right; its count under it */
    {
        const char *full = items[cursor].key && !strcmp(items[cursor].key, "FAV")
                         ? home_label("FAV", NULL)
                         : items[cursor].name && items[cursor].name[0] ? items[cursor].name
                         : home_label(items[cursor].key, NULL);
        const char *open = open_label ? open_label : T(S_HOME_OPEN_GAMES);
        int aw = page_hint_width("A", open, true), ny = HG_BAND_TOP + 20, fy2 = HG_BAND_TOP + 40 + 8 + 18;
        int room = ZYPEROS_SCREEN_W - HG_MARGIN - aw - 32 - HG_MARGIN, tw = ui_text_width(title, full);

        if (tw > room) {
            ui_text_marquee(r, title, full, HG_MARGIN, ui_cap_y(title, ny), room, phase, UI_TEXT);
            uint32_t next = ui_pingpong_wait(tw - room, phase);
            if (next < *wait) *wait = next;
        } else {
            ui_text(r, title, full, HG_MARGIN, ui_cap_y(title, ny), -1, UI_TEXT);
        }
        page_hint(r, "A", open, ZYPEROS_SCREEN_W - HG_MARGIN - aw, ny, true, false);
        ui_text(r, meta, count_label ? count_label : "", HG_MARGIN, ui_cap_y(meta, fy2), -1, UI_TEXT_DIM);
    }
    /* The foot, as Spotlight's: MENU and START at the left, the place in
     * the middle, SELECT at the right, on the one foot (DS_FOOT_Y, the
     * shelf grid's) */
    {
        char index[32];
        int sw = page_hint_width("SELECT", "MP4", false), end = home_foot_keys(r, HOME_GRID, HG_MARGIN);
        TTF_Font *chrome = page_font(PAGE_CHROME);

        snprintf(index, sizeof index, "%d / %d", cursor + 1, count);
        ui_text_tabular(r, chrome, index, home_foot_mid(end, ui_text_tabular_width(chrome, index)),
                        ui_cap_y(chrome, DS_FOOT_Y), 0, UI_TEXT_DIM);
        page_hint(r, "SELECT", "MP4", ZYPEROS_SCREEN_W - HG_MARGIN - sw, DS_FOOT_Y, false, false);
    }
    return *wait;
}

void home_focus_preview(home *h, SDL_Renderer *r, const home_item *items, SDL_Rect box, int focus)
{
    const ds_focus_marks m = ds_focus_marks_for(DS_FOCUS_STILL, focus);
    ds_focus_marks sm = m;
    const float s = floorf(box.w * .24f), gap = floorf(box.w * .07f);
    const float x0 = box.x + (box.w - 3 * s - 2 * gap) / 2, y = box.y + (box.h - s) / 2 - (m.bar ? 6 : 0);
    uint32_t wait = UINT32_MAX;

    sm.ring = sm.bar = false;
    for (int i = 0; i < 3; i++) {
        SDL_FRect cell = { roundf(x0 + i * (s + gap)), roundf(y), s, s }, st;
        float w = i == 1 ? 1.0f : 0.0f;

        if (i == 1 && m.ring) {
            SDL_FRect card = ds_focus_tile((SDL_FRect){ cell.x - HG_TILE_PAD, cell.y - HG_TILE_PAD,
                                                        cell.w + 2 * HG_TILE_PAD, cell.h + 2 * HG_TILE_PAD }, &m, 1);
            SDL_Rect q = { (int)lroundf(card.x), (int)lroundf(card.y), (int)lroundf(card.w), (int)lroundf(card.h) };

            ui_lift_focus(r, &q, false, 1);
            ui_round_rect(r, &q, DS_RADIUS_CARD, ui_col(theme()->surface, 255));
            ds_focus_ring(r, card, DS_RADIUS_CARD, 255);
        }
        st = ds_focus_tile(cell, &sm, w);
        sprite_shade(h, r, &items[i], st, m.lift, w);
        sprite(h, r, &items[i], st, false, 0, &wait, fg_dim_still(w));
        if (i == 1 && m.bar) ds_focus_bar(r, st, 255);
    }
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
    if(layout!=HOME_GRID) h->grid_live=false;   /* no glide from a grid not drawn last */
    if(layout==HOME_GRID) {
        home_grid_draw(h,r,items,count,cursor,count_label,open_label,now,phase,&wait);
        if(!animate) wait=UINT32_MAX;
        restore_state(r,saved);
        return wait;
    }
    if(!gallery) sprite(h,r,&items[cursor],FOCUS,true,phase,&wait,1);
    const ds_focus_marks marks=ds_focus_marks_of(DS_FOCUS_STILL);
    row_draw row[ROW_SLOTS]; int nr=row_draws(h,gallery,count,cursor,now,row);
    row_paint(h,r,items,row,nr,gallery,marks,phase,&wait,NULL);
    const char *label=home_label(items[cursor].key,items[cursor].name);
    if(gallery && items[cursor].name && items[cursor].name[0] &&
       ui_text_width(ui_font(UI_F_TITLE),items[cursor].name)<=880) label=items[cursor].name;
    /* the gallery's underline 8 under its focused still; the Spotlight's 16
     * under the focused sprite in the row (its middle 8 up, 64 to its foot) */
    const int under_y=gallery?HOME_GALLERY_Y+HOME_GALLERY_STILL/2+8:HOME_ROW_Y-8+64+16;
    int title_x=gallery?HOME_MID_X:HOME_MARGIN+HOME_TEXT_IN,
        title_y=gallery?under_y+HOME_GALLERY_NAME:HOME_STILL_MID_Y-HOME_LINE;
    /* Unknown tags are fitted only as a last resort; configured labels use title size. */
    char fitted[128]; ui_fit_text(ui_font(UI_F_TITLE),label,fitted,sizeof fitted,gallery?880:440);
    ui_text(r,ui_font(UI_F_TITLE),fitted,title_x,ui_cap_y(ui_font(UI_F_TITLE),title_y),gallery?0:-1,UI_TEXT);
    ui_text(r,page_font(PAGE_META),count_label?count_label:"",title_x,
            ui_cap_y(page_font(PAGE_META),title_y+HOME_LINE),gallery?0:-1,UI_TEXT_DIM);
    /* Bar's bar where the underline always was: a pill, on whole pixels */
    if(marks.bar) ds_focus_bar(r,(SDL_FRect){HOME_MID_X-64,(float)under_y-8,128,0},255);
    char index[32]; snprintf(index,sizeof index,"%d / %d",cursor+1,count);
    int keys_end=home_foot_keys(r,layout,HOME_MARGIN);
    ui_text(r,page_font(PAGE_CHROME),index,home_foot_mid(keys_end,ui_text_width(page_font(PAGE_CHROME),index)),
            ui_cap_y(page_font(PAGE_CHROME),DS_FOOT_Y),0,UI_TEXT_DIM);   /* on the hints' line */
    if(!gallery) page_hint(r,"SELECT","MP4",HOME_SELECT_X,DS_FOOT_Y,false,false);
    /* The gallery's A hint starts at 768 and keeps there while it ends by 1000;
     * a longer label (the pseudo-locale's) slides left to end there instead of
     * running off the screen */
    const char *open=open_label?open_label:T(S_HOME_OPEN_GAMES);
    int open_x=gallery?HOME_GALLERY_A_X:HOME_MARGIN+HOME_TEXT_IN;
    if(gallery) {
        int total=ui_button_width("A",40)+12+ui_text_width(page_font(PAGE_ACTION),open);
        if(open_x+total>ZYPEROS_SCREEN_W-DS_INSET) open_x=ZYPEROS_SCREEN_W-DS_INSET-total;
    }
    page_hint(r,"A",open,open_x,gallery?DS_FOOT_Y:HOME_STILL_MID_Y+HOME_A_DOWN,true,false);
    if(animate && row_moving(&h->motion,now)) wait=0;
    if(!animate) wait=UINT32_MAX;
    restore_state(r,saved);
    return wait;
}

uint32_t home_header(SDL_Renderer *r,const home_song *song,int status_left,unsigned phase)
{
    uint32_t wait=UINT32_MAX;
    draw_state saved=save_state(r);
    SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_BLEND);
    /* The clock stands at the left end now (src/topbar.h), not the
     * wordmark; the song keeps the middle, clear of both. */
    if(song && song->active) {
        const int left=272, right=status_left-24, mark=24, gap=12;
        if(right-left>=mark+gap+32) {
            TTF_Font *font=page_font(PAGE_CHROME);
            int maxw=right-left-mark-gap; if(maxw>256) maxw=256;
            /* A name wider than its room slides, as every other long name
             * does (ui_text_marquee: the same hold, pace and faded ends);
             * one that fits stands still, centered. The room is then the
             * whole of maxw, so the mark stays where it is while it moves. */
            int over=ui_text_width(font,song->title)-maxw;
            int width=over>0?maxw:ui_text_width(font,song->title);
            int x=left+(right-left-width-mark-gap)/2;
            /* The song's mark, drawn as the bar's other marks are (two
             * eighth notes, or a pause's bars): src/status.h */
            topbar_song_mark(r,x,theme_index(),song->paused);
            ui_text_marquee(r,font,song->title,x+mark+gap,ui_cap_y(font,64),width,phase,UI_TEXT_SOFT);
            /* Only the moments it moves are drawn: the holds at either end
             * are slept through (ui_pingpong_wait) */
            if(over>0) wait=ui_pingpong_wait(over,phase);
        }
    }
    restore_state(r,saved);
    return wait;
}
