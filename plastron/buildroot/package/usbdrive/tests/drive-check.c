/* SPDX-License-Identifier: GPL-3.0-or-later */
/* usbdrive's rules (src/drive.h): never exported while mounted or held; and
 * the export's end: ejected, the cable out, POWER, nobody taking it. */
#include <stdio.h>
#include <string.h>

#include "../src/drive.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
	fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

int main(void)
{
	drv_wait w;
	unsigned t;

	/* Fail closed */
	CHECK(!drv_may_export(true, 0), "exported while mounted");
	CHECK(!drv_may_export(false, 1), "exported while a program holds the card");
	CHECK(!drv_may_export(true, 3), "exported mounted and held");
	CHECK(drv_may_export(false, 0), "not exported, unmounted and free");

	/* A Mac takes it, writes, ejects */
	memset(&w, 0, sizeof w);
	CHECK(drv_wait_step(&w, true, true, false, false, 1000) == DRV_STAY, "at once: ended");
	CHECK(drv_wait_step(&w, true, true, true, false, 3000) == DRV_STAY, "configured: ended");
	CHECK(drv_wait_step(&w, true, true, true, false, 60000) == DRV_STAY, "a minute in use: ended");
	CHECK(drv_wait_step(&w, true, false, true, false, 61000) == DRV_EJECTED, "the file cleared: not ejected");

	/* No file ever (the LUN refused it): not an eject */
	memset(&w, 0, sizeof w);
	CHECK(drv_wait_step(&w, true, false, true, false, 0) == DRV_STAY, "no file at the start read as an eject");

	/* The cable out: a dip is not it, 1.5 s of no power is */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, true, false, 0);
	CHECK(drv_wait_step(&w, false, true, true, false, 5000) == DRV_STAY, "unplugged at once");
	CHECK(drv_wait_step(&w, true, true, true, false, 5400) == DRV_STAY, "power back: ended");
	CHECK(drv_wait_step(&w, false, true, true, false, 6000) == DRV_STAY, "the dip's start counted on");
	CHECK(drv_wait_step(&w, false, true, true, false, 6000 + DRV_UNPLUG_MS - 1) == DRV_STAY, "unplugged early");
	CHECK(drv_wait_step(&w, false, true, true, false, 6000 + DRV_UNPLUG_MS) == DRV_UNPLUGGED, "not unplugged");

	/* POWER: at once, whatever else */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, true, false, 0);
	CHECK(drv_wait_step(&w, true, true, true, true, 100) == DRV_POWER, "POWER not taken");

	/* A charger, or a cable that only charges: nobody configures it */
	memset(&w, 0, sizeof w);
	for (t = 0; t < DRV_NOHOST_MS; t += 200)
		CHECK(drv_wait_step(&w, true, true, false, false, t) == DRV_STAY, "no computer: ended at %u", t);
	CHECK(drv_wait_step(&w, true, true, false, false, DRV_NOHOST_MS) == DRV_NOHOST, "no computer: never given up");
	/* ...but one that took it a moment before keeps it */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, false, false, 0);
	drv_wait_step(&w, true, true, true, false, 59000);
	CHECK(drv_wait_step(&w, true, true, false, false, DRV_NOHOST_MS + 1) == DRV_STAY,
	      "a computer that took it, given up");

	/* The clock's wrap */
	memset(&w, 0, sizeof w);
	drv_wait_step(&w, true, true, true, false, 0xfffffe00u);
	drv_wait_step(&w, false, true, true, false, 0xffffff00u);
	CHECK(drv_wait_step(&w, false, true, true, false, 0xffffff00u + DRV_UNPLUG_MS) == DRV_UNPLUGGED,
	      "unplugged across the wrap");

	if (fails) {
		fprintf(stderr, "usbdrive drive-check: %d failed\n", fails);
		return 1;
	}
	printf("usbdrive drive-check: never exported mounted or held; ejected, unplugged (1.5 s), POWER, "
	       "no computer in a minute\n");
	return 0;
}
