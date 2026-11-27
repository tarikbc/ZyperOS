/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_APP_H
#define ZYPEROS_APP_H

/* The launcher's state, and what src/main.c shares with the files split from
 * it: the shelves' views and the app every screen is handed, the redraw
 * clock, the send-off's times, and the functions of main.c they call. */
#include "config.h"
#include "coverflow.h"
#include "games.h"
#include "gamelist.h"
#include "gamegrid.h"
#include "videoview.h"
#include "home.h"
#include "i18n.h"
#include "idle.h"
#include "library.h"
#include "mp4lib.h"
#include "mp4queue.h"
#include "platform.h"
#include "status.h"
#include "sysupdate.h"

#include <SDL.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef enum { SCREEN_SYSTEMS, SCREEN_GAMES } screen_id;

typedef struct {
	game_list list;
	SDL_Texture **tex;
	int *tw, *th;
	float *cb;      /* where each card's art stops, for its reflection */
	int cursor;
	/* Index into DMODES. Zero-initialized like the rest of this struct, so
	 * whatever sits at index 0 is what an untouched card plays at - see the
	 * note on DMODES itself. While the system's row was never set
	 * (dmode_kept false) it is its default (dmode_default), which follows
	 * its core: Original Shape for your PICO-8. */
	int dmode;
	bool dmode_kept;   /* display.<TAG> holds a pick */
	/* Index into SHADERS, this system's: its own choice, or the one MENU set
	 * for every system (shader_load). Zero is Off. */
	int shader;
	/* How strongly that shader is drawn, when it is an old screen
	 * (shader_has_strength): percent, 10 to 100, this system's own or
	 * MENU's (src/strength.h). Zero, never loaded, reads 100. */
	int strength;
	/* Index into SORTS. Zero is by name, which is what every shelf was
	 * before this existed and what an untouched one still is. On MP4's
	 * shelf an ml_order instead, whose zero is by artist. */
	int sort;
	/* MENU > Favorites First (DESIGN.md 11.70), a console shelf's own as
	 * Sort By is: its favorites ahead of its other games, each part in
	 * `sort`. False, as it ships, is the shelf as it always was. Where the
	 * favorites are once sorted: `nfav` of them from `fav_from` (after the
	 * folders, and Splore on PICO-8's), 0 when off or none, which the
	 * letter jump reads so the two parts are never one run of an initial. */
	bool fav_first;
	int fav_from, nfav;
	coverflow cf;
	games_row row;
	/* NULL on a real shelf, where every game belongs to the system whose
	 * shelf it is. Favorites is a shelf of games drawn from many systems, so
	 * each entry has to carry its own - the core to load, the folder the ROM
	 * is under, the accent, and where the save state lives all follow from
	 * it, and every one of them would be wrong if taken from the shelf. */
	int *owner;
	/* MP4's shelf only, NULL on every other: the album card k is. A card's
	 * cover, a fetched cover landing on it, and A on it all go by the album,
	 * and with two orders a card's place is no longer its album's number. */
	int *album;
	/* The letter jump groups by `name` instead of `title`. MP4's shelf in
	 * artist order has the artist in `name`: grouped by album title there,
	 * up and down would land somewhere that looks arbitrary. In album order
	 * the title is the order, and the jump goes by it as a games shelf's
	 * does. */
	bool jump_by_name;
	/* Folders on the shelf (src/shelfdir.h). `all`: every game of the
	 * system at every depth, when some are in folders, and `list` is then
	 * what the shelf shows of folder `dir` ("" the top) - its folders, the
	 * first `nfold` entries, then its games. NULL when no game is in a
	 * folder, and `list` is all of them, as it always was: sys_games says
	 * which, for whatever is about the system's games and not its shelf. */
	game_list *all;
	char dir[LIB_PATH];
	int nfold;
	/* The list view's place and motion (src/gamelist.h); unset places it
	 * again on the cursor */
	gl_motion lm;
	/* MENU's View for this shelf: its covers in a row (both false), as it
	 * always was, a list of names (DESIGN.md 11.39) or a grid of covers
	 * (11.47); never both */
	bool listed;
	bool grid;
	/* ...or the Video view (DESIGN.md 11.57, src/videoview.h): the focused
	 * game's snap on the whole screen, its covers in a band at the foot;
	 * never with either of those, and its band's motion */
	bool video;
	snaprest vm;
	/* The grid's place and motion, what it knows of each cover beyond its
	 * texture (src/gamegrid.h), and the shape kept for this shelf in the
	 * device's database (grid_shape_load), read when the grid opens it, and
	 * whether this opening has written what its covers measured */
	gg_motion gm;
	gg_arts gart;
	float grid_kept;
	bool grid_saved;
} sysview;

