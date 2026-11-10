/* SPDX-License-Identifier: GPL-3.0-only */
/* File Transfer's link steps (src/linksteps.h): a Mac that goes straight
 * through, a Windows computer with no driver, one that takes no address, a
 * charging cable, a replug after the page was open, Wi-Fi, a clock that
 * wraps, and the device's own /proc/net/arp and UDC state as they read. */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../src/linksteps.h"

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	               fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } \
} while (0)

static const link_sense NONE_ = { false, false, false, false, false };
static const link_sense TAKEN = { true, true, false, false, false };
static const link_sense ASKED = { true, false, false, false, false };
static const link_sense UP    = { true, true, true, false, false };
static const link_sense ADDR  = { true, true, true, true, false };

static void mac_straight_through(unsigned t0)
{
	link_steps s = { 0 };

	link_steps_cable(&s, &NONE_, 0, t0);
	CHECK(s.step == LS_LINK && link_steps_hint(&s, t0) == LH_NONE, "unplugged at the start");
	link_steps_cable(&s, &TAKEN, 0, t0 + 300);
	link_steps_cable(&s, &UP, 0, t0 + 600);
	CHECK(s.step == LS_ADDRESS, "the link up, no address: step %d", s.step);
	link_steps_cable(&s, &ADDR, 0, t0 + 1800);
	CHECK(s.step == LS_BROWSER && link_steps_hint(&s, t0 + 1800) == LH_NONE, "an address: step %d", s.step);
	link_steps_cable(&s, &ADDR, 3, t0 + 6000);
	CHECK(s.step == LS_DONE && link_steps_hint(&s, t0 + 6000) == LH_NONE, "a request: step %d", s.step);
	/* The neighbour entry goes stale while the page sits idle: still done */
	link_steps_cable(&s, &UP, 3, t0 + 90000);
	CHECK(s.step == LS_DONE, "done went away with a stale neighbour: step %d", s.step);
}

static void windows_no_driver(void)
{
	link_steps s = { 0 };

	link_steps_cable(&s, &TAKEN, 0, 1000);
	CHECK(link_steps_hint(&s, 1000 + LH_LINK_MS - 1) == LH_NONE, "the driver hint came early");
	CHECK(link_steps_hint(&s, 1000 + LH_LINK_MS) == LH_NO_DRIVER, "no driver hint after 10 s");
	/* Then the driver arrives: the hint goes, the address step starts */
	link_steps_cable(&s, &UP, 0, 30000);
	CHECK(s.step == LS_ADDRESS && link_steps_hint(&s, 30000) == LH_NONE, "the driver came: step %d", s.step);
	CHECK(link_steps_hint(&s, 30000 + LH_ADDRESS_MS) == LH_NO_ADDRESS, "no address hint after 10 s");
}

static void not_taken(void)
{
	link_steps s = { 0 };

	link_steps_cable(&s, &ASKED, 0, 0);
	CHECK(link_steps_hint(&s, LH_LINK_MS) == LH_NO_ANSWER, "a computer that never took it: no hint");
	/* It takes it later: the no-driver wait starts from then */
	link_steps_cable(&s, &TAKEN, 0, 20000);
	CHECK(link_steps_hint(&s, 20000 + LH_LINK_MS - 1) == LH_NONE, "the no-driver wait did not start again");
}

static void charging_cable(void)
{
	link_steps s = { 0 };
	unsigned t;

	for (t = 0; t <= LH_PLUG_MS; t += 200) link_steps_cable(&s, &NONE_, 0, t);
	CHECK(link_steps_hint(&s, LH_PLUG_MS) == LH_NO_COMPUTER, "a cable with no computer: no hint");
}

static void no_browser(void)
{
	link_steps s = { 0 };

	link_steps_cable(&s, &ADDR, 0, 500);
	CHECK(link_steps_hint(&s, 500 + LH_BROWSER_MS - 1) == LH_NONE, "the browser hint came early");
	CHECK(link_steps_hint(&s, 500 + LH_BROWSER_MS) == LH_NO_BROWSER, "no browser hint after 30 s");
	link_steps_cable(&s, &ADDR, 1, 500 + LH_BROWSER_MS + 5);
	CHECK(s.step == LS_DONE && link_steps_hint(&s, 500 + LH_BROWSER_MS + 5) == LH_NONE,
	      "the browser came: step %d", s.step);
}

/* The page was open, the cable comes out and goes back: the requests from
 * before do not count for the new link, only one after it */
static void replug(void)
{
	link_steps s = { 0 };

	link_steps_cable(&s, &ADDR, 0, 0);
	link_steps_cable(&s, &ADDR, 40, 1000);
	CHECK(s.step == LS_DONE, "first link: step %d", s.step);
	link_steps_cable(&s, &NONE_, 40, 2000);
	CHECK(s.step == LS_LINK, "the cable out: step %d", s.step);
	link_steps_cable(&s, &UP, 40, 4000);
	CHECK(s.step == LS_ADDRESS, "back in: step %d", s.step);
	link_steps_cable(&s, &ADDR, 40, 5000);
	CHECK(s.step == LS_BROWSER, "back in, an address: step %d", s.step);
	link_steps_cable(&s, &ADDR, 41, 5500);
	CHECK(s.step == LS_DONE, "the open page asked again: step %d", s.step);
}

