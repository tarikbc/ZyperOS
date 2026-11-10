/* SPDX-License-Identifier: GPL-3.0-only */
/* See mdns.h for what this answers and why it is this small. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "mdns.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define T_A     1
#define T_AAAA  28
#define T_NSEC  47
#define T_ANY   255
#define C_IN    1
#define C_FLUSH 0x8000      /* in a record: the cache-flush bit */
#define C_QU    0x8000      /* in a question: a unicast answer is wanted */
#define F_QR    0x8000
#define F_AA    0x0400

/* ---- packets ------------------------------------------------------------ */

static unsigned rd16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned long rd32(const uint8_t *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 |
	       (unsigned long)p[2] << 8 | p[3];
}

bool mdns_read_name(const uint8_t *p, size_t n, size_t *off, char *out, size_t on)
{
	size_t at = *off, used = 0, end = 0;
	bool jumped = false;
	int hops = 0;

	if (!on) return false;
	out[0] = '\0';
	for (;;) {
		unsigned len, i;

		if (at >= n) return false;
		len = p[at];
		if ((len & 0xC0) == 0xC0) {
			/* A pointer: followed, at most 16 times, so a loop ends */
			if (at + 1 >= n || ++hops > 16) return false;
			if (!jumped) end = at + 2;
			jumped = true;
			at = (size_t)((len & 0x3F) << 8 | p[at + 1]);
			continue;
		}
		if (len & 0xC0) return false;              /* 0x40 and 0x80 are no kind */
		if (len == 0) {
			if (!jumped) end = at + 1;
			break;
		}
		if (at + 1 + len > n) return false;
		if (used + 1 + len + 1 > on || used + 1 + len > 255) return false;
		if (used) out[used++] = '.';
		for (i = 0; i < len; i++) {
			unsigned char c = p[at + 1 + i];

			/* A dot or a NUL inside a label would read as another name */
			if (c == '.' || c == '\0') return false;
			out[used++] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
		}
		out[used] = '\0';
		at += 1 + len;
	}
	*off = end;
	return true;
}

/* One record's head, after its name: type, class, TTL and data */
typedef struct {
	char           name[256];
	unsigned       type, cls;
	unsigned long  ttl;
	const uint8_t *data;
	unsigned       dlen;
} rr;

static bool read_rr(const uint8_t *p, size_t n, size_t *off, rr *r)
{
	if (!mdns_read_name(p, n, off, r->name, sizeof r->name) || *off + 10 > n) return false;
	r->type = rd16(p + *off);
	r->cls  = rd16(p + *off + 2);
	r->ttl  = rd32(p + *off + 4);
	r->dlen = rd16(p + *off + 8);
	*off += 10;
	if (*off + r->dlen > n) return false;
	r->data = p + *off;
	*off += r->dlen;
	return true;
}

/* The name a packet would say, lowercase: "zyperos.local" */
static void fqdn(const mdns_self *me, char *out, size_t n)
{
	size_t i;

	snprintf(out, n, "%s.local", me->host);
	for (i = 0; out[i]; i++)
		if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] + 32);
}

typedef struct {
	uint8_t *p;
	size_t   n, at;
	bool     bad;
	size_t   name_at;     /* where the name was first written, for pointers */
} wbuf;

static void w8(wbuf *w, unsigned v)
{
	if (w->at + 1 > w->n) { w->bad = true; return; }
	w->p[w->at++] = (uint8_t)v;
}
static void w16(wbuf *w, unsigned v) { w8(w, v >> 8 & 0xFF); w8(w, v & 0xFF); }
static void w32(wbuf *w, unsigned long v) { w16(w, (unsigned)(v >> 16 & 0xFFFF)); w16(w, (unsigned)(v & 0xFFFF)); }
static void wbytes(wbuf *w, const void *b, size_t len)
{
	if (w->at + len > w->n) { w->bad = true; return; }
	memcpy(w->p + w->at, b, len);
	w->at += len;
}

/* The name in full: <host> "local" and the root */
static void wname_full(wbuf *w, const char *host)
{
	size_t len = strlen(host);

	w8(w, (unsigned)len);
	wbytes(w, host, len);
	w8(w, 5);
	wbytes(w, "local", 5);
	w8(w, 0);
}

/* The name, as a pointer to its first time when there was one */
static void wname(wbuf *w, const char *host)
{
	if (w->name_at) {
		w16(w, 0xC000 | (unsigned)w->name_at);
		return;
	}
	w->name_at = w->at;
	wname_full(w, host);
}

