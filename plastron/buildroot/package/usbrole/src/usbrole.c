/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * usbrole: the USB port is a host while something that wants power is
 * plugged into it, and a device otherwise.
 *
 * The Pixel 2's one USB-C port is both: a device to a computer (charging, and
 * the networking development runs over) and a host to a USB-C DAC. The USB
 * PHY tells them apart by the ID pin and reports it as its extcon's USB-HOST
 * cable, but nothing in the kernel acts on it: the dwc2 controller takes its
 * role from the role switch. This sets the role from the pin, once at startup
 * and then on every change, as ROCKNIX does on this device. It sleeps on the
 * kernel's uevents in between.
 *
 * It asks for host only while USB-HOST reads 1, so the port never supplies
 * power while a computer or a charger is powering it, which ROCKNIX warns can
 * damage the board. And the pin is not enough on its own: plugged into a
 * computer by a USB-C cable, it read 1 for a moment as the plug settled, and
 * the port went host for 4 to 10 seconds with the computer powering it, the
 * computer seeing nothing meanwhile (2026-11-05). And on 2026-11-10 it went
 * host with a Mac on the cable whose port had taken the sink side, and fed
 * it: such a Mac looks exactly like a DAC. When the port may be a host is
 * rule.c's: never on its own. A plug that wants power and gives none is
 * asked about by the launcher (Power It, or It's a Computer), and only Power
 * It for that plug, with every safety rule still holding, makes it one.
 *
 * The launcher and usbrole meet in /run/usbrole, root's alone (0700, made
 * and checked here): usbrole writes "state" (the plug's number, whether it
 * may be asked about, the readings and the role), and the launcher writes
 * "host" with the plug's number for Power It, which usbrole reads once and
 * deletes. usbrole stays the only writer of the role.
 *
 * And when a DAC's sound card appears, it turns the DAC's own playback volume
 * to full: the volume the player moves is a software one in front of it
 * (asound.conf's pcm.usb), the same for every DAC.
 *
 * And it loads the driver for anything plugged in that has one as a module,
 * by the alias the kernel gives the device, as udev would: a USB Wi-Fi
 * adapter's, whose driver and wireless stack are modules so a boot without
 * one starts none of it. Everything else here is built in, and modprobe
 * finds nothing to load for it.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <poll.h>
#include <signal.h>
#include <sound/asound.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "rule.h"

/* Room for a directory name of the most a name can be, 255 */
static char host_state[320];   /* the USB-HOST cable's state file */
static char ext_state[320];    /* its extcon's every cable: SDP=1, CDP=0, ... */
static char role_file[320];    /* the role switch */
#define RUN_DIR    "/run/usbrole"
#define STATE_FILE RUN_DIR "/state"
#define HOST_FILE  RUN_DIR "/host"
static int run_ok;             /* RUN_DIR is root's own directory */
/* Power coming in from the other end: a computer or a charger */
#define VBUS_IN "/sys/class/power_supply/rk817-charger/online"

static void log_to_kernel(const char *text)
{
	int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

	if (fd >= 0) {
		if (write(fd, text, strlen(text)) < 0) { /* nowhere to say so */ }
		close(fd);
	}
}

static int read_line(const char *path, char *out, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got;

	if (fd < 0)
		return -1;
	got = read(fd, out, n - 1);
	close(fd);
	if (got < 0)
		return -1;
	out[got] = '\0';
	out[strcspn(out, "\n")] = '\0';
	return 0;
}

/* The USB-HOST cable, among every extcon's cables, and the role switch */
static int find_files(void)
{
	DIR *d;
	struct dirent *e;

	if ((d = opendir("/sys/class/extcon"))) {
		while ((e = readdir(d)) && !host_state[0]) {
			for (int i = 0; i < 16; i++) {
				char path[320], name[32];

				snprintf(path, sizeof path, "/sys/class/extcon/%s/cable.%d/name",
				         e->d_name, i);
				if (read_line(path, name, sizeof name) < 0)
					break;
				if (strcmp(name, "USB-HOST"))
					continue;
				snprintf(host_state, sizeof host_state,
				         "/sys/class/extcon/%s/cable.%d/state", e->d_name, i);
				snprintf(ext_state, sizeof ext_state,
				         "/sys/class/extcon/%s/state", e->d_name);
				break;
			}
		}
		closedir(d);
	}
	if ((d = opendir("/sys/class/usb_role"))) {
		while ((e = readdir(d)) && !role_file[0]) {
			if (e->d_name[0] == '.')
				continue;
			snprintf(role_file, sizeof role_file, "/sys/class/usb_role/%s/role",
			         e->d_name);
		}
		closedir(d);
	}
	return host_state[0] && role_file[0] ? 0 : -1;
}

static unsigned now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000);
}

