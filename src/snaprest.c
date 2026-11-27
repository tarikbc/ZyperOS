/* SPDX-License-Identifier: GPL-3.0-only */
/* See snaprest.h. */
#include "snaprest.h"

#include "design.h"

void snaprest_reset(snaprest *m, uint32_t now)
{
	*m = (snaprest){ .set = true, .from = 0, .to = 0, .t = now, .ms = DS_MS_QUICK, .touched = now };
}

float snaprest_at(const snaprest *m, uint32_t now)
{
	if (!m->set) return 0;
	if (m->ms == 0 || now - m->t >= m->ms) return m->to;
	return m->from + (m->to - m->from) * ds_out((float)(now - m->t) / (float)m->ms);
}

/* To `to` from wherever it is, over `ms`: a new press retargets */
static void slide(snaprest *m, float to, uint32_t ms, uint32_t now)
{
	float here = snaprest_at(m, now);

	if (m->to == to && here == to) return;
	m->from = here;
	m->to = to;
	m->t = now;
	m->ms = ms;
}

void snaprest_touch(snaprest *m, uint32_t now)
{
	if (!m->set) snaprest_reset(m, now);
	m->touched = now;
	slide(m, 0, DS_MS_QUICK, now);
}

void snaprest_rest(snaprest *m, uint32_t now)
{
	if (!m->set) snaprest_reset(m, now);
	slide(m, 1, DS_MS_MOVE, now);
}

float snaprest_step(snaprest *m, bool playing, uint32_t now, uint32_t *wait)
{
	uint32_t w = UINT32_MAX;

	if (!m->set) snaprest_reset(m, now);
	/* The snap's start begins the rest again */
	if (playing && !m->was_playing && now > m->touched) m->touched = now;
	m->was_playing = playing;
	if (!playing && m->to > 0) slide(m, 0, DS_MS_QUICK, now);
	if (playing && m->to == 0 && now - m->touched >= DS_MS_HOLD_BAND) slide(m, 1, DS_MS_MOVE, now);
	if (now - m->t < m->ms) w = 0;
	else if (playing && m->to == 0) w = m->touched + DS_MS_HOLD_BAND - now;
	if (wait) *wait = w;
	return snaprest_at(m, now);
}
