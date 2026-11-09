/* SPDX-License-Identifier: GPL-3.0-only */
/* Whether a game resumes from its auto state, or starts fresh because the
 * state was left by the shelf's other core.
 *
 * A shelf can list more than one core (config/systems.cfg; the Core row
 * chooses), and a save state is one core's own: Diatom refuses a state another
 * core wrote (diatom/src/save.c names the core in each state's header), and
 * the game starts from its beginning. The Nintendo 64's are the case:
 * ParallelN64, the default since 2026-11-08, and Mupen64Plus-Next before it,
 * so every N64 game left on Mupen64Plus-Next has a resume point the new
 * default cannot load. Said, rather than left to look like a lost resume: the
 * launcher starts the game fresh, its battery save (.srm, the same file for
 * both cores) keeping the progress, and says so over the game, as a DS game
 * started in a new Game Language does (src/dsresume.h).
 *
 * Only a state another core of the same shelf wrote: a state no core of the
 * shelf wrote, or one that is not a Diatom state, goes to Diatom as before,
 * which refuses it or loads it.
 *
 * Plain C, no SDL: tools/corestate-check.c holds it. */
#ifndef ZYPEROS_CORESTATE_H
#define ZYPEROS_CORESTATE_H

#include <stdbool.h>
#include <stddef.h>

/* The core a Diatom state says wrote it, as the core names itself
 * ("ParaLLEl N64", "Mupen64Plus-Next"): Diatom's header, "DIATOMST", a
 * version 1 as four bytes, the payload's length as four, then that name in
 * 64. Into `buf`; false for a file that is not there, is shorter, or is not a
 * Diatom state of version 1. */
bool cst_writer(const char *state_path, char *buf, size_t n);

/* How a core of a shelf with more than one names itself, by its file name
 * (parallel_n64 is "ParaLLEl N64"); NULL for a core not listed here. Read
 * from each core's retro_get_system_info. */
const char *cst_library(const char *core);

/* Start fresh: `writer` (cst_writer's) is the name of a core in `cores` (a
 * shelf's comma separated list) other than `running`. */
bool cst_other_core(const char *writer, const char *running, const char *cores);

#endif
