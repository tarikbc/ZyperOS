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
	int cond;

	*again = 0;
	if (!s->started) {
		s->started = 1;
		s->start = now;
		s->pin_was = 0;
	}
	/* The pin going to "wants power" is a new plug: a number of its own,
	 * nothing granted and nothing known about it yet */
	if (in->pin == 1 && s->pin_was != 1) {
		s->plug++;
		if (!s->plug) s->plug = 1;
		s->granted = 0;
		s->computer = 0;
		s->since = 0;
	}
	if (in->pin != 1)
		s->granted = 0;
	s->pin_was = in->pin;
	/* A computer on the bus: this plug is a computer's until it ends */
	if (in->computer)
		s->computer = 1;
	if (s->host) {
		/* The port's own power is all the reading would say */
		if (in->computer || in->pin != 1) s->host = 0;
		s->can_ask = 0;
		s->since = 0;
		if (!s->host) s->granted = 0;
		return s->host ? UR_HOST : UR_DEVICE;
	}
	/* Only what is true now: power in, a detection or a failed reading
	 * (unsure is powered), and a computer seen during this plug */
	cond = in->pin == 1 && in->vbus == 0 && !in->charger && !s->computer;
	if (cond && now - s->start < UR_GRACE_MS) {
		*again = UR_GRACE_MS - (now - s->start);
		cond = 0;
	}
	s->can_ask = cond;
	if (!cond) {
		s->granted = 0;
		s->since = 0;
		return UR_DEVICE;
	}
	if (!s->since) s->since = nz(now);
	if (s->granted && s->granted == s->plug) {
		s->host = 1;
		return UR_HOST;
	}
	/* Power Automatically: once the plug has waited its settle */
	if (s->auto_mode) {
		if (now - s->since >= UR_SETTLE_MS) {
			s->granted = s->plug;
			s->host = 1;
			return UR_HOST;
		}
		*again = UR_SETTLE_MS - (now - s->since);
	}
	return UR_DEVICE;
}