static void w_a(wbuf *w, const mdns_self *me, unsigned cls, unsigned long ttl)
{
	wname(w, me->host);
	w16(w, T_A);
	w16(w, cls);
	w32(w, ttl);
	w16(w, 4);
	wbytes(w, me->addr, 4);
}

/* There is an A record and nothing else (RFC 6762 6.1): the next name is
 * the name itself, in full, and the bitmap has A's bit alone */
static void w_nsec(wbuf *w, const mdns_self *me, unsigned cls, unsigned long ttl)
{
	wname(w, me->host);
	w16(w, T_NSEC);
	w16(w, cls);
	w32(w, ttl);
	w16(w, (unsigned)strlen(me->host) + 8 + 3);
	wname_full(w, me->host);
	w8(w, 0);           /* window 0 */
	w8(w, 1);           /* one byte of bits */
	w8(w, 0x40);        /* type 1, A */
}

static void w_head(wbuf *w, unsigned id, unsigned flags, unsigned qd, unsigned an,
                   unsigned ns, unsigned ar)
{
	w16(w, id);
	w16(w, flags);
	w16(w, qd);
	w16(w, an);
	w16(w, ns);
	w16(w, ar);
}

mdns_reply mdns_respond(const mdns_self *me, const uint8_t *p, size_t n,
                        uint16_t src_port, uint8_t *out, size_t on)
{
	mdns_reply r = { 0, false };
	const bool legacy = src_port != MDNS_PORT;
	char fq[MDNS_HOST_MAX + 8], name[256];
	bool want_a = false, want_nsec = false, any = false, qu = true;
	unsigned flags, qd, an, i;
	size_t off = 12, qend;
	wbuf w = { out, on, 0, false, 0 };
	unsigned long ttl = legacy ? MDNS_LEGACY_TTL : MDNS_TTL;
	unsigned cls = legacy ? C_IN : (C_IN | C_FLUSH);

	if (n < 12) return r;
	flags = rd16(p + 2);
	if (flags & F_QR) return r;                 /* a response */
	if ((flags >> 11) & 0xF) return r;          /* not a standard query */
	qd = rd16(p + 4);
	an = rd16(p + 6);
	fqdn(me, fq, sizeof fq);
	for (i = 0; i < qd; i++) {
		unsigned type, qcls;

		if (!mdns_read_name(p, n, &off, name, sizeof name) || off + 4 > n) return r;
		type = rd16(p + off);
		qcls = rd16(p + off + 2);
		off += 4;
		if (strcmp(name, fq)) continue;
		if ((qcls & 0x7FFF) != C_IN && (qcls & 0x7FFF) != T_ANY) continue;
		any = true;
		if (type == T_A || type == T_ANY) want_a = true;
		else want_nsec = true;              /* AAAA above all: there is none */
		if (!(qcls & C_QU)) qu = false;
	}
	if (!any) return r;
	qend = off;
	/* What the asker already knows, with at least half its life left, is
	 * not said again (7.1) */
	for (i = 0; i < an; i++) {
		rr k;

		if (!read_rr(p, n, &off, &k)) break;
		if (strcmp(k.name, fq) || k.ttl < MDNS_TTL / 2) continue;
		if (k.type == T_A && k.dlen == 4 && !memcmp(k.data, me->addr, 4)) want_a = false;
		if (k.type == T_NSEC) want_nsec = false;
	}
	if (!want_a && !want_nsec) return r;

	/* The answer section has what was asked, the additional section the
	 * other: the A beside an NSEC, the NSEC beside an A, so one answer says
	 * both that the address is this and that there is no IPv6 one. A
	 * one-shot resolver gets its ID and its questions back (6.7). */
	if (legacy) {
		w_head(&w, rd16(p), F_QR | F_AA, qd, want_a + want_nsec, 0, 2 - (want_a + want_nsec));
		wbytes(&w, p + 12, qend - 12);
	} else {
		w_head(&w, 0, F_QR | F_AA, 0, want_a + want_nsec, 0, 2 - (want_a + want_nsec));
	}
	if (want_a) w_a(&w, me, cls, ttl);
	if (want_nsec) w_nsec(&w, me, cls, ttl);
	if (!want_a) w_a(&w, me, cls, ttl);
	if (!want_nsec) w_nsec(&w, me, cls, ttl);
	if (w.bad) return r;
	r.len = w.at;
	r.unicast = legacy || qu;
	return r;
}

