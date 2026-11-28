/* SPDX-License-Identifier: GPL-3.0-only */
/* A computer from the gadget controller's state (udcpeer.h) */
#include "udcpeer.h"

#include <string.h>

enum { UDC_K_NONE, UDC_K_WAIT, UDC_K_ADDRESSED, UDC_K_CONFIGURED, UDC_K_SUSPENDED, UDC_K_UNREAD };

static bool starts(const char *s, const char *word)
{
	return !strncmp(s, word, strlen(word));
}

static int kind_of(const char *s)
{
	if (!s) return UDC_K_UNREAD;
	if (starts(s, "configured")) return UDC_K_CONFIGURED;
	if (starts(s, "addressed")) return UDC_K_ADDRESSED;
	if (starts(s, "suspended")) return UDC_K_SUSPENDED;
	if (starts(s, "not attached")) return UDC_K_NONE;
	if (starts(s, "default") || starts(s, "powered") || starts(s, "attached") ||
	    starts(s, "reconnecting") || starts(s, "unauthenticated"))
		return UDC_K_WAIT;
	return UDC_K_UNREAD;
}

usb_peer udc_peer_step(udc_peer *p, const char *state, unsigned now, const char **why)
{
	int k = kind_of(state);
	char text[sizeof p->text];
	bool changed;
	const char *says;
	usb_peer peer;

	/* The state's own words, for a change within a kind too ("default" to
	 * "powered"): each one said in the log */
	text[0] = '\0';
	if (state) {
		size_t n = strcspn(state, "\n");

		if (n >= sizeof text) n = sizeof text - 1;
		memcpy(text, state, n);
		text[n] = '\0';
	}
	changed = !p->started || strcmp(text, p->text) != 0;
	if (changed) memcpy(p->text, text, sizeof text);
	if (!p->started || k != p->kind) {
		p->kind = k;
		p->since = now;
	}
	switch (k) {
	case UDC_K_CONFIGURED:
		p->took = true;
		peer = USB_PEER_COMPUTER;
		says = "a computer configured the gadget: a computer";
		break;
	case UDC_K_ADDRESSED:
		p->took = true;
		peer = USB_PEER_COMPUTER;
		says = "a computer gave the gadget its address: a computer";
		break;
	case UDC_K_SUSPENDED:
		peer = p->took ? USB_PEER_COMPUTER : USB_PEER_NONE;
		says = p->took ? "the bus asleep after a computer took the gadget: still a computer"
		               : "the bus asleep, and no computer took the gadget: nothing";
		break;
	case UDC_K_WAIT:
		if (p->took && now - p->since < UDC_PEER_WAIT_MS) {
			peer = USB_PEER_COMPUTER;
			says = "a reset or power after a computer took the gadget: a computer, while it asks again";
		} else {
			if (p->took && now - p->since >= UDC_PEER_WAIT_MS) p->took = false;
			peer = USB_PEER_NONE;
			says = "a reset or power and no address: not a computer (a charger, or a host that does not answer)";
		}
		break;
	case UDC_K_NONE:
		p->took = false;
		peer = USB_PEER_NONE;
		says = "nothing attached: nothing";
		break;
	default:
		p->took = false;
		peer = USB_PEER_NONE;
		says = "the state unreadable: nothing";
		break;
	}
	if (!p->started || peer != p->said) changed = true;
	p->started = true;
	p->said = peer;
	if (why) *why = changed ? says : NULL;
	return peer;
}
