/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_RAHASHJOB_H
#define ZYPEROS_RAHASHJOB_H

#include <stdbool.h>
#include <stddef.h>

/* A first play's RetroAchievements hash (ra_hash_rom), on a thread of its
 * own, so the launcher's wait loop under a running game - which is also the
 * power button's watchdog - never waits for one.
 *
 * Most hashes are milliseconds, but since 1.0.5 some are not: a PSP disc's
 * EBOOT.BIN out of a .chd or .cso is megabytes of LZMA or zlib, and a PSP
 * .pbp is hashed whole, up to 1.8 GB off the card. The thread runs at a
 * lower priority than the game's process, and the set is fetched once it
 * has its hash (main.c's on_game_tick).
 *
 * One at a time. A job carries the set it is for, so its answer goes to
 * that set even if another game is running when it lands. */

/* Start hashing `rom` (a systems.cfg `tag`) for `set_path`. False when a job
 * is still running, or the thread could not start: nothing is asked this
 * time, and the game's next launch asks again. */
bool rhj_begin(const char *rom, const char *tag, const char *set_path);

/* 0 still hashing; 1 done, the hash in `hash` (33 bytes) and the set it is
 * for in `set` (`setn` long); -1 done without a hash, the set in `set`. Each
 * job's answer is given once; after it, -2: no job. */
int rhj_poll(char *hash, char *set, size_t setn);

#endif
