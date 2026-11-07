/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_TWEEN_H
#define ZYPEROS_TWEEN_H

#include <stdbool.h>

/* One value moving to another over a fixed time on DESIGN.md 8's **out**
 * curve (ds_out), which a new target takes over from wherever the value is
 * when it comes: 8.2's "a new press retargets from wherever the thing is
 * now; nothing queues".
 *
 * The menus' focus plate and the give of their list are two of these, in
 * **quick**. Both were chases before - an exponential toward the target,
 * twice over - because a timed slide of 150 ms never finished under the
 * d-pad's 90 ms repeat: the plate trailed a row behind a held button, and the
 * row before a wrap was never seen lit. A slide in **quick** that takes over
 * from where the plate is does finish: ds_out(90/120) is 0.999, so each move
 * lands before the next repeat comes. tools/tween-check.c holds both.
 *
 * Plain C and no SDL, so the check links this file alone. Times are
 * milliseconds on any clock; they are compared by unsigned difference, so
 * the clock may wrap. */
typedef struct {
	float    from, to;
	unsigned t0;   /* when the move began */
	unsigned ms;   /* how long it takes; 0 is there already */
} tween;

/* At `v`, and not moving. A zeroed tween is at 0. */
void tween_snap(tween *tw, float v);

/* To `to` from wherever it is at `now`. Already headed for `to`, it goes on
 * as it was: a screen that draws every frame asks again every frame, and a
 * move that started over each time would never get anywhere. */
void tween_to(tween *tw, float to, unsigned ms, unsigned now);

/* Put back by `by` from wherever it is at `now`, and on to where it was
 * going over `ms`: the menus' list when its window scrolls, whose rows are
 * laid out a row on and drawn where they were. Where it is drawn does not
 * move at the kick, whatever the kicks add up to: the new start is its place
 * at `now` plus `by`, and nothing is clipped off it, since a clip would be a
 * jump of the part clipped. */
void tween_kick(tween *tw, float by, unsigned ms, unsigned now);

/* Where it is at `now`: `from` at the start, `to` from `ms` on. */
float tween_at(const tween *tw, unsigned now);

/* Whether it is still on its way at `now`. */
bool tween_moving(const tween *tw, unsigned now);

#endif
