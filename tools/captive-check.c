/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-captive
 *
 * The hotspot's pure half (src/captive.c): the DNS answer every name a phone
 * looks up gets, the Host check that tells a phone's internet check from a
 * request for the transfer page, the QR code's text and the password. Each is
 * a way the phone never opens its sign-in sheet, or opens it on the wrong
 * page, or cannot join at all - and none can be seen from the handheld. */
#include "../src/captive.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, what) do { \
	if (!(cond)) { printf("  FAIL: %s (line %d)\n", what, __LINE__); failures++; } \
} while (0)

static const uint8_t IP[4] = { 10, 42, 1, 1 };

/* A query for `name` of `type`, as a phone's resolver sends it: id 0x1234,
 * recursion desired, and an EDNS OPT record after the question when `edns`. */
static size_t query(uint8_t *q, const char *name, unsigned type, int edns)
{
	size_t at = 12;
	const char *p = name;

	memset(q, 0, 12);
	q[0] = 0x12; q[1] = 0x34; q[2] = 0x01; q[5] = 1;
	if (edns) q[11] = 1;
	while (*p) {
		const char *dot = strchr(p, '.');
		size_t l = dot ? (size_t)(dot - p) : strlen(p);

		q[at++] = (uint8_t)l;
		memcpy(q + at, p, l);
		at += l;
		p += l + (dot ? 1 : 0);
	}
	q[at++] = 0;
	q[at++] = (uint8_t)(type >> 8); q[at++] = (uint8_t)type;
	q[at++] = 0; q[at++] = 1;
	if (edns) {
		static const uint8_t opt[] = { 0, 0, 41, 0x10, 0, 0, 0, 0, 0, 0, 0 };

		memcpy(q + at, opt, sizeof opt);
		at += sizeof opt;
	}
	return at;
}

static void dns(void)
{
	uint8_t q[512], r[512];
	size_t n, k;

	puts("dns");
	n = query(q, "captive.apple.com", 1, 1);
	k = captive_dns_answer(q, n, IP, r, sizeof r);
	CHECK(k == 12 + 19 + 4 + 16, "an A question gets the question and one answer, no OPT");
	CHECK(r[0] == 0x12 && r[1] == 0x34, "the reply keeps the id");
	CHECK(r[2] == 0x85 && r[3] == 0x80, "QR, authoritative, RD kept, RA, no error");
	CHECK(r[5] == 1 && r[7] == 1 && r[9] == 0 && r[11] == 0, "one question, one answer, nothing else");
	CHECK(!memcmp(r + 12, q + 12, 23), "the question as it came");
	CHECK(r[35] == 0xc0 && r[36] == 12, "the answer points at the question's name");
	CHECK(r[38] == 1 && r[40] == 1, "type A, class IN");
	CHECK(r[44] == 10 && r[46] == 4, "ten seconds, four bytes");
	CHECK(!memcmp(r + 47, IP, 4), "the handheld's address");

	n = query(q, "connectivitycheck.gstatic.com", 28, 0);
	k = captive_dns_answer(q, n, IP, r, sizeof r);
	CHECK(k == n, "AAAA gets the question back and nothing else");
	CHECK((r[3] & 0x0f) == 0 && r[7] == 0, "an empty NOERROR, not an error a phone takes for no network");

	n = query(q, "www.msftconnecttest.com", 1, 0);
	q[2] |= 0x80;
	CHECK(captive_dns_answer(q, n, IP, r, sizeof r) == 0, "a reply is not answered");
	q[2] = 0x01 | (2 << 3);
	CHECK(captive_dns_answer(q, n, IP, r, sizeof r) == 0, "only the standard query");
	q[2] = 0x01;
	q[5] = 2;
	CHECK(captive_dns_answer(q, n, IP, r, sizeof r) == 0, "two questions is not a phone");
	q[5] = 1;
	CHECK(captive_dns_answer(q, n - 3, IP, r, sizeof r) == 0, "a question cut short");
	CHECK(captive_dns_answer(q, 11, IP, r, sizeof r) == 0, "shorter than a header");
	CHECK(captive_dns_answer(q, n, IP, r, n + 15) == 0, "no room for the answer: no half reply");
	q[12] = 0xc0;
	CHECK(captive_dns_answer(q, n, IP, r, sizeof r) == 0, "a pointer in the question");

	/* The name is a run of labels the packet must hold to its end */
	memset(q, 0, sizeof q);
	q[5] = 1;
	for (k = 12; k < 12 + 5 * 64; k += 64) q[k] = 63;
	CHECK(captive_dns_answer(q, 512, IP, r, sizeof r) == 0, "a name over 255");
	n = query(q, "zyperos", 1, 0);
	q[12 + 1 + 7] = 0;
	q[12] = 40;
	CHECK(captive_dns_answer(q, n, IP, r, sizeof r) == 0, "a label past the end");
}

