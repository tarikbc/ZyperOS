/* What Diatom runs now, in a file a tool on the device can read: the game,
 * its core, and, while someone asks, how fast it runs.
 *
 * /tmp/diatom-now (DIATOM_NOW for another path) is written once when a game
 * starts running and removed when it stops, so it is there exactly while a
 * game is loaded, paused in the menu or not: px2-snap (ZyperOS's
 * tools/px2-snap.sh) reads it to refuse a game that is open. It holds lines
 * of key=value:
 *
 *   pid      Diatom's process
 *   core     the core's path
 *   rom      the game's path, as RUN or --rom gave it
 *   fps      the game's own frame rate, as the core announces it
 *   seq      how many times the file was written for this game
 *   t_us     when, on Diatom's clock (diatom_port_now_us)
 *   frames   frames run since the clock started, fast ones too
 *   run_us   time since the clock started, less the menu's
 *   speed    the speed step in force (1, 1.5, 2...)
 *   dropped  audio frames the port would not take, since Diatom started
 *
 * The counters are 0 in the first writing. While a file of the same name with
 * .ask after it is there (px2-perf makes it), the file is written again every
 * quarter of a second as the game runs, so two readings a few seconds apart
 * give the speed against the clock, (frames x 1e6 / fps) / run_us, and the
 * audio dropped in between. The question costs one access() four times a
 * second; nothing more is written unless it is asked.
 *
 * Written whole to a temporary name and renamed, so a reader never sees half
 * of one. */
#ifndef DIATOM_NOW_H
#define DIATOM_NOW_H

#include <stdint.h>

/* The game is running: the file written, its counters 0 */
void diatom_now_begin(const char *core, const char *rom, double fps);

/* Once a turn, with the session's counters: written again when asked, at
 * most every 250 ms */
void diatom_now_turn(uint64_t now_us, long frames, uint64_t run_us,
                     double speed, uint64_t dropped);

/* The game has stopped, however it stopped: the file removed */
void diatom_now_end(void);

#endif /* DIATOM_NOW_H */