/* Every game of a system, at every depth of its folders: Favorites, search,
 * Random Game, Storage, the scraper and the saved library go by this, never
 * by what one folder of its shelf shows */
static inline game_list *sys_games(sysview *v)
{
	return v->all ? v->all : &v->list;
}
static inline const game_list *sys_games_c(const sysview *v)
{
	return v->all ? v->all : &v->list;
}

/* Diatom's display modes, in the order ZyperOS offers them: the sensible
 * default first, then whole-pixel scaling, then the ones that trade shape or
 * edges for coverage, with 1:1 last as a reference rather than a choice.
 *
 * The names are Diatom's protocol strings and have to match its own table in
 * src/scale.c exactly - it answers an unknown one with ERROR code=bad_display
 * and changes nothing. The labels are ours: `label` in English, and `text`,
 * through dmode_label, in the language that is on, which the menu shows. */
/* STRETCH IS FIRST, AND FIRST IS THE DEFAULT. A sysview is zero-initialized
 * and no display mode is seeded, so index 0 is what every system plays at
 * nobody has configured. Filling the panel is the right default on a handheld
 * whose screen is the whole device: the alternative spends a fifth of a
 * 1024x768 panel on black bars for the Game Boys before anyone has been given
 * a reason to choose. The rest keep their order. */
/* The labels say what a mode does, not how (1.0.6: "Integer" read as
 * nothing to a player; Stretch, Aspect, Integer, Integer tall, Overscale,
 * Fill and Native 1:1 before it), and `note` is the line under the Display
 * row that says it in a sentence. The names, which the card keeps
 * (display.<TAG>), are unchanged. */
/* Square Pixels (1.1.0, #346) sits between Original Shape and Sharp Pixels
 * because it is the step between them: Original Shape's full height, Sharp
 * Pixels' square pixels, without Sharp Pixels' whole sizes. Read down, the
 * list goes from the game's shape to its pixels. A card's names are kept by
 * name, never by place, so the row in the middle moves nobody's choice. */
static const struct { const char *name, *label; str_id text, note; } DMODES[] = {
	{ "stretch",          "Full Screen",          S_GAMEMENU_DMODE_STRETCH,      S_GAMEMENU_DMODE_STRETCH_NOTE      },
	{ "aspect",           "Original Shape",       S_GAMEMENU_DMODE_ASPECT,       S_GAMEMENU_DMODE_ASPECT_NOTE       },
	{ "square",           "Square Pixels",        S_GAMEMENU_DMODE_SQUARE,       S_GAMEMENU_DMODE_SQUARE_NOTE       },
	{ "integer",          "Sharp Pixels",         S_GAMEMENU_DMODE_INTEGER,      S_GAMEMENU_DMODE_INTEGER_NOTE      },
	{ "integer-vertical", "Sharp Pixels, Tall",   S_GAMEMENU_DMODE_INTEGER_TALL, S_GAMEMENU_DMODE_INTEGER_TALL_NOTE },
	{ "overscale",        "Sharp Pixels, Zoomed", S_GAMEMENU_DMODE_OVERSCALE,    S_GAMEMENU_DMODE_OVERSCALE_NOTE    },
	{ "fill",             "Fill, Edges Cut",      S_GAMEMENU_DMODE_FILL,         S_GAMEMENU_DMODE_FILL_NOTE         },
	{ "native",           "Actual Size",          S_GAMEMENU_DMODE_NATIVE,       S_GAMEMENU_DMODE_NATIVE_NOTE       },
};
#define DMODE_COUNT ((int)(sizeof DMODES / sizeof DMODES[0]))

