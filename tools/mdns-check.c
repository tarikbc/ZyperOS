/* SPDX-License-Identifier: GPL-3.0-only */
/* zyperos.local's responder (src/mdns.h), fed real queries.
 *
 * The queries below are what a Mac sent on 2026-11-10, read off the wire as
 * it looked the name up (dns-sd -G v4 and -G v4v6, and dig's one-shot query
 * to the group, EDNS and all): the first time with the QU bit, then without,
 * and A and AAAA in one packet with the name compressed. Each gets the
 * answer RFC 6762 says, and every shorter piece of each, and a run of random
 * bytes, gets an answer or none and never a read past the end. Then several
 * responders on a pretend network: two handhelds on one Wi-Fi end with two
 * names, a third takes the next, the cable takes its name at once, and a
 * goodbye lets the name go. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/mdns.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

static size_t unhex(const char *h, uint8_t *out, size_t on)
{
	size_t n = 0;

	while (h[0] && h[1] && n < on) {
		unsigned v;

		sscanf(h, "%2x", &v);
		out[n++] = (uint8_t)v;
		h += 2;
	}
	return n;
}

/* Read off a Mac's Wi-Fi, 2026-11-10 */
static const char *MAC_QU_A   = "000000000001000000000000077a797065726f73056c6f63616c0000018001";
static const char *MAC_QM_A   = "000000000001000000000000077a797065726f73056c6f63616c0000010001";
static const char *MAC_QU_AAAA_A =
	"000000000002000000000000077a797065726f73056c6f63616c00001c8001c00c00018001";
static const char *MAC_QM_AAAA_A =
	"000000000002000000000000077a797065726f73056c6f63616c00001c0001c00c00010001";
static const char *DIG_LEGACY =
	"3ed201200001000000000001077a797065726f73056c6f63616c00000100010000291000000000000000";

static const mdns_self ME = { "zyperos", { 10, 42, 0, 1 } };

static unsigned u16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned long u32(const uint8_t *p)
{
	return (unsigned long)p[0] << 24 | (unsigned long)p[1] << 16 | (unsigned long)p[2] << 8 | p[3];
}

/* An answer, read back: its header and the records in order */
typedef struct {
	unsigned id, flags, qd, an, ns, ar, nrr;
	struct { char name[256]; unsigned type, cls; unsigned long ttl; uint8_t data[64]; unsigned dlen; } rr[8];
	char qname[256];
} parsed;

static int parse(const uint8_t *p, size_t n, parsed *o)
{
	size_t off = 12;
	unsigned i;

	memset(o, 0, sizeof *o);
	if (n < 12) return 0;
	o->id = u16(p); o->flags = u16(p + 2);
	o->qd = u16(p + 4); o->an = u16(p + 6); o->ns = u16(p + 8); o->ar = u16(p + 10);
	for (i = 0; i < o->qd; i++) {
		if (!mdns_read_name(p, n, &off, o->qname, sizeof o->qname) || off + 4 > n) return 0;
		off += 4;
	}
	for (i = 0; i < o->an + o->ns + o->ar && i < 8; i++) {
		if (!mdns_read_name(p, n, &off, o->rr[i].name, sizeof o->rr[i].name) || off + 10 > n) return 0;
		o->rr[i].type = u16(p + off);
		o->rr[i].cls = u16(p + off + 2);
		o->rr[i].ttl = u32(p + off + 4);
		o->rr[i].dlen = u16(p + off + 8);
		off += 10;
		if (off + o->rr[i].dlen > n || o->rr[i].dlen > sizeof o->rr[i].data) return 0;
		memcpy(o->rr[i].data, p + off, o->rr[i].dlen);
		off += o->rr[i].dlen;
		o->nrr++;
	}
	return off == n;
}

static void check_a(const parsed *o, unsigned i, unsigned cls, unsigned long ttl, const char *what)
{
	CHECK(!strcmp(o->rr[i].name, "zyperos.local"), "%s: record %u's name is %s", what, i, o->rr[i].name);
	CHECK(o->rr[i].type == 1, "%s: record %u is type %u, not A", what, i, o->rr[i].type);
	CHECK(o->rr[i].cls == cls, "%s: record %u's class is %#x, not %#x", what, i, o->rr[i].cls, cls);
	CHECK(o->rr[i].ttl == ttl, "%s: record %u's TTL is %lu, not %lu", what, i, o->rr[i].ttl, ttl);
	CHECK(o->rr[i].dlen == 4 && !memcmp(o->rr[i].data, ME.addr, 4), "%s: record %u is not 10.42.0.1", what, i);
}

