/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#include "home.h"
#include "theme.h"
#include "ui.h"
#include "sys_menu.h"
#include "config.h"
#include "texload.h"
#include "systemart_data.h"
#include "cardgen.h"
#include <SDL_image.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The next result for shelf `sys`, freeing any other on the way; false after
 * five seconds with none. */
static bool take_for(int sys, int *idx, SDL_Surface **surf, int *theme, bool *generated)
{
    Uint32 until = SDL_GetTicks() + 5000;
    while (SDL_GetTicks() < until) {
        int got;
        if (!texload_take(&got, idx, surf, theme, generated)) { SDL_Delay(1); continue; }
        if (got == sys) return true;
        if (*surf) SDL_FreeSurface(*surf);
    }
    return false;
}

/* A texture's pixels as they are, copied onto the screen unblended and read
 * back: a card the worker drew and one ui_make_card made compare this way. */
static Uint32 *pixels_of(SDL_Renderer *r, SDL_Texture *t, int w, int h)
{
    Uint32 *px = malloc((size_t)w * h * 4);
    assert(px && t);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(r, t, NULL, &(SDL_Rect){0, 0, w, h});
    assert(SDL_RenderReadPixels(r, &(SDL_Rect){0, 0, w, h}, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0);
    return px;
}

/* The worker's card for `title` in theme `ix`, album or not, against the card
 * the main thread makes in that theme: the same size, marked with the theme,
 * and the same card pixel for pixel. */
static void card_matches(SDL_Renderer *r, int idx, const char *title, unsigned rgb, bool album, int ix)
{
    SDL_Surface *surf = NULL;
    SDL_Texture *mine, *made;
    int got, theme = -2, w = 0, h = 0;
    bool generated = false;
    Uint32 *a, *b;

    assert(texload_want_card(43, idx, "/no/such/art.png", NULL, title, rgb, album, ix));
    assert(take_for(43, &got, &surf, &theme, &generated));
    assert(got == idx && surf && generated && theme == ix);
    assert(surf->w == 512 && surf->h == (album ? 512 : 656));
    mine = ui_card_texture(r, surf, theme);
    assert(mine && ui_card_theme(mine) == ix);
    theme_init(ix == DS_DARK ? APPEAR_DARK : APPEAR_LIGHT);
    assert(theme_index() == ix);
    made = album ? ui_make_cover(r, title, rgb, &w, &h) : ui_make_card(r, title, rgb, &w, &h);
    assert(made && w == surf->w && h == surf->h);
    a = pixels_of(r, mine, w, h);
    b = pixels_of(r, made, w, h);
    assert(!memcmp(a, b, (size_t)w * h * 4));
    free(a); free(b);
    SDL_DestroyTexture(mine); SDL_DestroyTexture(made); SDL_FreeSurface(surf);
    theme_init(APPEAR_LIGHT);
}

int main(void)
{
    home h = {0};
    home_song song = {0};
    menu_row rows[MENU_MAX_ROWS];
    menu_bufs b;
    const char *heading;
    sys_ui menu = {0};
    /* These assertions fail if the default changes or the setting row disappears. */
    assert(home_parse(NULL) == HOME_SPOTLIGHT);
    assert(home_parse("gallery") == HOME_GALLERY);
    assert(home_parse("unknown") == HOME_SPOTLIGHT);
    sys_menu_build(&menu, rows, &b, &heading);
    assert(!strcmp(rows[PM_HOME].value, "Spotlight"));
    menu.home = "Gallery";
    sys_menu_build(&menu, rows, &b, &heading);
    assert(!strcmp(rows[PM_HOME].value, "Gallery"));
    assert(!strcmp(rows[PM_APPEARANCE].value, "Light"));
    assert(!strcmp(home_label("GBA", "Game Boy Advance"), "GBA"));
    assert(!strcmp(home_label("N64", "Nintendo 64"), "N64"));
    assert(!strcmp(home_label("FAV", "Favorites"), "Favorites"));
    /* Reversing a move starts at its current rendered position. */
    home_reset(&h, 0, 5, 100);
    home_select(&h, 1, 5, 1, 100);
    float midway = home_position(&h, 140);
    assert(midway > 0 && midway < 1);
    home_select(&h, 0, 5, -1, 140);
    assert(fabsf(home_position(&h, 140) - midway) < .0001f);
    assert(fabsf(home_position(&h, 300)) < .0001f);
    home_select(&h, 4, 5, -1, 300);
    assert(fabsf(home_position(&h, 460) + 1) < .0001f);
    home_reset(&h, 0, 2, 0);
    home_select(&h, 1, 2, -1, 0);
    assert(home_position(&h,160)==-1);
    home_select(&h, 0, 2, -1, 160);
    assert(home_position(&h,320)==-2);
    home_reset(&h, 0, 1, 0);
    home_select(&h, 0, 1, 1, 0);
    assert(home_position(&h, 100) == 0);
    /* L1/R1: three systems, round either end, and the row moves all three
     * the way pressed, through the wrap */
    assert(home_jump(0, 10, 1) == 3 && home_jump(3, 10, -1) == 0);
    assert(home_jump(8, 10, 1) == 1 && home_jump(1, 10, -1) == 8);
    assert(home_jump(2, 4, 1) == 1 && home_jump(1, 4, -1) == 2);
    assert(home_jump(0, 3, 1) == 2 && home_jump(0, 3, -1) == 1);
    assert(home_jump(0, 2, 1) == 1 && home_jump(1, 2, 1) == 0);
    assert(home_jump(0, 1, 1) == 0 && home_jump(0, 1, -1) == 0);
    home_reset(&h, 8, 10, 0);
    home_select(&h, home_jump(8, 10, 1), 10, 1, 0);
    assert(fabsf(home_position(&h, 1000) - 11) < .0001f);
    home_select(&h, home_jump(1, 10, -1), 10, -1, 1000);
    assert(fabsf(home_position(&h, 2000) - 8) < .0001f);
    /* Review P2: visible small-list sprites never teleport across the row. */
    for(int count=2;count<=5;count++) {
        home_reset(&h,0,count,0);
        for(int step=0;step<12;step++) {
            unsigned base=step*DS_MS_MOVE;
            int next=(h.cursor+(step%3==2?-1:1)+count)%count;
            row_slot before[ROW_SLOTS],after[ROW_SLOTS];
            int n=row_sample(&h.motion,base,before);
            home_select(&h,next,count,step%3==2?-1:1,base);
            int m=row_sample(&h.motion,base,after);
            for(int i=0;i<n;i++) for(int j=0;j<m;j++) if(before[i].item==after[j].item) {
                assert(fabsf(before[i].offset-after[j].offset)<.0001f);
                assert(fabsf(before[i].alpha-after[j].alpha)<.0001f);
            }
            for(unsigned t=1;t<=DS_MS_MOVE;t++) {
                n=row_sample(&h.motion,base+t-1,before); m=row_sample(&h.motion,base+t,after);
                for(int i=0;i<n;i++) for(int j=0;j<m;j++) if(before[i].item==after[j].item &&
                    before[i].alpha>.01f && after[j].alpha>.01f)
                    assert(fabsf(before[i].offset-after[j].offset)<.3f);
            }
        }
    }
    /* The header changes on metadata/state, never on playback position. */
    assert(!home_song_update(&song, false, false, ""));
    assert(home_song_update(&song, true, false, "A song"));
    assert(!home_song_update(&song, true, false, "A song"));
    assert(home_song_update(&song, true, true, "A song"));
    assert(home_song_update(&song, true, false, "A song"));
    assert(home_song_update(&song, true, false, "Next song"));
    assert(home_song_update(&song, false, false, "Next song"));
    assert(!song.title[0] && !song.active);
    assert(SDL_Init(SDL_INIT_TIMER) == 0);
    IMG_Init(IMG_INIT_PNG);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,1024,768,32,SDL_PIXELFORMAT_ARGB8888);
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
    assert(r);
    theme_set_file(NULL);
    theme_init(APPEAR_LIGHT);
    assert(ui_init(r,"res/fonts/menu.ttf"));
    assert(page_fonts_init("res/fonts/menu.ttf"));
    systems_cfg cfg;
    assert(cfg_load_systems("config/systems.cfg",&cfg));
    for(int i=0;i<cfg.count;i++) {
        const char *label=home_label(cfg.systems[i].tag,cfg.systems[i].name);
        if(ui_text_width(ui_font(UI_F_TITLE),label)>440) fprintf(stderr,"too wide: %s\n",label);
        assert(ui_text_width(ui_font(UI_F_TITLE),label)<=440);
    }
    home_item items[]={{"GB","Game Boy"},{"GBA","Game Boy Advance"},{"SFC","SNES"},
                       {"PS1","PlayStation"},{"N64","Nintendo 64"}};
    home_free(&h);
    home_reset(&h,0,5,0);
    home_prepare(&h,r,"res",items,5,0,0);
    assert(h.atlas.tex);
    for(int n=1;n<=5;n++) {
        home_reset(&h,0,n,0);
        home_prepare(&h,r,"res",items,n,0,0);
        uint32_t delay=home_draw(&h,r,HOME_SPOTLIGHT,items,n,0,"1 game","Open games",0,true);
        assert(delay>0 && delay<UINT32_MAX);
        assert(home_draw(&h,r,HOME_GALLERY,items,n,0,"1 game","Open games",0,false)==UINT32_MAX);
        int resident=0;
        for(int j=0;j<HOME_STILLS;j++) if(h.still[j].tex) resident++;
        assert(resident==n);
    }
    /* The selected atlas honors every source delay, including its loop seam. */
    home_reset(&h,0,5,0);
    const sa_asset *gb=&SA_ASSETS[h.atlas.asset];
    uint32_t elapsed=0;
    for(unsigned i=0;i<gb->nframes;i++) {
        assert(home_draw(&h,r,HOME_GALLERY,items,5,0,"1 game","Open games",elapsed,true)==gb->frames[i].ms);
        assert(home_draw(&h,r,HOME_GALLERY,items,5,0,"1 game","Open games",elapsed+gb->frames[i].ms-1,true)==1);
        elapsed+=gb->frames[i].ms;
    }
    assert(elapsed==gb->duration_ms);
    assert(home_draw(&h,r,HOME_GALLERY,items,5,0,"1 game","Open games",elapsed,true)==gb->frames[0].ms);
    /* All configured systems, through repeated changes, stay within the budget. */
    home_item all[CFG_MAX_SYSTEMS];
    for(int i=0;i<cfg.count;i++) all[i]=(home_item){cfg.systems[i].tag,cfg.systems[i].name};
    for(int i=0;i<cfg.count;i++) {
        home_prepare(&h,r,"res",all,cfg.count,i,1000+i*DS_MS_MOVE);
        size_t bytes=0; int width,height,stills=0;
        home_texture *atlases[]={&h.atlas,&h.near[0],&h.near[1]};
        for(int j=0;j<3;j++) if(atlases[j]->tex) {
            SDL_QueryTexture(atlases[j]->tex,NULL,NULL,&width,&height);
            bytes+=(size_t)width*height*4;
        }
        for(int j=0;j<HOME_STILLS;j++) if(h.still[j].tex) {
            SDL_QueryTexture(h.still[j].tex,NULL,NULL,&width,&height);
            bytes+=(size_t)width*height*4; stills++;
        }
        assert(stills<=HOME_STILLS);
        assert(bytes<=(1+HOME_NEAR)*SA_MAX_ATLAS_RGBA_BYTES+HOME_STILLS*128u*128u*4u);
    }
    /* Every configured system's still is resident after a few passes, so a
     * scroll never meets a sprite still loading (2026-11-05). */
    for(int pass=0;pass<8;pass++) home_prepare(&h,r,"res",all,cfg.count,0,20000);
    {
        int stills=0;
        for(int j=0;j<HOME_STILLS;j++) if(h.still[j].tex) stills++;
        assert(stills==cfg.count);
        assert(h.atlas.tex && h.near[0].tex && h.near[1].tex);
    }
    home_prepare(&h,r,"res",items,5,0,10000);
    /* Header pixels must never enter the measured status region. */
    SDL_SetRenderDrawColor(r,71,81,91,255); SDL_RenderClear(r);
    assert(home_song_update(&song,true,false,"A long song title with accents é 日本語 and more words beyond the status corner"));
    home_header(r,&song,640);
    SDL_RenderPresent(r);
    Uint32 untouched=SDL_MapRGBA(surface->format,71,81,91,255);
    for(int y=0;y<80;y++) for(int x=640;x<1024;x++)
        assert(((Uint32*)((Uint8*)surface->pixels+y*surface->pitch))[x]==untouched);
    /* The caller's target, viewport, scale, clip, color and blend all survive. */
    SDL_Texture *target=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,1024,768);
    assert(target); SDL_SetRenderTarget(r,target);
    SDL_RenderSetScale(r,.75f,.75f);
    SDL_Rect viewport={8,16,900,700},clip={24,40,800,600};
    SDL_RenderSetViewport(r,&viewport); SDL_RenderSetClipRect(r,&clip);
    SDL_SetRenderDrawColor(r,10,20,30,40); SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);
    home_header(r,&song,720);
    home_draw(&h,r,HOME_GALLERY,items,5,0,"1 game","Open games",0,false);
    SDL_Rect got; float sx,sy; Uint8 cr,cg,cb,ca; SDL_BlendMode blend;
    assert(SDL_GetRenderTarget(r)==target);
    SDL_RenderGetViewport(r,&got); assert(!memcmp(&got,&viewport,sizeof got));
    SDL_RenderGetClipRect(r,&got); assert(!memcmp(&got,&clip,sizeof got));
    assert(SDL_RenderIsClipEnabled(r)); SDL_RenderGetScale(r,&sx,&sy); assert(sx==.75f && sy==.75f);
    SDL_GetRenderDrawColor(r,&cr,&cg,&cb,&ca); assert(cr==10 && cg==20 && cb==30 && ca==40);
    SDL_GetRenderDrawBlendMode(r,&blend); assert(blend==SDL_BLENDMODE_ADD);
    SDL_SetRenderTarget(r,NULL); SDL_DestroyTexture(target);
    SDL_RenderSetViewport(r,NULL); SDL_RenderSetScale(r,1,1); SDL_RenderSetClipRect(r,NULL);
    /* Home requests must not erase an unrelated game-image request. */
    home_free(&h);
    assert(texload_start());
    assert(texload_want(42,0,"res/system-icons/GB-still.png",NULL));
    home_reset(&h,0,5,0);
    home_prepare(&h,r,"res",items,5,0,0);
    assert(!h.atlas.tex); /* No main-thread decode while workers exist. */
    bool game_landed=false;
    Uint32 until=SDL_GetTicks()+5000;
    while(SDL_GetTicks()<until && (!game_landed || !h.atlas.done || !texload_idle())) {
        int shelf,id,theme; SDL_Surface *decoded; bool generated;
        while(texload_take(&shelf,&id,&decoded,&theme,&generated)) {
            assert(!generated); /* files read, and nothing asked for a card */
            if(shelf==42) { assert(decoded); game_landed=true; }
            else assert(home_accept(&h,r,shelf,id,decoded));
            if(decoded) SDL_FreeSurface(decoded);
        }
        SDL_Delay(1);
    }
    assert(game_landed && h.atlas.tex);
    /* A late result for another selection never replaces the active atlas. */
    SDL_Texture *kept=h.atlas.tex;
    SDL_Surface *wrong=SDL_CreateRGBSurfaceWithFormat(0,1,1,32,SDL_PIXELFORMAT_ARGB8888);
    assert(home_accept(&h,r,HOME_LOADER_ATLAS,h.atlas.asset+1,wrong));
    assert(h.atlas.tex==kept);
    SDL_FreeSurface(wrong);
    /* A game with no art. Before the workers' faces are open no card comes
     * back, and the shelf makes it on the frame as it always has; once they
     * are, the worker draws it, in the theme it was asked in, and it is the
     * card the main thread makes in that theme, pixel for pixel - a game's
     * and an album's, in both themes. Closed, no card again. */
    {
        SDL_Surface *none=NULL; int id=-1;
        assert(texload_want_card(43,0,"/no/such/art.png",NULL,"Contra",0xC8102E,false,DS_LIGHT));
        assert(take_for(43,&id,&none,NULL,NULL) && id==0 && !none);
        texload_fonts_open();
        texload_fonts_open();   /* open already: nothing */
        card_matches(r,1,"Castlevania III: Dracula's Curse",0xC8102E,false,DS_LIGHT);
        card_matches(r,2,"Castlevania III: Dracula's Curse",0xC8102E,false,DS_DARK);
        card_matches(r,3,"A Night at the Opera",0xBEF23C,true,DS_LIGHT);
        card_matches(r,4,"   lowercase first, no color",CFG_NO_COLOR,true,DS_DARK);
        /* A card drawn for the theme that was is told apart on landing */
        assert(!cardgen_kept(DS_DARK,theme_index()) && cardgen_kept(DS_LIGHT,theme_index()));
        texload_fonts_close();
        texload_fonts_close();  /* closed already: nothing */
        bool generated=true; int theme=-2;
        assert(texload_want_card(43,5,"/no/such/art.png",NULL,"Contra",0xC8102E,false,DS_LIGHT));
        assert(take_for(43,&id,&none,&theme,&generated) && id==5 && !none && !generated && theme==-1);
        texload_fonts_open();   /* left open: texload_stop closes them */
    }
    texload_stop();
    /* Invalid/missing sources produce a neutral placeholder, with no retry loop. */
    home_free(&h); home_reset(&h,0,1,0);
    home_prepare(&h,r,"/no/such/home-art",items,1,0,0);
    assert(h.atlas.done && !h.atlas.tex);
    assert(home_draw(&h,r,HOME_SPOTLIGHT,items,1,0,"No games","Open games",0,true)==UINT32_MAX);
    home_free(&h);
    home_item missing={0};
    missing.key="MISSING"; missing.name="A full name which must remain canonical elsewhere";
    home_reset(&h,0,1,0); home_prepare(&h,r,"res",&missing,1,0,0);
    assert(!h.atlas.tex);
    assert(home_draw(&h,r,HOME_GALLERY,&missing,1,0,"No games","Open games",0,true)==UINT32_MAX);
    assert(home_draw(&h,r,HOME_GALLERY,NULL,0,0,"","",0,true)==UINT32_MAX);
    home_free(&h); page_fonts_quit(); ui_quit(); SDL_DestroyRenderer(r); SDL_FreeSurface(surface); IMG_Quit(); SDL_Quit();
    puts("ok: home choice, labels, motion, cached music, renderer state, bounded assets, worker isolation, worker-drawn cards, and missing art");
    return 0;
}