/* Display mode k's label in the language that is on; past the ends, the
 * first's */
static inline const char *dmode_label(int k)
{
	return T(DMODES[k >= 0 && k < DMODE_COUNT ? k : 0].text);
}

/* And its line under the Display row */
static inline const char *dmode_note(int k)
{
	return T(DMODES[k >= 0 && k < DMODE_COUNT ? k : 0].note);
}

/* The shaders the Shader rows offer, in their order: Off first, and Off is
 * the default, today's picture at today's cost. `name` is Diatom's filter
 * (SETDISPLAY filter=, its src/scale.c diatom_filters), so Off is its
 * nearest. Kept per system as shader.<TAG> beside display.<TAG>, and for
 * every system as `shader`, what MENU's row sets (shader_load). One more is
 * a row here, a string, and Diatom's filter of that name.
 *
 * `gpu`: an old screen only a GPU draws (Diatom's port/pixel2.c), offered
 * where plat_gpu_shaders says so and stepped over elsewhere (shader_step).
 * `only`: the systems' tags it is for, a space before and after each, or
 * NULL for every system (shader_for). LCD No Backlight is the screen the
 * Game Boy's three had, so only their shelves and games offer it, never
 * ZyperOS's own screens (screen_shader_offered), and a system that is not
 * one of them and names it (an edited card) gets its default instead
 * (display_load). */
static const struct { const char *name; str_id text; bool gpu; const char *only; } SHADERS[] = {
	{ "nearest",     S_MENU_OFF_TITLE,               false, NULL },
	{ "shimmerless", S_GAMEMENU_SHADER_SHIMMERLESS,  false, NULL },
	{ "crt",         S_GAMEMENU_SHADER_CRT,          true,  NULL },
	{ "crt-curved",  S_GAMEMENU_SHADER_CRT_CURVED,   true,  NULL },
	{ "scanlines",   S_GAMEMENU_SHADER_SCANLINES,    true,  NULL },
	{ "lcd",         S_GAMEMENU_SHADER_LCD,          true,  NULL },
	{ "lcd-unlit",   S_GAMEMENU_SHADER_LCD_UNLIT,    true,  " GB GBC GBA " },
};
#define SHADER_COUNT ((int)(sizeof SHADERS / sizeof SHADERS[0]))

/* Whether shader k is one the system tagged `tag` may have, on any device
 * (`only`); NULL, ZyperOS's own screens, has only the ones for every system */
static inline bool shader_for(int k, const char *tag)
{
	char key[16];

	if (k < 0 || k >= SHADER_COUNT) return false;
	if (!SHADERS[k].only) return true;
	if (!tag || !*tag || strlen(tag) + 3 > sizeof key) return false;
	snprintf(key, sizeof key, " %s ", tag);
	return strstr(SHADERS[k].only, key) != NULL;
}

/* Shader k's label in the language that is on; past the ends, Off's */
static inline const char *shader_label(int k)
{
	return T(SHADERS[k >= 0 && k < SHADER_COUNT ? k : 0].text);
}

/* Whether shader k has a Strength row under it (src/strength.h): the old
 * screens, whose lines and grid are an effect over the picture. Off and
 * Sharp-Shimmerless only scale it, with nothing to weaken. The old screens
 * are the `gpu` ones today, so only where they are drawn (a card moved to a
 * Brick draws them as Sharp-Shimmerless); a GPU scaler added later is not
 * an effect and would want a flag of its own here. */
static inline bool shader_has_strength(int k)
{
	return k >= 0 && k < SHADER_COUNT && SHADERS[k].gpu && plat_gpu_shaders();
}

