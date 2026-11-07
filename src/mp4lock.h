/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_MP4LOCK_H
#define ZYPEROS_MP4LOCK_H

#include <stdbool.h>

#include "design.h"
#include "inbutton.h"

/* MP4's lock screen, the rules alone: what a press does while locked.
 *
 * mine, 2026-11-05: while locked every button is blocked, POWER included,
 * and one clear gesture unlocks - SELECT and START held together for
 * hold-unlock (MP4_UNLOCK_MS). Muse's pocket lock before it let power and
 * volume through (Eric's call, 2026-09-30: powering off is far more common
 * than pocketing a handheld). A pocket presses power as easily as anything
 * else, so now it does not get through; what cannot be stopped is the power
 * chip's own cut-off, about ten seconds of holding POWER, which no software
 * sees.
 *
 * THE VOLUME KEYS GET THROUGH, mine, 2026-11-06: "allow volume change
 * while locked". The same steps and the same bar as on every other screen
 * (src/osdbar.h). A volume key in a pocket changes how loud it is and
 * nothing else, which is the one press a locked player is worth answering:
 * turning it down without unlocking. With music the screen is dark, so a
 * volume press lights it for as long as the bar shows - hold-bar after the
 * last press and the bar's fade-out after that (MP4_VOLUME_LIT_MS) - and a
 * key held keeps it lit; then it goes dark again. What it lights is the lock
 * screen, with the bar over it: nothing else gets through. Over a video the
 * picture is on anyway, so a volume press brings no lock panel over it,
 * only the bar (mp4lock_lit says the panel, mp4lock_awake the light).
 *
 * Two keys held together for two whole seconds, which a pocket or a bag
 * almost never does - it presses one key at a time. I picked the pair
 * (SELECT + START over L1 + R1), 2026-11-05. The pair has to be
 * let go once after locking before it counts, so a hold that runs on past
 * the lock does not unlock it again.
 *
 * DARK UNTIL UNLOCKED, also his pick: a press of anything but the volume
 * lights nothing. The lock screen shows for a moment when it locks
 * (MP4_SHOW_MS), so locking is seen to happen and the way out is read once,
 * and then only while the pair is held, with the bar filling, or while the
 * volume bar shows. Pure: no clock and no input of its own, so
 * tools/mp4lock-check.c can walk it. */

#define MP4_UNLOCK_MS     ((unsigned)DS_MS_HOLD_UNLOCK)
#define MP4_SHOW_MS       ((unsigned)DS_MS_HOLD_LOCK_LIT)
#define MP4_VOLUME_LIT_MS ((unsigned)(DS_MS_HOLD_BAR + DS_MS_FADE_OUT))
#define MP4_UNLOCK_A      IN_SELECT
#define MP4_UNLOCK_B      IN_START

typedef struct {
	bool     locked;
	bool     armed;        /* the pair has been let go since locking */
	unsigned held_from;    /* both down together since then; 0 not */
	unsigned lit_until;    /* the lock screen is lit until then */
	unsigned vol_until;    /* the screen is on for the volume bar until then */
	unsigned now;
} mp4lock;

/* The buttons that get through the lock: the volume keys, and only them.
 * The caller acts on these as it would unlocked, and on nothing else. */
bool mp4lock_passes(in_button b);

/* Lock, at `now`. `pair_down` is whether SELECT and START are both down right
 * now, which makes the pair count only after it is let go. */
void mp4lock_engage(mp4lock *l, unsigned now, bool pair_down);

/* One pass while locked: what is down and what was pressed this pass, all
 * buttons, as the input layer reports them. True when this pass unlocked. */
bool mp4lock_step(mp4lock *l, unsigned now, const bool down[IN_COUNT],
                  const bool pressed[IN_COUNT]);

/* How far the unlock hold has got, 0 to 1; 0 with no hold. */
float mp4lock_progress(const mp4lock *l);

/* Whether the lock panel should show now: as it locks, and while the pair is
 * held. */
bool mp4lock_lit(const mp4lock *l);

/* Whether the screen should be on now, with no video to keep it on: the lock
 * panel showing, or the volume bar. */
bool mp4lock_awake(const mp4lock *l);

/* How long until mp4lock_lit or mp4lock_awake changes by itself, in ms, or
 * (unsigned)-1 when neither will - for the caller's next wake. */
unsigned mp4lock_next(const mp4lock *l);

#endif
