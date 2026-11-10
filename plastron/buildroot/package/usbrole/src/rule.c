/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See rule.h. */
#include "rule.h"

static unsigned nz(unsigned t) { return t ? t : 1; }

int ur_grant(ur_state *s, unsigned plug)
{
	if (!plug || plug != s->plug || !s->can_ask)
		return 0;
	s->granted = plug;
	return 1;
}

int ur_decide(ur_state *s, const ur_sense *in, unsigned now, unsigned *again)
{
	int quiet, powered_now, cond;

	*again = 0;
	if (!s->started) {
		s->started = 1;
		s->start = now;
		s->pin_was = 0;
	}
	/* The pin going to "wants power" is a new plug: a number of its own,
	 * and nothing granted for it yet */
	if (in->pin == 1 && s->pin_was != 1) {
		s->plug++;
		if (!s->plug) s->plug = 1;
	}
	if (in->pin != 1)
		s->granted = 0;
	s->pin_was = in->pin;
	if (s->host) {
		/* The port's own power is all the reading would say */
		if (in->computer) {
			s->powered = 1;
			s->host = 0;
		} else if (in->pin != 1) {
			s->host = 0;
		}
		s->quiet_since = 0;
		s->can_ask = 0;
		if (!s->host) s->granted = 0;
		return s->host ? UR_HOST : UR_DEVICE;
	}
	/* Empty long enough is a new plug: what the last one was is forgotten */
	if (s->quiet_since && now - s->quiet_since >= UR_UNPLUG_MS)
		s->powered = 0;
	quiet = in->pin == 0 && in->vbus == 0 && !in->charger && !in->computer;
	if (quiet) {
		if (!s->quiet_since) s->quiet_since = nz(now);
	} else {
		s->quiet_since = 0;
	}
	/* Unsure is powered: a reading that fails says nothing is safe */
	powered_now = in->vbus != 0 || in->charger || in->computer;
	if (powered_now)
		s->powered = 1;
	cond = in->pin == 1 && !powered_now && !s->powered;
	if (cond && now - s->start < UR_GRACE_MS) {
		*again = UR_GRACE_MS - (now - s->start);
		cond = 0;
	}
	s->can_ask = cond;
	if (!cond) {
		s->granted = 0;
		return UR_DEVICE;
	}
	if (s->granted && s->granted == s->plug) {
		s->host = 1;
		return UR_HOST;
	}
	return UR_DEVICE;
}
