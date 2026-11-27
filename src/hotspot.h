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

/* The same, for a caller that does not want this file to answer the DNS.
 * hotspot_start is hotspot_start_dns(true): File Transfer, where the launcher
 * answers the DNS in its frame loop. A game's hotspot (NES Player 2) is
 * hotspot_start_dns(false): p2d answers the DNS there (its --dns), and only
 * one owner at a time may answer on UDP 53, so here no socket is opened and
 * hotspot_poll does no DNS work. Everything else is the same: hostapd, udhcpd
 * with the handheld as router and DNS, the supplicant stopped and brought back
 * by hotspot_stop. hotspot_stop cleans up in either mode, and a start in the
 * other mode after a stop works. hotspot_poll stays bounded in both modes and
 * is safe to call from a game's tick (on_game_tick). */
bool hotspot_start_dns(bool own_dns);
/* A game's hotspot under a name and password of the caller's: the Game
 * Boy's Link Cable (src/link.h), whose joiner makes the same password from
 * the name and so needs no code. As hotspot_start_dns(false) in all else:
 * no DNS here, udhcpd, the supplicant stopped and brought back by
 * hotspot_stop. False for a name of none or over 32 bytes, or a password
 * under WPA2's 8 or over the 15 this file keeps. */
bool hotspot_start_named(const char *ssid, const char *pass);

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
