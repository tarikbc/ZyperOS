/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_HWREPORT_H
#define ZYPEROS_HWREPORT_H

/* The log pack's hardware report (about.txt's Hardware lines, src/xfer_screen.c):
 * what a tester's handheld has, in their logs, so nobody asks them to look
 * it up. Each device's platform file gathers the facts (plat_hw_facts, read
 * on the handheld from the kernel's log and /sys); this formats them and
 * holds the small readers of the kernel's text, which the host checks
 * (tools/hwreport-check.c). */

#include <stdbool.h>
#include <stddef.h>

#define HW_INPUTS 10

typedef struct {
	char panel[96];          /* which panel the kernel found, "" when it doesn't say */
	char inputs[HW_INPUTS][64];
	int  n_inputs;
	char gpu_kernel[128];    /* the GPU driver's own line in the kernel's log */
	char gpu_user[160];      /* the GL renderer and version the launcher got */
	char wifi[64];           /* the Wi-Fi chip's driver, "" for none up */
	int  closed_present;     /* the closed files on the card, of closed_total */
	int  closed_total;       /* -1: a device without closed files */
	/* The LEDs the kernel has (/sys/class/leds), how many and their names,
	 * "" where none is read; and a motor's power rail, its regulator's name,
	 * microvolts and state, "" where none is read: for the Brick's lights
	 * and rumble, which its next work needs facts about */
	char leds[256];
	char motor_rail[96];
	char boot_times[1024];   /* the start's own marks, a line each */
} hw_facts;

/* The facts as about.txt's lines into out (n bytes): "Hardware:", then one
 * "  name: value" a fact, "-" for what is not known, and the start's marks
 * under "Boot times:". The length written. */
size_t hwreport_build(const hw_facts *f, char *out, size_t n);

/* The input devices' names from /proc/bus/input/devices' text, into out
 * (max of them): the number found */
int hw_inputs_from(const char *devices, char (*out)[64], int max);

/* The last line of the kernel's log text with `needle` in it, into out:
 * from the needle on, its timestamp gone. False when no line has it. */
bool hw_klog_line(const char *klog, const char *needle, char *out, size_t n);

/* The kernel's log as it is now into out (n bytes), "" where it can't be
 * read (the host) */
void hw_klog_text(char *out, size_t n);

/* The GL renderer and version, noted once the launcher's renderer is made
 * (main.c), "" on a renderer without GL; and what was noted */
void hw_note_gl(void);
const char *hw_gl(void);

/* The device's facts, read where it runs (src/device/<id>.c) */
void plat_hw_facts(hw_facts *f);

#endif
