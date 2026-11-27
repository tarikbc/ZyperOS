/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_INGAME_SCREEN_H
#define ZYPEROS_INGAME_SCREEN_H

/* The in-game menu (DESIGN.md 11.4): the menu over a paused game or a port,
 * its tabs (Game, Display, Controls, Achievements), the save slots' strip,
 * the Controls page, the achievement list and card, and a game's synopsis.
 * The screens are src/ingame_screen.c; the rows are src/game_menu.c's. */
#include "confirmq.h"
#include "achtab.h"
#include "achtab_screen.h"
#include "app.h"
#include "controls.h"
#include "ctltab.h"
#include "ctltab_screen.h"
#include "menu_panel.h"
#include "panel.h"

typedef struct {
	int  count;
	int  index;
	char label[128];
} gm_disc_state;

typedef struct {
	int  tab;
	int  row[GT_TABS];
	bool scope_all;
} gm_mem_state;

/* The in-game menu's context. `resume` is what the epilogue turns on: whether
 * the player is going back to the game or the game is over. */
typedef struct {
	app         *a;
	SDL_Texture *bg;       /* the paused frame, as Diatom last wrote it */
	gm_bufs      bufs;
	menu_style   st;
	bool         resume;
	/* Over your PICO-8: a Display mode picked and agreed to, so the menu
	 * ends with PORT_MENU_RESTART (port_restart_ask) */
	bool         restart;
	/* ...or Reset asked and agreed to: PORT_MENU_RESET, CTRL-R after it */
	bool         reset;
	menu_tabs    tabs;     /* Game, Display, Controls; none over a port */
	int          seen;     /* the runner's cursor (menu_style.sel_out) */
	/* The Controls tab's rows' value colors and sound classes, which are
	 * more than gm_bufs holds (src/ctltab_screen.h) */
	unsigned      ct_vcol[CT_MAX_ROWS];
	unsigned char ct_sound[CT_MAX_ROWS];
	/* The Achievements tab's rows' values, colors and sounds
	 * (src/achtab_screen.h) */
	achtab_bufs   ach;
} gm_ctx;

/* What main.c uses of it; each is described where it is defined */
void paused_backdrop(app *a, SDL_Texture *bg, float k);
extern gm_disc_state g_gm_disc;
extern int g_shot_discs;
extern bool g_shot_undo;
extern bool g_shot_rumble;
extern const char *g_shot_disc_label;
extern gm_mem_state g_gm_mem;
extern SDL_Renderer *g_badge_r;
extern ctl_game g_port_ctl;
/* The program's game, for the Controls list over your PICO-8 (port_menu_main):
 * its file as its shelf names it (in the shelf's folder; SPLORE_FILE for
 * Splore) and its name, for Reset Buttons' question */
extern const char *g_port_file, *g_port_title;
extern int shot_achtab;
extern bool g_menu_unasked;
void slot_when(char *out, size_t n, const struct tm *tm);
void slot_draw(app *a, const slot_view *sv, int sel);
void gm_gather(app *a, gm_ui *out);
void gm_mem_reset(void);
const char *gm_tab_name_i(int i);
/* An achievement's card, opened with A from its list (src/ingame_screen.c):
 * its rows, made once, and its draw, the badge in the panel's header. Shared
 * with --cheevo-card. CHV_CARD_LINES is the description's bytes, more lines
 * than any description can make, so nothing is cut. */
#define CHV_CARD_LINES CHV_DESC
#define CHV_CARD_BADGE (12 * DS_GRID)
typedef struct {
	char lines[CHV_CARD_LINES][CHV_WRAP_COLS];
	menu_row rows[CHV_CARD_LINES + 3];
	unsigned vcols[CHV_CARD_LINES + 3];
	char pts[24];
	int n;
} cheevo_card;
int cheevo_card_rows(const cheevo *c, bool pending, int fixed, cheevo_card *k);
void cheevo_card_draw(app *a, const cheevo_card *k, const char *title,
                      SDL_Texture *badge, int fixed);