size_t mdns_probe_packet(const mdns_self *me, uint8_t *out, size_t on)
{
	wbuf w = { out, on, 0, false, 0 };

	w_head(&w, 0, 0, 1, 0, 1, 0);
	wname(&w, me->host);
	w16(&w, T_ANY);
	w16(&w, C_IN | C_QU);
	w_a(&w, me, C_IN, MDNS_TTL);       /* the authority: what it means to own */
	return w.bad ? 0 : w.at;
}

size_t mdns_announce_packet(const mdns_self *me, uint8_t *out, size_t on, bool goodbye)
{
	wbuf w = { out, on, 0, false, 0 };
	unsigned long ttl = goodbye ? 0 : MDNS_TTL;

	w_head(&w, 0, F_QR | F_AA, 0, 1, 0, 1);
	w_a(&w, me, C_IN | C_FLUSH, ttl);
	w_nsec(&w, me, C_IN | C_FLUSH, ttl);
	return w.bad ? 0 : w.at;
}

int mdns_clash(const mdns_self *me, const uint8_t *p, size_t n)
{
	char fq[MDNS_HOST_MAX + 8], name[256];
	unsigned flags, qd, an, ns, ar, i;
	size_t off = 12;
	bool asks = false, lost = false, other = false, won = false;

	if (n < 12) return MDNS_NO_CLASH;
	flags = rd16(p + 2);
	qd = rd16(p + 4);
	an = rd16(p + 6);
	ns = rd16(p + 8);
	ar = rd16(p + 10);
	fqdn(me, fq, sizeof fq);
	for (i = 0; i < qd; i++) {
		if (!mdns_read_name(p, n, &off, name, sizeof name) || off + 4 > n) return MDNS_NO_CLASH;
		off += 4;
		if (!strcmp(name, fq)) asks = true;
	}
	for (i = 0; i < an + ns + ar; i++) {
		rr k;

		if (!read_rr(p, n, &off, &k)) break;
		if (strcmp(k.name, fq)) continue;
		if (flags & F_QR) {
			/* Somebody answers for the name with an address not ours; a
			 * goodbye (TTL 0) is letting it go, not claiming it */
			if (k.type == T_A && k.dlen == 4 && k.ttl > 0 && memcmp(k.data, me->addr, 4))
				return MDNS_CLASH;
		} else if (asks && i >= an && i < an + ns) {
			/* A probe for the same name: its authority records against
			 * ours, class, then type, then data, the later winning (8.2).
			 * Ours is one A record, so an A decides by its address; a type
			 * after A, with no A of its, is later. */
			if (k.type == T_A && k.dlen == 4) {
				int c = memcmp(k.data, me->addr, 4);

				if (c > 0) lost = true;
				else if (c < 0) won = true;
			} else if ((k.cls & 0x7FFF) > C_IN || ((k.cls & 0x7FFF) == C_IN && k.type > T_A)) {
				other = true;
			}
		}
	}
	if (lost) return MDNS_TIE_LOST;
	if (other && !won) return MDNS_TIE_LOST;
	return MDNS_NO_CLASH;
}

void mdns_next_host(char *host, size_t n)
{
	const char *dash = strrchr(host, '-');
	int k = 1;

	if (dash && dash[1]) {
		const char *d;

		for (d = dash + 1; *d >= '0' && *d <= '9'; d++) {}
		if (!*d) {
			k = 0;
			for (d = dash + 1; *d; d++) k = k * 10 + (*d - '0');
		} else {
			dash = NULL;
		}
	} else {
		dash = NULL;
	}
	{
		char base[MDNS_HOST_MAX];
		size_t blen = dash ? (size_t)(dash - host) : strlen(host);

		if (blen >= sizeof base) blen = sizeof base - 1;
		memcpy(base, host, blen);
		base[blen] = '\0';
		snprintf(host, n, "%s-%d", base, k < 1 ? 2 : k + 1);
	}
}

int mdns_accept(const mdns_self *me, const uint8_t mask[4], const mdns_from *f)
{
	bool subnet = true;
	int i;

	for (i = 0; i < 4; i++)
		if ((f->src[i] & mask[i]) != (me->addr[i] & mask[i])) subnet = false;
	if (f->to_group) {
		static const uint8_t group[4] = { 224, 0, 0, 251 };

		if (memcmp(f->dst, group, 4) || f->ttl != 255) return MDNS_DROP;
		return subnet ? MDNS_ON_SUBNET : MDNS_ON_LINK;
	}
	if (memcmp(f->dst, me->addr, 4) || !subnet) return MDNS_DROP;
	return MDNS_ON_SUBNET;
}

