/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_DESKCABLE_H
#define ZYPEROS_DESKCABLE_H

/* Desk mode's stand-in USB cable, for a computer to open USB File Transfer
 * as a Pixel 2 does: ZYPEROS_DESK_CABLE names a folder that stands in for
 * the handheld's files, and the platform's cable calls (src/device/brick.c's,
 * the host build's device file) come here. In the folder:
 *
 *   peer                 computer, charger or none (the port's PHY)
 *   udc/fake.udc/state   the controller's state ("configured" ...)
 *   udc/fake.udc/soft_connect
 *   gadget/UDC           the gadget's UDC: "fake.udc" when bound
 *   net/usb0/carrier     1 once the computer opened its network
 *   address              1 once it answers at its address
 *   answers              1: the computer answers an ARP request
 *
 * ZYPEROS_DESK_CABLE_DELAY_MS makes each controller write that slow (hwput's
 * child waits it out first). The writes are hwput's and the link gadget.h's,
 * the device's own code: only the files are not. Host only (mk/native.mk). */

#include <stdbool.h>
#include <stddef.h>

#include "../linksteps.h"
#include "../usbpeer.h"

bool     deskcable_on(void);
bool     deskcable_link(char *addr, size_t n);
usb_peer deskcable_peer(void);
bool     deskcable_usb_link(int op);
int      deskcable_configured(void);
bool     deskcable_sense(link_sense *o);
void     deskcable_reconnect(void);

#endif
