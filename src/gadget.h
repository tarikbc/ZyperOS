/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_GADGET_H
#define ZYPEROS_GADGET_H

#include <stdbool.h>

/* The USB gadget's link to a computer, asked for and never waited on.
 *
 * Opening USB File Transfer over a Pixel 2's Developer Mode link froze its
 * screen for about 10 s (2026-11-24): the gadget unbound and bound again,
 * the controller toggled, SSH started again, all on the launcher's main
 * thread before the page drew. hwput.h already keeps a write that hangs from
 * stopping the launcher; this keeps the link's work from holding the screen
 * at all. A caller says what it wants of the link (on, off, a fresh bind, a
 * fresh link as File Transfer opens) and goes on at once; the work happens in
 * hwput's child, one job at a time, and what is asked while a job runs waits
 * for its end (the last word wins, nothing is lost). The screens follow the
 * link as it comes, by its files (plat_cable_sense, the link steps,
 * linksteps.h), which is their loading state.
 *
 * gadget_pump, which every look at the cable makes (plat_usb_peer, five
 * times a second from the shelf and every screen; plat_cable_sense), reaps
 * a job that ended, says its end ("usb: the link to the computer is on"),
 * hands a new bind to the network's own bring-up (after_bind, which the
 * device runs off the main thread), and starts what was asked. A bind that
 * failed is not tried again for GADGET_RETRY_MS.
 *
 * The device's files are the configuration's, so a check or desk mode can
 * give a folder of its own (tools/usbhang-check.c, src/device/deskcable.c).
 * Pure but for those files and hwput's child. */

#define GADGET_RETRY_MS 5000u

typedef struct {
	const char *udc;          /* the controller's folder, /sys/class/udc/<name>;
	                           * NULL: the first one in udc_class */
	const char *udc_class;    /* /sys/class/udc */
	const char *gadget_udc;   /* the gadget's UDC file in configfs */
	const char *net;          /* NULL: /sys/class/net */
	const char *ifname;       /* "usb0" */
	const char *peer;         /* the computer's address on it */
	bool        state_tells;  /* its state is worth reading (linksteps.h) */
	void      (*after_bind)(void);   /* a new bind's network, off the main thread */
	unsigned  (*now_ms)(void);
} gadget_cfg;

void gadget_init(const gadget_cfg *c);

/* plat_usb_link's: 1 on, 0 off, 2 off and on again (a fresh bind, a new
 * plug), -1 only asks. Never waits. The answer is the link as it will be:
 * as asked while a job for it is on its way, the file's otherwise. */
bool gadget_link(int op);

/* plat_cable_reconnect's: File Transfer's fresh link, when the link needs it
 * (cablefresh.h), after whatever job is on its way */
void gadget_fresh(void);

/* The work: a job that ended reaped and said, what was asked started.
 * Cheap: a waitpid and, with nothing asked, nothing else. */
void gadget_pump(void);

/* Nothing asked and no job on its way */
bool gadget_idle(void);

/* The controller's state file says "configured": 1, 0, or -1 when it cannot
 * tell (no file, or a state that says nothing: state_tells) */
int gadget_configured(void);

/* The controller's folder, found once */
const char *gadget_udc_dir(void);

/* For tools/xferopen-check.py's "before": as 1.1.0 had it, each write waited
 * for where it was asked, and File Transfer's fresh link made every time */
void gadget_test_before(bool on);

#endif
