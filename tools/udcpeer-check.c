/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-udcpeer: a computer from the gadget controller's state alone
 * (src/udcpeer.h), as the R36S's plat_usb_peer reads it (RK_PEER_FROM_UDC):
 * a Mac enumerating the gadget, idle a moment, idle for good (an unplug
 * reads so on the R36S), unplugged; nothing on the cable; a
 * charger whose shorted data lines hold the controller after a reset; a
 * host enumerating it again; an unreadable state. And a sentence for the
 * log at each change of the state or the answer, and none in between. */
#include <stdio.h>
#include <string.h>

#include "../src/udcpeer.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

static const char *name(usb_peer p)
{
	return p == USB_PEER_COMPUTER ? "computer" : p == USB_PEER_CHARGER ? "charger"
	     : p == USB_PEER_NONE ? "nothing" : "unknown";
}

/* One reading: the answer `want`, and a sentence or not (`said`) */
static void step(udc_peer *p, const char *state, unsigned now, usb_peer want, int said,
                 const char *what)
{
	const char *why = NULL;
	usb_peer got = udc_peer_step(p, state, now, &why);

	CHECK(got == want, "%s: \"%s\" at %u read %s, not %s", what, state ? state : "(none)", now,
	      name(got), name(want));
	CHECK(!!why == said, "%s: \"%s\" at %u %s", what, state ? state : "(none)", now,
	      said ? "said nothing for the log" : "said something with nothing changed");
}

int main(void)
{
	udc_peer p;
	unsigned t;

	/* Nothing on the cable: nothing, said once */
	memset(&p, 0, sizeof p);
	step(&p, "not attached\n", 0, USB_PEER_NONE, 1, "empty cable");
	step(&p, "not attached\n", 200, USB_PEER_NONE, 0, "empty cable, again");

	/* A Mac takes the gadget: its reset, its address, its configuration */
	step(&p, "default\n", 1000, USB_PEER_NONE, 1, "Mac's reset");
	step(&p, "addressed\n", 1040, USB_PEER_COMPUTER, 1, "Mac's address");
	step(&p, "configured\n", 1100, USB_PEER_COMPUTER, 1, "Mac's configuration");
	step(&p, "configured\n", 9000, USB_PEER_COMPUTER, 0, "Mac's configuration, held");
	/* idle a moment, and back */
	step(&p, "suspended\n", 20000, USB_PEER_COMPUTER, 1, "Mac idle");
	step(&p, "suspended\n", 20000 + UDC_PEER_ASLEEP_MS - 1, USB_PEER_COMPUTER, 0, "Mac idle, a while");
	step(&p, "configured\n", 20000 + UDC_PEER_ASLEEP_MS - 1, USB_PEER_COMPUTER, 1, "Mac back");
	/* idle for good: unplugged (the R36S's B session is an override, so an
	 * unplug reads as an idle bus) or asleep; awake again, a new plug */
	step(&p, "suspended\n", 40000, USB_PEER_COMPUTER, 1, "cable out, or Mac asleep");
	step(&p, "suspended\n", 40000 + UDC_PEER_ASLEEP_MS, USB_PEER_NONE, 1, "idle too long: nothing");
	step(&p, "suspended\n", 90000, USB_PEER_NONE, 0, "idle, still");
	step(&p, "configured\n", 91000, USB_PEER_COMPUTER, 1, "Mac awake or plugged again");
	/* enumerating it again (a fresh link): a computer while it asks again */
	step(&p, "default\n", 92000, USB_PEER_COMPUTER, 1, "Mac's reset again");
	step(&p, "default\n", 92000 + UDC_PEER_WAIT_MS - 1, USB_PEER_COMPUTER, 0, "Mac's reset, waiting");
	step(&p, "addressed\n", 92000 + UDC_PEER_WAIT_MS - 1, USB_PEER_COMPUTER, 1, "Mac's address again");
	/* unplugged */
	step(&p, "not attached\n", 100000, USB_PEER_NONE, 1, "Mac unplugged");
	/* asleep with no computer before: nothing */
	step(&p, "suspended\n", 101000, USB_PEER_NONE, 1, "bus idle, nobody");

	/* A charger with its data lines shorted: a reset and no address, for
	 * ever. Never a computer. */
	memset(&p, 0, sizeof p);
	step(&p, "not attached", 0, USB_PEER_NONE, 1, "charger, before");
	step(&p, "default", 500, USB_PEER_NONE, 1, "charger's reset");
	for (t = 600; t < 60000; t += 250)
		step(&p, "default", t, USB_PEER_NONE, 0, "charger, held");
	step(&p, "powered", 60000, USB_PEER_NONE, 1, "charger, powered");

	/* A computer that took it, then a reset it never answers: a computer
	 * for the wait, then not, and said when it turns */
	memset(&p, 0, sizeof p);
	step(&p, "configured", 0, USB_PEER_COMPUTER, 1, "host");
	step(&p, "default", 1000, USB_PEER_COMPUTER, 1, "host's reset");
	step(&p, "default", 1000 + UDC_PEER_WAIT_MS - 1, USB_PEER_COMPUTER, 0, "host's reset, waiting");
	step(&p, "default", 1000 + UDC_PEER_WAIT_MS, USB_PEER_NONE, 1, "host's reset, given up");
	step(&p, "default", 1000 + 10 * UDC_PEER_WAIT_MS, USB_PEER_NONE, 0, "host's reset, still");
	/* and once given up, asleep is not a computer either */
	step(&p, "suspended", 40000, USB_PEER_NONE, 1, "given up, asleep");

	/* Power with no reset (B-valid), and the state unreadable */
	memset(&p, 0, sizeof p);
	step(&p, "attached", 0, USB_PEER_NONE, 1, "power only");
	step(&p, NULL, 100, USB_PEER_NONE, 1, "unreadable");
	step(&p, "configured", 200, USB_PEER_COMPUTER, 1, "after unreadable");
	step(&p, NULL, 300, USB_PEER_NONE, 1, "unreadable, forgets");
	step(&p, "suspended", 400, USB_PEER_NONE, 1, "unreadable forgot the computer");

	if (fails) {
		fprintf(stderr, "udcpeer: %d failed\n", fails);
		return 1;
	}
	printf("ok: a computer only once one addressed or configured the gadget, idle %u ms after it, "
	       "and for %u ms of a reset after it; a charger's reset never; each change said\n",
	       UDC_PEER_ASLEEP_MS, UDC_PEER_WAIT_MS);
	return 0;
}
