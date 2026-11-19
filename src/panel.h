/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_PANEL_H
#define ZYPEROS_PANEL_H

/* The menus' engine, which is src/main.c's, for the screens split from it:
 * the runner's results and style, a panel's open, frames and close, and
 * what a screen with a panel calls every frame. Each is described in
 * main.c. */
#include "app.h"
#include "dslang.h"
#include "pacing.h"
#include "game_menu.h"
#include "hare.h"
#include "heat.h"
#include "hotspot.h"
#include "keyboard.h"
#include "menu.h"
#include "menumotion.h"
#include "menusnap.h"
#include "noticeq.h"
#include "p2.h"
#include "platform.h"
#include "rahash.h"
#include "sys_menu.h"
#include "texload.h"
#include "tour.h"
#include "uisound.h"
#include "wifi.h"

#include <stdbool.h>
#include <sys/stat.h>

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
	/* Each row's choices by place (menu.h's menu_choice): more than two
	 * and A opens their list, two and A turns it over, the runner sending
	 * the row's own Left and Right to apply it. Read every frame through
	 * the pointer, so the build may write it. NULL: no option rows. */
	const struct menu_choice *choices;
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
	/* A footer kept still under the rows (a MENU_RULE and only notes past
	 * it) where they do not all fit: the rows above the rule scroll in what
	 * room is left, as a card's body over its pinned footer. An option's
	 * list sets it, so what the choice under the cursor does stays in view
	 * (menu_pick); elsewhere a long list's footer goes round with it. */
	bool     pin_foot;
	/* Every row reachable by the cursor, dead ones too, where `live` says
	 * how a row reads rather than whether it can be chosen: the
	 * Achievements tab's list, whose unearned rows are quieter and still
	 * open with A (menu_row_moves's visits_all) */
	bool     visits_all;
	/* A picture at each row's left, by place: the Achievements tab's
	 * badges (main.c's g_row_icon while the panel is drawn). NULL: none. */
	SDL_Texture *(*row_icon)(int row);
	/* Told the cursor's row each pass, after the keys and before the
	 * frame is drawn, so an option's list shows the choice under it
	 * (src/ds/hover.h) in the frame the cursor reaches it. NULL: none. */
	void   (*at)(app *a, void *ctx, int sel);
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
void result_preview(app *a, const char *heading, const char *text);
kb_result keyboard_prompt(app *a, const char *title, char *buf, int cap);
/* MENU pressed in a screen over the one asking: every screen closing */
extern bool g_menu_closing;
/* The menus' measures: a row's height, the air inside a panel, the margin
 * around it and its top */
int menu_row_h(void);
int menu_pad(void);
/* The widest content a panel can have (11.3): the screen less the margin
 * and the padding on both sides */
int menu_most_w(void);
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
/* Where the corner and the games title's capitals were drawn this frame,
 * for a shot to hold the corner clear of them (main.c) */
extern SDL_Rect g_corner_box, g_title_caps;
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

/* ---- what src/shots.c calls of the launcher's; each is described where it
 * is defined ---- */
/* What --shot-at presses at 0 in a --menu shot (shot_motion): a step onto
 * its row; or with --open, --close and --swap, the top menu opening on the
 * shelf, closing onto it, or giving way to MENU > Controls. `shot_lv` is the
 * level the menu motion's frames are drawn at, 0 when there is none, and
 * `shot_pressed` says the press has come. */
enum { SHOT_STEP, SHOT_OPEN, SHOT_CLOSE, SHOT_SWAP };
extern bool g_loads_held;
extern bool g_full_shot;
extern bool shot_usb_said;
extern bool shot_usb_offer;
extern SDL_Rect g_clock_box;
extern int g_pct_natural;
extern int shot_press;
extern bool shot_pressed;
extern unsigned shot_t0;
extern const char *const TOUR_STEP_NAMES[TOUR_STEPS];
unsigned notice_look(nq_kind kind);
void menu_message(app *a, const char *heading, menu_row row,
                  unsigned accent);
