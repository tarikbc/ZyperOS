/* SPDX-License-Identifier: GPL-3.0-only */
/* The phone link's rules (src/phonelink.h): which interface is a phone's
 * tethering, which device is an iPhone, and File Transfer opening once a
 * link. */
#include <stdio.h>

#include "../src/phonelink.h"

static int failed;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what); failed++; } } while (0)

int main(void)
{
	/* The drivers: Android's RNDIS, CDC NCM and ECM, an iPhone's; not the
	 * handheld's own gadget link, not a Wi-Fi adapter's, not loopback */
	CHECK(phonelink_driver("rndis_host"), "Android's RNDIS not a phone's");
	CHECK(phonelink_driver("cdc_ncm"), "CDC NCM not a phone's");
	CHECK(phonelink_driver("cdc_ether"), "CDC ECM not a phone's");
	CHECK(phonelink_driver("ipheth"), "an iPhone's not a phone's");
	CHECK(!phonelink_driver("configfs-gadget.px2"), "the gadget's own usb0 taken for a phone");
	CHECK(!phonelink_driver("rtl8xxxu"), "a Wi-Fi adapter taken for a phone");
	CHECK(!phonelink_driver(""), "no driver taken for a phone");
	CHECK(!phonelink_driver(NULL), "NULL taken for a phone");

	/* The interface: the gadget's usb0 and lo are never it; a phone's is,
	 * whatever the kernel named it; one already renamed wins */
	{
		const char *names[] = { "lo", "usb0", "usb1" };
		const char *drivers[] = { "", "configfs-gadget.px2", "rndis_host" };

		CHECK(phonelink_pick(names, drivers, 3) == 2, "the phone's usb1 not picked beside the gadget's usb0");
	}
	{
		const char *names[] = { "lo", "usb0" };
		const char *drivers[] = { "", "rndis_host" };

		CHECK(phonelink_pick(names, drivers, 2) == 1, "a phone's usb0 (no gadget yet) not picked");
	}
	{
		const char *names[] = { "usb1", PHONELINK_IF };
		const char *drivers[] = { "cdc_ncm", "rndis_host" };

		CHECK(phonelink_pick(names, drivers, 2) == 1, "the renamed one not preferred");
	}
	{
		const char *names[] = { "lo", "usb0", "wlan0" };
		const char *drivers[] = { "", "configfs-gadget.px2", "rtl8xxxu" };

		CHECK(phonelink_pick(names, drivers, 3) == -1, "a link picked with no phone");
		CHECK(phonelink_pick(names, drivers, 0) == -1, "a link picked from nothing");
	}

	/* An iPhone (or an iPad): Apple's and not a Mac */
	CHECK(phonelink_is_iphone("05ac", "iPhone"), "an iPhone not seen");
	CHECK(phonelink_is_iphone("05ac", "iPad"), "an iPad not seen");
	CHECK(!phonelink_is_iphone("05ac", "Mac"), "a Mac taken for an iPhone");
	CHECK(!phonelink_is_iphone("18d1", "QCS_KALAMAP-HDK_CID:0437_SN:9C6287A0"), "the Thor taken for an iPhone");
	CHECK(!phonelink_is_iphone("05ac", NULL), "Apple with no product taken for an iPhone");
	CHECK(!phonelink_is_iphone(NULL, "iPhone"), "no vendor taken for an iPhone");
	/* As sysfs gives them: each file ends in a newline (seen on the device,
	 * 2026-11-13: the iPhone on the bus and no iPhone message) */
	CHECK(phonelink_is_iphone("05ac\n", "iPhone\n"), "an iPhone not seen as sysfs reads it");
	CHECK(phonelink_is_iphone("05ac\n", "iPad\n"), "an iPad not seen as sysfs reads it");
	CHECK(!phonelink_is_iphone("05ac\n", "Mac\n"), "a Mac taken for an iPhone as sysfs reads it");
	CHECK(!phonelink_is_iphone("18d1\n", "QCS_KALAMAP-HDK_CID:0437_SN:9C6287A0\n"),
	      "the Thor taken for an iPhone as sysfs reads it");
	CHECK(!phonelink_is_iphone("05ac0\n", "iPhone\n"), "another vendor that starts 05ac taken for Apple");
	CHECK(!phonelink_is_iphone("05ac\n", "\n"), "Apple with an empty product taken for an iPhone");
	CHECK(!phonelink_is_iphone("05ac\n", "Mac \n"), "a Mac with a trailing space taken for an iPhone");

	/* Once per link: File Transfer opens when the address comes, not again
	 * while the same link stays (the player closed it), again for a new
	 * address or after the link went */
	{
		char last[64] = "";

		CHECK(!phonelink_new(last, PHONELINK_IF, ""), "opened with no address yet");
		CHECK(phonelink_new(last, PHONELINK_IF, "192.168.42.37"), "not opened when the address came");
		CHECK(!phonelink_new(last, PHONELINK_IF, "192.168.42.37"), "opened again on the same link");
		CHECK(phonelink_new(last, PHONELINK_IF, "192.168.42.99"), "not opened for a new address");
		CHECK(!phonelink_new(last, NULL, NULL), "opened with the link gone");
		CHECK(phonelink_new(last, PHONELINK_IF, "192.168.42.99"), "not opened for the phone back");
	}

	if (failed) {
		printf("phonelink-check: %d failed\n", failed);
		return 1;
	}
	printf("phonelink-check: a phone's link told from the gadget's, iPhones seen, File Transfer once a link\n");
	return 0;
}