typedef struct {
	systems_cfg sys;

	home home;
	home_layout home_layout;
	/* When the focused system's shelf is asked for ahead of A: once the
	 * home's cursor has rested on it (src/shelf_keys.c); 0 for none */
	Uint32 home_prime_at;
	sysview view[CFG_MAX_SYSTEMS];
	/* The systems MENU > Display > Home Systems turned off (src/homesys.h),
	 * kept after the home's shelves, at sys.systems[] and view[] from
	 * sys.count on (sys_kept): no shelf of the home, nor Search's or Random
	 * Game's, but their games stay on Favorites and Recents and start from
	 * there, as an owner of those (shelf_owner). Davistoa's ask, my OK on
	 * 2026-11-19: "hide every system and live in Favorites". */
	int parked;

	screen_id screen;
	int sys_cursor;
	int menu_w;             /* cached shelf-menu content width; 0 = unmeasured */
	unsigned tint;          /* eased toward the focused system's accent */
	in_state in;
	bool running;
	/* Set for one launch only: the game that comes back after a shutdown
	 * opens with the menu up. Cleared as it is used, so quitting to the
	 * shelf and launching the same game again behaves normally. */
	bool resume_menu;
	int  auto_off;              /* seconds without input, 0 off */
	idle_clock idle;            /* Auto Off's clock - src/idle.h */
	SDL_Renderer *r;
} app;

/* How many systems have their games in memory: the home's shelves and, after
 * them, the ones turned off the home (app.parked). A loop over a game's own
 * system by its index or tag goes this far; a loop over the home stops at
 * sys.count. */
static inline int sys_kept(const app *a)
{
	return a->sys.count + a->parked;
}

/* The earliest moment something already on screen will look different with
 * nobody touching anything - a card mid-move, the tint still easing toward a
 * new system, a long title about to scroll, the volume line about to go.
 * REDRAW_NEVER means nothing drawn in the last frame changes by itself.
 *
 * Gathered WHILE drawing, because the things that move are the things that
 * know they are moving: cf_draw already returns whether a move is in flight,
 * and a marquee is the only thing that knows where it is in its hold. render
 * resets it and every drawing path that animates pulls it earlier. The main
 * loop then only draws when it has come due, or when there was input.
 *
 * It exists because the shelf was redrawn and presented at the refresh rate
 * whether or not anything had changed. Measured on the Brick 2026-09-13, idle
 * on a shelf: 26.6% of a core and 88 voluntary context switches a second, all
 * of it the main thread, for a picture that was not moving. */
#define REDRAW_NEVER UINT32_MAX
extern Uint32 g_redraw_at;
/* When something last asked for a frame within 50 ms of being drawn: motion
 * on screen, which the status corner holds its forks back for (see
 * draw_status). Kept apart from g_redraw_at, which only render resets: a
 * menu's loop draws the shelf without it, and there g_redraw_at goes stale. */
extern Uint32 g_moved_at;

static inline void redraw_at(Uint32 t)
{
	Uint32 now = plat_now_ms();

	if (t <= now + 50) g_moved_at = now | 1;
	if (t < g_redraw_at) g_redraw_at = t;
}
static inline void redraw_now(void)
{
	g_moved_at = plat_now_ms() | 1;
	g_redraw_at = 0;
}
/* A frame asked for within 50 ms, this frame or the one before it. */
static inline bool frames_wanted(void)
{
	return g_moved_at && plat_now_ms() - g_moved_at < 100;
}

/* The send-off, and the boot animation's gesture run backward: there Zyper
 * comes up, cocks his head and puts the tongue out; here he comes in from the
 * left, the tongue goes back in as the head straightens, and then he dims to
 * the ground.
 *
 * The theme's ground. Salt & Pepper's is the ink of his nose and eyes, which
 * this always cleared to, so there nothing moved; on Beard he fades into
 * Beard's own light ground, rather than a dark screen being cut into it.
 *
 * Timed against the shutdown rather than chosen. Measured 2026-08-28: 1.42 s
 * from the Power off press to adbd dying, of which the old 620 ms animation
 * was the first slice - the rest was a black screen while sync and the kernel
 * finished. So the animation runs to about 900 ms and then DOES NOT clear: the
 * dimmed head is its last frame. power_off turns the backlight off right
 * after, rather than leaving him lit until the kernel cuts the panel (which is
 * how it was until 2026-10-02).
 *
 * So this screen is its own exception to DESIGN.md 8.2 and 8.3, twice. Its
 * three times, POWEROFF_IN, POWEROFF_HEAD and POWEROFF_DIM, are no tokens:
 * they share out the time the device takes to go down, not the time a motion
 * should take to feel right, and a token retuned for the interface must not
 * pull the send-off off the shutdown. And the head's flinch is on **in** where
 * 8.2 puts Zyper's tilt and tongue on **out**: it accelerates into the cut to
 * the dimmed head, which is the point of a flinch. Coming in and dimming are
 * on **out**, as the design has them. */
