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
	LH_NO_NETWORK      /* Wi-Fi: not on a network */
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
	int           sub;        /* within LS_LINK: 0 no computer, 1 not taken, 2 no driver */
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

/* ---- what the device's files say, for plat_cable_sense ------------------ */

/* /sys/class/udc/<udc>/state: "configured" */
bool link_udc_configured(const char *state_text);

/* /proc/net/arp's text: a complete entry (flags 0x2) on `ifname`, its
 * address into `ip` (may be NULL). */
bool link_arp_complete(const char *arp_text, const char *ifname, char *ip, size_t ipn);

#endif
