/* SPDX-License-Identifier: GPL-3.0-only */
#include "home.h"
#include "theme.h"
#include "ui.h"
#include "sys_menu.h"
#include "config.h"
#include "texload.h"
#include "systemart_data.h"
#include <SDL_image.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    home h = {0};
    home_song song = {0};
    menu_row rows[MENU_MAX_ROWS];
    menu_bufs b;
    const char *heading;
    /* every row of MENU, in the enum's order: no tab (sys_menu.h) */
    sys_ui menu = { .tab = PT_TABS };
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
    /* Up/Down on the home: with A to Z, the first system of the next letter, or
     * of its own letter and then the previous; round the ends like L1/R1. */
    {
        /* F A A B B B D, Ports last: groups of 1, 2, 3, 1, 1 */
        const unsigned char k[] = { 1, 'A', 'A', 'B', 'B', 'B', 'D', 2 };
        const int n = sizeof k;
        assert(home_letter_jump(k, n, 0, 1) == 1);          /* off Favorites */
        assert(home_letter_jump(k, n, 1, 1) == 3 && home_letter_jump(k, n, 2, 1) == 3);
        assert(home_letter_jump(k, n, 3, 1) == 6 && home_letter_jump(k, n, 5, 1) == 6);
        assert(home_letter_jump(k, n, 6, 1) == 7);          /* onto Ports */
        assert(home_letter_jump(k, n, 7, 1) == 0);          /* round, as R1 does */
        assert(home_letter_jump(k, n, 5, -1) == 3 && home_letter_jump(k, n, 4, -1) == 3);
        assert(home_letter_jump(k, n, 3, -1) == 1);         /* already first: the previous */
        assert(home_letter_jump(k, n, 2, -1) == 1);
        assert(home_letter_jump(k, n, 1, -1) == 0);
        assert(home_letter_jump(k, n, 0, -1) == 7);         /* round, as L1 does */
        assert(home_letter_jump(k, n, 7, -1) == 6);
        const unsigned char one[] = { 'A', 'A', 'A' };
        assert(home_letter_jump(one, 3, 1, 1) == 1 && home_letter_jump(one, 3, 2, -1) == 0);
        assert(home_letter_jump(one, 3, 0, -1) == 0);       /* one group: nowhere to go */
        assert(home_letter_jump(one, 1, 0, 1) == 0 && home_letter_jump(one, 0, 0, 1) == 0);
        assert(home_group_key("FAV", "x", "Favorites") == 1);
        assert(home_group_key("PRT", "port", "Ports") == 2);
        assert(home_group_key("GB", "gambatte", "game boy") == 'G');
        assert(home_group_key("A", "c", "  3DO") == '3' && home_group_key("A", "c", "...") == '#');
    }
    /* The same on the real list, sorted as A to Z sorts it: every Down lands on
     * the first system of a letter, and ten of them go all the way round. */
    {
        systems_cfg sc;
        unsigned char keys[CFG_MAX_SYSTEMS];
        int cur, groups = 0, i;
        assert(cfg_load_systems("config/systems.cfg", &sc) && sc.count > 10);
        cfg_sort_systems(&sc);
        for (i = 0; i < sc.count; i++)
            keys[i] = home_group_key(sc.systems[i].tag, sc.systems[i].core, sc.systems[i].name);
        for (i = 0; i < sc.count; i++)
            if (i == 0 || keys[i] != keys[i - 1]) groups++;
        /* the letters are in order, so a letter is one run and never two */
        for (i = 1; i < sc.count; i++)
            if (keys[i] != keys[i - 1])
                for (int j = 0; j < i - 1; j++) assert(keys[j] != keys[i]);
        cur = 0;
        for (i = 0; i < groups; i++) {
            int to = home_letter_jump(keys, sc.count, cur, 1);
            assert(to == 0 ? i == groups - 1 : (to > cur && keys[to] != keys[to - 1]));
            cur = to;
        }
        assert(cur == 0);                                   /* all the way round */
        for (i = 0; i < groups; i++) {
            int to = home_letter_jump(keys, sc.count, cur, -1);
            assert(keys[to] != keys[(to + sc.count - 1) % sc.count] || sc.count == 1);
            cur = to;
        }
        assert(cur == 0);
    }
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
        /* The pose due is the one written into the atlas's texture */
        assert(h.atlas.motion && h.atlas.shown==(int)gb->frames[i].tile);
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
        /* An atlas: its texture, the first pose, and in RAM its regions */
        for(int j=0;j<3;j++) if(atlases[j]->tex) {
            size_t one;
            SDL_QueryTexture(atlases[j]->tex,NULL,NULL,&width,&height);
            assert(width==(int)SA_CELL && height==(int)SA_CELL);
            one=(size_t)width*height*4;
            if(atlases[j]->motion) one=(size_t)atlases[j]->motion->w*atlases[j]->motion->h*4;
            assert(one<=SA_MAX_ATLAS_RGBA_BYTES);
            bytes+=one;
        }
        for(int j=0;j<HOME_STILLS;j++) if(h.still[j].tex) {
            SDL_QueryTexture(h.still[j].tex,NULL,NULL,&width,&height);
            bytes+=(size_t)width*height*4; stills++;
        }
        assert(stills<=HOME_STILLS);
        assert(bytes<=(1+HOME_NEAR)*SA_MAX_ATLAS_RGBA_BYTES+HOME_STILLS*SA_STILL*SA_STILL*4u);
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
    /* A name wider than its room slides, so it is due again (after its hold) */
    assert(home_header(r,&song,640,0)!=UINT32_MAX);
    SDL_RenderPresent(r);
    Uint32 untouched=SDL_MapRGBA(surface->format,71,81,91,255);
    for(int y=0;y<80;y++) for(int x=640;x<1024;x++)
        assert(((Uint32*)((Uint8*)surface->pixels+y*surface->pitch))[x]==untouched);
    {
        home_song brief; home_song_update(&brief,true,false,"Night");
        /* One that fits stands still: nothing to wake for */
        assert(home_header(r,&brief,720,0)==UINT32_MAX);
    }
    /* The caller's target, viewport, scale, clip, color and blend all survive. */
    SDL_Texture *target=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,1024,768);
    assert(target); SDL_SetRenderTarget(r,target);
    SDL_RenderSetScale(r,.75f,.75f);
    SDL_Rect viewport={8,16,900,700},clip={24,40,800,600};
    SDL_RenderSetViewport(r,&viewport); SDL_RenderSetClipRect(r,&clip);
    SDL_SetRenderDrawColor(r,10,20,30,40); SDL_SetRenderDrawBlendMode(r,SDL_BLENDMODE_ADD);
    home_header(r,&song,720,0);
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
        texload_got got;
        while(texload_take(&got)) {
            if(got.sys==42) { assert(got.surf); game_landed=true; }
            else assert(home_accept(&h,r,got.sys,got.idx,got.surf));
            if(got.surf) SDL_FreeSurface(got.surf);
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
    puts("ok: home choice, labels, motion, cached music, renderer state, bounded assets, worker isolation, and missing art");
    return 0;
}
