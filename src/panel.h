/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PANEL_H
#define ZYPEROS_PANEL_H

/* The menus' engine, which is src/main.c's, for the screens split from it:
 * the runner's results and style, a panel's open, frames and close, and
 * what a screen with a panel calls every frame. Each is described in
 * main.c. */
#include "app.h"
#include "keyboard.h"
#include "menu.h"
#include "menumotion.h"
#include "platform.h"

#include <stdbool.h>

/* The accent of a menu that is no system's: the launcher's own, which are
 * ZyperOS's. Its heading rule is the plain **edge** one, and it is lifted with
 * a neutral light. Not a color, so no system can have it, and not
 * CFG_NO_COLOR either: a system with no color still owns its menu, and says so
 * with a **text-dim** rule as it does with its other marks. Only menu_draw_ex
 * reads it; nothing else is ever handed it. */
#define MENU_ZYPEROS 0xFE000000u

typedef enum {
	MENU_STAY,   /* keep the screen up */
	MENU_DONE,   /* leave it */
	/* Keep it up, and say how the press went, which Zyper answers in the top
	 * ZyperOS menu (menu_style's `zyper`): something changed - a setting, a
	 * sign-in, the box art - or what was asked for could not be done. The
	 * same as MENU_STAY to every other screen. */
	MENU_CHANGED,
	MENU_FAILED
} menu_result;

/* Where a panel is drawing the picture in the column at its rows' left
 * (menu_style's `side`), and in what pass:
 *   MENU_SIDE_REST  the panel at rest, straight on the screen
 *   MENU_SIDE_PASS  the panel moving (DESIGN.md 8.2), into the pass that
 *                   scales and fades it: a picture drawn here goes with it
 *   MENU_SIDE_OVER  the same panel's frame, after the pass, on the screen at
 *                   the panel's place at rest: a picture that holds still
 *                   while the panel under it changes (the Controls tab's
 *                   handheld, as its pages open and close) is drawn here and
 *                   not in the pass
 * A drawer is called once a frame at REST, or twice, PASS then OVER, and
 * draws in one of them. */
typedef enum { MENU_SIDE_REST, MENU_SIDE_PASS, MENU_SIDE_OVER } menu_side_stage;

typedef struct {
	SDL_Rect panel;         /* the panel at rest */
	int      content_x;     /* the content's left edge: the heading rule's left end */
	int      content_y;     /* the rows' top */
	const menu_row *rows;   /* the rows drawn beside it, and the cursor's (-1 none) */
	int      n, sel;
	/* The panel is the screen's own, drawn by its loop now, and not a copy
	 * of one drawn leaving (src/menusnap.h) */
	bool     live;
	/* The tab row's move, when the panel has one: the tab on, the one it
	 * came from and which way (+1 R1, -1 L1), and when it began (0 at rest);
	 * -1, -1, 0, 0 for a panel with no tabs */
	int      tab_at, tab_from, tab_dir;
	unsigned tab_t0;
	unsigned now;           /* the motion's clock (anim_now) */
	menu_side_stage stage;
} menu_side;

/* How the panel is drawn, as opposed to what is in it. Everything here needs
 * the renderer or the app, which is exactly why it is not in the build
 * function. */
