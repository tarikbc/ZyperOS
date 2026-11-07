/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_CAPTIVE_H
#define ZYPEROS_CAPTIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The pure half of Wi-Fi File Transfer's hotspot (src/hotspot.c): what goes
 * into the QR code, the password, and the DNS answer that makes every name a
 * phone looks up land on the handheld. No sockets, no processes, no SDL, so
 * tools/captive-check.c holds all of it on the host.
 *
 * Why every name: a phone that joins a Wi-Fi network asks a known address
 * whether it is on the internet (Apple's captive.apple.com, Android's
 * connectivitycheck.gstatic.com, Windows' msftconnecttest.com). Here every
 * name is the handheld, so the question reaches its web server, which answers
 * with the transfer page instead of the expected reply - and the phone, sure
 * it is on a sign-in network, opens that page in its sign-in sheet by itself.
 * That is the whole captive portal. */

/* The hotspot's own address and the subnet it hands out. The USB cable's link
 * is 10.42.0.1 (plastron's S40usbgadget); the two never run together - the
 * adapter makes the USB port a host - but a different subnet keeps them
 * apart in anybody's head as well. */
#define CAPTIVE_ADDR      "10.42.1.1"
#define CAPTIVE_POOL_LO   "10.42.1.10"
#define CAPTIVE_POOL_HI   "10.42.1.60"

/* Seconds a phone may keep an answer. Short: the names are only the
 * handheld's while the screen is open. */
#define CAPTIVE_DNS_TTL   10

/* The reply to one DNS query in `q` (`n` bytes, as one UDP datagram), into
 * `out`. Every A question is answered with `ip`; any other type (AAAA, HTTPS,
 * ...) gets an empty NOERROR, so a phone asks again for A rather than taking
 * an error as "no network". Returns the reply's length, or 0 for a datagram
 * that is not one well-formed standard query, which gets no reply at all. */
size_t captive_dns_answer(const uint8_t *q, size_t n, const uint8_t ip[4],
                          uint8_t *out, size_t cap);

/* Whether a request's Host header names the handheld itself, `addr` with or
 * without a port. Anything else is a phone's internet check, or a page it
 * tried to open, and is answered with the way to the transfer page. A request
 * with no Host (HTTP/1.0) is taken as the handheld's. */
bool captive_host_is(const char *host, const char *addr);

/* The text of the QR code that joins a WPA network (the "WIFI:" form phones'
 * cameras read): WIFI:T:WPA;S:<ssid>;P:<password>;; with \ ; , : and "
 * escaped by a backslash. False if it does not fit. */
bool captive_wifi_qr(char *out, size_t n, const char *ssid, const char *pass);

/* A password made from `rnd` (at least CAPTIVE_PASS_RANDOM bytes from the
 * kernel's generator): eight lowercase letters and digits in two groups of
 * four, "q7mx-3kpa", without the ones that look alike (0 o 1 l i). WPA2 wants
 * 8 to 63 characters; this is 9. */
#define CAPTIVE_PASS_RANDOM 8
void captive_password(char *out, size_t n, const uint8_t *rnd);

/* The network's name: "ZyperOS-" and the last two bytes of the adapter's
 * address `mac` ("aa:bb:cc:dd:ee:ff") in capitals, so one handheld keeps one
 * name and a friend's differs. "ZyperOS" when the address cannot be read. */
void captive_ssid(char *out, size_t n, const char *mac);

#endif
