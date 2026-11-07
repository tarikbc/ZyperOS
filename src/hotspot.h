/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HOTSPOT_H
#define ZYPEROS_HOTSPOT_H

#include <stdbool.h>

/* Wi-Fi File Transfer's hotspot: when the handheld is on no network and a
 * Wi-Fi adapter is in, the adapter becomes an access point for as long as the
 * transfer screen is open, and a phone joins it by the QR code on the screen.
 *
 *   hostapd       the access point, WPA2 with a password made each time
 *                 (plastron's, over nl80211)
 *   udhcpd        an address for each phone in CAPTIVE_ADDR's /24, the
 *                 handheld as its router and its DNS (busybox's)
 *   this file     the DNS: every name is the handheld, so a phone's internet
 *                 check reaches hare, which answers with the transfer page,
 *                 and the phone opens it in its sign-in sheet (captive.h)
 *
 * The supplicant is stopped for it and comes back as it was when the screen
 * closes, so the Wi-Fi the player had is the Wi-Fi they get back.
 *
 * Like wifi.c, a client of programs the system already has, started with
 * fork/execv and never through a shell. On the host build every call fails
 * quietly and hotspot_available is false. */

typedef enum {
	HOTSPOT_OFF,
	HOTSPOT_STARTING,   /* hostapd is bringing the access point up */
	HOTSPOT_UP,         /* phones can join */
	HOTSPOT_FAILED      /* it did not come up; hotspot_stop cleans up */
} hotspot_state;

/* An adapter is in and the system has hostapd. Two stats, no fork: safe to
 * ask every time a menu is built. */
bool hotspot_available(void);

/* Stops the supplicant, makes the name, password and QR text, writes the
 * configurations and starts hostapd. Takes as long as the supplicant takes to
 * stop, up to two seconds; the rest happens in hotspot_poll. False when it
 * could not even start. */
bool hotspot_start(void);

/* Every frame while the screen is open: moves the start along, answers the
 * DNS questions waiting, and notices hostapd going away. Bounded work. */
hotspot_state hotspot_poll(void);

/* Everything off, and the supplicant back if it was running before. Blocks
 * while it comes back (wifi_up), a second or two. Safe to call in any state. */
void hotspot_stop(void);

/* What the screen shows. Empty strings until hotspot_start has run. */
const char *hotspot_ssid(void);
const char *hotspot_password(void);
const char *hotspot_qr_text(void);    /* the WIFI: form (captive_wifi_qr) */
const char *hotspot_addr(void);       /* CAPTIVE_ADDR */

/* Phones joined, by hostapd's count, or -1 until it has answered. */
int hotspot_phones(void);

#endif
