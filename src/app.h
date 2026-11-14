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
#include "home.h"
#include "i18n.h"
#include "idle.h"
#include "library.h"
#include "muselib.h"
#include "platform.h"

#include <SDL.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
	 * note on DMODES itself. */
	int dmode;
	/* Index into SHADERS, this system's: its own choice, or the one MENU set
	 * for every system (shader_load). Zero is Off. */
	int shader;
	/* How strongly that shader is drawn, when it is an old screen
	 * (shader_has_strength): percent, 10 to 100, this system's own or
	 * MENU's (src/strength.h). Zero, never loaded, reads 100. */
	int strength;
	/* Index into SORTS. Zero is by name, which is what every shelf was
	 * before this existed and what an untouched one still is. On Muse's
	 * shelf an ml_order instead, whose zero is by artist. */
	int sort;
	coverflow cf;
	games_row row;
	/* NULL on a real shelf, where every game belongs to the system whose
	 * shelf it is. Favorites is a shelf of games drawn from many systems, so
	 * each entry has to carry its own - the core to load, the folder the ROM
	 * is under, the accent, and where the save state lives all follow from
	 * it, and every one of them would be wrong if taken from the shelf. */
	int *owner;
	/* Muse's shelf only, NULL on every other: the album card k is. A card's
	 * cover, a fetched cover landing on it, and A on it all go by the album,
	 * and with two orders a card's place is no longer its album's number. */
	int *album;
	/* The letter jump groups by `name` instead of `title`. Muse's shelf in
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
	/* MENU's View for this shelf: its covers in a row (false), as it always
	 * was, or a list of names (DESIGN.md 11.39) */
	bool listed;
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
static const struct { const char *name, *label; str_id text; } DMODES[] = {
	{ "stretch",          "Stretch",      S_GAMEMENU_DMODE_STRETCH       },
	{ "aspect",           "Aspect",       S_GAMEMENU_DMODE_ASPECT        },
	{ "integer",          "Integer",      S_GAMEMENU_DMODE_INTEGER       },
	{ "integer-vertical", "Integer tall", S_GAMEMENU_DMODE_INTEGER_TALL  },
	{ "overscale",        "Overscale",    S_GAMEMENU_DMODE_OVERSCALE     },
	{ "fill",             "Fill",         S_GAMEMENU_DMODE_FILL          },
	{ "native",           "Native 1:1",   S_GAMEMENU_DMODE_NATIVE        },
};
#define DMODE_COUNT ((int)(sizeof DMODES / sizeof DMODES[0]))

/* Display mode k's label in the language that is on; past the ends, the
 * first's */
static inline const char *dmode_label(int k)
{
	return T(DMODES[k >= 0 && k < DMODE_COUNT ? k : 0].text);
}

/* The shaders the Shader rows offer, in their order: Off first, and Off is
 * the default, today's picture at today's cost. `name` is Diatom's filter
 * (SETDISPLAY filter=, its src/scale.c diatom_filters), so Off is its
 * nearest. Kept per system as shader.<TAG> beside display.<TAG>, and for
 * every system as `shader`, what MENU's row sets (shader_load). One more is
 * a row here, a string, and Diatom's filter of that name.
 *
 * `gpu`: an old screen only a GPU draws (Diatom's port/pixel2.c), offered
 * where plat_gpu_shaders says so and stepped over elsewhere (shader_step). */
static const struct { const char *name; str_id text; bool gpu; } SHADERS[] = {
	{ "nearest",     S_MENU_OFF_TITLE,               false },
	{ "shimmerless", S_GAMEMENU_SHADER_SHIMMERLESS,  false },
	{ "crt",         S_GAMEMENU_SHADER_CRT,          true  },
	{ "crt-curved",  S_GAMEMENU_SHADER_CRT_CURVED,   true  },
	{ "scanlines",   S_GAMEMENU_SHADER_SCANLINES,    true  },
	{ "lcd",         S_GAMEMENU_SHADER_LCD,          true  },
};
#define SHADER_COUNT ((int)(sizeof SHADERS / sizeof SHADERS[0]))

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
	sysview view[CFG_MAX_SYSTEMS];

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
void state_path(app *a, int s, const game_entry *g, char *out, size_t n);
int  shelf_owner(app *a, int sysidx, int item);
void box_art_path(const char *folder, const char *name, char *out, size_t n);
SDL_Texture *game_get_tex(void *ctx, int i, int *w, int *h, float *cb);

/* Muse: the place in a book or a film kept, and the player polled */
void book_keep(bool now);
void muse_poll(void);

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
void sort_load(app *a);
/* MENU > View, each shelf's: its covers or a list (DESIGN.md 11.39) */
void view_load(app *a);
bool view_listed(const char *tag);
void sort_shelf(app *a, int sys);
void sort_all(app *a);
/* Every texture of the renderer the launcher holds, let go */
void free_all_textures(app *a);
/* A copy that leaves the destination alone when the source is missing */
void copy_file(const char *from, const char *to);
bool file_nonempty(const char *p);

/* The launcher's own accent, Muse's and Favorites': ui.h and theme.h's */
#define ZYPEROS_ACCENT ui_rgb(theme()->accent)

/* ---- Muse's library, which the shelf building reads and builds ---- */

/* Each album's cover, as far as it is known: g_cov (main.c) */
enum { COV_UNKNOWN, COV_ASKED, COV_JPG, COV_PNG, COV_FOLDER, COV_NONE };
typedef struct { unsigned char st; unsigned asked_ms; } cover_state;

extern ml_lib       g_muse;
extern char         g_muse_root[CFG_STR * 2];
extern unsigned     g_muse_gen;
extern cover_state *g_cov;
extern int          g_muse_kind;
extern bool        *g_book_done;

ml_kind muse_kind_shown(void);
void book_key(int al, char *out, size_t n);
int  muse_artist_of(int al);
void muse_album_dir(int al, char *out, size_t n);
/* Now Playing's cover let go, the library about to change */
void np_forget(void);

#endif
