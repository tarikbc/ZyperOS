/* SPDX-License-Identifier: GPL-3.0-only */
/* See mp4lock.h. */
#include "mp4lock.h"

bool mp4lock_passes(in_button b)
{
	return b == IN_VOLUP || b == IN_VOLDN;
}

void mp4lock_engage(mp4lock *l, unsigned now, bool pair_down)
{
	l->locked = true;
	l->armed = !pair_down;
	l->held_from = 0;
	/* Lit at once, so locking is seen to happen and the way out is read. */
	l->lit_until = now + MP4_SHOW_MS;
	l->vol_until = now;
	l->now = now;
}

bool mp4lock_step(mp4lock *l, unsigned now, const bool down[IN_COUNT],
                  const bool pressed[IN_COUNT])
{
	bool a = down[MP4_UNLOCK_A], b = down[MP4_UNLOCK_B];
	int k;

	l->now = now;
	if (!l->locked) return false;
	/* A volume key, pressed or still held: the bar's whole life from now.
	 * Pressed as well as down, for a tap let go inside one pass. */
	for (k = 0; k < IN_COUNT; k++)
		if (mp4lock_passes((in_button)k) && (down[k] || pressed[k]))
			l->vol_until = now + MP4_VOLUME_LIT_MS;
	if (!a && !b) l->armed = true;
	if (!l->armed || !a || !b) {
		l->held_from = 0;
		return false;
	}
	if (!l->held_from) l->held_from = now ? now : 1;
	if (now - l->held_from >= MP4_UNLOCK_MS) {
		l->locked = false;
		l->held_from = 0;
		return true;
	}
	return false;
}

float mp4lock_progress(const mp4lock *l)
{
	float k;

	if (!l->locked || !l->held_from) return 0;
	k = (float)(l->now - l->held_from) / MP4_UNLOCK_MS;
	return k < 0 ? 0 : k > 1 ? 1 : k;
}

bool mp4lock_lit(const mp4lock *l)
{
	return l->locked && (l->held_from || (int)(l->lit_until - l->now) > 0);
}

bool mp4lock_awake(const mp4lock *l)
{
	return mp4lock_lit(l) || (l->locked && (int)(l->vol_until - l->now) > 0);
}

unsigned mp4lock_next(const mp4lock *l)
{
	unsigned next = (unsigned)-1;

	if (!l->locked) return next;
	if (l->held_from) return 0;                  /* the bar moves */
	if ((int)(l->lit_until - l->now) > 0) next = l->lit_until - l->now;
	if ((int)(l->vol_until - l->now) > 0 && l->vol_until - l->now < next)
		next = l->vol_until - l->now;
	return next;
}