static void check_nsec(const parsed *o, unsigned i, unsigned cls, unsigned long ttl, const char *what)
{
	/* The next name in full (15 bytes), then window 0, one byte, A's bit */
	static const uint8_t want[] = { 7, 'z', 'y', 'p', 'e', 'r', 'o', 's', 5, 'l', 'o', 'c', 'a', 'l', 0, 0, 1, 0x40 };

	CHECK(!strcmp(o->rr[i].name, "zyperos.local"), "%s: NSEC's name is %s", what, o->rr[i].name);
	CHECK(o->rr[i].type == 47, "%s: record %u is type %u, not NSEC", what, i, o->rr[i].type);
	CHECK(o->rr[i].cls == cls, "%s: NSEC's class is %#x", what, o->rr[i].cls);
	CHECK(o->rr[i].ttl == ttl, "%s: NSEC's TTL is %lu", what, o->rr[i].ttl);
	CHECK(o->rr[i].dlen == sizeof want && !memcmp(o->rr[i].data, want, sizeof want),
	      "%s: NSEC's data is not 'zyperos.local, A only'", what);
}

static mdns_reply answer(const char *hex, uint16_t port, uint8_t *out, size_t on, parsed *o)
{
	uint8_t q[512];
	size_t n = unhex(hex, q, sizeof q);
	mdns_reply r = mdns_respond(&ME, q, n, port, out, on);

	if (r.len) CHECK(parse(out, r.len, o), "an answer that does not parse (%zu bytes)", r.len);
	return r;
}

