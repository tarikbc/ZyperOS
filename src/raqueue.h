/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_RAQUEUE_H
#define ZYPEROS_RAQUEUE_H

/* RetroAchievements' unlocks: what the account already holds merged into the
 * local store, and what is owed sent behind the shelf, on a thread of its
 * own. Main thread only, all of them; each is described in src/raqueue.c. */

/* Mark the ids the account holds for the loaded game as earned; how many
 * were new */
int  ra_merge_unlocks(const int *ids, int n);
/* Hand what is owed to the worker, for the ROM of game `cur` just played */
void ra_flush_start(const char *rom, const char *tag, int cur);
/* Apply the worker's answers; every pass of the main loop */
void ra_flush_poll(void);
/* Tell the worker to stop, and do not wait for it */
void ra_flush_stop(void);
/* On the shelf, every pass of its loop: a network that came up with unlocks
 * waiting sends them, and one that stays up tries again every few minutes
 * after a pass that stopped short. Cheap between looks: a compare. */
void ra_flush_watch(unsigned now_ms);
/* How many unlocks that had waited - earned offline, or owed from before -
 * the account has taken since the last call, for the notice */
int  ra_flush_take_sent(void);

#endif
