/* SPDX-License-Identifier: GPL-3.0-only */
/* make check-hwreport: the log pack's hardware report (src/hwreport.c): its
 * lines from the facts, "-" for what isn't known, nothing cut past its
 * buffer, and the readers of the kernel's text it is made from. */
#include <stdio.h>
#include <string.h>

#include "../src/hwreport.h"

static int fails, checks;

static void ck(int cond, const char *what)
{
	checks++;
	if (!cond) {
		fails++;
		printf("  FAIL %s\n", what);
	}
}

int main(void)
{
	hw_facts f;
	char out[2048], line[96], in[HW_INPUTS][64];
	static const char devices[] =
		"I: Bus=0019 Vendor=0001 Product=0001 Version=0100\n"
		"N: Name=\"axp2202-pek\"\nP: Phys=m1kbd/input2\n\n"
		"I: Bus=0003 Vendor=0000 Product=0000 Version=0000\n"
		"N: Name=\"brick-pad\"\nH: Handlers=event3 js0\n\n";
	static const char klog[] =
		"<6>[    0.512345] brick panel: 0x0b = 01 02 03 04 (11)\n"
		"<6>[    0.512400] brick panel: jd9168, with TrimUI's waits\n"
		"<6>[    1.002000] PVR_K:  301: Read BVNC 22.102.54.38 from HW device registers\n";

	memset(&f, 0, sizeof f);
	strcpy(f.panel, "jd9168, with TrimUI's waits");
	f.n_inputs = 2;
	strcpy(f.inputs[0], "brick-pad");
	strcpy(f.inputs[1], "axp2202-pek");
	strcpy(f.gpu_kernel, "PVR_K: 301: Read BVNC 22.102.54.38");
	strcpy(f.gpu_user, "PowerVR Rogue GE8300, OpenGL ES 3.2 build 1.11@5516664");
	strcpy(f.wifi, "xr829");
	f.closed_present = 13;
	f.closed_total = 13;
	strcpy(f.boot_times, "0.61 init\n0.72 closed\n\n1.43 card\n");
	strcpy(f.leds, "2: sunxi_led0r sunxi_led0g");
	strcpy(f.motor_rail, "axp2202-bldo3, 3300000 uV, enabled");
	hwreport_build(&f, out, sizeof out);
	ck(!strcmp(out,
	           "Hardware:\n"
	           "  Panel: jd9168, with TrimUI's waits\n"
	           "  Inputs: brick-pad, axp2202-pek\n"
	           "  GPU driver: PVR_K: 301: Read BVNC 22.102.54.38\n"
	           "  Graphics: PowerVR Rogue GE8300, OpenGL ES 3.2 build 1.11@5516664\n"
	           "  Wi-Fi: xr829\n"
	           "  Closed files: 13 of 13\n"
	           "  LEDs: 2: sunxi_led0r sunxi_led0g\n"
	           "  Motor rail: axp2202-bldo3, 3300000 uV, enabled\n"
	           "Boot times:\n"
	           "  0.61 init\n"
	           "  0.72 closed\n"
	           "  1.43 card\n"),
	   "the Brick's facts, a line each, the boot times indented and no empty line");

	memset(&f, 0, sizeof f);
	f.closed_total = -1;
	hwreport_build(&f, out, sizeof out);
	ck(!strcmp(out, "Hardware:\n  Panel: -\n  Inputs: -\n  GPU driver: -\n  Graphics: -\n  Wi-Fi: -\n"
	                "Boot times:\n  -\n"),
	   "nothing known: every value -, and no closed files, LEDs or motor rail line on a device "
	   "without them (the Pixel 2's report as before)");

	memset(&f, 0, sizeof f);
	memset(f.panel, 'x', sizeof f.panel - 1);
	memset(f.boot_times, 'y', sizeof f.boot_times - 1);
	ck(hwreport_build(&f, line, sizeof line) == sizeof line - 1 && strlen(line) == sizeof line - 1,
	   "a report longer than its buffer is cut there, never past it");

	ck(hw_inputs_from(devices, in, HW_INPUTS) == 2 && !strcmp(in[0], "axp2202-pek") && !strcmp(in[1], "brick-pad"),
	   "the input devices' names from /proc/bus/input/devices");
	ck(hw_inputs_from(devices, in, 1) == 1, "no more names than asked for");
	ck(hw_klog_line(klog, "brick panel: ", line, sizeof line) &&
	   !strcmp(line, "brick panel: jd9168, with TrimUI's waits"),
	   "the panel: the kernel log's last line about it");
	ck(hw_klog_line(klog, "PVR_K:", line, sizeof line) && !strncmp(line, "PVR_K:  301: Read BVNC", 22),
	   "the GPU driver's line");
	ck(!hw_klog_line(klog, "panfrost", line, sizeof line), "no line with it: false");
	if (fails) {
		printf("hwreport: %d of %d checks failed\n", fails, checks);
		return 1;
	}
	printf("hwreport: %d checks passed\n", checks);
	return 0;
}
