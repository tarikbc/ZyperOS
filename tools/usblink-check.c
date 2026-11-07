/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* USB charges only until asked (src/usblink.h).
 *
 *     make check-usblink
 *
 * The port and the link are stubs here; the rule is the real one, with a
 * real device database in /tmp. What must hold: a computer is asked about
 * once per plug and a charger never; unplugging ends every choice; File
 * Transfer's link goes when its screen closes and Developer (SSH)'s when the
 * cable does; Developer Mode keeps the link on with no question, is on by
 * default on a development image and absent on a release one (which writes
 * neither it nor its file), and its file on the card says what a development
 * image's S40usbgadget will do at the next boot.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../src/usblink.h"
#include "../src/usbpeer.h"

static int failures;

#define CHECK(cond, ...)                                                      \
	do {                                                                      \
		if (!(cond)) {                                                        \
			printf("  FAIL: ");                                               \
			printf(__VA_ARGS__);                                              \
			printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
			failures++;                                                       \
		}                                                                     \
	} while (0)

/* ---- the hardware, as stubs ------------------------------------------- */

static usb_peer g_port = USB_PEER_NONE;
static bool     g_bound;
static int      g_binds;

usb_peer plat_usb_peer(void) { return g_port; }

bool plat_usb_link(int op)
{
	if (op == 2) g_bound = false;            /* a fresh link: off, then on */
	if ((op == 1 || op == 2) && !g_bound) { g_bound = true; g_binds++; }
	if (op == 0) g_bound = false;
	return g_bound;
}

static unsigned t;
static bool step(void) { t += 600; return usblink_follow(db_dev(), t); }

static char flag_path[256];
static int flag(void)
{
	FILE *f = fopen(flag_path, "r");
	int c;

	if (!f) return -1;
	c = fgetc(f);
	fclose(f);
	return c == '1' ? 1 : c == '0' ? 0 : -2;
}

int main(void)
{
	char dir[] = "/tmp/usblink-checkXXXXXX", dev[300], lib[300], script[300];
	FILE *f;

	if (!mkdtemp(dir)) { perror("mkdtemp"); return 1; }
	snprintf(dev, sizeof dev, "%s/zyperos.db", dir);
	snprintf(lib, sizeof lib, "%s/library.db", dir);
	snprintf(flag_path, sizeof flag_path, "%s/developer", dir);
	/* The development image's marker, where the build pointed DEV_SCRIPT */
	snprintf(script, sizeof script, "%s", USBLINK_CHECK_SCRIPT);
	unlink(script);
	if (!db_init(dev, lib, NULL)) { printf("no database\n"); return 1; }

	printf("  a release image (no dropbear at boot):\n");
	CHECK(!usblink_dev_image(), "read as a development image");
	CHECK(!usblink_developer(db_dev()), "Developer Mode on a release image");
	usblink_sync(db_dev(), dir);
	CHECK(flag() == -1, "a release image wrote the card's file (%d)", flag());
	usblink_set_developer(db_dev(), dir, true);
	CHECK(!usblink_developer(db_dev()) && flag() == -1, "Developer Mode turned on in a release image");

	printf("  a charger is never asked about, and binds nothing:\n");
	g_port = USB_PEER_CHARGER;
	CHECK(!step() && !step(), "a charger was asked about");
	CHECK(!g_bound, "a charger bound the link");

	printf("  a computer is asked about once per plug:\n");
	g_port = USB_PEER_COMPUTER;
	CHECK(step(), "a computer was not asked about");
	CHECK(!g_bound, "the question bound the link before an answer");
	CHECK(!step() && !step(), "asked twice in one plug");
	g_port = USB_PEER_NONE;
	step();
	g_port = USB_PEER_COMPUTER;
	CHECK(step(), "a second plug was not asked about");

	printf("  Charge Only keeps the link off:\n");
	usblink_hold(false);
	CHECK(!g_bound && !step(), "Charge Only bound, or asked again");

	printf("  File Transfer: on with its screen, off as it closes:\n");
	usblink_for_transfer();
	CHECK(g_bound, "the screen opened with the link off");
	usblink_after_transfer(db_dev());
	CHECK(!g_bound, "the link outlived the screen");

	printf("  Developer (SSH): on until the cable comes out:\n");
	usblink_hold(true);
	CHECK(g_bound, "Developer (SSH) left the link off");
	usblink_for_transfer();
	usblink_after_transfer(db_dev());
	CHECK(g_bound, "closing USB File Transfer took Developer (SSH)'s link");
	step();
	CHECK(g_bound, "the link went with the cable still in");
	g_port = USB_PEER_NONE;
	step();
	CHECK(!g_bound, "the link stayed with the cable out");

	printf("  a development image (dropbear at boot) is in Developer Mode:\n");
	f = fopen(script, "w");
	if (f) fclose(f);
	CHECK(usblink_dev_image(), "not read as a development image");
	CHECK(usblink_developer(db_dev()), "Developer Mode is not the default there");
	usblink_sync(db_dev(), dir);
	CHECK(flag() == 1, "the card's file says %d, not 1", flag());
	g_port = USB_PEER_COMPUTER;
	CHECK(!step(), "asked about a computer in Developer Mode");
	CHECK(g_bound, "Developer Mode left the link off");
	usblink_after_transfer(db_dev());
	CHECK(g_bound, "closing USB File Transfer took Developer Mode's link");
	g_port = USB_PEER_NONE;
	step();
	CHECK(g_bound, "Developer Mode let the link go with the cable out");
	{
		int was = g_binds;

		g_port = USB_PEER_COMPUTER;
		CHECK(!step(), "asked about a computer in Developer Mode, plugged in again");
		CHECK(g_bound && g_binds == was + 1,
		      "a computer plugged in again in Developer Mode got no fresh link (%d binds)",
		      g_binds - was);
		CHECK(!step() && g_binds == was + 1, "the link was made fresh again with no new plug");
		g_port = USB_PEER_NONE;
		step();
	}

	printf("  Developer Mode off: the link goes, the next computer is asked:\n");
	usblink_set_developer(db_dev(), dir, false);
	CHECK(!usblink_developer(db_dev()) && flag() == 0, "it did not turn off, or its file says %d", flag());
	CHECK(!g_bound, "the link stayed on");
	g_port = USB_PEER_COMPUTER;
	CHECK(step(), "the next computer was not asked about");
	usblink_set_developer(db_dev(), dir, true);
	CHECK(g_bound && flag() == 1 && !step(), "on again did not bind, or asked");

	db_shutdown();
	unlink(dev); unlink(lib); unlink(flag_path); unlink(script);
	{
		char wal[320];

		snprintf(wal, sizeof wal, "%s-wal", dev); unlink(wal);
		snprintf(wal, sizeof wal, "%s-shm", dev); unlink(wal);
		snprintf(wal, sizeof wal, "%s-wal", lib); unlink(wal);
		snprintf(wal, sizeof wal, "%s-shm", lib); unlink(wal);
	}
	rmdir(dir);
	if (failures) {
		printf("usblink: %d FAILED\n", failures);
		return 1;
	}
	printf("usblink: a computer is asked about once per plug and a charger never; every choice ends "
	       "with its screen or its cable; Developer Mode keeps the link, by default only on a "
	       "development image, and its file says so for the next boot\n");
	return 0;
}