typedef struct {
	/* 0: measure these rows. The system menu passes menu_shelf_width, a width
	 * measured once across every system and every display mode, because a
	 * panel that resizes while you cycle a value on it reads as a glitch. */
	int      fixed_w;
	unsigned accent;
	/* The shelf's animated tint instead of `accent`, read per frame. The
	 * system menu on a game shelf belongs to that system, and tick_tint is
	 * still moving toward its color while the menu is open. */
	bool     follow_tint;
	/* What goes behind the panel. NULL is the shelf, tinted and dimmed, which
	 * is what every screen reached from the shelf wants. The in-game menu
	 * draws the paused game instead, because the shelf is not what is under
	 * it. Called before the panel, every frame, with the scrim's strength
	 * (panel_frame), and by panel_leave_run as the panel leaves. */
	void   (*backdrop)(app *a, void *ctx, float scrim);
	/* What the power button and the idle timer mean here. NULL powers the
	 * device off. The in-game menu stops the GAME instead: the device is not
	 * going anywhere, and the wait loop below it has to be told, or it goes
	 * straight back to waiting on a game nobody is running. */
	menu_result (*on_power)(app *a, void *ctx);
	/* Each row's sound class (MENU_SND_*), which its build sets beside the
	 * rows by their ids and the runner reads by place (menu_sound_at). NULL:
	 * every row plain, as on a screen whose rows are named by their owners. */
	const unsigned char *sound;
	/* Zyper beside the heading (DESIGN.md 10): the top ZyperOS menu, the one
	 * MENU opens on the systems row, and no other. MENU_ZYPEROS is Wi-Fi,
	 * About and the sign-ins too, so the accent cannot say it. */
	bool     zyper;
	/* The row the cursor starts on: 0, the first, unless a screen says
	 * otherwise (MENU > Language starts on the language that is on) */
	int      start_sel;
	/* MENU's tabs (src/sys_menu.h, DESIGN.md 11.3): the tab row in place of
	 * the heading, L1 and R1 going round it, and a panel as tall as the
	 * screen allows on every tab, so it does not change size as they go.
	 * Its own count and names, so the in-game menu has a row of its own.
	 * NULL for every other menu. */
	const struct menu_tabs *tabs;
	/* Where the cursor is, written on every pass, for a screen that keeps
	 * it when the menu closes (MENU, by tab); NULL for none */
	int     *sel_out;
	/* A mark drawn before a row's value, at the value's left with its
	 * right edge at `right` and centered on `cy`: Theme's accent dot and
	 * the themes' chips (DESIGN.md 11.42). `on_fill` is how much of the
	 * focus fill is under it, 0 to 1. NULL: none. */
	void   (*row_mark)(app *a, const menu_row *row, int right, int cy, float on_fill);
	/* A picture between the heading's rule and the rows, `header_h` tall
	 * with the gap under it: Theme's two small themes. NULL: none. */
	void   (*header)(app *a, const SDL_Rect *r);
	int      header_h;
	/* Each row's value color by place, parallel to the rows the build made:
	 * 0 the ordinary rule, 1 (VCOL_EARNED) accent-ink, the Controls tab's
	 * changed values. Read every frame through the pointer, so the build
	 * may write it. NULL: every value plain. */
	const unsigned *vcolors;
	/* A picture in a column at the rows' left (the Controls tab's handheld,
	 * DESIGN.md 11.28): the rows and the focus fill are set in by `side_w`
	 * from the content's left edge, and `side` is called with where and in
	 * which pass (menu_side), whatever side_w is, so it can draw a picture
	 * leaving with a tab that has none. A panel with one is as tall as the
	 * screen allows, as MENU's tabs are, so it holds still as the pages
	 * beside it change. NULL: none. */
	void   (*side)(app *a, const menu_side *at);
	int      side_w;
} menu_style;

/* ---- the menu runner (main.c's menu_run) ------------------------------------
 *
 * How a screen ended, for the callers that act differently on each. */
typedef enum {
	MENU_LEFT_BACK,     /* B or MENU, the ordinary way out */
	MENU_LEFT_SCREEN,   /* the screen's own handler said so */
	MENU_LEFT_GONE      /* quit, power, or nothing left to show */
} menu_exit;

/* Fill `rows`, name the screen, and return how many rows; called every
 * frame. Its rules are main.c's, beside menu_run. */
typedef int (*menu_build_fn)(void *ctx, menu_row *rows, int max,
                             const char **heading);

/* A key the runner did not consume, and the row it happened on (its place
 * in the rows the last build made) */
typedef menu_result (*menu_key_fn)(app *a, void *ctx, in_button key, int sel);

/* One screen, one loop: polling, power, idle, quit, back, volume,
 * brightness, the cursor and the whole frame */
menu_exit menu_run(app *a, const menu_style *st,
                   menu_build_fn build, menu_key_fn on_key, void *ctx);

/* From a key handler: the runner's cursor goes to row `sel` of the rows it
 * builds next, because they changed under it (MENU's tabs; a Controls row
 * whose change put a warning above it). Forgotten when the runner ends. */
void menu_cursor_to(int sel);

/* A panel as menu_run draws one with `st` (its tabs, the picture beside
 * its rows, its value colors), for a loop of its own or a shot */
void menu_draw_styled(app *a, const menu_style *st, const char *heading,
                      const menu_row *rows, int n, int sel);

/* MENU was pressed in a screen over the one asking, and every screen is
 * closing (menu_leaving) */
bool menu_closing(void);

/* The launcher's motion clock (DESIGN.md 8): plat_now_ms, or a shot's held
 * time (--shot-at) */
unsigned anim_now(void);

