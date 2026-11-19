/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_DLQ_H
#define ZYPEROS_DLQ_H

#include <stdbool.h>

#include "net.h"

/* The downloads' queue: a lane per server, side by side.
 *
 * Box art, names and video snaps come from ScreenScraper, libretro and
 * SteamGridDB (src/artscrape.c's run); achievement sets and badges from
 * RetroAchievements (src/radl.c's run). Each run was a page's: it went one
 * step a frame while its page was open and stopped the moment the page
 * closed, so a library took its whole time with a page held open, and the
 * second run waited for the first.
 *
 * Now each run is a LANE here. A lane has its own network slot (net.h), so
 * its requests go beside the other lane's and beside every screen's, at its
 * own server's pace; and it is stepped from behind every screen - from
 * tick_theme as a screen draws and from the input wait while it rests - so a
 * page can close and its run goes on (B, "Continue in background", the same
 * words and behaviour as System Update's download). While a game runs the
 * lanes wait (my pick in the downloads lab, 2026-11-19: no frame of a game
 * is spent on them; a game's tick could step them later, once what that
 * costs is measured). POWER stops them, as it stops everything; Auto Off
 * waits for them (long_task_busy asks the runs themselves).
 *
 * What a lane does is its run's own code, unchanged: the queue only steps
 * it, on its slot, and tells it when to stop. The rules every lane keeps
 * live here once: the card's room is looked at every DLQ_ROOM_MS and a lane
 * that would write the card below DLQ_FLOOR is stopped (DLQ_FULL). */

typedef enum { DLQ_ART, DLQ_RA, DLQ_LANES } dlq_lane;

/* How a lane's last run ended */
typedef enum {
	DLQ_NONE,       /* none has ended */
	DLQ_ENDED,      /* on its own: done, or its run's own reason (its status) */
	DLQ_STOPPED,    /* stopped: Stop, POWER, or a run that could not go on */
	DLQ_FULL,       /* the card fell below DLQ_FLOOR */
} dlq_end;

/* What a lane is: its run's own functions, called on its slot */
typedef struct {
	const char *name;              /* for the log: "box art", "achievements" */
	net_slot slot;
	/* One unit of work: 1 still going, 0 finished, -1 could not go on */
	int  (*step)(unsigned now_ms);
	/* Once, as the run ends by any road, with how: the run keeps what a
	 * page will show and lets its state go (it cancels itself when it was
	 * stopped). Called on the lane's slot. */
	void (*done)(dlq_end how);
	/* How far it has got, 0 to 100, for the status corner's ring */
	int  (*pct)(void);
	/* At most one step this often, however many places tick */
	unsigned every_ms;
	/* The card it writes, for the room rule; NULL for none */
	const char *card;
} dlq_lane_def;

#ifndef DLQ_FLOOR
#define DLQ_FLOOR   (100ULL * 1000 * 1000)   /* the Scraper's floor, for every lane */
#endif
#ifndef DLQ_ROOM_MS
#define DLQ_ROOM_MS 2000u
#endif

/* Start `l` with `d` (kept: a static of the caller's): `begin` is called on
 * the lane's slot and says whether its run began. False when it did not, or
 * the lane is already running. */
bool dlq_start(dlq_lane l, const dlq_lane_def *d, bool (*begin)(void));
/* Stop one lane now: its done(DLQ_STOPPED), its request dropped */
void dlq_stop(dlq_lane l);
/* Every lane: POWER, the launcher quitting */
void dlq_stop_all(void);

bool dlq_running(dlq_lane l);
bool dlq_busy(void);           /* any lane running */
/* How the lane's last run ended, and a count of its ends, so a page that
 * opens can tell a run that ended while it was closed from one it showed */
dlq_end  dlq_last(dlq_lane l);
unsigned dlq_ends(dlq_lane l);

/* Step the running lanes, each at most once per its every_ms. From
 * tick_theme (every screen, as it draws; not while a button is held, so
 * scrolling never pays for a step), from the input wait (dlq_wait), and from
 * a page that shows a run, every frame. Nothing while a game runs. */
void dlq_tick(void);
/* The input wait's hook (plat_on_input_wait): a tick, and a wait cut to
 * 16 ms while a lane runs, so a resting screen keeps the lanes going */
unsigned dlq_wait(unsigned ms);

/* A game is running, or is over: the lanes wait while one runs */
void dlq_game(bool running);
bool dlq_in_game(void);
/* The developer switch dl.in_game (the device database, off unless set):
 * a game's tick (on_game_tick, every 100 ms) steps the lanes anyway, at
 * most DLQ_GAME_STEPS steps or `budget_ms` of this process's time a tick.
 * For the device test that decides whether a run may go on in games
 * (~/Documents/ZyperOS/downloads-plan.md, 10b: "If impact is low, continue
 * in games too"); nothing else sets it. */
void dlq_game_tick(unsigned budget_ms);
#ifndef DLQ_GAME_STEPS
#define DLQ_GAME_STEPS 8
#endif

/* The page that shows a lane's run is open: it takes the run's end itself,
 * and the status corner leaves that lane to it (as System Update's page
 * does its download) */
void dlq_page(dlq_lane l, bool open);
bool dlq_page_open(dlq_lane l);
/* A lane runs whose page is not open: the status corner's download mark */
bool dlq_behind(void);
/* The running lanes' progress together, 0 to 100, for the corner's ring;
 * one lane's, 0 when it is not running */
int  dlq_pct(void);
int  dlq_lane_pct(dlq_lane l);

#endif
