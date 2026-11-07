/* SPDX-License-Identifier: GPL-3.0-only */
#include "tween.h"
#include "design.h"

void tween_snap(tween *tw, float v)
{
	tw->from = tw->to = v;
	tw->t0 = 0;
	tw->ms = 0;
}

/* From `from` to `to`, starting at `now`. */
static void tween_go(tween *tw, float from, float to, unsigned ms, unsigned now)
{
	tw->from = from;
	tw->to = to;
	tw->t0 = now;
	tw->ms = ms;
}

void tween_to(tween *tw, float to, unsigned ms, unsigned now)
{
	if (to == tw->to) return;
	tween_go(tw, tween_at(tw, now), to, ms, now);
}

void tween_kick(tween *tw, float by, unsigned ms, unsigned now)
{
	tween_go(tw, tween_at(tw, now) + by, tw->to, ms, now);
}

float tween_at(const tween *tw, unsigned now)
{
	unsigned t = now - tw->t0;

	if (!tween_moving(tw, now)) return tw->to;
	return tw->from + (tw->to - tw->from) * ds_out((float)t / (float)tw->ms);
}

bool tween_moving(const tween *tw, unsigned now)
{
	return tw->ms > 0 && now - tw->t0 < tw->ms;
}