static void real_queries(void)
{
	uint8_t out[512];
	parsed o;
	mdns_reply r;

	/* The first query, QU: unicast, the A asked and the NSEC beside it */
	r = answer(MAC_QU_A, 5353, out, sizeof out, &o);
	CHECK(r.len && r.unicast, "the Mac's QU query for A: no unicast answer");
	CHECK(o.id == 0 && o.flags == 0x8400 && o.qd == 0 && o.an == 1 && o.ns == 0 && o.ar == 1,
	      "QU A: header id %u flags %#x qd %u an %u ar %u", o.id, o.flags, o.qd, o.an, o.ar);
	check_a(&o, 0, 0x8001, 120, "QU A");
	check_nsec(&o, 1, 0x8001, 120, "QU A");

	/* Then without QU: to the group */
	r = answer(MAC_QM_A, 5353, out, sizeof out, &o);
	CHECK(r.len && !r.unicast, "the Mac's QM query for A: no answer to the group");
	check_a(&o, 0, 0x8001, 120, "QM A");

	/* AAAA and A in one packet, the second name a pointer: both answered,
	 * the A and the NSEC that says there is no AAAA */
	r = answer(MAC_QU_AAAA_A, 5353, out, sizeof out, &o);
	CHECK(r.len && r.unicast && o.an == 2 && o.ar == 0, "AAAA+A: an %u ar %u", o.an, o.ar);
	check_a(&o, 0, 0x8001, 120, "AAAA+A");
	check_nsec(&o, 1, 0x8001, 120, "AAAA+A");
	r = answer(MAC_QM_AAAA_A, 5353, out, sizeof out, &o);
	CHECK(r.len && !r.unicast && o.an == 2, "QM AAAA+A");

	/* dig's one-shot query from port 49511: its ID and question back, a
	 * short TTL, no cache-flush bit, to its own port; the EDNS record it
	 * carried is not repeated */
	r = answer(DIG_LEGACY, 49511, out, sizeof out, &o);
	CHECK(r.len && r.unicast, "dig's legacy query: no unicast answer");
	CHECK(o.id == 0x3ed2 && o.qd == 1 && !strcmp(o.qname, "zyperos.local") && o.an == 1 && o.ar == 1,
	      "legacy: id %#x qd %u q %s an %u ar %u", o.id, o.qd, o.qname, o.an, o.ar);
	check_a(&o, 0, 0x0001, 10, "legacy");
	check_nsec(&o, 1, 0x0001, 10, "legacy");

	/* AAAA alone: the NSEC answers, the A beside it */
	{
		uint8_t q[64];
		size_t n = unhex(MAC_QM_A, q, sizeof q);

		q[n - 3] = 28;                         /* type AAAA */
		r = mdns_respond(&ME, q, n, 5353, out, sizeof out);
		CHECK(r.len && parse(out, r.len, &o) && o.an == 1 && o.ar == 1, "AAAA alone");
		check_nsec(&o, 0, 0x8001, 120, "AAAA alone");
		check_a(&o, 1, 0x8001, 120, "AAAA alone");
	}

	/* A name in capitals is the same name (RFC 6762 16) */
	{
		uint8_t q[64];
		size_t n = unhex(MAC_QM_A, q, sizeof q);

		memcpy(q + 13, "ZyPeRoS", 7);
		memcpy(q + 21, "LOCAL", 5);
		r = mdns_respond(&ME, q, n, 5353, out, sizeof out);
		CHECK(r.len, "ZyPeRoS.LOCAL is not answered");
	}

	/* Another name, a response, an update: nothing */
	{
		uint8_t q[64];
		size_t n = unhex(MAC_QM_A, q, sizeof q);

		q[13] = 'x';
		CHECK(!mdns_respond(&ME, q, n, 5353, out, sizeof out).len, "xyperos.local was answered");
		n = unhex(MAC_QM_A, q, sizeof q);
		q[2] = 0x84;
		CHECK(!mdns_respond(&ME, q, n, 5353, out, sizeof out).len, "a response was answered");
		n = unhex(MAC_QM_A, q, sizeof q);
		q[2] = 0x28;                           /* opcode 5, UPDATE */
		CHECK(!mdns_respond(&ME, q, n, 5353, out, sizeof out).len, "an update was answered");
	}

	/* Known answers (7.1): the asker has the A with 120 s left, so nothing
	 * for an A query; with 30 s left (under half), it is said again */
	{
		uint8_t q[128];
		size_t n = unhex(MAC_QM_A, q, sizeof q);
		static const uint8_t known[] = { 0xc0, 0x0c, 0, 1, 0x80, 1, 0, 0, 0, 120, 0, 4, 10, 42, 0, 1 };

		q[7] = 1;                              /* one answer */
		memcpy(q + n, known, sizeof known);
		CHECK(!mdns_respond(&ME, q, n + sizeof known, 5353, out, sizeof out).len,
		      "a known answer was said again");
		q[n + 9] = 30;
		CHECK(mdns_respond(&ME, q, n + sizeof known, 5353, out, sizeof out).len,
		      "a known answer with 30 s left was not refreshed");
		q[n + 9] = 120;
		q[n + 15] = 9;                         /* another address: not what we say */
		CHECK(mdns_respond(&ME, q, n + sizeof known, 5353, out, sizeof out).len,
		      "a wrong known answer kept ours back");
	}

	/* No room for the answer: none, never a short one */
	CHECK(!answer(MAC_QU_A, 5353, out, 40, &o).len, "an answer cut to 40 bytes");
}

/* Every shorter piece of every packet, and random bytes: an answer or none,
 * and nothing read past the end (the check is built with the address
 * sanitizer where the compiler has it) */
