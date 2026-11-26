/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SHELF_DRAW_H
#define ZYPEROS_SHELF_DRAW_H

/* The home and the shelves drawn, and what comes with every frame: the theme's
 * and the clock's ticks, the cards and their titles, the focused game's video
 * snap, the empty home, Zyper's song and "Getting hot" pop-ups, the focus
 * frames, the launch's zoom and the send-off. The code is src/shelf_draw.c;
 * the shelf's keys are src/shelf_keys.c. */
#include "app.h"
#include "panel.h"
#include "songpop.h"
#include "zyperrig.h"
#include "lastboot.h"

typedef struct {
	int sys, cursor;
	unsigned seen;
	bool valid, resume, achievements;
	int earned, total;
	char played[32], file[LIB_PATH];
	/* The firmware it needs and the card lacks, said under its name
	 * ("Needs disksys.rom in Bios"); "" when it has what it needs */
	char needs[CFG_STR + 64];
	/* Its cheats (src/cheats_screen.h): "2 cheats on" alone, and with
	 * " · the last session ended in a crash" when it stopped with them on;
	 * "" for none */
	char cheats_on[64], cheats[160];
	bool crashed;
} game_page_state;

/* Zyper in the cheer, posed by the rig at the cheer's clock */
typedef struct { zr_rig z; unsigned now; } cheer_ctx;

typedef struct {
	bool     on;
	unsigned at;       /* when he started up */
	unsigned drawn;    /* render's last frame of it */
	zr_rig   rig;
	/* Another word of his in the same bubble (System Update's news,
	 * up_notice_watch; the last start's restart, lastboot_watch): its two
	 * lines and no thermometer; "" for the heat */
	char     line1[160], line2[256];
} heatpop_state;

/* What main.c uses of it; each is described where it is defined */
extern Uint32 g_focus_at;
extern bool g_clock_held;
extern unsigned g_clock_at;
extern int g_home_phase;
extern int shot_phase;
extern int shot_focus_from;
extern game_page_state g_game_page;
extern int g_games_phase;
extern float g_shot_snap_k;
extern float g_shot_snap_wait;
/* --band F: the Video view's band F of the way down to its strip (0 up, 1
 * down), for a shot; below 0 its own */
extern float g_shot_band;
extern float g_shot_sheen;
extern float g_shot_sharp;
/* The focused card's sharp one (src/focusfx.h) is asked of the art workers
 * as shelf `s`'s SHELF_SHARP_SYS(s), apart from its covers; its result is
 * handed here */
#define SHELF_SHARP_SYS(s) (-10 - (s))
void shelf_focus_sharp_put(app *a, int sys, int idx, SDL_Surface *s, bool card, int theme_ix);
/* A game's card order's system and frames of play (src/cardart.h), for
 * game `g` of system `o`, on an order ui_card_order_set began (src/main.c) */
void game_card_order(app *a, int o, const game_entry *g, ui_card_order *card);
extern int g_eh_shot;
extern songpop g_songpop;
extern zr_rig g_songpop_zyper;
extern heatpop_state g_heatpop;
extern unsigned last_tint_ms;
Uint32 anim_ticks(void);
bool snap_new_picture(void);
/* The shelf drawn last was the Video view (DESIGN.md 11.57) with its snap
 * playing: somebody watching, which holds the screensaver's countdown as
 * music does (saver_screen.c) */
bool shelf_video_playing(void);
bool no_games(const app *a);
int games_on_card(const app *a);
void cheer_zyper(SDL_Renderer *r, float cx, float cy, float h, float alpha, void *ctx);
void empty_keys(app *a);
void empty_watch(app *a, bool empty);
unsigned song_hold(const char *head, const char *body);
void song_watch(void);
void song_popup_draw(app *a, unsigned now);
void heatpop_show(unsigned now);
void heatpop_draw(app *a, unsigned now);
/* Zyper's bubble saying the last start's restart (src/lastboot.h), after
 * heatpop_show */
void lastboot_bubble(const lastboot *lb);
void heat_watch(void);
void song_keys(app *a);
bool focus_ok(const app *a);
bool focus_draw(app *a);
void focus_frame(app *a);
void launch_frame(app *a, SDL_Texture *card, const SDL_Rect *from,
                  float k);
void poweroff_zyper(app *a, unsigned t, float cx, float cy, float h, float from,
                    Uint8 alpha);
void poweroff_frame(app *a, unsigned t);

#endif