/* ---- the responder: when to probe, announce and answer ------------------ */

enum { ST_OFF, ST_PROBING, ST_ANNOUNCING, ST_READY };

void mdns_engine_init(mdns_engine *e, const uint8_t addr[4], const uint8_t mask[4],
                      bool probe, mdns_send_fn send, void *ctx)
{
	memset(e, 0, sizeof *e);
	snprintf(e->me.host, sizeof e->me.host, "%s", MDNS_BASE);
	memcpy(e->me.addr, addr, 4);
	memcpy(e->mask, mask, 4);
	e->probe = probe;
	e->send = send;
	e->ctx = ctx;
	e->state = ST_PROBING;          /* timed at the first tick */
}

bool mdns_engine_ready(const mdns_engine *e)
{
	return e->state == ST_ANNOUNCING || e->state == ST_READY;
}

static void probe_from(mdns_engine *e, unsigned now, unsigned wait)
{
	e->state = ST_PROBING;
	e->sent = 0;
	e->next_at = now + wait;
	e->timed = true;
}

static void announce_from(mdns_engine *e, unsigned now)
{
	e->state = ST_ANNOUNCING;
	e->sent = 0;
	e->next_at = now;
	e->timed = true;
}

void mdns_engine_packet(mdns_engine *e, const uint8_t *p, size_t n, const mdns_from *f,
                        unsigned now)
{
	uint8_t out[512];
	int clash, where;

	if (e->state == ST_OFF) return;
	if ((where = mdns_accept(&e->me, e->mask, f)) == MDNS_DROP) return;
	clash = mdns_clash(&e->me, p, n);
	if (clash == MDNS_CLASH) {
		if (e->state == ST_PROBING) {
			/* Taken: the next name, a few times. Somebody who claims
			 * every name it tries gets no more: it stops answering by
			 * name, and the screen shows the address alone */
			if (++e->clashes > MDNS_MAX_RENAMES) {
				e->state = ST_OFF;
				e->gave_up = true;
				return;
			}
			mdns_next_host(e->me.host, sizeof e->me.host);
			probe_from(e, now, 0);
		} else {
			/* Somebody says our name is theirs: probed again, and
			 * whichever of us has it answers the other (9) */
			probe_from(e, now, 0);
		}
		return;
	}
	if (clash == MDNS_TIE_LOST && e->state == ST_PROBING) {
		probe_from(e, now, 1000);
		return;
	}
	if (e->state != ST_PROBING) {
		mdns_reply r = mdns_respond(&e->me, p, n, f->port, out, sizeof out);

		if (!r.len) return;
		/* Unicast only to the interface's own subnet; a one-shot query
		 * from elsewhere is not answered, a QU one goes to the group */
		if (r.unicast && where != MDNS_ON_SUBNET) {
			if (f->port != MDNS_PORT) return;
			r.unicast = false;
		}
		if (r.unicast) {
			e->send(e->ctx, out, r.len, true);
		} else if (!e->last_multi || now - e->last_multi >= 250u) {
			/* Once a quarter second to the group at most; a probe's
			 * answer goes out whatever (it asks with QU, unicast) */
			e->last_multi = now ? now : 1;
			e->send(e->ctx, out, r.len, false);
		}
	}
}

void mdns_engine_tick(mdns_engine *e, unsigned now)
{
	uint8_t out[512];

	if (e->state == ST_OFF) return;
	/* The first tick: a probe waits up to 250 ms first, so two handhelds
	 * that start together do not probe in step (8.1) */
	if (!e->timed) {
		if (e->probe) probe_from(e, now, (e->me.addr[3] * 97u + now) % 250u);
		else announce_from(e, now);
	}
	if (e->state == ST_READY || (int)(now - e->next_at) < 0) return;
	if (e->state == ST_PROBING) {
		if (e->sent < 3) {
			e->send(e->ctx, out, mdns_probe_packet(&e->me, out, sizeof out), false);
			e->sent++;
			e->next_at = now + 250;
		} else {
			announce_from(e, now);
		}
	}
	if (e->state == ST_ANNOUNCING && (int)(now - e->next_at) >= 0) {
		e->send(e->ctx, out, mdns_announce_packet(&e->me, out, sizeof out, false), false);
		if (++e->sent >= 2) e->state = ST_READY;
		else e->next_at = now + 1000;
	}
}

/* ---- the socket ---------------------------------------------------------- */