void zyperos_menu_draw(app *a, int sel);

/* ---- what src/menu_runner.c calls of the launcher's; each is described where it
 * is defined ---- */
extern menu_snap *g_snap_next;
extern menu_snap *g_snap;
extern uis_steps g_steps;
void wifi_pref_save(bool on);
void snap_record(menu_snap *s, int kind, const char *heading,
                 const menu_row *rows, int n, int sel, int fixed_w,
                 unsigned accent, const unsigned *vcolors,
                 bool visits_all, int loop_at, const char *heading_note);
void snap_draw(app *a, mm_pose pose);
int snap_shown(void);
int menu_step_heard(app *a, const menu_row *rows, int n, int sel,
                    int dir, in_button b);
int panel_settle(int lv);
void panel_close_at(app *a, int lv, bool cut, unsigned now);

/* ---- what src/scraper_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
/* What the next panel drawn is to be, which panel_frame says and panel_show
 * puts back at rest as it takes it: a panel drawn by anything else, a message
 * or a shot, is drawn at rest. */
#define PANEL_REST ((mm_frame){ { 1.0f, 0.0f }, { 1.0f, 1.0f }, 1.0f, false })
extern const systems_cfg *g_snap_sys;
extern bool g_snap_run_off;
bool es_art_cover(const char *dir, const char *file, const char *stem);
bool es_art_video(const char *dir, const char *file, const char *stem);
void tiles_panel(app *a, int kind, const char *heading, const menu_row *tiles,
                 const int *marks, int n, int sel, mm_pose pose);

/* ---- what src/xfer_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
bool up_running(void);
unsigned pace_rows(const char *heading, const menu_row *rows, int n, int sel, float k,
                   unsigned tint);
bool pace_busy(const app *a);
void ram_idle_set(bool idle);
const char *tiles_hints(const char *b_word);

/* ---- what src/usbdrive_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
extern bool g_usb_kept;
extern bool g_usb_offer_due;
extern bool g_tiles_hints;
void usb_offer_count(void);
void tiles_show(app *a, int kind, const char *heading, const menu_row *rows, int n,
                const int *marks, int nmarks, int sel);
bool confirm_rows_as(app *a, const char *heading, menu_row *rows, int nq,
                     const char *yes_label, const char *no_label, int fixed_w, int lv,
                     usbpower_guard *guard);
const char *list_hints(const char *b_word);
void ask_settle(app *a, usbpower_guard *g);
void ask_guard_start(app *a, usbpower_guard *g);

/* ---- what src/about_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
#ifndef ZYPEROS_VERSION
#define ZYPEROS_VERSION "0.0"       /* set by the makefiles from Makefile's VERSION */
#endif
extern mm_frame g_pframe;

/* ---- what src/tour_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
void language_apply(app *a, lang l, bool keep);

/* ---- what src/player2_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
/* Which drawer a copy is for. A question in tiles says its style by its kind
 * (tiles_show): PANEL_TILES a row of tiles, PANEL_TILES_GRID two to a row
 * (Computer Connected), PANEL_LIST rows the panel's width with a line under
 * each name (What did you plug in?, the Scraper's Settings). They were read
 * off the first tile's name before (tile_glyph), which cut "ScreenScraper". */
enum { PANEL_MENU, PANEL_HOT, PANEL_TILES, PANEL_P2, PANEL_MOVE, PANEL_TILES_GRID, PANEL_LIST };
typedef struct {
	bool     run;            /* ours runs now, as `mode`, and has the name (mdns) */
	bool     up;             /* sshlink_start or sshlink_start_net said yes */
	bool     spent;          /* the cable's SSH ran for this plug: not again until the cable is out */
	int      mode;           /* sshlink_mode it runs as: SSHLINK_CABLE or SSHLINK_NET */
	char     pw[16], ip[64]; /* the password (the cable's only) and the address to type */
	char     ifn[16];        /* over Wi-Fi: the radio's interface, for the name */
	bool     radio;          /* an 802.11 interface at the last look */
	int      nkeys;
	unsigned at, retry, addr_at;
} dv_state;
extern dv_state g_dv;
void notice_queue(nq_kind kind, int id, int ms, const char *head,
                  const char *body, const char *badge, int value, int target);
