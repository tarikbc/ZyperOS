/* SPDX-License-Identifier: GPL-3.0-only */
/* The File Transfer page on the host: the real routes (src/hare.c) and the
 * real server (src/httpd.c) over a scratch card, for looking at the page in
 * a browser and for the README's pictures of it (tools/readme-shots.py).
 *
 *     make build-native/portal-serve
 *     build-native/portal-serve CARD WEB [usb] [update=off|on|ready] [space=FREE/TOTAL] [lang=CODE]
 *
 * CARD holds Roms/, Music/, Bios/, Saves/ and the rest as a card does,
 * ZyperOS/systems.cfg for the consoles' colors and icons, and
 * ZyperOS/system-icons for the pictures (res/system-icons); WEB is res/web
 * with the two fonts beside it, as mk/payload.sh lays it out. "usb" serves
 * the page as the USB cable does: under the cable's name, USB File Transfer,
 * with SSH's line; without it the page is Wi-Fi File Transfer's. "update="
 * answers Settings' update over the cable with a stand-in for the launcher
 * and its update program (main.c's xfer_update): "off" as an image with no
 * release key, "on" as one that takes the files and installs them over a few
 * seconds, "ready" as one with an update staged; the files the page sends
 * land in CARD/.userdata/pixel2/update/cable and nothing is checked or
 * installed. "space=88.4/119" answers the card's room, in GB, as a card of
 * that size would rather than the computer's disk. "lang=fr" is MENU >
 * Language: the page and the server's words in that language (src/i18n.h),
 * English without it. It prints the PIN and the port on one line, then
 * serves until stdin closes, or a line on it says to stop as the handheld's
 * question does:
 * "after" (once the files on their way are in) or "now"; "usb" and "wifi"
 * on it change the link the page is named after, as a handheld opened over
 * the other would, with the same server and the browser's cache. Like the
 * device it listens on every address (port 8080 or 8081 here, 80 needing
 * root), so use a scratch card. No SDL, no device. */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../src/hare.h"
#include "../src/i18n.h"

#define MAX_SYS 64

static struct { char folder[64], key[32]; unsigned rgb; } g_sys[MAX_SYS];
static int g_nsys;

/* The shelf's art for a tag (src/home.c's asset_for) */
static const char *art_key(const char *tag)
{
	if (!strcmp(tag, "SFC")) return "SNES";
	if (!strcmp(tag, "MD"))  return "GENESIS";
	if (!strcmp(tag, "PRT")) return "PORTS";
	return tag;
}

/* sys|name|folder|core|tag|art|accent|... from the card's systems.cfg */
static void read_systems(const char *card)
{
	char path[1024], line[512];
	FILE *f;

	snprintf(path, sizeof path, "%s/ZyperOS/systems.cfg", card);
	f = fopen(path, "r");
	while (f && g_nsys < MAX_SYS && fgets(line, sizeof line, f)) {
		char *field[8] = { 0 }, *p = line;
		int n = 0;

		if (strncmp(line, "sys|", 4)) continue;
		while (n < 8 && (field[n] = strsep(&p, "|"))) n++;
		if (n < 7) continue;
		snprintf(g_sys[g_nsys].folder, sizeof g_sys[g_nsys].folder, "%s", field[2]);
		snprintf(g_sys[g_nsys].key, sizeof g_sys[g_nsys].key, "%s", art_key(field[4]));
		g_sys[g_nsys++].rgb = (unsigned)strtoul(field[6], NULL, 16);
	}
	if (f) fclose(f);
}

static unsigned folder_color(const char *folder)
{
	for (int i = 0; i < g_nsys; i++)
		if (!strcmp(g_sys[i].folder, folder)) return g_sys[i].rgb;
	return 0xFF000000u;
}

static const char *folder_icon(const char *folder)
{
	for (int i = 0; i < g_nsys; i++)
		if (!strcmp(g_sys[i].folder, folder)) return g_sys[i].key;
	return !strcmp(folder, "BGM") ? "BGM" : NULL;
}

/* space=FREE/TOTAL, in GB: the card's room as a card has it, for pictures */
static long long g_free = -1, g_total = -1;
static bool card_space(long long *free, long long *total)
{
	*free = g_free;
	*total = g_total;
	return true;
}

/* ---- the update's stand-in ------------------------------------------- */

static const char *g_update;            /* off, on, ready */
static char        g_cable[1024];
static const char *g_state = "idle";
static long long   g_time = 1;
static time_t      g_work;              /* when the stand-in's work started */
static const char *g_doing;             /* check or install */

static char *dup_len(const char *s, size_t *len)
{
	char *p = strdup(s);

	if (p) *len = strlen(p);
	return p;
}