/* A computer on the bus as a USB device: a Mac whose port took the device
 * side, "Product: Mac" by Apple (2026-11-10). Only there while host. */
static int computer_on_bus(void)
{
	DIR *d = opendir("/sys/bus/usb/devices");
	struct dirent *e;
	int found = 0;

	if (!d)
		return 0;
	while (!found && (e = readdir(d))) {
		char path[320], v[16], p[64];

		if (e->d_name[0] == '.' || !strncmp(e->d_name, "usb", 3) || strchr(e->d_name, ':'))
			continue;
		snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/idVendor", e->d_name);
		if (read_line(path, v, sizeof v) < 0 || strcmp(v, "05ac"))
			continue;
		snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/product", e->d_name);
		found = read_line(path, p, sizeof p) == 0 && !strcmp(p, "Mac");
	}
	closedir(d);
	return found;
}

static ur_state rule;

/* RUN_DIR made, or found, and checked to be root's own directory and no
 * link: else nothing is written or read there, and no plug can be powered */
static void run_dir(void)
{
	struct stat st;

	if (mkdir(RUN_DIR, 0700) < 0 && errno != EEXIST)
		return;
	if (lstat(RUN_DIR, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != 0 ||
	    (st.st_mode & 0077)) {
		log_to_kernel("usbrole: " RUN_DIR " is not root's own: no plug can be powered\n");
		return;
	}
	unlink(HOST_FILE);             /* a request from before this start */
	run_ok = 1;
}

/* The state for the launcher, when it changed: written beside and renamed,
 * so it is read whole */
static void write_state(const ur_sense *in, const char *role)
{
	static char last[128];
	char line[128];
	int fd, n;

	if (!run_ok)
		return;
	n = snprintf(line, sizeof line, "plug=%u ask=%d pin=%d vbus=%d powered=%d role=%s\n",
	             rule.plug, rule.can_ask, in->pin, in->vbus, rule.powered, role);
	if (n <= 0 || !strcmp(line, last))
		return;
	fd = open(STATE_FILE ".new", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
	if (fd < 0)
		return;
	if (write(fd, line, (size_t)n) == n && rename(STATE_FILE ".new", STATE_FILE) == 0)
		snprintf(last, sizeof last, "%s", line);
	close(fd);
}

/* The launcher's Power It: the plug's number in HOST_FILE, read once (a
 * regular file of root's, a few bytes) and deleted */
static void read_request(void)
{
	char text[32];
	struct stat st;
	ssize_t got;
	unsigned plug;
	int fd;

	if (!run_ok)
		return;
	fd = open(HOST_FILE, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
	if (fd < 0)
		return;
	if (fstat(fd, &st) < 0 || !S_ISREG(st.st_mode) || st.st_uid != 0 ||
	    st.st_size >= (off_t)sizeof text) {
		close(fd);
		unlink(HOST_FILE);
		return;
	}
	got = read(fd, text, sizeof text - 1);
	close(fd);
	unlink(HOST_FILE);
	if (got <= 0)
		return;
	text[got] = '\0';
	plug = (unsigned)strtoul(text, NULL, 10);
	log_to_kernel(ur_grant(&rule, plug) ? "usbrole: power it, as the player said\n"
	                                    : "usbrole: a request for a plug that cannot have power now\n");
}

/* The port's facts, rule.c's answer, and the role switch set to it. How
 * long until it wants to look again with no event, 0 for not. */
static unsigned follow_pin(void)
{
	char state[8], role[16], vbus[8], ext[256] = "";
	ur_sense in;
	const char *want;
	unsigned again;
	int fd;
	static int said;   /* the pin said host on a powered plug: said once a plug */

	if (read_line(role_file, role, sizeof role) < 0)
		return 0;
	in.pin = read_line(host_state, state, sizeof state) < 0 ? -1 : !strcmp(state, "1");
	in.vbus = read_line(VBUS_IN, vbus, sizeof vbus) < 0 ? -1 : !strcmp(vbus, "1");
	/* Every cable of the PHY's extcon, one a line: its charger detection */
	in.charger = 0;
	if ((fd = open(ext_state, O_RDONLY | O_CLOEXEC)) >= 0) {
		ssize_t got = read(fd, ext, sizeof ext - 1);

		close(fd);
		ext[got > 0 ? got : 0] = '\0';
		in.charger = strstr(ext, "SDP=1") || strstr(ext, "CDP=1") || strstr(ext, "DCP=1");
	}
	in.computer = !strcmp(role, "host") && computer_on_bus();
	want = ur_decide(&rule, &in, now_ms(), &again) == UR_HOST ? "host" : "device";
	write_state(&in, want);
	if (in.pin != 1 || !rule.powered)
		said = 0;
	else if (!said++)
		log_to_kernel("usbrole: the pin says host, but this plug has power: device\n");
	if (in.computer)
		log_to_kernel("usbrole: a computer on the bus: device until it is unplugged\n");
	if (!strcmp(role, want))
		return again;
	fd = open(role_file, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return again;
	if (write(fd, want, strlen(want)) > 0)
		log_to_kernel(strcmp(want, "host") ?
		              "usbrole: device, nothing plugged in wants power\n" :
		              "usbrole: host, something plugged in wants power\n");
	close(fd);
	return again;
}

/* The output's controls, "... Playback Volume" and "... Playback Switch", at
 * full and on. Not a headset's microphone monitor or sidetone, which are
 * playback controls too: full there is the room in your ears, or feedback. */
static void dac_full(int card)
{
	struct snd_ctl_elem_id ids[64];
	struct snd_ctl_elem_list list = { .space = 64, .pids = ids };
	char path[32];
	int fd, set = 0;

	snprintf(path, sizeof path, "/dev/snd/controlC%d", card);
	if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
		return;
	if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_LIST, &list) < 0)
		list.used = 0;
	for (unsigned int i = 0; i < list.used; i++) {
		struct snd_ctl_elem_info info = { .id = ids[i] };
		struct snd_ctl_elem_value v = { .id = ids[i] };
		const char *name = (const char *)ids[i].name;
		int volume = strstr(name, "Playback Volume") != NULL;
		int on = strstr(name, "Playback Switch") != NULL;

		if ((!volume && !on) || strstr(name, "Mic") || strstr(name, "Sidetone") ||
		    strstr(name, "Monitor"))
			continue;
		if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_INFO, &info) < 0 ||
		    !(info.access & SNDRV_CTL_ELEM_ACCESS_WRITE) ||
		    info.type != (volume ? SNDRV_CTL_ELEM_TYPE_INTEGER
		                         : SNDRV_CTL_ELEM_TYPE_BOOLEAN))
			continue;
		for (unsigned int c = 0; c < info.count && c < 128; c++)
			v.value.integer.value[c] = volume ? info.value.integer.max : 1;
		if (ioctl(fd, SNDRV_CTL_IOCTL_ELEM_WRITE, &v) >= 0)
			set++;
	}
	close(fd);
	if (set) {
		char text[64];

		snprintf(text, sizeof text, "usbrole: card %d's playback at full\n", card);
		log_to_kernel(text);
	}
}