void panel_show(app *a, int kind, const char *heading,
                const menu_row *rows, int n, int sel, int fixed_w,
                unsigned accent, const unsigned *vcolors,
                bool visits_all, int loop_at, const char *heading_note);

/* ---- what src/shelf_keys.c calls of the launcher's; each is described where it
 * is defined ---- */
void shelf_into(app *a, sysview *v);
void shelf_up(app *a, sysview *v);
void game_info_screen(app *a);

/* ---- what src/shelf_draw.c calls of the launcher's; each is described where it
 * is defined ---- */
/* The battery's low and critical warnings, 10% and 5%, are src/status.h's
 * (STATUS_BATT_LOW, STATUS_BATT_CRITICAL), which the shelf's battery mark and
 * Diatom's disc over a game both go by. Critical blinks: the last minutes
 * before BATT_EMPTY_PCT ends the game and powers off, which a steady mark gave
 * no warning of. */
/* BATT_EMPTY_PCT, where the device powers off, is src/status.h's too:
 * portkeys powers off there over a port. */
#define TEX_KEEP_NEAR   8   /* card textures kept around the cursor, in view  */
extern unsigned t_boot0;
extern home_song g_home_song;
extern SDL_Texture *g_folder_icon;
extern bool g_theme_late;
void home_items(const app *a, home_item *items);
void shelf_put(void *ctx, texload_got *g);
void grid_note(app *a, int s, int i, SDL_Surface *surf, bool generated);
void texload_drain(app *a);
void prime_toward(app *a, int s, int center);
void heat_said(const char *where);
void usb_kept_save(void);
bool muse_shelf_shown(const app *a);
void draw_status(app *a);
void tick_update_daily(bool in_game);
bool usb_drive_offered(void);
bool replug_shown(void);
void dev_ssh_tick(void);
void xs_tick_behind(app *a);
void xs_end(void);
void xfer_screen_way(app *a, int way);

/* ---- what src/muse_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
#define TEX_KEEP_FAR    4   /* ...and around the cursor of a system you left  */
extern app *g_logs_app;
const char *b_legend(str_id word);
int book_place(int al, double *at);
double book_speed(int al);
void muse_play_videos(const char *const *paths, int n, int start,
                      const char *artist, const char *album);
void evict_far(sysview *v, int keep);
/* The grid's: by rows round its window, or the rows in sight alone (main.c) */
void evict_grid(sysview *v, int cols, bool in_sight);
void panel_base_close(int lv);
int shelf_letter_jump(sysview *v, int dir);

