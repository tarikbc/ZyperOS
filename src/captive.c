/* SPDX-License-Identifier: GPL-3.0-only */
/* The pure half of the hotspot. See captive.h. */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "captive.h"

#define DNS_HEAD   12
#define DNS_A      1
#define DNS_IN     1

static unsigned be16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }

static void put16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)(v >> 8);
	p[1] = (uint8_t)v;
}

size_t captive_dns_answer(const uint8_t *q, size_t n, const uint8_t ip[4],
                          uint8_t *out, size_t cap)
{
	unsigned flags, qtype, qclass;
	size_t at = DNS_HEAD, name = 0, end, len;
	bool answer;

	if (!q || !out || n < DNS_HEAD || n > 512) return 0;
	flags = be16(q + 2);
	/* A query (QR clear) of the standard kind (opcode 0), asking one thing.
	 * A phone never asks two at once; anything else is not a phone looking
	 * a name up, and silence is the safest answer to it. */
	if ((flags & 0x8000) || ((flags >> 11) & 0xf) != 0) return 0;
	if (be16(q + 4) != 1) return 0;

	/* The name, label by label. No compression pointers: a question is the
	 * first name in the packet, with nothing before it to point at. */
	for (;;) {
		unsigned l;

		if (at >= n) return 0;
		l = q[at];
		if (l == 0) { at++; break; }
		if (l > 63) return 0;
		name += l + 1;
		if (name > 255) return 0;
		at += 1 + l;
	}
	if (at + 4 > n) return 0;
	qtype  = be16(q + at);
	qclass = be16(q + at + 2);
	end = at + 4;

	answer = qtype == DNS_A && qclass == DNS_IN;
	len = end + (answer ? 16 : 0);
	if (len > cap) return 0;

	/* The question as it came, then the answer pointing back at its name. */
	memcpy(out, q, end);
	/* QR, authoritative, recursion available, the asker's RD kept, no error:
	 * as far as a phone can tell, this is the network's own resolver and
	 * that is the address. */
	put16(out + 2, 0x8000 | 0x0400 | (flags & 0x0100) | 0x0080);
	put16(out + 6, answer ? 1 : 0);
	put16(out + 8, 0);
	put16(out + 10, 0);
	if (answer) {
		uint8_t *a = out + end;

		put16(a, 0xc000 | DNS_HEAD);       /* the question's name */
		put16(a + 2, DNS_A);
		put16(a + 4, DNS_IN);
		put16(a + 6, 0);
		put16(a + 8, CAPTIVE_DNS_TTL);
		put16(a + 10, 4);
		memcpy(a + 12, ip, 4);
	}
	return len;
}

bool captive_host_is(const char *host, const char *addr)
{
	size_t k;

	if (!host || !*host) return true;
	if (!addr || !*addr) return false;
	k = strlen(addr);
	if (strncasecmp(host, addr, k)) return false;
	host += k;
	if (*host == '.') host++;              /* a fully qualified "x." */
	if (*host == '\0') return true;
	if (*host++ != ':' || !*host) return false;
	while (*host) if (!isdigit((unsigned char)*host++)) return false;
	return true;
}

/* One field of the WIFI: form, its specials escaped. */
static bool put_field(char *out, size_t n, size_t *at, const char *s)
{
	for (; *s; s++) {
		bool esc = strchr("\\;,:\"", *s) != NULL;

		if (*at + (esc ? 2 : 1) >= n) return false;
		if (esc) out[(*at)++] = '\\';
		out[(*at)++] = *s;
	}
	return true;
}

static bool put_text(char *out, size_t n, size_t *at, const char *s)
{
	size_t k = strlen(s);

	if (*at + k >= n) return false;
	memcpy(out + *at, s, k);
	*at += k;
	return true;
}

bool captive_wifi_qr(char *out, size_t n, const char *ssid, const char *pass)
{
	size_t at = 0;

	if (!out || n == 0 || !ssid || !pass) return false;
	out[0] = '\0';
	if (!put_text(out, n, &at, "WIFI:T:WPA;S:") || !put_field(out, n, &at, ssid) ||
	    !put_text(out, n, &at, ";P:") || !put_field(out, n, &at, pass) ||
	    !put_text(out, n, &at, ";;")) {
		out[0] = '\0';
		return false;
	}
	out[at] = '\0';
	return true;
}

/* Digits and lowercase letters, less 0 o 1 l i: what is left can be read off
 * a three-inch screen and typed on a laptop without a second look. */
static const char PASS_SET[] = "23456789abcdefghjkmnpqrstuvwxyz";

void captive_password(char *out, size_t n, const uint8_t *rnd)
{
	size_t k, at = 0;

	if (!out || n < 10) { if (out && n) out[0] = '\0'; return; }
	for (k = 0; k < CAPTIVE_PASS_RANDOM; k++) {
		if (k == 4) out[at++] = '-';
		out[at++] = PASS_SET[rnd[k] % (sizeof PASS_SET - 1)];
	}
	out[at] = '\0';
}

void captive_ssid(char *out, size_t n, const char *mac)
{
	int k;
	char tail[5];

	if (!out || n == 0) return;
	/* "aa:bb:cc:dd:ee:ff": the last two bytes are at 12 and 15 */
	if (mac && strlen(mac) >= 17) {
		const char *p[4] = { mac + 12, mac + 13, mac + 15, mac + 16 };

		for (k = 0; k < 4; k++) {
			if (!isxdigit((unsigned char)*p[k])) break;
			tail[k] = (char)toupper((unsigned char)*p[k]);
		}
		if (k == 4 && mac[14] == ':') {
			tail[4] = '\0';
			snprintf(out, n, "ZyperOS-%s", tail);
			return;
		}
	}
	snprintf(out, n, "ZyperOS");
}
