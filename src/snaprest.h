/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_SNAPREST_H
#define ZYPEROS_SNAPREST_H

/* What makes way for a video snap at rest: the Video view's band of covers
 * and keys (DESIGN.md 11.57, src/videoview.h), and the List view's details
 * over the media (11.39, src/gamelist.h). One rule, one clock.
 *
 * The Video view's (my pick in the Library lab, 2026-11-19: "when rested
 * for more than 2s, the bottom bar goes down"), and the List view's (the
 * List View lab, 2026-11-23: "collapse the details in 2s so u can see the
 * full media"): after **hold-band** with no press on a game whose snap
 * plays, counted from the later of the last press and the snap's start, it
 * goes down in **move** (out); a press brings it back up in **quick** (out),
 * and so does the snap stopping (a move, the dark screen). A game with no
 * snap never goes down.
 *
 * Pure: the caller says whether the snap plays and when a press came; this
 * says how far down it is and when to draw again. tools/snaprest-check.c
 * holds the timing. */
#include <stdbool.h>
#include <stdint.h>

/* Where it is, 0 up to 1 down, and where it is going: its slide began at
 * `t` from `from` and takes `ms`; the last press, for the rest; and whether
 * the snap played the frame before, whose start begins the rest again */
typedef struct {
	bool     set;
	float    from, to;
	uint32_t t, ms, touched;
	bool     was_playing;
} snaprest;

/* Up, rested from `now`: a shelf opened (a frame asked through quick) */
void snaprest_reset(snaprest *m, uint32_t now);

/* A press, or a button held, or a page or a menu over it: back up in
 * quick, the rest from `now` */
void snaprest_touch(snaprest *m, uint32_t now);

/* Down now, in move, as the rest would take it after hold-band (A in the
 * Screenshots viewer, 2026-11-20: "it goes straight to full without
 * needing to wait for it to collapse"); the next press brings it back */
void snaprest_rest(snaprest *m, uint32_t now);

/* Where it is at `now`, 0 up to 1 down, eased; 0 before it was set */
float snaprest_at(const snaprest *m, uint32_t now);

/* One frame: `playing` whether the snap is in and playing. Returns how far
 * down it is at `now`, 0 to 1, eased, and puts in *wait (when not NULL)
 * when to draw again: 0 while it slides, the end of the rest while it
 * waits, UINT32_MAX for never. */
float snaprest_step(snaprest *m, bool playing, uint32_t now, uint32_t *wait);

#endif
