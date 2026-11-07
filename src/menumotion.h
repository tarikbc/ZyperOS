/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MENUMOTION_H
#define ZYPEROS_MENUMOTION_H

/* Where a menu's panel is in its open or its close (DESIGN.md 8.2), as plain
 * numbers: src/main.c draws the panel this answers through a render target,
 * scaled about its middle and faded, and the scrim under it at the strength
 * this gives. Nothing here knows a renderer or a clock, so
 * tools/menumotion-check.c holds the motion to its times.
 *
 * A panel arrives in **quick** on **out**, from MM_SCALE_FROM of its size and
 * clear to whole and opaque; it leaves in **exit** on **in**, back the same
 * way. One panel at a time: a screen opened over a panel waits for that one
 * to leave and then arrives, the scrim as it is, and going back is the same
 * the other way. Only a panel opened on a base - the shelf, Muse's shelf -
 * brings the scrim in with it, and only one closed onto a base takes it out.
 *
 * The screens with a panel are modal loops, each nested in the one under it,
 * so the open screens are a stack: levels 1 up, over the shelf at level 0.
 * What leaves is always the panel on screen, which the caller keeps a copy of
 * and says the level of (`shown`): the screen that comes next draws it
 * leaving before its own arrives - the child over its parent's panel, the
 * parent over its child's, and a base over the last one.
 *
 * Any press puts the panel where it belongs at once: an arrival at rest, a
 * leave gone. Nothing waits on the motion; it only says where things are.
 *
 * Times are milliseconds on any clock that only goes forward, compared by
 * unsigned difference, so the clock may wrap. */

#include <stdbool.h>

/* The size a panel arrives from and leaves to, as a part of its own: 96%,
 * DESIGN.md 8.2's, the mock kit's .96 (docs/design/mock/components.html). */
#define MM_SCALE_FROM 0.96f

/* How many screens deep the stack goes, the shelf included. A screen opened
 * deeper than this is drawn at rest and never moves. */
#define MM_LEVELS 16

typedef struct {
	float scale;   /* MM_SCALE_FROM to 1, about the panel's middle */
	float alpha;   /* 0 to 1 */
} mm_pose;

/* One frame of a screen with a panel. At rest: nothing before it, its panel
 * whole, the scrim whole. */
typedef struct {
	mm_pose before;   /* the panel that was on screen, leaving first; alpha 0
	                   * once it has gone, or when there was none */
	mm_pose panel;    /* this screen's own; alpha 0 until its turn */
	float   scrim;    /* the scrim's strength under them, 0 to 1 */
	bool    moving;   /* not at rest: the next frame is another picture */
} mm_frame;

/* No screens open: the shelf's loop, which is never under one. A leave still
 * on its way over the shelf goes on. */
void mm_reset(void);

/* How many screens are open, Muse's shelf counted; the level of the top one. */
int mm_depth(void);

/* A screen with a panel opens at `now`. On a base its panel and the scrim
 * arrive together, and a leave still going over the base is cut. Over a
 * panel, the panel on screen leaves first when there is one (`shown`), then
 * this one arrives with the scrim as it is. Returns its level, or 0 when the
 * stack is full: level 0 is always at rest. */
int mm_open(unsigned now, bool shown);

/* A screen with no panel of its own that panels open on, Muse's shelf. Over
 * a panel on screen (`shown`), that panel leaves over it with the scrim, as
 * over the shelf. Returns its level, for mm_close. */
int mm_base(unsigned now, bool shown);

/* Level `lv` closes at `now`, and anything left open over it with it.
 * `shown` is the level whose panel is on screen, 0 for none: this one's, or
 * one deeper that never got to leave.
 *
 * Onto a base, that panel leaves over the base with the scrim (mm_leaving).
 * Onto a panel, the panel on screen leaves and the one under arrives again;
 * when the one under is the panel on screen - this one was closed before its
 * own had come - it is already where it belongs. `all` is MENU closing every
 * screen at once (main.c's g_menu_closing): the one under is closing too, and
 * nothing arrives, so of a whole unwinding only the last close, onto the
 * base, leaves anything, and once.
 *
 * A base closing hands back to the panel under it, which comes in again as it
 * opened, with the scrim. */
void mm_close(int lv, unsigned now, int shown, bool all);

/* A press on level `lv`'s screen: its panel at rest at once, the one before
 * it gone. */
void mm_press(int lv);

/* A press that acts on level `lv`'s screen at `now` - opens something over
 * it, or closes it after a submenu it came back from - while `shown` is the
 * level of the panel on screen. As mm_press; and when `lv` was still waiting
 * for that panel to leave, the leave is over, and `lv`'s own panel, at rest,
 * is the one on screen now, so what leaves next is what the press left
 * there. Returns the level of the panel on screen once the press has acted. */
int mm_settle(int lv, unsigned now, int shown);

/* Level `lv` is the top again: the screens over it were closed with MENU
 * (`all`), and MENU stops here - Muse's menu, which MENU closes and nothing
 * under it. As after a back: the panel on screen (`shown`) leaves and `lv`'s
 * arrives, or `lv`'s is the one on screen and stays. A base brings nothing
 * back. */
void mm_return(int lv, unsigned now, int shown);

/* Level `lv`'s frame at `now`. */
mm_frame mm_at(int lv, unsigned now);

/* The panel leaving over a base, which the base draws over itself: `before`
 * and the scrim going out together. `moving` is false, and both are 0, when
 * there is none or it has gone. */
mm_frame mm_leaving(unsigned now);

/* A press over a leave: it has gone. */
void mm_leaving_cut(void);

#endif