/* The driver for a device, by its alias ("usb:v0BDAp8179..."). Not waited
 * for: SIGCHLD is ignored, so nothing has to reap it. */
static void load_driver(const char *alias)
{
	if (strncmp(alias, "usb:", 4) || fork() != 0)
		return;
	execl("/sbin/modprobe", "modprobe", "-q", alias, (char *)NULL);
	_exit(127);
}

/* Devices plugged in before this started: the boot's own events came before
 * there was anyone to hear them */
static void load_present(void)
{
	DIR *d = opendir("/sys/bus/usb/devices");
	struct dirent *e;

	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[320], alias[256];

		if (e->d_name[0] == '.')
			continue;
		snprintf(path, sizeof path, "/sys/bus/usb/devices/%s/modalias", e->d_name);
		if (read_line(path, alias, sizeof alias) == 0)
			load_driver(alias);
	}
	closedir(d);
}

int main(void)
{
	struct sockaddr_nl addr = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	char msg[4096];
	int sock;

	if (find_files() < 0) {
		log_to_kernel("usbrole: no USB-HOST cable or no role switch\n");
		/* init respawns us; don't spin */
		sleep(10);
		return 1;
	}
	sock = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if (sock < 0 || bind(sock, (struct sockaddr *)&addr, sizeof addr) < 0) {
		log_to_kernel("usbrole: can't listen for the kernel's events\n");
		sleep(10);
		return 1;
	}

	signal(SIGCHLD, SIG_IGN);
	run_dir();
	int watch = run_ok ? inotify_init1(IN_NONBLOCK | IN_CLOEXEC) : -1;

	if (watch >= 0 && inotify_add_watch(watch, RUN_DIR, IN_CLOSE_WRITE | IN_MOVED_TO) < 0) {
		close(watch);
		watch = -1;
	}
	unsigned again = follow_pin();
	/* A DAC already in, if init restarted us. Card 0 is the RK817. */
	for (int card = 1; card < 8; card++)
		dac_full(card);
	load_present();
	/* The PHY's extcon announces each change of its cables as a uevent:
	 * "change@/devices/.../extcon/extcon0". A sound card's controls arrive
	 * as "add@/devices/.../sound/card1/controlC1", the node already made. */
	for (;;) {
		struct pollfd p[2] = { { .fd = sock, .events = POLLIN }, { .fd = watch, .events = POLLIN } };
		ssize_t got;
		const char *last;
		int r;

		/* The grace running out: looked at again on time, events or not */
		r = poll(p, watch >= 0 ? 2 : 1, again ? (int)again : -1);
		if (r == 0) {
			again = follow_pin();
			continue;
		}
		/* The launcher's request, the moment it is written */
		if (watch >= 0 && (p[1].revents & POLLIN)) {
			char ev[1024];

			while (read(watch, ev, sizeof ev) > 0) {}
			read_request();
			again = follow_pin();
		}
		if (!(p[0].revents & POLLIN))
			continue;
		got = recv(sock, msg, sizeof msg - 1, 0);
		if (got <= 0)
			continue;
		msg[got] = '\0';
		last = strrchr(msg, '/');
		/* The pin's cable, and the charger's input, which can settle
		 * after it */
		if (strstr(msg, "/extcon/") || strstr(msg, "/power_supply/") ||
		    strstr(msg, "/usb1/1-"))
			again = follow_pin();
		else if (!strncmp(msg, "add@", 4) && last && !strncmp(last, "/controlC", 9) &&
		         atoi(last + 9) > 0)
			dac_full(atoi(last + 9));
		/* The message's fields follow its header, each ended by a 0 */
		if (!strncmp(msg, "add@", 4))
			for (const char *f = msg + strlen(msg) + 1; f < msg + got; f += strlen(f) + 1)
				if (!strncmp(f, "MODALIAS=", 9))
					load_driver(f + 9);
	}
}