int syn_layout(const char *text, int fixed,
               char (*lines)[CHV_WRAP_COLS], menu_row *rows,
               int *loop_at);
void synopsis_screen(app *a, const char *title, const char *text,
                     unsigned accent);
void cheevos_screen(app *a, SDL_Texture *bg, bool over_shelf);
bool ctl_head_fits(void *ctx, const char *head);
ctl_game game_controls_of(app *a);
int controls_page_rows(const ctl_game *g, int page, int width, ctl_buf *b,
                       const menu_row **out, int *loop_at);
int game_controls_width(app *a, const ctl_game *g);
int gm_width(app *a);
void gm_at_facts(at_facts *f);
at_source gm_at_source(void);
void gm_ctl_game(app *a, gm_ctx *c, ctlscr_game *g);
void gm_backdrop(app *a, void *ctx, float k);
/* The paused frame the Display list's picture draws (NULL: the stand-in),
 * and a shot's --gm-source */
void gm_preview_frame(SDL_Texture *bg);
extern int    shot_gm_src_w, shot_gm_src_h;
extern double shot_gm_aspect;
void game_menu(app *a);
/* Reset Game's hotkey: its question over the paused game, then RESET or
 * not and back to the game (PAUSED for=reset) */
void game_reset_ask(app *a);
/* MENU's lock chord over a game Diatom paused for it (PAUSED for=lock):
 * locked as on every screen, then the game again (src/menukeys.h) */
void game_lock(app *a);
/* Next Shader (`shader`) or Next Display, the hotkeys, while the game runs:
 * stepped, kept and sent; the name now on into out, for the pop-up */
void gm_hotkey_step(app *a, bool shader, char *out, size_t n);
int port_menu(app *a);
/* Over your PICO-8 (src/portmenu.h's port_menu_display): the question before
 * it starts again, as Reset Game's hotkey asks, over `over`'s backdrop with
 * its power rule (NULL: the program's frame, POWER ending it), the cursor on
 * the left answer: question `q` (CQ_P8_RESTART_DISPLAY or _BUTTONS), its
 * words `line` (cq_text). The answer, as confirm_ask_ex says it: CQ_RIGHT is
 * Start Again. */
int port_restart_ask(app *a, const menu_style *over, void *ctx, cq_id q, const char *line);
/* Display mode `k` kept for the program's system (display.<TAG>) and, with
 * it, written to PORT_RESTART_FILE for launch.sh to start it again in. The
 * mode on already for a start again for another reason (new buttons).
 * False when the file could not be written. */
bool port_restart_write(app *a, int k);
/* --display (PORT_MENU_DISPLAY_ARG): Next Display over your PICO-8, with no
 * menu: the next mode, round, and the question over the frame.
 * PORT_MENU_RESTART for Start Again, PORT_MENU_POWER for POWER, else
 * PORT_MENU_CONTINUE with nothing kept */
int port_display_next(app *a);
/* --reset (PORT_MENU_RESET_ARG): Reset Game's hotkey over your PICO-8, with
 * no menu: Reset Game's question over the frame, as over a game
 * (game_reset_ask). PORT_MENU_RESET for Reset, PORT_MENU_POWER for POWER,
 * else PORT_MENU_CONTINUE */
int port_reset_ask(app *a);
/* PICO-8's 128 x 128 screen cut out of `frame` (the panel as the menu found
 * it) where Display mode `k` put it: the Display list's picture over it, and
 * its shot's. A texture to destroy, or NULL */
SDL_Texture *port_screen_of(app *a, SDL_Texture *frame, int k);

/* The in-game menu's option rows' choices by gm_row, for the state `u`
 * (menu.h's menu_choice): what gm_menu_build gives the runner */
const menu_choice *gm_choices(app *a, const gm_ui *u);

/* --rows-report: the in-game menu's Game and Display tabs (menu_rows_print) */
void gm_rows_report(app *a);

#endif