#define POWEROFF_IN   430u
#define POWEROFF_HEAD 260u
#define POWEROFF_DIM  210u
#define POWEROFF_MS   (POWEROFF_IN + POWEROFF_HEAD + POWEROFF_DIM)

/* Set by SIGTERM (main.c, on_sigterm): every screen's loop ends on it */
extern volatile sig_atomic_t want_quit;

/* ---- main.c's, for the files split from it; each is described there ---- */

/* Folders on a shelf (src/shelfdir.h): shelf `s` on folder `dir` of its
 * games, or the nearest one up that still holds a game, sorted, its cursor
 * on the entry whose file is `select` (none: the first) and its row still -
 * nothing on a shelf with no folders; and shelf `s` on game `file` of its
 * system, into the folder it is kept in first */
void shelf_open_dir(app *a, int s, const char *dir, const char *select);
void shelf_reveal(app *a, int s, const char *file);

/* The cards' textures and the paths they come from */
SDL_Texture *load_image(SDL_Renderer *r, const char *path, int *w, int *h,
                        float *cb);
bool tex_stale(SDL_Texture *t);
void preview_path(app *a, int s, const game_entry *g, char *out, size_t n);
void card_frame_path(app *a, int s, const game_entry *g, char *out, size_t n);
void state_path(app *a, int s, const game_entry *g, char *out, size_t n);
int  shelf_owner(app *a, int sysidx, int item);
void box_art_path(const char *folder, const char *name, char *out, size_t n);
SDL_Texture *game_get_tex(void *ctx, int i, int *w, int *h, float *cb);

/* MP4: the place in a book or a film kept, and the player polled */
void book_keep(bool now);
/* MP4's resume row gone (src/mp4resume.h): a port about to take the sound,
 * after which nothing should open by itself */
void resume_clear(void);
void mp4_poll(void);
/* MP4's queue handed over a program of its own that keeps it playing
 * (src/mp4hand.h): written, and taken on (true when a queue came) */
bool mp4_hand_save(void);
bool mp4_hand_take(void);
/* Your PICO-8 quiet while MP4 plays (src/main.c): the equalizer's file's cut
 * set for what MP4 does as the launcher hands over to your PICO-8 (`pico8`),
 * cleared for any other program and as the launcher starts */
void port_quiet_hand(bool pico8);
/* SELECT in the in-game menu over such a program: MP4 made ready there,
 * false over any other (src/main.c) */
bool port_mp4_ready(app *a);

/* Charge Limit, and the one full charge asked for past it */
int  charge_limit_now(void);
void charge_full_once(void);
void charge_full_end(void);
/* A full charge asked for and not over; the limit saved in MENU (0 Off) */
bool charge_full_active(void);
int  charge_limit_saved(void);

/* "Getting hot": the chip's temperature, read */
void heat_tick(void);

/* The status corner's clock, `t` as it shows it */
bool status_clock(time_t t, char *out, size_t n);

/* The power-off: the send-off, and everything it does around it */
void anim_poweroff(app *a);
void power_off_with(app *a, void (*anim)(app *a));
void snap_off(void);

/* Each system's display mode, core and sort order, loaded; a shelf sorted */
void display_load(app *a);
void core_load(app *a);
/* The Display mode a system plays at while its row was never set, by its
 * core (main.c says why), and that mode put on the system's view while it
 * still has no pick */
int  dmode_default(const system_cfg *s);
void dmode_settle(app *a, int sys);
void sort_load(app *a);
/* MENU > View, each shelf's: its covers or a list (DESIGN.md 11.39) */
void view_load(app *a);
/* A shelf's View, in the order Right and START step it (view_turn): Covers,
 * Grid, List, Video (DESIGN.md 11.39, 11.47, 11.57) */