/* ---- what src/saver_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
typedef struct {
	char folder[CFG_STR];
	time_t mtime;
	unsigned looked;
	unsigned gen;                    /* snapshelf_media_gen() when listed */
	bool on;
	char **stems;
	int n;
} snap_names;
typedef struct {
	unsigned long long total, win, rate;   /* the bytes since the start, this second's, its speed */
	unsigned moved;                        /* when it last moved */
	bool active;
} xs_flow;
typedef struct {
	bool        on;             /* the server is up */
	bool        shown;          /* its screen is open */
	bool        cable, hot;
	/* On a phone's USB tethering (plat_phone_link): the cable's name, the
	 * address the phone gave, the phone's browser; no SSH, no steps of the
	 * cable's (the link is the phone's network, as Wi-Fi's is) */
	bool        phone;
	bool        hot_failed, ssh_shown;
	bool        rescan_due;     /* ended behind the shelf with the shelf changed */
	const char *name;
	char        pin[32], login[96], addr[96];
	unsigned long long in, out;
	hare_stats  st;
	hotspot_state hs;
	unsigned    addr_at, moved_at, link_lost_at, restart_at;
	int         busy;
	link_steps  steps;          /* the link, step by step (linksteps.h) */
	link_sense  sense;          /* the cable's facts, read five times a second */
	bool        sensed;         /* the device can tell them: steps over the cable */
	bool        joined;         /* Wi-Fi: on a network */
	/* Someone came in: a browser entered the PIN (hare_signed_once) or SSH
	 * signed in. The screen is the way in until then and its news after,
	 * and stays the news when they leave (DESIGN.md 11.35) */
	bool        met;
	/* The cable's loading bar (xs_load_step): its fill as drawn, 0..1, and
	 * the moment it last moved */
	float       load_k;
	unsigned    load_ms;
	unsigned    sense_at;
	side_lights_batch lit;      /* files arriving, for the side lights */
	side_lights_move  mv;       /* files moving either way, for their wave */
	/* The panel's tiles (xs_move_*): a file's bytes each way, Download
	 * then Upload; the second they count from; the last frame's uploads
	 * and downloads, whose last bytes land in the poll that ends them */
	xs_flow     flow[2];
	unsigned    flow_at;
	bool        flow_up, flow_send;
} xfer_sess;
extern heat_state g_heat;
extern xfer_sess g_xs;
extern int g_shot_saver;
extern float g_shot_saver_alpha;
extern const char *g_shot_saver_pic;
int snap_name_cmp(const void *x, const void *y);
const snap_names *snap_names_for(const char *folder);

/* ---- what src/launch_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
#define HEAT_HEAD T(S_HEAT_HEAD)
#define HEAT_BODY T(S_HEAT_SLOWING)
extern unsigned t_back0;
extern long g_sync_game;
extern bool g_sync_flush;
extern char g_sync_rom[LIB_PATH * 2], g_sync_tag[16];
extern char g_sync_rom[LIB_PATH * 2], g_sync_tag[16];
extern int g_sync_cur;
extern char g_pending_active[LIB_PATH * 2];
extern bool g_pending_hashing;
extern unsigned g_watch_ms;
extern char g_watch_active[LIB_PATH * 2];
extern int g_idle_secs;
extern bool g_idle_charging;
extern noticeq g_nq;
void present_ground(app *a);
bool ds_resume_fresh(const system_cfg *s, const char *romfile, const char *st,
                     char *lang, size_t n);
bool core_resume_fresh(const system_cfg *s, const char *romfile, const char *st);
void ds_resume_record(const char *st, const char *lang, const struct stat *before,
                      bool had);
bool two_screens(const system_cfg *s);
void prime_home(app *a);
void pause_preview_path(char *out, size_t n);
void quick_bind(app *a, int s, const game_entry *g);
void persist_dir_ensure(app *a, int s);
bool has_box_art(const char *folder, const char *name);
void prime_window(app *a, int s);
void chv_active_path(char *out, size_t n);
void sync_begin(long game, const char *set_path);
int sync_poll(void);
void render(app *a);
void anim_launch(app *a);
bool up_read(const char *path, char *buf, size_t n);
void up_first_line(const char *path, char *buf, size_t n);
bool xs_before_game(app *a);

/* ---- what src/sysmenu_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
#define AUTO_OFF_COUNT ((int)(sizeof AUTO_OFF / sizeof AUTO_OFF[0]))
#define CHARGE_LIMITS_COUNT ((int)(sizeof CHARGE_LIMITS / sizeof CHARGE_LIMITS[0]))
/* MENU > Battery Icon: the corner's battery as the mark alone ("bars", the
 * default), with its charge written beside it ("percentage"), my ask of
 * 2026-11-05, or with the time left written there ("time"), my ask of
 * 2026-11-07. Read with the clock's setting; draw_topbar writes the text.
 * The time left is the estimate in src/battery_eta.h off the cable and the
 * time to full (or to the charge limit) on it, from the gauge's charge and
 * current at the first read. Where there is none yet but one is coming it
 * writes three dots filling in turn (2026-11-08, from a first try on the Pixel
 * 2 that kept the percentage for minutes); held at the charge limit, "On
 * hold"; full on the cable, "Charged"; and where none can come - a gauge
 * that cannot say its current on the cable - the percentage, so the corner
 * is never empty. */