/* A request before the neighbour entry shows: an address, and a browser */
static void request_first(void)
{
	link_steps s = { 0 };

	link_steps_cable(&s, &UP, 0, 0);
	link_steps_cable(&s, &UP, 1, 100);
	CHECK(s.step == LS_DONE, "a request with no neighbour entry yet: step %d", s.step);
}

static void wifi(void)
{
	link_steps s = { 0 };

	link_steps_wifi(&s, false, 0, 0);
	CHECK(s.step == LS_LINK && link_steps_hint(&s, LH_PLUG_MS) == LH_NO_NETWORK, "Wi-Fi gone: no hint");
	link_steps_wifi(&s, true, 0, 6000);
	CHECK(s.step == LS_BROWSER && link_steps_hint(&s, 6000) == LH_NONE, "on Wi-Fi: step %d", s.step);
	CHECK(link_steps_hint(&s, 6000 + LH_BROWSER_MS) == LH_NO_BROWSER, "Wi-Fi, no browser: no hint");
	link_steps_wifi(&s, true, 2, 40000);
	CHECK(s.step == LS_DONE, "Wi-Fi, a browser: step %d", s.step);
}

/* A Mac whose port took the sink side: no power, so no computer by the
 * PHY, but usbrole says something waits for power. "waiting", then after
 * 5 s the replug hint, not the charging cable's */
static void computer_waits(void)
{
	static const link_sense WAITS = { false, false, false, false, true };
	link_steps s = { 0 };

	link_steps_cable(&s, &WAITS, 0, 0);
	CHECK(s.step == LS_LINK && s.sub == 4, "the computer waits: step %d.%d", s.step, s.sub);
	CHECK(link_steps_hint(&s, LH_PLUG_MS - 1) == LH_NONE, "the replug hint came early");
	CHECK(link_steps_hint(&s, LH_PLUG_MS) == LH_REPLUG, "no replug hint after 5 s");
	/* Plugged in again, the Mac gives power: on to the address */
	link_steps_cable(&s, &UP, 0, 9000);
	CHECK(s.step == LS_ADDRESS, "after the replug: step %d", s.step);
}

static void the_files(void)
{
	/* /proc/net/arp on the Pixel 2, 2026-11-10, a Mac on the cable */
	static const char arp[] =
		"IP address       HW type     Flags       HW address            Mask     Device\n"
		"10.42.0.2        0x1         0x2         02:70:78:32:00:02     *        usb0\n";
	static const char incomplete[] =
		"IP address       HW type     Flags       HW address            Mask     Device\n"
		"10.42.0.2        0x1         0x0         00:00:00:00:00:00     *        usb0\n";
	static const char other[] =
		"IP address       HW type     Flags       HW address            Mask     Device\n"
		"192.168.1.1      0x1         0x2         a4:91:b1:00:11:22     *        wlan0\n"
		"10.42.0.2        0x1         0x0         00:00:00:00:00:00     *        usb0\n"
		"10.42.0.3        0x1         0x2         02:70:78:32:00:02     *        usb0";
	char ip[64] = "";

	CHECK(link_arp_complete(arp, "usb0", ip, sizeof ip) && !strcmp(ip, "10.42.0.2"), "the Mac's entry: %s", ip);
	CHECK(!link_arp_complete(incomplete, "usb0", NULL, 0), "an incomplete entry read as an address");
	CHECK(!link_arp_complete(arp, "wlan0", NULL, 0), "usb0's entry read for wlan0");
	CHECK(link_arp_complete(other, "usb0", ip, sizeof ip) && !strcmp(ip, "10.42.0.3"),
	      "the complete usb0 entry after others, no newline at the end: %s", ip);
	CHECK(!link_arp_complete("", "usb0", NULL, 0) && !link_arp_complete(NULL, "usb0", NULL, 0),
	      "nothing read as an address");
	CHECK(!link_arp_complete("IP address HW type Flags HW address Mask Device", "usb0", NULL, 0),
	      "the header alone read as an address");

	CHECK(link_udc_configured("configured\n"), "configured");
	CHECK(!link_udc_configured("not attached\n") && !link_udc_configured("addressed\n") &&
	      !link_udc_configured("default\n") && !link_udc_configured("suspended\n") &&
	      !link_udc_configured(""), "a state other than configured read as configured");
}

int main(void)
{
	mac_straight_through(1000);
	mac_straight_through(UINT_MAX - 2500);       /* the clock wraps in the middle */
	{
		link_steps s = { 0 };

		link_steps_cable(&s, &ADDR, 0, UINT_MAX - 100);
		CHECK(link_steps_hint(&s, LH_BROWSER_MS - 101) == LH_NO_BROWSER, "a wrapped clock lost the hint");
	}
	windows_no_driver();
	not_taken();
	charging_cable();
	no_browser();
	replug();
	request_first();
	wifi();
	computer_waits();
	the_files();
	if (fails) {
		fprintf(stderr, "linksteps-check: %d failed\n", fails);
		return 1;
	}
	printf("linksteps-check: every step and hint of the cable and Wi-Fi, and the device's files\n");
	return 0;
}
