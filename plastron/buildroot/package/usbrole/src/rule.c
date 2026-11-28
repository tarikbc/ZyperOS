/* SPDX-License-Identifier: GPL-3.0-or-later */
/* See rule.h. */
#include "rule.h"

#include <string.h>

static unsigned nz(unsigned t) { return t ? t : 1; }

int ur_grant(ur_state *s, unsigned plug)
{
	if (!plug || plug != s->plug || !s->can_ask)
		return 0;
	s->granted = plug;
	s->paused = 0;          /* the player said: a pause has done its work */
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
	/* A computer on the bus: this plug is a computer's until it ends, and
	 * Automatic pauses until the port has rested empty */
	if (in->computer)
		s->computer = s->paused = 1;
	if (in->pin == 0 && in->vbus == 0) {
		if (!s->empty_since) s->empty_since = nz(now);
		if (s->paused && now - s->empty_since >= UR_REST_MS) s->paused = 0;
		else if (s->paused) *again = UR_REST_MS - (now - s->empty_since);
	} else {
		s->empty_since = 0;
	}
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
	/* Power Automatically: once the plug has waited its settle, unless a
	 * computer paused it; then the plug is asked about */
	if (s->auto_mode && !s->paused) {
		if (now - s->since >= UR_SETTLE_MS) {
			s->granted = s->plug;
			s->host = 1;
			return UR_HOST;
		}
		*again = UR_SETTLE_MS - (now - s->since);
	}
	return UR_DEVICE;
}

const char *ur_fixed_role(const char *text)
{
	for (const char *l = text; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		if (!strncmp(l, "ROLE=host", 9) && (!l[9] || l[9] == '\n'))
			return "host";
		if (!strncmp(l, "ROLE=device", 11) && (!l[11] || l[11] == '\n'))
			return "device";
	}
	return NULL;
}

/* The keys whose values move without a fact changing */
static int moving(const char *key, unsigned long len)
{
	static const char *const part[] = {
		"_NOW", "_AVG", "CAPACITY", "TEMP", "CHARGE_COUNTER", "CHARGE_FULL", "CHARGE_EMPTY",
		"ENERGY", "TIME_TO", "CYCLE_COUNT", "_MAX", "_MIN",
	};
	char k[64];

	if (len >= sizeof k) return 0;
	memcpy(k, key, len);
	k[len] = '\0';
	for (unsigned i = 0; i < sizeof part / sizeof part[0]; i++)
		if (strstr(k, part[i])) return 1;
	return 0;
}

void ur_uevent_line(const char *uevent, char *out, unsigned long n)
{
	unsigned long used = 0;

	if (!n) return;
	out[0] = '\0';
	for (const char *l = uevent; l && *l; l = strchr(l, '\n') ? strchr(l, '\n') + 1 : NULL) {
		const char *end = strchr(l, '\n'), *eq;
		unsigned long len = end ? (unsigned long)(end - l) : strlen(l);

		if (!len) continue;
		if (!strncmp(l, "POWER_SUPPLY_", 13)) { l += 13; len -= 13; }
		eq = memchr(l, '=', len);
		if (!eq || moving(l, (unsigned long)(eq - l))) continue;
		if (used && used + 1 < n) out[used++] = ' ';
		for (unsigned long i = 0; i < len && used + 1 < n; i++) out[used++] = l[i];
		out[used] = '\0';
	}
}
