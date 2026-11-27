/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CABLEFRESH_H
#define ZYPEROS_CABLEFRESH_H

#include <stdbool.h>

/* File Transfer's fresh link on a device with a USB gadget (the Pixel 2,
 * the R36S, the TrimUI Brick): plat_cable_reconnect's whole work, the rule
 * linksteps.h's link_wants_fresh and the writes hwput.h's, so the launcher
 * never waits on the controller for it.
 *
 * The controller's state and usb0's carrier are read here (files the
 * driver answers from memory); a link that looks whole is asked for the
 * computer's answer in the job's child (an ARP request over usb0, 0.4 s at
 * most), and only a link that needs it is made fresh: soft_connect
 * "disconnect", a third of a second, "connect". Nothing is waited for: the
 * screen opens at once and follows the link as it comes. */
typedef struct {
	const char *udc;          /* the controller's folder: /sys/class/udc/<udc> */
	const char *ifname;       /* the gadget's network: "usb0" */
	const char *peer;         /* the computer's address on it */
	bool        state_tells;  /* its state is worth reading (linksteps.h) */
	bool        just_bound;   /* the link was bound under LINK_FRESH_MS ago */
	const char *net;          /* NULL: /sys/class/net (a check's own folder) */
} cable_fresh_cfg;

void cable_fresh(const cable_fresh_cfg *c);

/* For the check: the computer's answer from this, in the child, in place of
 * the ARP request over usb0 (NULL: the real one) */
void cable_fresh_test_probe(bool (*answered)(void));

#endif
