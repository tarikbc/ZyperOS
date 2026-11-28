/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SEEKSTEP_H
#define ZYPEROS_SEEKSTEP_H

#include <stdbool.h>

/* How far one step of MP4's seek goes (left and right on Now Playing and on
 * MP4's shelf): ten seconds a tap; held, a step on every repeat of the
 * launcher's clock (src/keyrepeat.h: 90 ms after 300 ms), growing with
 * repeat_stage as before - ten seconds, a minute, five minutes - but never
 * more than a share of the track a second: a sixteenth for the first second
 * held, a tenth to the third, a sixth after. So a five-minute song takes
 * about seven seconds of holding to cross, and a step is never under a
 * second.
 *
 * A seventeen-hour book is as it was (Eric's, 2026-09-29: about twenty
 * seconds to cross), its shares far above the steps. A song was not: at ten
 * seconds a repeat it crossed a minute a second from the first repeat and a
 * minute a step after one second held, end to end in under two seconds
 * (1.1.1-rc.1: "Seek is too fast"). `len` is the track's length in seconds,
 * 0 when not known yet: the steps as they are then. Pure, for the check. */

#define SEEK_TAP_S 10.0

double seek_step_for(bool tap, unsigned held_ms, double len);

#endif