static struct {
	int                fd;
	mdns_engine        e;
	bool               on;
	char               ifname[IFNAMSIZ];
	char               addr[INET_ADDRSTRLEN];
	char               name[MDNS_HOST_MAX + 8];
	char               said[MDNS_HOST_MAX];   /* the name last logged */
	struct sockaddr_in from;                  /* the packet being answered */
} g = { .fd = -1 };

static void sock_send(void *ctx, const uint8_t *b, size_t len, bool unicast)
{
	struct sockaddr_in group;
	const struct sockaddr_in *to = &g.from;

	(void)ctx;
	if (!len || g.fd < 0) return;
	if (!unicast) {
		memset(&group, 0, sizeof group);
		group.sin_family = AF_INET;
		group.sin_port = htons(MDNS_PORT);
		inet_pton(AF_INET, "224.0.0.251", &group.sin_addr);
		to = &group;
	}
	/* A link that is down, or a full queue: the next one tries again */
	if (sendto(g.fd, b, len, 0, (const struct sockaddr *)to, sizeof *to) < 0) { }
}

const char *mdns_name(void)
{
	if (!g.on || g.e.gave_up) return NULL;
	snprintf(g.name, sizeof g.name, "%s.local", g.e.me.host);
	return g.name;
}

bool mdns_ready(void) { return g.on && mdns_engine_ready(&g.e); }

bool mdns_start(const char *ifname, const char *addr, bool probe)
{
	struct sockaddr_in any;
	struct in_addr ia;
	unsigned char ttl = 255, loop = 0;
	int one = 1, ttl_u = 255;

	if (!ifname || !addr || inet_pton(AF_INET, addr, &ia) != 1) return false;
	if (g.on && !strcmp(g.ifname, ifname) && !strcmp(g.addr, addr)) return true;
	mdns_stop();
	if (!if_nametoindex(ifname)) return false;
	g.fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (g.fd < 0) return false;
	fcntl(g.fd, F_SETFD, FD_CLOEXEC);
	fcntl(g.fd, F_SETFL, fcntl(g.fd, F_GETFL) | O_NONBLOCK);
	setsockopt(g.fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_REUSEPORT
	setsockopt(g.fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof one);
#endif
#ifdef SO_BINDTODEVICE
	/* Only this interface's packets, and only out of it */
	if (setsockopt(g.fd, SOL_SOCKET, SO_BINDTODEVICE, ifname, (socklen_t)strlen(ifname) + 1) < 0)
		fprintf(stderr, "mdns: SO_BINDTODEVICE %s: %s\n", ifname, strerror(errno));
#endif
	memset(&any, 0, sizeof any);
	any.sin_family = AF_INET;
	any.sin_port = htons(MDNS_PORT);
	any.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(g.fd, (struct sockaddr *)&any, sizeof any) < 0) {
		fprintf(stderr, "mdns: port %d: %s\n", MDNS_PORT, strerror(errno));
		goto fail;
	}
	{
#ifdef __linux__
		struct ip_mreqn m;

		memset(&m, 0, sizeof m);
		inet_pton(AF_INET, "224.0.0.251", &m.imr_multiaddr);
		m.imr_address = ia;
		m.imr_ifindex = (int)if_nametoindex(ifname);
#else
		struct ip_mreq m;

		memset(&m, 0, sizeof m);
		inet_pton(AF_INET, "224.0.0.251", &m.imr_multiaddr);
		m.imr_interface = ia;
#endif
		if (setsockopt(g.fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof m) < 0) {
			fprintf(stderr, "mdns: the group on %s: %s\n", ifname, strerror(errno));
			goto fail;
		}
	}
	setsockopt(g.fd, IPPROTO_IP, IP_MULTICAST_IF, &ia, sizeof ia);
	/* 255 both ways: a receiver drops what came through a router (11) */
	setsockopt(g.fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof ttl);
	setsockopt(g.fd, IPPROTO_IP, IP_TTL, &ttl_u, sizeof ttl_u);
	/* Our own packets back would read as somebody else probing */
	setsockopt(g.fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof loop);

	/* Each packet's TTL and where it was sent, for mdns_accept */
#ifdef IP_PKTINFO
	setsockopt(g.fd, IPPROTO_IP, IP_PKTINFO, &one, sizeof one);
#endif
#ifdef IP_RECVDSTADDR
	setsockopt(g.fd, IPPROTO_IP, IP_RECVDSTADDR, &one, sizeof one);
#endif
	setsockopt(g.fd, IPPROTO_IP, IP_RECVTTL, &one, sizeof one);
	/* The interface's subnet: a /30 on the cable, the network's on Wi-Fi */
	{
		struct ifaddrs *all, *a;
		uint8_t mask[4] = { 255, 255, 255, 255 };

		if (getifaddrs(&all) == 0) {
			for (a = all; a; a = a->ifa_next)
				if (a->ifa_addr && a->ifa_netmask && a->ifa_addr->sa_family == AF_INET &&
				    !strcmp(a->ifa_name, ifname) &&
				    ((struct sockaddr_in *)a->ifa_addr)->sin_addr.s_addr == ia.s_addr)
					memcpy(mask, &((struct sockaddr_in *)a->ifa_netmask)->sin_addr, 4);
			freeifaddrs(all);
		}
		snprintf(g.ifname, sizeof g.ifname, "%s", ifname);
		snprintf(g.addr, sizeof g.addr, "%s", addr);
		mdns_engine_init(&g.e, (const uint8_t *)&ia, mask, probe, sock_send, NULL);
	}
	g.said[0] = '\0';
	g.on = true;
	return true;
fail:
	close(g.fd);
	g.fd = -1;
	return false;
}

