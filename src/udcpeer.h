/* SPDX-License-Identifier: GPL-3.0-only */
/* A computer on the USB cable, from the gadget controller's state alone, on
 * a device where nothing else can tell: the R36S (RK_PEER_FROM_UDC in
 * src/device/r36s.c). Its PHY's otg-port is disabled in every tree it has
 * (no ID pin, no B-valid, so no extcon answer), and its RK817 reads no
 * power from the OTG port, so plat_usb_peer said "nothing" with a Mac on the
 * cable and File Transfer said "No computer found" whatever happened.
 *
 * The controller's state (/sys/class/udc/<udc>/state) is what a host does to
 * the gadget: a host's reset makes it "default", its address "addressed",
 * its configuration "configured". So:
 *
 * - "configured" or "addressed": a computer;
 * - "suspended": a computer for UDC_PEER_ASLEEP_MS if one took the gadget
 *   since the cable was last empty, then nothing until the bus wakes. The
 *   B session is the driver's override on the R36S, so a cable pulled out
 *   reads exactly as a computer asleep (the bus idle, D+ held up by the
 *   gadget's own pull-up): without the limit an unplug was never seen and
 *   the next plug was no new plug. A computer that wakes takes it again
 *   ("configured"), a new plug;
 * - "default", "powered", "attached" (a reset or power, no address yet): a
 *   computer only for UDC_PEER_WAIT_MS after one took the gadget (a host
 *   enumerating it again), else nothing. Never a computer on its own, for
 *   ever: a charger whose data lines are shorted can hold the controller
 *   there;
 * - "not attached", or unreadable: nothing, and what was seen is forgotten.
 *
 * Pure: the state's text and the clock in, the answer out, with a sentence
 * for the log whenever the state or the answer changes. tools/udcpeer-check.c
 * walks it. */
#ifndef UDCPEER_H
#define UDCPEER_H

#include <stdbool.h>

#include "usbpeer.h"

/* How long a reset or power with no address yet stays a computer that was
 * one: a host addresses a gadget within a second of its reset */
#define UDC_PEER_WAIT_MS 3000u
/* How long an idle bus after a computer stays that computer: a link in use
 * is never idle this long, an unplug is idle for ever */
#define UDC_PEER_ASLEEP_MS 10000u

typedef struct {
	int      kind;          /* the last state read, UDC_K_* in udcpeer.c */
	char     text[24];      /* and its text, its line end off ("" unreadable) */
	unsigned since;         /* when it began */
	bool     took;          /* a computer took the gadget since the cable was empty */
	usb_peer said;          /* the last answer */
	bool     started;
} udc_peer;

/* The answer for the state's text now (NULL: unreadable). *why is set to a
 * sentence when the state or the answer changed since the last call, NULL
 * otherwise. */
usb_peer udc_peer_step(udc_peer *p, const char *state, unsigned now_ms, const char **why);

#endif
