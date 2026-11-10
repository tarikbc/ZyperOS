/* SPDX-License-Identifier: GPL-3.0-only */
/* zyperos.local: a small multicast DNS responder (RFC 6762) for File
 * Transfer's link, so a browser can open http://zyperos.local rather than an
 * address typed by hand.
 *
 * It answers one name, the handheld's, with one IPv4 address, on one
 * interface: the USB cable's (usb0) while USB File Transfer or Developer
 * Mode's SSH is on, and Wi-Fi's (wlan0) while Wi-Fi File Transfer is open.
 * Nothing else: no services, no browsing, no IPv6, no daemon. It lives in the
 * launcher and runs from the transfer screen's loop, so it exists exactly as
 * long as the thing the name is for. avahi would bring D-Bus and a daemon to a
 * read-only image for the sake of one A record.
 *
 * What it does, from RFC 6762:
 *
 *   - Answers a query for <name>.local's A record (or ANY) with the address,
 *     and a query for any other type of that name (AAAA above all, which
 *     every computer asks beside A) with an NSEC saying there is only A, so
 *     the computer does not wait for an IPv6 answer that will not come.
 *   - Answers by multicast, or by unicast when the question asks for it (the
 *     QU bit), and a one-shot resolver (a query from a port other than 5353)
 *     with its own ID and question back, as section 6.7 says.
 *   - Says nothing when the query already lists the answer (known-answer
 *     suppression).
 *   - On Wi-Fi, where another ZyperOS may be on the same network, it first
 *     probes for the name three times, 250 ms apart. If something answers for
 *     it with another address, it takes the next name, zyperos-2.local, then
 *     -3, and so on; two handhelds probing at once settle it by comparing
 *     addresses (8.2), and the loser probes again a second later. Then it
 *     announces the name twice, a second apart. A handheld that already has
 *     the name defends it by answering the other's probe.
 *   - On the cable only the computer is on the link, so it takes the name at
 *     once and announces it.
 *
 * Linux and macOS (the host check and the desktop build); SO_BINDTODEVICE
 * keeps it to the one interface on Linux. */
#ifndef MDNS_H
#define MDNS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MDNS_BASE      "zyperos"
#define MDNS_HOST_MAX  32       /* "zyperos-99" and room to spare */
#define MDNS_PORT      5353
#define MDNS_TTL       120      /* seconds, RFC 6762's for an A record */
#define MDNS_LEGACY_TTL 10      /* to a one-shot resolver (6.7) */

/* Answer on `ifname` ("usb0", "wlan0") with `addr` ("10.42.0.1"). `probe`:
 * make sure the name is free first (Wi-Fi); else take it at once (the
 * cable). Running already on the same interface and address, it goes on as
 * it is; on another, it starts again there. False when the socket could not
 * be had (the interface is not there, or has no multicast), and it is off. */
bool mdns_start(const char *ifname, const char *addr, bool probe);

/* Off, and the socket closed. Says goodbye (a TTL of 0) where it had the
 * name, so a computer forgets it at once. */
void mdns_stop(void);

/* Bounded: the packets that came (a few at most), the answers, and the
 * probes and announcements due. Call it from the screen's loop. */
void mdns_poll(unsigned now_ms);

/* The name it answers to, or is probing for: "zyperos.local",
 * "zyperos-2.local". NULL while off. */
const char *mdns_name(void);

/* The name is its own (probing is over): it answers queries. */
bool mdns_ready(void);

/* ---- the parts a host check can reach (tools/mdns-check.c) ------------ */

typedef struct {
	char     host[MDNS_HOST_MAX];    /* "zyperos", "zyperos-2": no ".local" */
	uint8_t  addr[4];                /* 10.42.0.1 as 10, 42, 0, 1 */
} mdns_self;

typedef struct {
	size_t len;          /* 0: say nothing */
	bool   unicast;      /* to the sender's address and port, not the group */
} mdns_reply;

/* The answer to packet `p` (n bytes) that came from port `src_port`, into
 * `out`. A response, a malformed packet, or a query about anything else is
 * answered with nothing. */
mdns_reply mdns_respond(const mdns_self *me, const uint8_t *p, size_t n,
                        uint16_t src_port, uint8_t *out, size_t on);

/* A probe for me's name (a query for ANY with the QU bit, and the record it
 * means to own in the authority section), an announcement (an unsolicited
 * response: the A record and the NSEC), and a goodbye (the same with a TTL
 * of 0). Their lengths, 0 when `on` is too small. */
size_t mdns_probe_packet(const mdns_self *me, uint8_t *out, size_t on);
size_t mdns_announce_packet(const mdns_self *me, uint8_t *out, size_t on, bool goodbye);

/* What a packet another host sent means for me's name:
 *   MDNS_CLASH     it answers for the name with another address: the name
 *                  is taken (while probing, take the next one)
 *   MDNS_TIE_LOST  it probes for the same name at the same time and its
 *                  record wins the tie-break (8.2): probe again in a second
 *   MDNS_NO_CLASH  anything else, our own packets included */
enum { MDNS_NO_CLASH, MDNS_CLASH, MDNS_TIE_LOST };
int mdns_clash(const mdns_self *me, const uint8_t *p, size_t n);

/* The name after a clash: "zyperos" -> "zyperos-2" -> "zyperos-3". */
void mdns_next_host(char *host, size_t n);

/* The responder without its socket: what it sends, and when, for the
 * packets it is given and the clock. `send` gets a packet for the group, or
 * (unicast) for the sender of the packet being answered. The socket in
 * mdns.c runs one; the check runs several on a pretend network. */
typedef void (*mdns_send_fn)(void *ctx, const uint8_t *b, size_t len, bool unicast);
typedef struct {
	mdns_self    me;
	int          state, sent, clashes;
	unsigned     next_at, last_multi;
	bool         timed, probe;
	mdns_send_fn send;
	void        *ctx;
} mdns_engine;

void mdns_engine_init(mdns_engine *e, const uint8_t addr[4], bool probe,
                      mdns_send_fn send, void *ctx);
void mdns_engine_packet(mdns_engine *e, const uint8_t *p, size_t n, uint16_t src_port,
                        unsigned now_ms);
void mdns_engine_tick(mdns_engine *e, unsigned now_ms);
bool mdns_engine_ready(const mdns_engine *e);

/* A name from a packet at `*off`, compression followed, as dotted lowercase
 * text into `out` ("zyperos.local"), and `*off` moved past it. False when it
 * is malformed (a pointer loop, a label past the end, longer than 255). */
bool mdns_read_name(const uint8_t *p, size_t n, size_t *off, char *out, size_t on);

#endif
