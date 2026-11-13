/* SPDX-License-Identifier: GPL-3.0-only */
/* A phone's USB tethering on the Pixel 2's USB port (DESIGN.md 11.36).
 *
 * A phone on a USB-C to USB-C cable takes the side that waits for power at
 * every plug, and the handheld, with no Type-C controller, cannot make it
 * swap. So Phone powers it (usbrole's host), and the phone joins the
 * handheld's USB bus as a device (2026-11-13: an AYN Thor as 18d1:4e11, an
 * iPhone as 05ac:12a8 "iPhone"). Its USB tethering then makes it a USB
 * network device - RNDIS on most Android phones, CDC NCM or ECM on some -
 * whose driver (rndis_host, cdc_ncm, cdc_ether; plastron builds them as
 * modules) usbrole loads by the phone's alias. The handheld takes an address
 * from the phone's DHCP server on that link, and USB File Transfer opens on
 * it, for the phone's own browser. An iPhone shares its network only with a
 * computer it trusts (paired, through Apple's usbmuxd), which the handheld
 * cannot be: it gets a message instead.
 *
 * The rules, apart from the device files so tools/phonelink-check.c reaches
 * them: which network interface is a phone's, its name, and which device on
 * the bus is an iPhone. The platform reads the files (usbpeer.h). */
#ifndef PHONELINK_H
#define PHONELINK_H

#include <stdbool.h>

/* The name the phone's interface is given. The kernel names a USB network
 * device "usb0" when that is free, and USB File Transfer's own link (the
 * NCM gadget, registered at its first bind) needs "usb0": every part of it
 * looks for that name (plastron's S40usbgadget, the launcher's link). So the
 * phone's is renamed while it is still down, before anything uses it. */
#define PHONELINK_IF "phone0"

/* A network interface's driver (the basename of its device/driver link) is
 * a phone's tethering: rndis_host (Android's RNDIS), cdc_ncm and cdc_ether
 * (CDC NCM and ECM), ipheth (an iPhone's, which needs it paired first) */
bool phonelink_driver(const char *driver);

/* The interface to use, from `n` interfaces' names and drivers: the one
 * already called PHONELINK_IF, else the first whose driver is a phone's;
 * -1 for none */
int phonelink_pick(const char *const *names, const char *const *drivers, int n);

/* The device on the bus, by its idVendor and product strings, is an iPhone
 * or an iPad: Apple's (05ac) and not a Mac (a Mac whose port took the side
 * that waits for power shows up as "Mac", which usbrole stops feeding) */
bool phonelink_is_iphone(const char *vendor, const char *product);

/* Opened once per link: true when the link `ifname` at `ip` is new since
 * the last call that said true, so File Transfer opens on it by itself once,
 * and not again after the player closes it while the same phone stays.
 * A link gone (ip NULL or empty) forgets the last one. `last` is the
 * caller's own, "" at first. */
bool phonelink_new(char last[64], const char *ifname, const char *ip);

#endif
