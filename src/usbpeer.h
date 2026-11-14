/* SPDX-License-Identifier: GPL-3.0-only */
/* The USB port's two questions, apart from platform.h so src/usblink.c and
 * its check need no SDL. The device files answer them. */
#ifndef USBPEER_H
#define USBPEER_H

#include <stdbool.h>
#include <stddef.h>

#include "linksteps.h"
#include "usbpower.h"

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
/* What the device can tell of the cable's link, for File Transfer's steps
 * (linksteps.h): the computer, the controller, usb0's carrier and the
 * computer's address there. While the computer has no address it is asked
 * for one (an ARP request, by sending it a datagram), at most once a second.
 * False on a device that cannot tell (the Brick), which shows no steps. A few
 * small reads: the screen asks five times a second. */
bool plat_cable_sense(link_sense *out);
/* plastron's usbrole and the launcher (usbpower.h): its state now, and Power
 * It for a plug. False on a device without usbrole (the Brick). */
bool plat_usb_power(usb_power *out);
bool plat_usb_power_grant(unsigned plug);
/* MENU > USB Accessories for usbrole: true for Automatic (usbpower_mode) */
bool plat_usb_power_mode(bool automatic);
/* A phone's USB tethering on the port (phonelink.h): true while a phone's
 * network interface is there, its name in `ifname` and its IPv4 address in
 * `ip` once the phone gave one (else ""). With `act`, for the plug the
 * player answered Phone, it is renamed, brought up and a lease asked for as
 * it comes, and the lease's client goes with it; without, it is only looked
 * at. False with none, and on a device whose port is never a host (the
 * Brick). */
bool plat_phone_link(bool act, char *ifname, size_t in, char *ip, size_t n);
/* The device the port powers is an iPhone (or an iPad), which shares its
 * network only with a computer it trusts */
bool plat_usb_iphone(void);

#endif
