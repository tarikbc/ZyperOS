/* SPDX-License-Identifier: GPL-3.0-only */
/* File Transfer's link, step by step, as the handheld sees it (DESIGN.md
 * 11.35).
 *
 * A computer that never opens the page is the commonest File Transfer
 * problem, and the handheld can see each step from its side: the cable (the
 * computer took the USB device and opened its network: the controller says
 * "configured", then usb0's carrier comes on when the computer's driver
 * selects the data interface), the address (the computer answers at its
 * address on usb0: a complete neighbour entry), and the browser (the first
 * request to the transfer server). Over Wi-Fi only two of them are the
 * handheld's to see: it is on a network, and a browser asked.
 *
 * The screen shows the steps until a browser has asked, and one short hint
 * under them when a step stalls, saying what to do about that step. Once a
 * browser asked it is the usual screen; if the cable or the address goes,
 * the steps come back.
 *
 * Pure: the device's facts and the clock come in, the step and the hint go
 * out. tools/linksteps-check.c walks it through every way it can go. */
#ifndef LINKSTEPS_H
#define LINKSTEPS_H

#include <stdbool.h>
#include <stddef.h>

/* What the device can tell about the cable, read by the platform
 * (plat_cable_sense, usbpeer.h). */
typedef struct {
	bool computer;     /* a computer's data port is on the cable */
	bool configured;   /* the computer took the USB device */
	bool carrier;      /* its network driver opened the link */
	bool address;      /* it answers at its address over the link */
	bool waits;        /* something on the cable waits for power the handheld
	                    * does not give: a computer whose port took the sink
	                    * side (usbpower.h) */
} link_sense;

/* The steps, in order; LS_DONE once all are */
enum { LS_LINK, LS_ADDRESS, LS_BROWSER, LS_DONE };

/* A hint for the step that stalled, or none */
enum {
	LH_NONE,
	LH_NO_COMPUTER,    /* the cable is in, no computer on it: a charging cable */
	LH_NO_ANSWER,      /* a computer, but it did not take the device */
	LH_NO_DRIVER,      /* it took the device and opened no network on it */
	LH_NO_ADDRESS,     /* the network is up and it has no address on it */
	LH_NO_BROWSER,     /* all of that, and no browser yet */
	LH_NO_NETWORK,     /* Wi-Fi: not on a network */
	LH_REPLUG          /* the computer waits for power: plug it in again */
};

/* How long a step waits before its hint: long enough that a computer that
 * is merely slow is not told it is broken (Windows installs a driver the
 * first time, macOS takes a second or two for an address), short enough to
 * be there when somebody looks up from the computer wondering. */
#define LH_LINK_MS     10000u
#define LH_ADDRESS_MS  10000u
#define LH_BROWSER_MS  30000u
#define LH_PLUG_MS      5000u

typedef struct {
	int           step;       /* the first step not done */
	int           sub;        /* within LS_LINK: 0 no computer, 1 not taken, 2 no driver,
	                           * 3 no Wi-Fi, 4 the computer waits for power */
	unsigned      since;      /* when it began waiting at that step (and sub) */
	unsigned long base;       /* requests counted when the link last came up */
	bool          started;
} link_steps;

/* Over the cable: the facts, the transfer server's request count (it only
 * grows), and the clock. A request since the link came up is a browser,
 * and an address too, whatever the neighbour table says. */
void link_steps_cable(link_steps *s, const link_sense *in, unsigned long requests,
                      unsigned now_ms);

/* Over Wi-Fi: on a network (joined), and the request count. */
void link_steps_wifi(link_steps *s, bool joined, unsigned long requests, unsigned now_ms);

/* The hint the screen shows now, LH_NONE before a step has waited its time. */
int link_steps_hint(const link_steps *s, unsigned now_ms);

/* ---- a fresh link as File Transfer opens (plat_cable_reconnect) -------- */

/* The controller lets go of the cable and takes it again, so the computer
 * sees the handheld plugged in afresh: for a computer that dropped the
 * handheld while the controller here went on saying "configured" and never
 * offered itself again (a Mac, 2026-10-08; only a restart brought it back).
 * Only for that: every File Transfer did it before, over a link in use too,
 * and the Pixel 2's controller can hang in the toggle (hwput.h, 2026-11-21).
 *
 * So not for a link bound a moment ago (LINK_FRESH_MS: the computer is
 * meeting it now), and for any other link that is not whole (not
 * "configured", or no carrier: there is nothing to keep); and for one that
 * looks whole, only when the computer does not answer at its address over
 * it (an ARP request, link_arp_request: what a computer that dropped the
 * handheld cannot do, and what one with the link in use does at once,
 * whatever its firewall says). `state_tells`: the controller's state is
 * worth reading (the TrimUI Brick's says "configured" through unplugs and
 * chargers, so there only the carrier and the answer count). */
#define LINK_FRESH_MS 5000u

/* Whether to look for the computer's answer first: a link that is whole */
bool link_fresh_ask(bool just_bound, bool state_tells, bool configured, bool carrier);

/* Whether to make it fresh, with the answer when it was asked for */
bool link_wants_fresh(bool just_bound, bool state_tells, bool configured, bool carrier,
                      bool answered);

/* The ARP request "who has `tip`, tell `sip`" from `smac` (addresses in
 * network order), the 28 bytes that follow the Ethernet header */
#define LINK_ARP_LEN 28
void link_arp_request(unsigned char out[LINK_ARP_LEN], const unsigned char smac[6],
                      unsigned sip, unsigned tip);

/* Whether `p` (`n` bytes after the Ethernet header) is the reply from `tip` */
bool link_arp_answer(const unsigned char *p, size_t n, unsigned tip);

/* ---- what the device's files say, for plat_cable_sense ------------------ */

/* /sys/class/udc/<udc>/state: "configured" */
bool link_udc_configured(const char *state_text);

/* /proc/net/arp's text: a complete entry (flags 0x2) on `ifname`, its
 * address into `ip` (may be NULL). */
bool link_arp_complete(const char *arp_text, const char *ifname, char *ip, size_t ipn);

#endif