enum { VIEW_COVERS, VIEW_GRID, VIEW_LIST, VIEW_VIDEO, VIEW_COUNT };
static inline int view_index(const sysview *v)
{
	return v->listed ? VIEW_LIST : v->grid ? VIEW_GRID : v->video ? VIEW_VIDEO : VIEW_COVERS;
}
bool view_video(const char *tag);
/* Recents' Card Picture (DESIGN.md 11.48): false its covers, as it ships;
 * true the screen each game was left on, its cover small in a corner
 * (src/screencard.h). recents_picture_turn turns it over, saves it, and has
 * Recents' cards asked for again. */
#define RECENTS_PICTURE_KEY "recents.picture"
extern bool g_recents_screen;
void recents_picture_turn(app *a);
bool view_listed(const char *tag);
bool view_grid(const char *tag);
void sort_shelf(app *a, int sys);
/* sort_shelf with the cursor kept on its game, the shelf drawn again */
void sort_shelf_keep(app *a, int sys);
/* MENU > Favorites First (DESIGN.md 11.70): whether shelf `sys` has it (a
 * console's), and it turned over there, kept and applied */
bool fav_first_offered(app *a, int sys);
void fav_first_turn(app *a, int sys);
void sort_all(app *a);
/* Every texture of the renderer the launcher holds, let go */
void free_all_textures(app *a);
/* A copy that leaves the destination alone when the source is missing */
void copy_file(const char *from, const char *to);
bool file_nonempty(const char *p);

/* For px2-open's commands (src/ctl_channel.c): a core on the card, whether
 * a shelf is MP4's, and the in-game menu's Quit (a disc system's BIOS is
 * src/biosfile.h's) */
bool core_present(const char *core);
/* What config/systems.cfg's brings line asks of the player for `core` on
 * `s` is on the card (`missing` gets the first path that is not), and a
 * core that can run: on the card and with those files (src/main.c) */
bool core_brought(const system_cfg *s, const char *core, char *missing, size_t n);
bool core_ready(const system_cfg *s, const char *core);
/* A PICO-8 zip the player copied into Bios/PICO-8, taken (src/pico8.h,
 * p8_take_zip), before the cores are chosen; what to say of it once the home
 * is up in g_p8_said, "" for nothing */
extern char g_p8_said[256];
void p8_take_now(void);
/* The cores the shelf's Core row offers: those that can run, and the one
 * chosen */
int  core_offered_count(const system_cfg *s);
bool core_offered_at(const system_cfg *s, int i, char *out, int cap);
bool is_mp4(const system_cfg *s);
void gm_quit(void);
/* A pop-up over the running game, as the quick slot's (main.c's popup_now):
 * `head` and under it `body`, at the top for about a second and a half */
void popup_say(const char *head, const char *body);
/* A Ports shelf's core, and a standalone emulator's: "run:<script>"
 * (main.c's launch_port hands both to launch.sh) */
#define PORT_CORE    "port"
#define RUN_PREFIX   "run:"
/* A game Random Game, Search Games or px2-open picked, started once the
 * menu has gone (main.c's pick_launch) */
typedef struct { int shelf, game; unsigned hold_ms; } game_pick;
extern game_pick g_pick;
void pick_launch(app *a);

/* System Update (src/update_screen.c reads it): its files, its program
 * started, what it says (read at most four times a second, g_up_read_at
 * the last time), and its page up, which keeps the corner's mark off */
void up_path(char *buf, size_t n, const char *name);
void up_spawn(const char *cmd);
const su_view *up_view(bool now);
extern unsigned g_up_read_at;
extern bool g_up_shown;

/* For MENU's Search Games, Date & Time and Menu Music (src/search_screen.c,
 * src/clock_screen.c, src/bgm_screen.c): the theme and the boot screen told
 * a setting changed, the internet asked about, the clock's 24 hours, and
 * Menu Music's Song Pop-up */
void theme_changed(app *a);
void boot_theme_changed(void);
bool net_internet(bool *metered);
extern bool g_clock24;
extern bool g_song_popup;
/* --status, for a shot: what the corner shows in place of the device's
 * clock, radio and battery (main.c's shot_st says how) */
