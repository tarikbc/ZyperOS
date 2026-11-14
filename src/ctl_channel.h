/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CTL_CHANNEL_H
#define ZYPEROS_CTL_CHANNEL_H

/* px2-open's commands on the launcher (src/ctl.h has the protocol): the
 * channel opened at the start, read by every screen's loop and a game's
 * tick. The commands are src/ctl_channel.c. */
#include "app.h"
#include "config.h"
#include "library.h"

#include <stdbool.h>

/* Where the channel is read from */
enum { CTL_AT_SHELF, CTL_AT_GAME, CTL_AT_SCREEN };
bool ctl_poll(app *a, int at);
extern app *g_ctl_app;            /* for a game's tick, which has no app */
extern bool g_ctl_shelf_pass;     /* the shelf's loop is asking: not a screen */
/* The in-game menu asked to close by a quit from the channel, its game told
 * to stop already */
extern bool g_ctl_close_menu;
/* The game Diatom was handed, for px2-open's status: from RUN to the end of
 * its wait loop, its in-game menu up or not, asked to stop by the channel */
typedef struct {
	bool on, paused, stopping;
	char tag[8], name[CFG_STR], core[CFG_STR], path[LIB_PATH * 2];
	long since;
} ctl_game_state;
extern ctl_game_state g_ctl_game;
/* The launch under way is px2-open's --fresh: no auto state this once */
extern bool g_ctl_fresh;

/* The channel opened, and shut */
void ctl_start(app *a);
void ctl_stop(void);
/* The shelf's turn: an open waiting for the game it quit, else a command */
bool ctl_shelf_turn(app *a);

#endif