static void hostile(void)
{
	const char *all[] = { MAC_QU_A, MAC_QM_A, MAC_QU_AAAA_A, MAC_QM_AAAA_A, DIG_LEGACY };
	uint8_t q[512], out[512];
	size_t i, k;
	unsigned seed = 12345;

	for (i = 0; i < sizeof all / sizeof *all; i++) {
		size_t n = unhex(all[i], q, sizeof q);

		for (k = 0; k < n; k++) {
			uint8_t *piece = malloc(k ? k : 1);

			memcpy(piece, q, k);
			(void)mdns_respond(&ME, piece, k, 5353, out, sizeof out);
			(void)mdns_clash(&ME, piece, k);
			free(piece);
		}
	}
	/* A pointer to itself, and to the pointer before it: no name, no loop */
	{
		static const uint8_t loop[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xc0, 12, 0, 1, 0, 1 };
		static const uint8_t pair[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0xc0, 14, 0xc0, 12, 0, 1, 0, 1 };
		char name[256];
		size_t off = 12;

		CHECK(!mdns_read_name(loop, sizeof loop, &off, name, sizeof name), "a pointer loop read as a name");
		off = 12;
		CHECK(!mdns_read_name(pair, sizeof pair, &off, name, sizeof name), "two pointers in a loop read as a name");
		CHECK(!mdns_respond(&ME, loop, sizeof loop, 5353, out, sizeof out).len, "a loop was answered");
	}
	/* A dot inside a label is not two labels */
	{
		static const uint8_t dotted[] = { 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
		                                  13, 'z', 'y', 'p', 'e', 'r', 'o', 's', '.', 'l', 'o', 'c', 'a', 'l', 0, 0, 1, 0, 1 };

		CHECK(!mdns_respond(&ME, dotted, sizeof dotted, 5353, out, sizeof out).len,
		      "the one label 'zyperos.local' was answered");
	}
	for (i = 0; i < 200000; i++) {
		size_t n = 12 + (seed = seed * 1103515245u + 12345u) % 80;

		for (k = 0; k < n; k++) q[k] = (uint8_t)((seed = seed * 1103515245u + 12345u) >> 16);
		/* Often a query with a question or two, so the parse goes deep */
		if (i & 1) { q[2] = 0; q[3] = 0; q[4] = 0; q[5] = (uint8_t)(1 + i % 3); }
		if (i % 7 == 0) memcpy(q + 12, "\x07zyperos\x05local\x00", n - 12 < 15 ? n - 12 : 15);
		(void)mdns_respond(&ME, q, n, (uint16_t)(i % 3 ? 5353 : 1234), out, sizeof out);
		(void)mdns_clash(&ME, q, n);
	}
}

static void names(void)
{
	char h[MDNS_HOST_MAX] = "zyperos";

	mdns_next_host(h, sizeof h);
	CHECK(!strcmp(h, "zyperos-2"), "after zyperos: %s", h);
	mdns_next_host(h, sizeof h);
	CHECK(!strcmp(h, "zyperos-3"), "after zyperos-2: %s", h);
	snprintf(h, sizeof h, "zyperos-9");
	mdns_next_host(h, sizeof h);
	CHECK(!strcmp(h, "zyperos-10"), "after zyperos-9: %s", h);
	snprintf(h, sizeof h, "my-pixel");
	mdns_next_host(h, sizeof h);
	CHECK(!strcmp(h, "my-pixel-2"), "after my-pixel: %s", h);
}

/* ---- a pretend network ---------------------------------------------------
 *
 * Every packet one responder sends to the group reaches every other one; a
 * unicast answer reaches the one whose packet it answers. A packet sent at a
 * tick is delivered at the next. */

#define HOSTS 4

static struct {
	mdns_engine e[HOSTS];
	int         n;
	struct { int from, to, uni; uint8_t b[512]; size_t len; } q[256];
	int         nq;
	int         answering;    /* whose packet is being answered */
} net;

static void net_send(void *ctx, const uint8_t *b, size_t len, bool unicast)
{
	int me = (int)(long)ctx, to;

	if (!len) return;
	for (to = 0; to < net.n; to++) {
		if (to == me || (unicast && to != net.answering) || net.nq >= 256) continue;
		net.q[net.nq].from = me;
		net.q[net.nq].to = to;
		net.q[net.nq].uni = unicast;
		memcpy(net.q[net.nq].b, b, len);
		net.q[net.nq].len = len;
		net.nq++;
	}
}

static void net_run(unsigned from_ms, unsigned to_ms)
{
	unsigned t;
	int i;

	for (t = from_ms; t <= to_ms; t += 10) {
		int nq = net.nq;

		net.nq = 0;
		{
			/* What was sent last tick, delivered now: copied, since the
			 * answers it brings are queued as it goes */
			static struct { int from, to, uni; uint8_t b[512]; size_t len; } now_q[256];

			memcpy(now_q, net.q, sizeof now_q[0] * (size_t)nq);
			for (i = 0; i < nq; i++) {
				mdns_from f = { .port = 5353, .ttl = 255, .to_group = !now_q[i].uni,
				                .dst = { 224, 0, 0, 251 } };

				memcpy(f.src, net.e[now_q[i].from].me.addr, 4);
				if (now_q[i].uni) memcpy(f.dst, net.e[now_q[i].to].me.addr, 4);
				net.answering = now_q[i].from;
				mdns_engine_packet(&net.e[now_q[i].to], now_q[i].b, now_q[i].len, &f, t);
			}
		}
		for (i = 0; i < net.n; i++) mdns_engine_tick(&net.e[i], t);
	}
}