/* A question asked as Delete Game asks it: wrapped to the panel a line a
 * row, a rule, then `yes_label` and Cancel, the cursor on Cancel. Over
 * `over`'s backdrop with its power rule (NULL: over the shelf, POWER the
 * device's). True for the yes. */
bool ask_panel(app *a, const menu_style *over, void *ctx, const char *heading,
               const char *q, const char *yes_label);

/* The in-game menu's read-only Controls pages over `over`'s backdrop with
 * its power rule, at their own width: the Controls tab's Hotkeys row, which
 * opens them less the system's buttons (the tab shows those;
 * ctl_game.no_buttons). Left and Right turn them, B goes back. */
void game_controls_pages(app *a, const menu_style *over, void *ctx);

/* What a screen with a panel draws under it, and asks every frame */
void draw_shelf(app *a);
void tick_tint(app *a);
bool tick_theme(app *a);

/* A panel's motion (DESIGN.md 8.2): opened, before each frame, closed */
int  panel_open(void);
bool panel_frame(app *a, int lv, float *scrim);
void panel_close(app *a, int lv, bool cut);
/* B, MENU, or a MENU pressed deeper in: the screen leaves */
bool menu_leaving(app *a);
/* A step along a list or shelf, heard */
void step_sound(app *a, in_button b, int from, int to);
/* A list kept as a plain index 0 to n - 1, stepped by Up or Down `b` as
 * every menu steps (menu_step_index: a press goes round, a held direction
 * stops at the end) and heard; the new index */
int list_step_heard(app *a, in_button b, int n, int sel);

/* A menu's panel: `n` rows, `sel` focused, `fixed_w` wide (0 measures) */
void menu_draw(app *a, const char *heading, const menu_row *rows, int n,
               int sel, int fixed_w, unsigned accent);
/* The width the launcher's menus share, and how many rows of a list fit
 * around a screen's fixed rows, rules and notes */
int  menu_std_width(app *a);
int  menu_list_fit(int fixed, int rules, int notes);
/* One line on a panel over the shelf, before something that blocks */
void wait_panel(app *a, const char *heading, const char *msg);
/* A question on a panel over the shelf, Yes on `yes_label`: true on Yes */
bool confirm_panel(app *a, const char *heading, const char *msg,
                   const char *yes_label);
/* What a screen with its own page draws first: the theme's ground */
void draw_background(app *a);
/* A move on a page heard: `b`'s step sound */
void moved_sound(app *a, in_button b);
/* The runner's messages: rows held for a while, or until a press; one
 * result's words; and the on-screen keyboard's prompt */
bool timed_message(app *a, const char *heading, const menu_row *rows,
                   int n, int fixed_w, unsigned accent);
bool result_message(app *a, const char *heading, const char *text);
kb_result keyboard_prompt(app *a, const char *title, char *buf, int cap);
/* MENU pressed in a screen over the one asking: every screen closing */
extern bool g_menu_closing;
/* The menus' measures: a row's height, the air inside a panel, the margin
 * around it and its top */
int menu_row_h(void);
int menu_pad(void);
/* The air between the panel and the screen edge, on every side. */
#define MENU_MARGIN 24
/* ...but the top, where the status corner stands (DESIGN.md 11.12) on every
 * screen a panel opens over - the shelf, Now Playing, a paused game: a panel
 * starts a grid step under the corner's row, and a list that would reach
 * higher scrolls in its window instead. Centered as before when it is short
 * enough; only a tall one moves down. */
#define MENU_TOP    96
/* The height a panel can have between the two. */
#define MENU_AVAIL_H (ZYPEROS_SCREEN_H - MENU_TOP - MENU_MARGIN)
/* A question on a panel already open at `lv`, its rows `rows`, Yes on
 * `yes_label`: true on Yes */
bool confirm_rows(app *a, const char *heading, menu_row *rows, int nq,
                  const char *yes_label, int fixed_w, int lv);
/* `src` wrapped to `w` in `f`, a line a row of `out`; how many lines */
#define CHV_WRAP_COLS  96
int wrap_text(TTF_Font *f, const char *src, int w,
              char out[][CHV_WRAP_COLS], int max);
/* The roles a row of menu_draw_ex's `vcolors` names. VCOL_EARNED is
 * the value in **accent-ink**, lime as a mark: an earned achievement. On the
 * focus fill it is **on-accent**, like every other value. */
#define VCOL_EARNED  1u
/* The row is a one-line message (menu_message): its label in **text-soft**,
 * as the mock draws one, whatever `live` says. `live` stays the caller's,
 * since menu_row_moves reads it to decide whether a long message scrolls. */
