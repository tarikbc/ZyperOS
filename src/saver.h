/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SAVER_H
#define ZYPEROS_SAVER_H

#include <stdbool.h>
#include <stdint.h>

#include "idle.h"

/* The screensaver (DESIGN.md 11.37): after a while with nothing pressed on the
 * shelf, the video snaps of random games play over the whole screen, one after
 * another, each with its game's name and system. START boots the game on
 * screen, as A on its card would; any other button goes back to the shelf as
 * it was.
 *
 * *(A request, 2026-11-12: "a screensaver that plays videos from random
 * games in the collection; if you press Start on one of the videos, that game
 * boots".)*
 *
 * This file is the policy, with no SDL and no device, so tools/saver-check.c
 * holds it: when it starts and what keeps it from starting, when it ends on
 * its own, what is shown next and for how long, and how each clip fades. What
 * it needs of the launcher is in src/main.c (saver_*), which gathers the
 * inputs - the buttons, the charger, the battery - and draws.
 *
 * THE SCREENSAVER NEVER OUTLIVES AUTO OFF. It is a mode of the shelf's loop,
 * and every pass of that loop asks idle_due before it asks anything here, so
 * a device left alone powers off at Auto Off's time with the screensaver on
 * as it would with the shelf (src/idle.h); on the charger Auto Off's time
 * turns the screen off (src/chargedark.h), which ends the screensaver too.
 * It adds no hold of its own: nothing here touches Auto Off's clock.
 *
 * ITS POWER COST IS KEPT LOW by what it does not do. It plays the snaps
 * muted (no sound thread is even opened for a muted snap), draws a frame only
 * when the daemon has put out a new picture and while a fade moves, and shows
 * a still cover as one drawn frame. It does not start under 20% on battery
 * (SAVER_MIN_BATTERY), and on battery it ends by itself after
 * SAVER_BATTERY_MAX_MS and waits for a press, so an hour of video is never
 * what a forgotten handheld does with its last charge. */

/* The delay: seconds with nothing pressed on the shelf, 0 for never, stepped
 * round the way Auto Off's is (30 s, 1, 2, 5 and 10 minutes). One minute to
 * start with: shorter than Auto Off's two, so a device on battery shows it
 * before it powers off. */
#define SAVER_DEFAULT_S 60
int saver_step(int seconds, int dir);   /* the next rung, clamped at both ends */

/* ---- when it starts and when it ends --------------------------------------- */

#define SAVER_MIN_BATTERY      20          /* percent: not started under it on battery */
#define SAVER_BATTERY_MAX_MS   (10u * 60u * 1000u)

typedef struct {
	idle_clock clock;      /* its own countdown, Auto Off's arithmetic */
	bool       spent;      /* it ended on its own: it waits for a press */
} saver_start;

/* Too low a battery for it: off the cable and under SAVER_MIN_BATTERY. A
 * battery that cannot be read (pct < 0) is no reason. */
bool saver_low_battery(int pct, bool charging);

/* One pass of the shelf's loop: true when the screensaver should start now.
 * `delay_s` is the setting. `input` is any button at all. `held` is anything
 * that makes the device not idle - music playing, File Transfer on, a long
 * task running (src/taskhold.h), a battery too low (saver_low_battery) -
 * and it HOLDS the countdown as the charger holds Auto Off's, so the whole
 * delay passes after it ends. The charger is not one: the screensaver is
 * for a device that is plugged in too. Passes come at most a second apart
 * (src/idle.h: a longer gap restarts the countdown, which is how a menu or a
 * game in between restarts it). */
bool saver_due(saver_start *s, unsigned now_ms, int delay_s, bool input, bool held);

/* It ended on its own (the battery's cap, a long task starting): no restart
 * until a button is pressed. */
void saver_spend(saver_start *s);

/* Whether a running screensaver must end though nothing was pressed:
 * something that makes the device not idle began (`held`), the battery fell
 * too low, or - off the cable - it has run SAVER_BATTERY_MAX_MS. `started` is
 * when it began. */
typedef enum { SAVER_GO_ON, SAVER_END_HELD, SAVER_END_BATTERY, SAVER_END_CAP } saver_end;
saver_end saver_must_end(unsigned now_ms, unsigned started_ms, bool held, int pct,
                         bool charging);

/* ---- what is shown, and for how long --------------------------------------- */

/* A clip fades up from black over SAVER_IN_MS (out), is shown for
 * SAVER_VIDEO_MS (a video; it loops if it is shorter) or SAVER_STILL_MS (a
 * cover), and fades to black over SAVER_OUT_MS (linear), then the next is
 * asked for. A clip whose first picture does not come in SAVER_LOAD_MS is
 * skipped. */
#define SAVER_IN_MS     400u
#define SAVER_OUT_MS    300u
#define SAVER_VIDEO_MS  15000u
#define SAVER_STILL_MS  8000u
#define SAVER_LOAD_MS   4000u

typedef enum { SHOW_LOAD, SHOW_IN, SHOW_ON, SHOW_OUT } show_phase;

typedef struct {
	show_phase phase;
	unsigned   t;          /* when the phase began */
	unsigned   on_ms;      /* how long this clip is shown */
} saver_show;

/* A clip is asked for: nothing is on screen until it is ready. */
void saver_show_begin(saver_show *s, unsigned now_ms, bool still);

/* One frame. `ready` says the clip's first picture (a cover's texture) is
 * there. *alpha is how far it has faded up, 0 to 1. True when it is time to
 * ask for the next clip, once. */
bool saver_show_step(saver_show *s, unsigned now_ms, bool ready, float *alpha);

/* Whether a frame is wanted though no new picture came: a fade is moving. */
bool saver_show_moving(const saver_show *s);

/* ---- which one ------------------------------------------------------------- */

/* The last few shown, so the next is another: indexes into the pool. */
#define SAVER_RECENT 12
typedef struct { int ring[SAVER_RECENT]; int n; } saver_recent;

void saver_recent_clear(saver_recent *r);
void saver_recent_add(saver_recent *r, int idx);

/* One of `n` pool entries for the random number `r`, not one of the last
 * SAVER_RECENT shown while there is another to give (and never the very last
 * when n > 1). -1 when n <= 0. */
int saver_pick(int n, uint32_t r, const saver_recent *recent);

#endif
