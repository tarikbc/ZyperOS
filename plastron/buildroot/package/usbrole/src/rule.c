/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See rule.h. */
#include "rule.h"

static unsigned nz(unsigned t) { return t ? t : 1; }

int ur_decide(ur_state *s, const ur_sense *in, unsigned now, unsigned *again)
{
	int quiet, powered_now, cond;

	*again = 0;
	if (!s->started) {
		s->started = 1;
		s->start = now;
	}
	if (s->host) {
		/* The port's own power is all the reading would say */
		if (in->computer) {
			s->powered = 1;
			s->host = 0;
		} else if (in->pin != 1) {
			s->host = 0;
		}
		s->since = 0;
		s->quiet_since = 0;
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
	if (!cond) {
		s->since = 0;
		return UR_DEVICE;
	}
	if (now - s->start < UR_GRACE_MS) {
		s->since = 0;
		*again = UR_GRACE_MS - (now - s->start);
		return UR_DEVICE;
	}
	if (!s->since)
		s->since = nz(now);
	if (now - s->since >= UR_HOLD_MS) {
		s->host = 1;
		s->since = 0;
		return UR_HOST;
	}
	*again = UR_STEP_MS;
	return UR_DEVICE;
}
