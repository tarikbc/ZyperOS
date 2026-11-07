/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
#ifndef ZYPEROS_PANEL_H
#define ZYPEROS_PANEL_H

/* The menus' engine, which is src/main.c's, for the screens split from it:
 * the runner's results and style, a panel's open, frames and close, and
 * what a screen with a panel calls every frame. Each is described in
 * main.c. */
#include "app.h"
#include "menu.h"
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
} menu_style;

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

/* A menu's panel: `n` rows, `sel` focused, `fixed_w` wide (0 measures) */
void menu_draw(app *a, const char *heading, const menu_row *rows, int n,
               int sel, int fixed_w, unsigned accent);
/* The width the launcher's menus share, and how many rows of a list fit
 * around a screen's fixed rows, rules and notes */
int  menu_std_width(app *a);
int  menu_list_fit(int fixed, int rules, int notes);
/* One line on a panel over the shelf, before something that blocks */
void wait_panel(app *a, const char *heading, const char *msg);

/* SELECT: Muse, over whatever is on screen */
bool muse_open(app *a, const menu_style *over, void *ctx);
/* POWER, Auto Off, the battery empty */
void power_off(app *a);
/* Into the shelf of the system under the cursor */
void enter_system(app *a);

#endif