static void hosts(void)
{
	puts("hosts");
	CHECK(captive_host_is("10.42.1.1", CAPTIVE_ADDR), "the address");
	CHECK(captive_host_is("10.42.1.1:80", CAPTIVE_ADDR), "with its port");
	CHECK(captive_host_is("10.42.1.1.", CAPTIVE_ADDR), "fully qualified");
	CHECK(captive_host_is(NULL, CAPTIVE_ADDR), "HTTP/1.0 has no Host");
	CHECK(captive_host_is("", CAPTIVE_ADDR), "an empty Host");
	CHECK(!captive_host_is("captive.apple.com", CAPTIVE_ADDR), "Apple's check");
	CHECK(!captive_host_is("connectivitycheck.gstatic.com", CAPTIVE_ADDR), "Android's");
	CHECK(!captive_host_is("www.msftconnecttest.com", CAPTIVE_ADDR), "Windows'");
	CHECK(!captive_host_is("10.42.1.10", CAPTIVE_ADDR), "a longer address");
	CHECK(!captive_host_is("10.42.1.1x", CAPTIVE_ADDR), "a name that starts with it");
	CHECK(!captive_host_is("10.42.1.1:", CAPTIVE_ADDR), "a colon and no port");
	CHECK(!captive_host_is("10.42.1.1:8o", CAPTIVE_ADDR), "a port that is not a number");
	CHECK(!captive_host_is("10.42.1.1", ""), "no address of our own");
}

static void qr(void)
{
	char t[160];

	puts("qr text");
	CHECK(captive_wifi_qr(t, sizeof t, "ZyperOS-7Q2K", "q7mx-3kpa") &&
	      !strcmp(t, "WIFI:T:WPA;S:ZyperOS-7Q2K;P:q7mx-3kpa;;"), "the plain form");
	CHECK(captive_wifi_qr(t, sizeof t, "a;b,c:d\"e\\f", "p;w") &&
	      !strcmp(t, "WIFI:T:WPA;S:a\\;b\\,c\\:d\\\"e\\\\f;P:p\\;w;;"), "every special escaped");
	CHECK(captive_wifi_qr(t, sizeof t, "Zyper's Pixel 2", "x") &&
	      !strcmp(t, "WIFI:T:WPA;S:Zyper's Pixel 2;P:x;;"), "an apostrophe and spaces as they are");
	CHECK(!captive_wifi_qr(t, 20, "ZyperOS-7Q2K", "q7mx-3kpa") && t[0] == '\0', "too long: nothing");
	CHECK(captive_wifi_qr(t, 40, "ZyperOS-7Q2K", "q7mx-3kpa") && strlen(t) == 39, "exactly fits");
}

static void password(void)
{
	uint8_t r[CAPTIVE_PASS_RANDOM];
	char p[16];
	int k, i, seen[256] = { 0 };

	puts("password");
	for (k = 0; k < 256; k++) {
		for (i = 0; i < CAPTIVE_PASS_RANDOM; i++) r[i] = (uint8_t)(k + i * 37);
		captive_password(p, sizeof p, r);
		CHECK(strlen(p) == 9 && p[4] == '-', "two groups of four");
		for (i = 0; i < 9; i++) {
			if (i == 4) continue;
			seen[(unsigned char)p[i]] = 1;
			CHECK(!strchr("01oli", p[i]) && (p[i] >= 'a' || (p[i] >= '2' && p[i] <= '9')),
			      "no look-alikes, nothing but digits and lowercase");
		}
	}
	for (i = 0, k = 0; i < 256; i++) k += seen[i];
	CHECK(k == 31, "every one of the 31 used");
	captive_password(p, 9, r);
	CHECK(p[0] == '\0', "no room: nothing");
}

static void ssid(void)
{
	char s[40];

	puts("ssid");
	captive_ssid(s, sizeof s, "50:3e:aa:12:7a:2b");
	CHECK(!strcmp(s, "ZyperOS-7A2B"), "the adapter's last two bytes, in capitals");
	captive_ssid(s, sizeof s, "50:3e:aa:12:7a:2b\n");
	CHECK(!strcmp(s, "ZyperOS-7A2B"), "as sysfs gives it, with its newline");
	captive_ssid(s, sizeof s, NULL);
	CHECK(!strcmp(s, "ZyperOS"), "no address");
	captive_ssid(s, sizeof s, "50:3e:aa:12:7g:2b");
	CHECK(!strcmp(s, "ZyperOS"), "not hex");
	captive_ssid(s, sizeof s, "50:3e:aa:12:7a");
	CHECK(!strcmp(s, "ZyperOS"), "too short");
}

int main(void)
{
	dns();
	hosts();
	qr();
	password();
	ssid();
	if (failures) { printf("captive: %d FAILED\n", failures); return 1; }
	puts("captive: ok");
	return 0;
}