#define VCOL_MESSAGE 2u
/* A menu's panel with each row's value role (`vcolors`, NULL for none),
 * `visits_all` when the cursor reaches every row, and the row a synopsis
 * loops at (0 none) */
void menu_draw_ex(app *a, const char *heading, const menu_row *rows,
                  int n, int sel, int fixed_w, unsigned accent,
                  const unsigned *vcolors, bool visits_all, int loop_at);
/* confirm_rows over a screen's own backdrop (`over`, `ctx`) */
bool confirm_rows_over(app *a, const char *heading, menu_row *rows, int nq,
                       const char *yes_label, int fixed_w, int lv,
                       const menu_style *over, void *ctx);
/* A screen that is a base for panels (Muse's shelf, Storage): its level,
 * and its own context while it is up */
int  panel_base(void);
extern void (*g_base_page)(app *, void *);
extern void *g_base_ctx;
/* A panel leaving over the shelf, or over a game (`game`) */
bool panel_leave_draw(app *a, int game);
/* --shot-at's panel level (with shot_motion's, main.c) */
extern int shot_lv;
/* What a message says when the network a screen needs is not there, one
 * sentence in every screen (DESIGN.md 12). A row that is dead for the same
 * reason says "needs Wi-Fi" in its value instead. */
#define MSG_NEEDS_WIFI T(S_PANEL_NEEDS_WIFI)
/* The panel pass (main.c): a panel in its open or close drawn through a
 * texture, scaled about its middle */
bool panel_pass_begin(app *a);
void panel_pass_end(app *a, const SDL_Rect *panel, mm_pose pose);
SDL_FRect panel_scaled(const SDL_Rect *q, float s);
/* The marquees' clocks (main.c's mq_phase): one a slot, each keyed on what
 * it scrolls */
/* Two note slots because a panel can carry two footer notes and both may be
 * long enough to move. Sharing one would be the bug this enum exists to
 * prevent, a step smaller: each would see the other's key every frame and
 * neither would ever get past the opening hold. */
#define MQ_NOTES 2
/* MQ_FIXED is for rows that move because nothing can ever select them, as
 * distinct from the ones that move because something just did.
 *
 * The other slots are keyed on the cursor, which is right when the row that
 * scrolls is the row you landed on: a new selection is a new subject and its
 * text should start from the beginning. It is wrong for a fact. The game
 * screen's Genre row is unselectable and scrolls whenever it is long, and
 * stepping between Synopsis and Cheevos was restarting it - a cursor move that
 * has nothing to do with the row it interrupted. Reported 2026-09-17, the day
 * Genre became the first long permanently-unselectable row on a screen that
 * also has a cursor.
 *
 * Keyed on the SCREEN instead - its heading and how many rows it has - which
 * is the same value for every such row in a frame, so they still share one
 * clock and still move together rather than resetting each other.
 *
 * It replaced a slot of its own for VALUES, which was the wrong axis to split
 * on: a label and a value never move in the same row - the wider of the two
 * takes the room and the other is laid out around it - so what needed separate
 * clocks was never the two columns. It was the selected row against the rest,
 * and only one row is ever selected. */
enum { MQ_SHELF, MQ_MENU, MQ_FIXED, MQ_NOTE0, MQ_NOTE1, MQ_VSCROLL,
       MQ_HEAD, MQ_NP, MQ_SONG, MQ_SLOTS };
unsigned mq_phase(int who, int key_a, int key_b);
unsigned mq_row_phase(bool selected, const char *heading, int n, int sel);
/* The last menu panel and its body (main.c) */
extern SDL_Rect g_menu_panel, g_menu_body;
/* A marquee's clock started again (a screen that has just opened); and the
 * screens over the one on top closed by MENU, which stops there (main.c) */
void mq_reset(int who);
void panel_return(void);
/* A result over Muse over a paused game stopped by that menu's power
 * rule: every screen above unwinds (main.c, beside the runner) */
extern bool g_message_stopped;
/* What Muse is open over: NULL the shelf, else that menu's style and its
 * ctx; g_muse_gone when its power rule ended what was underneath */
extern const menu_style *g_muse_over;
extern void             *g_muse_over_ctx;
extern bool              g_muse_gone;

/* SELECT: Muse, over whatever is on screen */
bool muse_open(app *a, const menu_style *over, void *ctx);
/* POWER, Auto Off, the battery empty */
void power_off(app *a);
/* Into the shelf of the system under the cursor */
void enter_system(app *a);

#endif
