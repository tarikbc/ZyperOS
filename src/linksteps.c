/* SPDX-License-Identifier: GPL-3.0-only */
/* See linksteps.h for the steps and why these are the handheld's to see. */
#include "linksteps.h"

#include <stdio.h>
#include <string.h>

/* At a step (and, at the link, which part of it): the clock starts again
 * only when it changes */
static void at(link_steps *s, int step, int sub, unsigned now)
{
	if (s->started && s->step == step && s->sub == sub) return;
	s->step = step;
	s->sub = sub;
	s->since = now;
	s->started = true;
}

void link_steps_cable(link_steps *s, const link_sense *in, unsigned long requests,
                      unsigned now)
{
	if (!in->computer || !in->configured || !in->carrier) {
		/* Down: whatever asked before was over the link that went, so the
		 * next browser is counted from here */
		s->base = requests;
		at(s, LS_LINK, !in->computer ? (in->waits ? 4 : 0) : !in->configured ? 1 : 2, now);
		return;
	}
	/* A request over this link: a browser, so an address too */
	if (requests > s->base) {
		at(s, LS_DONE, 0, now);
		return;
	}
	/* Done stays done while the link holds: a neighbour entry that goes
	 * stale while the page sits idle is no reason to show the steps again */
	if (s->started && s->step == LS_DONE) return;
	at(s, in->address ? LS_BROWSER : LS_ADDRESS, 0, now);
}

void link_steps_wifi(link_steps *s, bool joined, unsigned long requests, unsigned now)
{
	if (!joined) {
		s->base = requests;
		at(s, LS_LINK, 3, now);
		return;
	}
	if (requests > s->base) {
		at(s, LS_DONE, 0, now);
		return;
	}
	if (s->started && s->step == LS_DONE) return;
	/* Over the air the computer's address is not the handheld's to see */
	at(s, LS_BROWSER, 0, now);
}

int link_steps_hint(const link_steps *s, unsigned now)
{
	unsigned waited = now - s->since;

	if (!s->started) return LH_NONE;
	switch (s->step) {
	case LS_LINK:
		switch (s->sub) {
		case 0:  return waited >= LH_PLUG_MS ? LH_NO_COMPUTER : LH_NONE;
		case 1:  return waited >= LH_LINK_MS ? LH_NO_ANSWER : LH_NONE;
		case 2:  return waited >= LH_LINK_MS ? LH_NO_DRIVER : LH_NONE;
		case 4:  return waited >= LH_PLUG_MS ? LH_REPLUG : LH_NONE;
		default: return waited >= LH_PLUG_MS ? LH_NO_NETWORK : LH_NONE;
		}
	case LS_ADDRESS: return waited >= LH_ADDRESS_MS ? LH_NO_ADDRESS : LH_NONE;
	case LS_BROWSER: return waited >= LH_BROWSER_MS ? LH_NO_BROWSER : LH_NONE;
	default:         return LH_NONE;
	}
}

bool link_udc_configured(const char *state)
{
	return state && !strncmp(state, "configured", 10);
}

bool link_arp_complete(const char *text, const char *ifname, char *ip, size_t ipn)
{
	const char *p = text;

	if (!text || !ifname) return false;
	/* The first line is the header */
	p = strchr(p, '\n');
	while (p && *++p) {
		char addr[64], mac[64], mask[64], dev[32];
		unsigned hw, flags;
		const char *eol = strchr(p, '\n');
		char line[256];
		size_t len = eol ? (size_t)(eol - p) : strlen(p);

		if (len >= sizeof line) len = sizeof line - 1;
		memcpy(line, p, len);
		line[len] = '\0';
		p = eol;
		if (sscanf(line, "%63s %x %x %63s %63s %31s", addr, &hw, &flags, mac, mask, dev) != 6)
			continue;
		if (strcmp(dev, ifname) || !(flags & 0x2) || !strcmp(mac, "00:00:00:00:00:00"))
			continue;
		if (ip && ipn) snprintf(ip, ipn, "%s", addr);
		return true;
	}
	return false;
}