enum { BATT_BARS, BATT_PCT, BATT_TIME, BATT_MODES };
/* The system menu's context. It holds the app rather than a copy of what was
 * on screen when it opened, so `a->screen` and `a->sys_cursor` are read live
 * exactly as the hand-written loop read them. Neither can change while the
 * menu is up; reading them is simply one fewer thing that could drift. */
typedef struct {
	app      *a;
	menu_bufs bufs;   /* the built rows point into this, so it outlives them */
	/* Which of the two menus to build. Normally the screen you are on, but
	 * Both has no screens to be on: there MENU means the firmware and B
	 * means this system, and neither can be inferred from where you are. */
	int       screen;
	/* MENU's cursor as the runner last had it (menu_style's sel_out) */
	int       seen;
} sysmenu_ctx;
extern int g_shader_all;
/* Auto Off's and Charge Limit's steps (main.c): sized here for their
 * counts; a step more there is a type conflict until this says so */
extern const int AUTO_OFF[6];
extern int g_saver_s;
extern const int CHARGE_LIMITS[4];
extern int g_charge_limit;
extern bool g_usb_auto;
extern unsigned g_usb_wait;
extern bool shot_usb_menu;
extern const char *const BATT_IDS[BATT_MODES];
extern int g_batt_mode;
extern char g_scraper_val[32];
/* The screens' shader, MENU's row from the home (main.c, src/screenfx.h):
 * its SHADERS index and strength, and a list's cursor's in their place */
extern int g_screen_shader, g_screen_strength, g_screen_shown, g_screen_shown_strength;
bool screen_shader_offered(int k);
int  screen_shader_step(int k, int d);
void screen_shader_load(void);
void screen_shader_set(int k);
void screen_strength_set(int pct);
void screen_shader_apply(void);
bool screen_strength_shown(void);
const char *screen_strength_label(void);
const char *core_label(const char *core, char *buf, size_t n);
void core_step(app *a, int sys, int d);
dsl_choice dslang_load(void);
pacing_choice pacing_load(const system_cfg *s);
bool pacing_row_shown(const app *a, int sys);
bool dslang_row_shown(const app *a, int sys);
const char *dslang_label(dsl_choice c, char *buf, size_t n);
void view_turn(app *a, int sys, int dir);
void home_turn(app *a, int dir);
float grid_shape_load(const app *a, int sys);
void grid_shape_save(const app *a, int sys, float shape);
void sort_save(app *a);
bool muse_books_shown(void);
int muse_rows_kind(void);
int muse_kinds(void);
void free_view_textures(sysview *v);
void muse_show(sysview *v, ml_kind kind);
void auto_off_save(int seconds);
void saver_save(int seconds);
void charge_limit_apply(void);
void clock24_save(bool on);
void usb_auto_save(bool on);
bool shot_time(time_t *t);
wifi_state menu_wifi(char *ssid, int cap);
bool xfer_link_up(void);
bool up_have(void);
bool random_pick(app *a, bool all, int *shelf, int *game);
void wifi_screen(app *a);
menu_result ra_signin_screen(app *a);
void usb_power_ask(app *a, unsigned plug);
void dev_screen(app *a);
bool xs_row_cable(void);
const char *xs_row_value(void);
void xfer_screen(app *a);
void scraper_page(app *a, const char *only);
menu_result scraper_screen(app *a);
void language_screen(app *a);
void lights_screen(app *a);
void about_screen(app *a);
void controls_screen(app *a);
menu_style zyperos_menu_style(app *a, int screen);
int sysmenu_build(void *ctx, menu_row *rows, int max,
                  const char **heading);
