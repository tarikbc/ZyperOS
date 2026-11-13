/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TASKHOLD_H
#define ZYPEROS_TASKHOLD_H

#include <stdbool.h>

/* Long tasks, and Auto Off waiting for them.
 *
 * ONE RULE, in one place. A job the device was asked to do (the scraper's
 * art and video downloads, System Update's download, PortMaster's install,
 * Storage's walk of the card, File Transfer's copies, Muse's cover fetch)
 * holds Auto Off's clock the way the charger does: the countdown does not
 * run while the job does, and a whole fresh one starts when it ends. Nobody
 * has to be pressing buttons for a download to be work somebody is waiting
 * for, and a handheld that powers itself off in the middle of one throws the
 * work away (2026-11-11: a System Update, which was the first to be held).
 *
 * Before this, each job held the clock its own way or not at all: System
 * Update inside idle_due, File Transfer and the cover fetch by resetting the
 * clock from their own loops, and the scraper, PortMaster and Storage by a
 * comment saying they did not. The rule is long_task_busy (src/zyperdraw.c),
 * asked by idle_due for every screen and by the game's SETIDLE; this file is
 * its half that a loop can say, and what a check can hold without a device.
 *
 * Two ways for a job to be known, and a job picks the one that cannot leak:
 *   - a job that is a process or a state of its own (the update program, the
 *     PortMaster installer, the scraper's run) is ASKED: long_task_busy looks.
 *   - a job that is a loop of the launcher says so each pass it works,
 *     task_note, and the note is good for TASK_HOLD_MS. A loop that ends
 *     without telling anybody, by any road out, stops holding by itself: a
 *     flag set and never cleared would keep a handheld on until the battery
 *     ran flat, which is the one failure worse than the one this prevents.
 *
 * The battery running flat is not a task's to hold: battery_empty still ends
 * everything, before the clock is even looked at (idle_due).
 *
 * No SDL, no device: tools/taskhold-check.c holds it. Times are milliseconds
 * from any monotonic clock and every comparison is of a difference, so the
 * clock may wrap. */

typedef enum {
	TASK_TRANSFER,      /* File Transfer: bytes moving, a browser, an SSH session, a phone */
	TASK_COVERS,        /* Muse's album covers coming */
	TASK_STORAGE,       /* Storage's walk of the card */
	TASK_COUNT
} task_id;

/* How long a note holds. Longer than a screen's longest wait between passes
 * (a few hundred ms) and shorter than anyone would call "still going". */
#define TASK_HOLD_MS 2000u

/* The loop for `t` is working now. Cheap: a store. */
void task_note(task_id t, unsigned now_ms);

/* Whether any task was noted within TASK_HOLD_MS, and which was noted last in
 * *which (may be NULL). */
bool task_noted(unsigned now_ms, task_id *which);

/* Its name, as the log says it. */
const char *task_name(task_id t);

/* Forget every note: a check's fresh start. */
void task_reset(void);

#endif
