/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_RAQUEUE_H
#define ZYPEROS_RAQUEUE_H

/* RetroAchievements' unlocks: what the account already holds merged into the
 * local store, and what is owed sent behind the shelf, on a thread of its
 * own. Main thread only, all four; each is described in src/raqueue.c. */

/* Mark the ids the account holds for the loaded game as earned; how many
 * were new */
int  ra_merge_unlocks(const int *ids, int n);
/* Hand what is owed to the worker, for the ROM of game `cur` just played */
void ra_flush_start(const char *rom, const char *tag, int cur);
/* Apply the worker's answers; every pass of the main loop */
void ra_flush_poll(void);
/* Tell the worker to stop, and do not wait for it */
void ra_flush_stop(void);

#endif