static void add_host(const uint8_t addr[4], bool probe)
{
	static const uint8_t mask[4] = { 255, 255, 255, 0 };

	mdns_engine_init(&net.e[net.n], addr, mask, probe, net_send, (void *)(long)net.n);
	net.n++;
}

/* A computer asks host i's name: who answers, with what */
static int who_answers(const char *host, uint8_t addr[4])
{
	uint8_t q[64], out[512];
	size_t n = unhex(MAC_QU_A, q, sizeof q);
	int i, found = -1;

	/* Same length names only: rewritten in place */
	if (strlen(host) != 7) {
		/* zyperos-2: the name built by hand */
		size_t at = 12, hl = strlen(host);

		q[at++] = (uint8_t)hl;
		memcpy(q + at, host, hl); at += hl;
		memcpy(q + at, "\x05local\x00\x00\x01\x80\x01", 11); at += 11;
		n = at;
	}
	for (i = 0; i < net.n; i++) {
		mdns_reply r;
		parsed o;

		if (!mdns_engine_ready(&net.e[i])) continue;
		r = mdns_respond(&net.e[i].me, q, n, 5353, out, sizeof out);
		if (!r.len) continue;
		CHECK(parse(out, r.len, &o), "host %d's answer does not parse", i);
		if (found >= 0) {
			CHECK(0, "%s is answered by two hosts, %d and %d", host, found, i);
		}
		found = i;
		memcpy(addr, o.rr[0].data, 4);
	}
	return found;
}

static void two_on_wifi(void)
{
	static const uint8_t a[4] = { 192, 168, 1, 50 }, b[4] = { 192, 168, 1, 60 },
	                     c[4] = { 192, 168, 1, 70 };
	uint8_t got[4];

	/* Two handhelds whose File Transfer opens at the same moment */
	memset(&net, 0, sizeof net);
	add_host(a, true);
	add_host(b, true);
	net_run(0, 4000);
	CHECK(mdns_engine_ready(&net.e[0]) && mdns_engine_ready(&net.e[1]), "two at once: not both settled");
	CHECK(strcmp(net.e[0].me.host, net.e[1].me.host), "two at once: both are %s", net.e[0].me.host);
	CHECK((!strcmp(net.e[0].me.host, "zyperos") && !strcmp(net.e[1].me.host, "zyperos-2")) ||
	      (!strcmp(net.e[1].me.host, "zyperos") && !strcmp(net.e[0].me.host, "zyperos-2")),
	      "two at once: %s and %s", net.e[0].me.host, net.e[1].me.host);
	CHECK(who_answers("zyperos", got) >= 0, "two at once: nobody answers zyperos.local");
	CHECK(who_answers("zyperos-2", got) >= 0, "two at once: nobody answers zyperos-2.local");

	/* A third, later: zyperos and zyperos-2 answer its probes, so -3 */
	add_host(c, true);
	net_run(4010, 8000);
	CHECK(mdns_engine_ready(&net.e[2]) && !strcmp(net.e[2].me.host, "zyperos-3"),
	      "the third: %s", net.e[2].me.host);
	CHECK(who_answers("zyperos-3", got) == 2 && !memcmp(got, c, 4), "zyperos-3.local is not the third");

	/* One after the other: the first keeps zyperos, the second takes -2 */
	memset(&net, 0, sizeof net);
	add_host(b, true);
	net_run(0, 3000);
	add_host(a, true);
	net_run(3010, 6000);
	CHECK(!strcmp(net.e[0].me.host, "zyperos") && !strcmp(net.e[1].me.host, "zyperos-2"),
	      "one after the other: %s and %s", net.e[0].me.host, net.e[1].me.host);
	CHECK(who_answers("zyperos", got) == 0 && !memcmp(got, b, 4), "zyperos.local is not the first");

	/* The first lets go (its goodbye says TTL 0): that is no clash, and
	 * the second keeps its name until it starts again */
	{
		uint8_t bye[128];
		size_t len = mdns_announce_packet(&net.e[0].me, bye, sizeof bye, true);

		CHECK(mdns_clash(&net.e[1].me, bye, len) == MDNS_NO_CLASH, "a goodbye read as a clash");
	}
}

