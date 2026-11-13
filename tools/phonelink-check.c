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

	/* A way out: Wi-Fi, or a default route on a link that is not lo; the
	 * cable's link to a computer is never one. The tables as the kernel
	 * writes them: a header, tabs, a newline after each line (the Pixel 9
	 * Pro XL's lease, 2026-11-13: 10.44.47.242/24 via 10.44.47.248) */
	{
		static const char HDR[] = "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n";
		static const char CABLE[] = "usb0\t00002A0A\t00000000\t0001\t0\t0\t0\tFCFFFFFF\t0\t0\t0\n";
		static const char PHONE[] = "phone0\t00000000\tF82F2C0A\t0003\t0\t0\t0\t00000000\t0\t0\t0\n"
		                            "phone0\t002F2C0A\t00000000\t0001\t0\t0\t0\t00FFFFFF\t0\t0\t0\n";
		char t[1024];
		bool metered = true;

		CHECK(!phonelink_way_out(false, NULL, NULL), "a way out with no Wi-Fi and no table");
		CHECK(!phonelink_way_out(false, "", &metered) && !metered, "a way out from an empty table");
		CHECK(phonelink_way_out(true, NULL, &metered) && !metered, "Wi-Fi not a way out, or taken for the phone's");
		CHECK(!phonelink_way_out(false, HDR, NULL), "the header taken for a route");
		snprintf(t, sizeof t, "%s%s", HDR, CABLE);
		CHECK(!phonelink_way_out(false, t, NULL), "the cable to a computer taken for a way out");
		snprintf(t, sizeof t, "%s%s%s", HDR, CABLE, PHONE);
		CHECK(phonelink_way_out(false, t, &metered), "the phone's lease not a way out");
		CHECK(metered, "the phone's way out not taken for its mobile data");
		CHECK(phonelink_way_out(true, t, &metered) && metered, "Wi-Fi said while the route goes by the phone");
		snprintf(t, sizeof t, "%seth0\t00000000\t0202000A\t0003\t0\t0\t100\t00000000\t0\t0\t0\n", HDR);
		CHECK(phonelink_way_out(false, t, &metered) && !metered, "QEMU's network not a way out, or taken for a phone");
		snprintf(t, sizeof t, "%swlan0\t00000000\t0101A8C0\t0003\t0\t0\t0\t00000000\t0\t0\t0\n"
		         "phone0\t00000000\tF82F2C0A\t0003\t0\t0\t10\t00000000\t0\t0\t0\n", HDR);
		CHECK(phonelink_way_out(true, t, &metered) && !metered, "the phone taken over Wi-Fi's lower metric");
		snprintf(t, sizeof t, "%swlan0\t00000000\t0101A8C0\t0003\t0\t0\t20\t00000000\t0\t0\t0\n"
		         "phone0\t00000000\tF82F2C0A\t0003\t0\t0\t10\t00000000\t0\t0\t0\n", HDR);
		CHECK(phonelink_way_out(true, t, &metered) && metered, "the phone's lower metric not taken");
		snprintf(t, sizeof t, "%slo\t00000000\t00000000\t0001\t0\t0\t0\t00000000\t0\t0\t0\n", HDR);
		CHECK(!phonelink_way_out(false, t, NULL), "lo taken for a way out");
		snprintf(t, sizeof t, "%sphone0\t00000000\tF82F2C0A\t0002\t0\t0\t0\t00000000\t0\t0\t0\n", HDR);
		CHECK(!phonelink_way_out(false, t, NULL), "a route that is not up taken for a way out");
		CHECK(phonelink_way_out(false, "phone0\t00000000\tF82F2C0A\t0003\t0\t0\t0\t00000000\t0\t0\t0",
		                        &metered) && metered, "a last line with no newline not read");
	}

	if (failed) {
		printf("phonelink-check: %d failed\n", failed);
		return 1;
	}
	printf("phonelink-check: a phone's link told from the gadget's, iPhones seen, File Transfer once a link, a way out and whose\n");
	return 0;
}
