/* SPDX-License-Identifier: GPL-3.0-only */
/* The USB port's two questions, apart from platform.h so src/usblink.c and
 * its check need no SDL. The device files answer them. */
#ifndef USBPEER_H
#define USBPEER_H

#include <stdbool.h>

/* What is on the other end of the USB port, for USB's charge-only rule
 * (src/usblink.h): the PHY tells a computer's port (SDP, CDP) from a wall
 * charger (DCP) by itself, with the link unbound. UNKNOWN on a device whose
 * link is not the launcher's to bind (the Brick), which then keeps its own. */
typedef enum {
	USB_PEER_UNKNOWN = -1,
	USB_PEER_NONE,       /* nothing, or a host cable (a DAC, an adapter) */
	USB_PEER_CHARGER,    /* power only */
	USB_PEER_COMPUTER    /* a data port: the question is asked */
} usb_peer;
usb_peer plat_usb_peer(void);
/* The USB link to a computer (the gadget bound to the controller): op 1
 * binds it, 0 lets it go, 2 lets it go and binds it again (a fresh link, as
 * a new plug needs), -1 only asks. True when it is bound (after the op); a
 * device whose link is always on says true. */
bool plat_usb_link(int op);

#endif