static void the_cable(void)
{
	static const uint8_t px[4] = { 10, 42, 0, 1 };
	uint8_t got[4];

	/* Only the computer on the link: no probe, the name at once and two
	 * announcements a second apart */
	memset(&net, 0, sizeof net);
	add_host(px, false);
	net_run(0, 0);
	CHECK(mdns_engine_ready(&net.e[0]) && !strcmp(net.e[0].me.host, "zyperos"),
	      "the cable: not ready at once");
	CHECK(who_answers("zyperos", got) == 0 && !memcmp(got, px, 4), "the cable: zyperos.local is not 10.42.0.1");
}

static void probes(void)
{
	static const mdns_self b = { "zyperos", { 192, 168, 1, 60 } };
	static const mdns_self c = { "zyperos", { 192, 168, 1, 70 } };
	uint8_t p[256], out[512];
	size_t n = mdns_probe_packet(&b, p, sizeof p);
	parsed o;
	mdns_reply r;

	CHECK(n && parse(p, n, &o), "a probe that does not parse");
	CHECK(o.flags == 0 && o.qd == 1 && o.ns == 1 && !strcmp(o.qname, "zyperos.local") &&
	      o.rr[0].type == 1 && o.rr[0].cls == 1 && !memcmp(o.rr[0].data, b.addr, 4),
	      "the probe is not a QU ANY query with its A in authority");
	/* Its own probe back is no clash; c's, with a later address, wins */
	CHECK(mdns_clash(&b, p, n) == MDNS_NO_CLASH, "its own probe read as a clash");
	CHECK(mdns_clash(&c, p, n) == MDNS_NO_CLASH, "c lost to an earlier address");
	n = mdns_probe_packet(&c, p, sizeof p);
	CHECK(mdns_clash(&b, p, n) == MDNS_TIE_LOST, "b did not lose to a later address");
	/* A holder of the name answers a probe, and that answer is a clash */
	r = mdns_respond(&ME, p, n, 5353, out, sizeof out);
	CHECK(r.len && r.unicast, "a probe was not answered by the name's holder");
	CHECK(mdns_clash(&c, out, r.len) == MDNS_CLASH, "an answer for the name read as no clash");
	/* The holder's own answer is not a clash for itself */
	CHECK(mdns_clash(&ME, out, r.len) == MDNS_NO_CLASH, "an answer with our address read as a clash");
}

/* On the link only (RFC 6762 11) */
static void on_link(void)
{
	static const uint8_t cable[4] = { 255, 255, 255, 252 };
	mdns_from f = { .src = { 10, 42, 0, 2 }, .dst = { 224, 0, 0, 251 }, .port = 5353, .ttl = 255,
	                .to_group = true };

	CHECK(mdns_accept(&ME, cable, &f) == MDNS_ON_SUBNET, "the computer on the cable, to the group");
	f.ttl = 64;
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_DROP, "TTL 64 to the group was taken");
	f.ttl = -1;
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_DROP, "no TTL to the group was taken");
	f.ttl = 255;
	f.dst[3] = 1;                                 /* 224.0.0.1 */
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_DROP, "a packet to 224.0.0.1 was taken");
	f.dst[3] = 251;
	f.src[0] = 169; f.src[1] = 254; f.src[2] = 32; f.src[3] = 109;   /* a self-assigned Mac */
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_ON_LINK, "another subnet on the link, to the group");
	/* To the handheld's own address: from its subnet only, any TTL */
	f.to_group = false;
	memcpy(f.dst, ME.addr, 4);
	f.ttl = 64;
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_DROP, "unicast from 169.254 was taken");
	f.src[0] = 10; f.src[1] = 42; f.src[2] = 0; f.src[3] = 2;
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_ON_SUBNET, "unicast from the computer was dropped");
	f.src[3] = 9;                                  /* 10.42.0.9: off the /30 */
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_DROP, "unicast from off the /30 was taken");
	f.src[3] = 2;
	f.dst[3] = 7;                                  /* to another address */
	CHECK(mdns_accept(&ME, cable, &f) == MDNS_DROP, "unicast to another address was taken");
}

/* The engine's answers by where the query came from */
static struct { int n, unicast; } sent;
static void count_send(void *ctx, const uint8_t *b, size_t len, bool unicast)
{
	(void)ctx; (void)b;
	if (len) { sent.n++; sent.unicast += unicast; }
}