void album_art_screen(app *a);

/* ---- what src/ingame_screen.c calls of the launcher's; each is described where it
 * is defined ---- */
/* How panel_leave_run ended: the panel gone, or a press that ended it at
 * once, which is the same to the caller; the power button or Auto Off; or a
 * quit. */
typedef enum { LEAVE_GONE, LEAVE_POWER, LEAVE_QUIT } leave_end;
/* wrap_text and syn_layout's sizes, the synopsis's (src/ingame_screen.c) */
#define SYN_WRAP_LINES 80
/* The blank lines and the rule between the two copies of a synopsis */
#define SYN_LOOP_GAP 5
#define SYN_ROWS (SYN_WRAP_LINES * 2 + SYN_LOOP_GAP)
/* The slot carousel: Auto plus the manual slots for Load, the manual slots for
 * Save. One slot at a
 * time, large - the paused frame at a size you can actually read - with the
 * save's own timestamp under it and a dot rail for where you are. Left and
 * right cycle; Load skips slots with nothing behind them, Save cannot aim at
 * Auto, which belongs to the exit funnel alone. */
typedef struct {
	SDL_Texture *thumb[GM_SLOTS + 1];   /* [0]=Auto, [1..GM_SLOTS] */
	int have[GM_SLOTS + 1];
	char when[GM_SLOTS + 1][40];
	/* Every slot of one game holds the same machine's frame, so one aspect
	 * describes them all and the picture can be framed exactly rather than
	 * dropped into a fixed box with bars down its sides. 4:3 until a slot
	 * with a picture in it says otherwise. */
	float aspect;
	int saving;
} slot_view;
extern bool g_port_menu;
extern bool g_port_waiting;
extern const ds_rgb DIATOM_PAGE;
extern bool g_game_dual;
extern char g_pending_set[LIB_PATH * 2];
extern char g_game_set[LIB_PATH * 2];
extern ra_hash_kind g_game_no_hash;
extern bool g_shot_unchecked;
extern ra_hash_kind g_shot_no_hash;
extern bool g_in_game_menu;
extern bool g_shot_p2;
extern const char *shot_path;
extern int shot_cheevos_waiting;
void present_clear(app *a, ds_rgb c);
void drawn_forget(void);
int shader_step(int k, int d, const char *tag);
void display_save(app *a);
void shader_save(app *a, int sys);
const char *strength_row_label(const app *a, int sys);
void screens_get(const char *tag, int *layout, bool *swap);
void screens_put(const char *tag, int layout, bool swap);
int quick_slot_get(const char *tag);
void quick_slot_put(const char *tag, int slot);
bool pak_system(const system_cfg *s);
const char *pak_get(const system_cfg *s, const char *file);
void pak_put(const char *tag, const char *file, const char *value);
void slot_state_path(app *a, int s, const game_entry *g, int slot,
                     char *out, size_t n);
void slot_preview_path(app *a, int s, const game_entry *g, int slot,
                       char *out, size_t n);
sysview *owner_view(app *a);
void menu_draw_caption(app *a, const char *heading, const menu_row *rows,
                       int n, int sel, int fixed_w, unsigned accent,
                       const unsigned *vcolors, bool visits_all, int loop_at,
                       const char *heading_note);
leave_end panel_leave_run(app *a, void (*backdrop)(app *, void *, float),
                          void *ctx);
int menu_measure(const menu_row *rows, int n, const char *heading);
p2_state p2_row_state(void);
void player2_page(app *a, SDL_Texture *bg);
bool is_port(const system_cfg *s);

/* SELECT: Muse, over whatever is on screen */
bool muse_open(app *a, const menu_style *over, void *ctx);
/* POWER, Auto Off, the battery empty */
void power_off(app *a);
/* Into the shelf of the system under the cursor */
void enter_system(app *a);

#endif