typedef struct {
	bool         shot;
	bool         set;
	int          minutes;
	int          h24;          /* -1 for the saved setting */
	int          eta;          /* minutes left for Time Left, -1 for none */
	int          calc;         /* the calculating dots, this many filled, -1 for none */
	bool         full;         /* on the cable and charged */
	int          icon;         /* BATT_BARS.., -1 for the saved setting */
	int          year, mon, mday;  /* date=YYYY-MM-DD, the day the shot shows; 0 for today */
	status_marks m;
} shot_status;
extern shot_status shot_st;

/* For MENU > Storage (src/storage_screen.c): MP4's place in a book or film
 * let go of the file about to be deleted */
void mp4_before_delete(const char *abs);

/* For a game's Cover and Video page (src/media_screen.c): the status bar
 * and its half, the focused title's marquee phase, the shelf's video snap
 * asked for or stopped (at g_snap_force_vol's sound, -1 for the setting's),
 * the account's one async slot let go, and the titles read again */
unsigned draw_topbar(SDL_Renderer *r, int theme, SDL_Color soft, bool moving);
int  header_half(void);
unsigned title_phase(app *a);
void snap_ask(const char *req, bool hold);
extern int g_snap_force_vol;
void sync_abandon(void);
void refresh_game_titles(app *a);

/* The launcher's own accent, MP4's and Favorites': ui.h and theme.h's */
#define ZYPEROS_ACCENT ui_rgb(theme()->accent)

/* ---- MP4's library, which the shelf building reads and builds ---- */

/* Each album's cover, as far as it is known: g_cov (main.c) */
enum { COV_UNKNOWN, COV_ASKED, COV_JPG, COV_PNG, COV_FOLDER, COV_NONE };
typedef struct { unsigned char st; unsigned asked_ms; } cover_state;

extern ml_lib       g_mp4;
extern char         g_mp4_root[CFG_STR * 2];
extern unsigned     g_mp4_gen;
extern cover_state *g_cov;
extern int          g_mp4_kind;
extern bool        *g_book_done;

ml_kind mp4_kind_shown(void);
void book_key(int al, char *out, size_t n);
int  mp4_artist_of(int al);
void mp4_album_dir(int al, char *out, size_t n);
/* Now Playing's cover let go, the library about to change */
void np_forget(void);

/* How an MP4 screen was left: back one level, MP4 closed altogether, or the
 * library rebuilt under it by Rescan Folder, so every album index it held is
 * stale and the shelf has to find itself again. */
typedef enum { MP4_BACK, MP4_CLOSE, MP4_REBUILT } mp4_exit;

/* For MP4's Now Playing (src/nowplaying_screen.c): a book's next speed, a
 * new queue played, the play mode moved on (its marks, MP4_MODES), the
 * power button inside MP4 and what MP4 is open over (g_mp4_over, its
 * ctx, and g_mp4_gone when that rule ended it), the Now Playing cover
 * brought up to date (g_np), and a menu for a screen of the launcher's */
void book_cycle_speed(void);
void mp4_play(const char *const *paths, int n, int start, double at,
               bool book, double speed, const char *artist, const char *album);
void mp4_cycle_mode(void);
void video_cycle_repeat(void);   /* Y on a video: MP4's Repeat, Off, One, All */
typedef struct { const char *key; int glyph; } mp4_mode_name;
extern const mp4_mode_name MP4_MODES[MUQ_MODES];
void mp4_power(app *a);
typedef struct {
	int          album;
	bool         done;
	SDL_Texture *tex;
	/* The cover's main color (src/covercolor.h), CFG_NO_COLOR for an album
	 * with none, whose card is drawn in the accent */
	unsigned     rgb;
} np_cover_state;
extern np_cover_state g_np;
void np_cover(app *a, int al);
void zyperos_menu_for(app *a, int screen);
/* MP4's covers: whether the card has answered every album's ask */
bool cover_answers(void);
/* A count of the frames drawn: each shelf frame and each clear (main.c) */
extern unsigned g_frame_no;

#endif