static void answers_by_place(void)
{
	static const uint8_t cable[4] = { 255, 255, 255, 252 };
	mdns_engine e;
	uint8_t q[128];
	size_t n;
	mdns_from link = { .src = { 169, 254, 32, 109 }, .dst = { 224, 0, 0, 251 }, .port = 5353,
	                   .ttl = 255, .to_group = true };
	mdns_from comp = { .src = { 10, 42, 0, 2 }, .dst = { 224, 0, 0, 251 }, .port = 5353,
	                   .ttl = 255, .to_group = true };
	mdns_from far = comp;

	mdns_engine_init(&e, ME.addr, cable, false, count_send, NULL);
	mdns_engine_tick(&e, 0);
	mdns_engine_tick(&e, 1000);
	CHECK(mdns_engine_ready(&e), "the cable's engine is not ready");
	/* A QU query from another subnet on the link: to the group, not unicast */
	memset(&sent, 0, sizeof sent);
	n = unhex(MAC_QU_A, q, sizeof q);
	mdns_engine_packet(&e, q, n, &link, 5000);
	CHECK(sent.n == 1 && sent.unicast == 0, "QU from 169.254: %d sent, %d unicast", sent.n, sent.unicast);
	/* From the computer: unicast */
	memset(&sent, 0, sizeof sent);
	mdns_engine_packet(&e, q, n, &comp, 6000);
	CHECK(sent.n == 1 && sent.unicast == 1, "QU from the computer: %d sent, %d unicast", sent.n, sent.unicast);
	/* dig's one-shot query from another subnet: nothing; from the computer: answered */
	memset(&sent, 0, sizeof sent);
	n = unhex(DIG_LEGACY, q, sizeof q);
	link.port = 49511;
	mdns_engine_packet(&e, q, n, &link, 7000);
	CHECK(sent.n == 0, "a one-shot query from 169.254 was answered");
	comp.port = 49511;
	mdns_engine_packet(&e, q, n, &comp, 7100);
	CHECK(sent.n == 1 && sent.unicast == 1, "a one-shot query from the computer was not answered");
	/* Through a router (TTL 63): nothing */
	memset(&sent, 0, sizeof sent);
	n = unhex(MAC_QM_A, q, sizeof q);
	far.ttl = 63;
	mdns_engine_packet(&e, q, n, &far, 8000);
	CHECK(sent.n == 0, "a query with TTL 63 was answered");
}

/* Somebody on the Wi-Fi who answers for every name the handheld tries: it
 * gives up after MDNS_MAX_RENAMES renames rather than going on */
static void squatter(void)
{
	static const uint8_t mask[4] = { 255, 255, 255, 0 };
	static const uint8_t mine[4] = { 192, 168, 1, 50 };
	mdns_engine e;
	unsigned t;
	int renames = 0;
	char last[MDNS_HOST_MAX] = "zyperos";

	mdns_engine_init(&e, mine, mask, true, count_send, NULL);
	for (t = 0; t < 30000 && !e.gave_up; t += 10) {
		mdns_engine_tick(&e, t);
		if (t % 100 == 0) {
			/* Its answer for whatever name the handheld has now */
			mdns_self them = { "", { 192, 168, 1, 66 } };
			uint8_t a[256];
			size_t n;
			mdns_from f = { .src = { 192, 168, 1, 66 }, .dst = { 224, 0, 0, 251 }, .port = 5353,
			                .ttl = 255, .to_group = true };

			snprintf(them.host, sizeof them.host, "%s", e.me.host);
			n = mdns_announce_packet(&them, a, sizeof a, false);
			mdns_engine_packet(&e, a, n, &f, t);
			if (strcmp(last, e.me.host)) { renames++; snprintf(last, sizeof last, "%s", e.me.host); }
		}
	}
	CHECK(e.gave_up, "the squatter: never gave up");
	CHECK(renames == MDNS_MAX_RENAMES, "the squatter: %d renames", renames);
	CHECK(!mdns_engine_ready(&e), "the squatter: still answering by name");
}

int main(void)
{
	on_link();
	answers_by_place();
	squatter();
	real_queries();
	hostile();
	names();
	probes();
	two_on_wifi();
	the_cable();
	if (fails) {
		fprintf(stderr, "mdns-check: %d failed\n", fails);
		return 1;
	}
	printf("mdns-check: the Mac's queries answered, hostile packets refused, two handhelds on one Wi-Fi named apart\n");
	return 0;
}
