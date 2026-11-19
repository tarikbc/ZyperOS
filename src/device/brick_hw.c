/* SPDX-License-Identifier: GPL-3.0-only */
/* The TrimUI Brick's facts for the log pack's hardware report
 * (src/hwreport.h): which of its six panels the kernel found (plastron's
 * kernel patch 0012 says it in the kernel's log), its inputs, the PowerVR
 * driver's own line and the GL it gave the launcher, its Wi-Fi chip, how
 * many of its closed files are on the card (plastron's lists of them, which
 * its start checks too), and the start's marks. On the host, the build's
 * desktop, what the host has. */
#include "hwfacts.h"

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __linux__
/* The closed files: each of the image's lists (size and name a line) against
 * the boot partition's closed/ */
static void closed_count(hw_facts *f)
{
	static const char *const lists[] = { "/usr/share/zyperos/closed/pvr-ge8300.list",
	                                     "/usr/share/zyperos/closed/xr829-firmware.list" };
	char line[160], name[128], path[200];
	long size;
	struct stat st;
	size_t i;

	f->closed_present = f->closed_total = 0;
	for (i = 0; i < sizeof lists / sizeof lists[0]; i++) {
		FILE *l = fopen(lists[i], "r");

		while (l && fgets(line, sizeof line, l)) {
			if (sscanf(line, "%ld %127s", &size, name) != 2) continue;
			f->closed_total++;
			snprintf(path, sizeof path, "/boot/closed/%s", name);
			if (!stat(path, &st) && st.st_size == size) f->closed_present++;
		}
		if (l) fclose(l);
	}
}

#endif

#ifdef __linux__
/* The LEDs on the A133's LED controller (its device tree's ledc, 23 of
 * them, CONFIG_LEDS_SUNXI), as /sys/class/leds names them: how many, then
 * their names in the kernel's order, as many as fit */
static void leds_read(hw_facts *f)
{
	DIR *d = opendir("/sys/class/leds");
	struct dirent *e;
	char names[256] = "";
	size_t at = 0;
	int n = 0;

	while (d && (e = readdir(d))) {
		int k;

		if (e->d_name[0] == '.') continue;
		n++;
		k = snprintf(names + at, sizeof names - at, "%s%s", at ? " " : "", e->d_name);
		if (k > 0) at += (size_t)k < sizeof names - at ? (size_t)k : sizeof names - at - 1;
	}
	if (d) closedir(d);
	snprintf(f->leds, sizeof f->leds, "%d%s%.200s", n, n ? ": " : " (none)", names);
}

/* The rumble motor's rail: the AXP2202's BLDO3, which TrimUI's own kernel
 * takes for its motor (its LED driver's regulator_get "axp2202-bldo3"). Its
 * microvolts and state, read only: a later change sets a strength on it only
 * at or under what the boot left (the Brick parity plan's item 14) */
static void motor_rail_read(hw_facts *f)
{
	DIR *d = opendir("/sys/class/regulator");
	struct dirent *e;

	while (d && (e = readdir(d))) {
		char path[300], name[64] = "", uv[24] = "", state[24] = "";

		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "/sys/class/regulator/%s/name", e->d_name);
		hwf_text(path, name, sizeof name);
		name[strcspn(name, "\n")] = 0;
		if (strcmp(name, "axp2202-bldo3")) continue;
		snprintf(path, sizeof path, "/sys/class/regulator/%s/microvolts", e->d_name);
		hwf_text(path, uv, sizeof uv);
		uv[strcspn(uv, "\n")] = 0;
		snprintf(path, sizeof path, "/sys/class/regulator/%s/state", e->d_name);
		hwf_text(path, state, sizeof state);
		state[strcspn(state, "\n")] = 0;
		snprintf(f->motor_rail, sizeof f->motor_rail, "%s, %s uV, %s", name,
		         uv[0] ? uv : "-", state[0] ? state : "-");
		break;
	}
	if (d) closedir(d);
}
#endif

void plat_hw_facts(hw_facts *f)
{
	memset(f, 0, sizeof *f);
	f->closed_total = -1;
	hwf_common(f);
#ifdef __linux__
	static char klog[1 << 18];   /* the kernel's whole buffer (LOG_BUF_SHIFT 18) */
	char line[160];

	hwf_klog(klog, sizeof klog);
	if (hw_klog_line(klog, "brick panel: ", line, sizeof line))
		snprintf(f->panel, sizeof f->panel, "%s", line + strlen("brick panel: "));
	if (hw_klog_line(klog, "PVR_K:", line, sizeof line))
		snprintf(f->gpu_kernel, sizeof f->gpu_kernel, "%s", line);
	closed_count(f);
	leds_read(f);
	motor_rail_read(f);
#endif
}