void mdns_stop(void)
{
	if (g.fd >= 0) {
		if (g.on && mdns_engine_ready(&g.e)) {
			uint8_t b[128];

			sock_send(NULL, b, mdns_announce_packet(&g.e.me, b, sizeof b, true), false);
			fprintf(stderr, "mdns: %s.local let go\n", g.e.me.host);
		}
		close(g.fd);
	}
	g.fd = -1;
	g.on = false;
	g.ifname[0] = g.addr[0] = '\0';
}

void mdns_poll(unsigned now)
{
	uint8_t in[1500];
	int i;

	if (!g.on) return;
	for (i = 0; i < 8; i++) {
		union { struct cmsghdr h; char b[256]; } ctl;
		struct iovec v = { in, sizeof in };
		struct msghdr m;
		struct cmsghdr *c;
		mdns_from f;
		ssize_t got;

		memset(&m, 0, sizeof m);
		m.msg_name = &g.from;
		m.msg_namelen = sizeof g.from;
		m.msg_iov = &v;
		m.msg_iovlen = 1;
		m.msg_control = ctl.b;
		m.msg_controllen = sizeof ctl.b;
		if ((got = recvmsg(g.fd, &m, 0)) <= 0) break;
		if (!memcmp(&g.from.sin_addr, g.e.me.addr, 4)) continue;    /* ours */
		memset(&f, 0, sizeof f);
		f.ttl = -1;
		memcpy(f.src, &g.from.sin_addr, 4);
		f.port = ntohs(g.from.sin_port);
		for (c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) {
			if (c->cmsg_level != IPPROTO_IP) continue;
#ifdef IP_PKTINFO
			if (c->cmsg_type == IP_PKTINFO)
				memcpy(f.dst, &((struct in_pktinfo *)CMSG_DATA(c))->ipi_addr, 4);
#endif
#ifdef IP_RECVDSTADDR
			if (c->cmsg_type == IP_RECVDSTADDR)
				memcpy(f.dst, CMSG_DATA(c), 4);
#endif
			/* Linux says IP_TTL with an int, macOS IP_RECVTTL with a byte */
			if (c->cmsg_type == IP_TTL) {
				int t;

				memcpy(&t, CMSG_DATA(c), sizeof t);
				f.ttl = t;
			}
#ifdef __APPLE__
			if (c->cmsg_type == IP_RECVTTL) f.ttl = *(unsigned char *)CMSG_DATA(c);
#endif
		}
		f.to_group = f.dst[0] >= 224 && f.dst[0] <= 239;
		mdns_engine_packet(&g.e, in, (size_t)got, &f, now);
	}
	mdns_engine_tick(&g.e, now);
	if (g.e.gave_up && strcmp(g.said, "-")) {
		snprintf(g.said, sizeof g.said, "-");
		fprintf(stderr, "mdns: %d names taken on %s: no name this session, the address alone\n",
		        g.e.clashes - 1, g.ifname);
	} else if (mdns_engine_ready(&g.e) && strcmp(g.said, g.e.me.host)) {
		snprintf(g.said, sizeof g.said, "%s", g.e.me.host);
		fprintf(stderr, "mdns: %s.local is %s on %s\n", g.e.me.host, g.addr, g.ifname);
	}
}