static char *update_hook(int op, const char *name, char *path, size_t pn,
                         size_t *len, char *why, size_t wn)
{
	char json[1024];
	bool on = strcmp(g_update, "off") != 0;
	bool running = g_doing && time(NULL) - g_work < 3;

	if (g_doing && !running) {
		g_state = !strcmp(g_doing, "check") ? "available" : "ready";
		g_doing = NULL;
		g_time++;
	}
	if (op == HARE_UPDATE_FILE) {
		if (!on) { snprintf(why, wn, "updates are off here"); return NULL; }
		if (strncmp(name, "ZyperOS-", 8) || strchr(name, '/')) {
			snprintf(why, wn, "That is not one of this handheld's update files.");
			return NULL;
		}
		{
			char d[1024];

			/* mkdir -p: the scratch card has no .userdata of its own */
			snprintf(d, sizeof d, "%s", g_cable);
			for (char *q = d + 1; *q; q++)
				if (*q == '/') { *q = '\0'; mkdir(d, 0755); *q = '/'; }
			mkdir(d, 0755);
		}
		snprintf(path, pn, "%s/%s", g_cable, name);
		return dup_len("{}", len);
	}
	if (op == HARE_UPDATE_CHECK || op == HARE_UPDATE_INSTALL) {
		g_doing = op == HARE_UPDATE_CHECK ? "check" : "install";
		g_state = op == HARE_UPDATE_CHECK ? "checking" : "downloading";
		g_work = time(NULL);
		g_time++;
		running = true;
	}
	if (op == HARE_UPDATE_RESTART) {
		if (strcmp(g_state, "ready")) { snprintf(why, wn, "No update is ready to install."); return NULL; }
		return dup_len("{\"restarting\":true}", len);
	}
	snprintf(json, sizeof json,
	         "{\"on\":%s,\"why\":%s,\"device\":\"pixel2\",\"installed\":\"0.4.0\",\"system\":\"0.4.0\","
	         "\"slots\":false,\"running\":%s,\"state\":\"%s\",\"version\":\"0.4.1\",\"kind\":\"folder\","
	         "\"download\":0,\"done\":%d,\"total\":100,\"files_done\":0,\"files_total\":0,"
	         "\"message\":\"\",\"result\":\"\",\"time\":%lld,\"battery\":80,\"charging\":true,\"files\":[]}",
	         on ? "true" : "false",
	         on ? "\"\"" : "\"This system image has no release key yet, so it cannot check that an update is real. Use the ZyperOS Installer.\"",
	         running ? "true" : "false", g_state, running ? (int)((time(NULL) - g_work) * 33) : 0, g_time);
	return dup_len(json, len);
}

int main(int argc, char **argv)
{
	char roms[1024], shared[1024], icons[1024];
	struct pollfd in = { .fd = 0, .events = POLLIN };

	if (argc < 3) {
		fprintf(stderr, "usage: %s CARD WEB [usb] [update=off|on|ready] [space=FREE/TOTAL] [lang=CODE]\n", argv[0]);
		return 2;
	}
	snprintf(roms, sizeof roms, "%s/Roms", argv[1]);
	snprintf(shared, sizeof shared, "%s/.userdata/shared", argv[1]);
	snprintf(icons, sizeof icons, "%s/ZyperOS/system-icons", argv[1]);
	read_systems(argv[1]);
	hare_set_folder_color(folder_color);
	hare_set_folder_icon(folder_icon);
	hare_set_icons(icons);
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "usb")) {
			hare_set_cable(true);
			hare_set_ssh("root@10.42.0.1");
		} else if (!strncmp(argv[i], "space=", 6)) {
			double f = 0, t = 0;

			if (sscanf(argv[i] + 6, "%lf/%lf", &f, &t) == 2 && t > 0) {
				g_free = (long long)(f * 1e9);
				g_total = (long long)(t * 1e9);
				hare_set_space(card_space);
			}
		} else if (!strncmp(argv[i], "lang=", 5)) {
			i18n_set(lang_parse(argv[i] + 5));
		} else if (!strncmp(argv[i], "update=", 7)) {
			g_update = argv[i] + 7;
			if (!strcmp(g_update, "ready")) g_state = "ready";
			snprintf(g_cable, sizeof g_cable, "%s/.userdata/pixel2/update/cable", argv[1]);
			hare_set_update(update_hook);
		}
	}
	if (!hare_start(roms, argv[1], shared, argv[2])) {
		fprintf(stderr, "portal-serve: no port to listen on\n");
		return 1;
	}
	printf("%s %d\n", hare_pin(), hare_port());
	fflush(stdout);
	/* A line on stdin is the handheld's stop question, for tools/portal-check.py:
	 * "after" closes after the files on their way, "now" cancels them; or the
	 * link the page is named after, "usb" or "wifi" */
	for (;;) {
		hare_poll();
		if (poll(&in, 1, 4) > 0) {
			char b[64];
			ssize_t n = read(0, b, sizeof b - 1);

			if (n <= 0) break;
			b[n] = '\0';
			if (strstr(b, "usb")) hare_set_cable(true);
			if (strstr(b, "wifi")) hare_set_cable(false);
			if (strstr(b, "after")) hare_close_after();
			if (strstr(b, "now")) { hare_stop(); return 0; }
		}
		if (hare_closing() && hare_inflight() == 0) { hare_stop(); return 0; }
	}
	hare_stop();
	return 0;
}
