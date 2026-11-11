/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * usbdrive: the GKD Pixel 2's card as a USB drive, the rules in drive.h.
 *
 *   usbdrive FRAMES LAUNCH
 *
 * Run by ZyperOS's launch.sh in place of itself (exec) when ZyperOS asks
 * for it (Computer Connected > USB Drive): FRAMES is a folder in /tmp with
 * the pictures ZyperOS drew for it before it left, in its language and theme
 * (raw, the panel's own 480x640 turned, as splash --still shows them):
 * connected.raw ("Connected as a USB drive. Eject it on the computer to
 * finish."), finishing.raw and failed.raw. LAUNCH is launch.sh on the card,
 * run again once the card is back. Everything here is the image's, so
 * nothing holds the card while it is the computer's.
 *
 * With ZyperOS's atlas in FRAMES too (atlas.h), connected.raw is held here
 * rather than by splash, and its two tiles say what moves, how fast and
 * how much each way, from the games partition's /sys stat once a second
 * (drivestat.h); only a change is drawn, so an idle drive costs nothing.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "atlas.h"
#include "drive.h"
#include "drivestat.h"
#include "panel.h"

#define CARD       "/mnt/SDCARD"
#define PART       "/dev/mmcblk0p3"
#define GADGET     "/sys/kernel/config/usb_gadget/px2"
#define MS         GADGET "/functions/mass_storage.0"
#define LUN        MS "/lun.0"
#define VBUS_IN    "/sys/class/power_supply/rk817-charger/online"
#define PART_STAT  "/sys/class/block/mmcblk0p3/stat"

static pid_t still;          /* splash --still showing a frame */
static char  frames[PATH_MAX];

static void say(const char *fmt, const char *arg)
{
	char line[256];
	int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

	snprintf(line, sizeof line, fmt, arg ? arg : "");
	if (fd >= 0) {
		if (write(fd, line, strlen(line)) < 0) { /* nothing to do */ }
		close(fd);
	}
}

static unsigned now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned)(t.tv_sec * 1000u + t.tv_nsec / 1000000u);
}

static int put(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC), ok;

	if (fd < 0) return -1;
	ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text) ? 0 : -1;
	close(fd);
	return ok;
}

static int get(const char *path, char *out, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got;

	out[0] = '\0';
	if (fd < 0) return -1;
	got = read(fd, out, n - 1);
	close(fd);
	out[got > 0 ? got : 0] = '\0';
	out[strcspn(out, "\n")] = '\0';
	return 0;
}

/* One of ZyperOS's pictures on the panel, in place of the last */
static void show(const char *name)
{
	char path[PATH_MAX + 32];

	if (still > 0) { kill(still, SIGTERM); waitpid(still, NULL, 0); still = 0; }
	snprintf(path, sizeof path, "%s/%s", frames, name);
	if (access(path, R_OK) || access("/usr/bin/splash", X_OK)) return;
	if ((still = fork()) == 0) {
		execl("/usr/bin/splash", "splash", "--still", path, (char *)NULL);
		_exit(127);
	}
}

/* The connected page with its live rows (atlas.h): the page ZyperOS drew,
 * the atlas, and the page being made, all landscape */
static struct {
	bool on;
	at_layout l;
	uint32_t *base, *atlas, *page;
	dst_state d;
	unsigned looked;
} live;

static bool live_start(void)
{
	char path[PATH_MAX + 32], text[4096];
	size_t n = 0;
	FILE *f;

	snprintf(path, sizeof path, "%s/atlas.txt", frames);
	if (!(f = fopen(path, "r"))) return false;
	n = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	text[n] = '\0';
	if (!at_parse(text, &live.l)) {
		say("usbdrive: the atlas is not usable: the page as it is\n", NULL);
		return false;
	}
	n = (size_t)live.l.w * live.l.h;
	live.base = malloc(n * 4);
	live.atlas = malloc(n * 4);
	live.page = malloc(n * 4);
	if (!live.base || !live.atlas || !live.page) return false;
	snprintf(path, sizeof path, "%s/connected.raw", frames);
	if (!at_load(path, live.l.w, live.l.h, true, live.base)) return false;
	snprintf(path, sizeof path, "%s/atlas.raw", frames);
	if (!at_load(path, live.l.w, live.l.h, true, live.atlas)) return false;
	memcpy(live.page, live.base, n * 4);
	if (!panel_open(live.page, live.l.w, live.l.h)) return false;
	live.on = true;
	return true;
}

/* A way's three slots: its mark, moving or idle, its speed, its total */
static void live_way(const dst_way *w, int mark_slot, int on, int off, int speed_slot, int total_slot)
{
	int tok[DST_FMT_MAX], n, k;
	const int slots[3] = { mark_slot, speed_slot, total_slot };

	for (k = 0; k < 3; k++) {
		const at_rect *r = &live.l.slot[slots[k]];

		if (k == 0) {
			tok[0] = w->active ? on : off;
			n = 1;
		} else {
			int fam = k == 1 ? AT_BIG : AT_SMALL, i;

			n = k == 1 ? dst_fmt_rate(w->rate, tok, DST_FMT_MAX) : dst_fmt_total(w->total, tok, DST_FMT_MAX);
			for (i = 0; i < n; i++) tok[i] += fam;
		}
		at_compose(&live.l, slots[k], tok, n, live.atlas, live.page);
		panel_update(live.page, r->x, r->y, r->w, r->h);
	}
}

/* The stat once a second; the tiles drawn when what they say changed */
static void live_follow(void)
{
	unsigned long long f[7] = { 0 };
	char text[256];
	unsigned now = now_ms();

	if (!live.on || (live.looked && now - live.looked < 1000u)) return;
	live.looked = now;
	if (get(PART_STAT, text, sizeof text) ||
	    sscanf(text, "%llu %llu %llu %llu %llu %llu %llu", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) != 7)
		return;
	if (!dst_step(&live.d, f[2], f[6], now)) return;
	live_way(&live.d.in, AT_SLOT_MARK_IN, AT_G_IN_ON, AT_G_IN_OFF, AT_SLOT_SPEED_IN, AT_SLOT_TOTAL_IN);
	live_way(&live.d.out, AT_SLOT_MARK_OUT, AT_G_OUT_ON, AT_G_OUT_OFF, AT_SLOT_SPEED_OUT, AT_SLOT_TOTAL_OUT);
}

/* The live page down: whatever splash put up since is on glass */
static void live_stop(void)
{
	if (live.on) panel_close();
	live.on = false;
}

static bool mounted(void)
{
	char line[512];
	FILE *f = fopen("/proc/mounts", "r");
	bool on = false;

	while (f && fgets(line, sizeof line, f))
		if (strstr(line, " " CARD " ")) on = true;
	if (f) fclose(f);
	return on;
}

/* Is this link under the card? */
static bool on_card(const char *link)
{
	char to[PATH_MAX];
	ssize_t n = readlink(link, to, sizeof to - 1);

	if (n <= 0) return false;
	to[n] = '\0';
	return !strncmp(to, CARD "/", strlen(CARD) + 1) || !strcmp(to, CARD);
}

/* Every process but this one and its picture that has the card open: its
 * folder, its program or a file. Signalled with `sig` when it is not 0. */
static int holders(int sig)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int n = 0;

	while (d && (e = readdir(d))) {
		char p[64], fdd[64];
		pid_t pid = (pid_t)atoi(e->d_name);
		bool holds = false;
		DIR *fds;
		struct dirent *f;

		if (pid <= 1 || pid == getpid() || pid == still) continue;
		snprintf(p, sizeof p, "/proc/%d/cwd", pid);
		holds = on_card(p);
		snprintf(p, sizeof p, "/proc/%d/exe", pid);
		holds = holds || on_card(p);
		snprintf(fdd, sizeof fdd, "/proc/%d/fd", pid);
		if (!holds && (fds = opendir(fdd))) {
			while (!holds && (f = readdir(fds))) {
				char fp[PATH_MAX];

				if (f->d_name[0] == '.') continue;
				snprintf(fp, sizeof fp, "%s/%s", fdd, f->d_name);
				holds = on_card(fp);
			}
			closedir(fds);
		}
		if (!holds) continue;
		n++;
		if (sig) kill(pid, sig);
	}
	if (d) closedir(d);
	return n;
}

/* The card free: what holds it asked to go, then made to; what a port
 * mounted from it unmounted; synced and unmounted as at shutdown */
static bool card_off(void)
{
	char line[512], mounts[16][256];
	int n = 0, i, tries;
	FILE *f;

	if (holders(SIGTERM)) sleep(1);
	if (holders(SIGKILL)) usleep(300000);
	/* A port's loop devices and bind mounts under the card, newest first */
	if ((f = fopen("/proc/mounts", "r"))) {
		while (n < 16 && fgets(line, sizeof line, f)) {
			char dev[256], dir[256];

			if (sscanf(line, "%255s %255s", dev, dir) == 2 &&
			    !strncmp(dir, CARD "/", strlen(CARD) + 1))
				snprintf(mounts[n++], sizeof mounts[0], "%s", dir);
		}
		fclose(f);
	}
	for (i = n - 1; i >= 0; i--)
		if (umount(mounts[i])) umount2(mounts[i], MNT_DETACH);
	sync();
	for (tries = 0; tries < 60 && mounted(); tries++) {
		if (!umount(CARD)) break;
		usleep(50000);
	}
	return !mounted();
}

static void card_on(void)
{
	/* datapart's own start: checked first when the computer left it dirty */
	if (system("/etc/init.d/datapart start > /dev/null 2>&1")) say("usbdrive: the card did not mount again\n", NULL);
}

static char udc[64];

static void gadget_bind(bool on)
{
	if (!udc[0]) {
		DIR *d = opendir("/sys/class/udc");
		struct dirent *e;

		while (d && (e = readdir(d)))
			if (e->d_name[0] != '.') { snprintf(udc, sizeof udc, "%s", e->d_name); break; }
		if (d) closedir(d);
	}
	put(GADGET "/UDC", on ? udc : "\n");
}

/* The partition as the gadget's one function, in NCM's place; a device id
 * of its own (bcdDevice), so Windows does not reuse what it chose for NCM */
static bool export_on(void)
{
	gadget_bind(false);
	unlink(GADGET "/configs/c.1/ncm.usb0");
	mkdir(MS, 0755);
	put(LUN "/removable", "1");
	put(LUN "/ro", "0");
	put(LUN "/nofua", "0");
	put(LUN "/inquiry_string", "ZyperOS GKD Pixel 2 card");
	if (put(LUN "/file", PART)) { say("usbdrive: the LUN did not take " PART "\n", NULL); return false; }
	if (symlink(MS, GADGET "/configs/c.1/mass_storage.0") && errno != EEXIST) return false;
	put(GADGET "/bcdDevice", "0x0301");
	put(GADGET "/configs/c.1/strings/0x409/configuration", "Mass Storage");
	gadget_bind(true);
	return true;
}

static void export_off(void)
{
	gadget_bind(false);
	put(LUN "/file", "\n");
	unlink(GADGET "/configs/c.1/mass_storage.0");
	rmdir(LUN);
	rmdir(MS);
	put(GADGET "/bcdDevice", "0x0201");
	put(GADGET "/configs/c.1/strings/0x409/configuration", "NCM");
	if (symlink(GADGET "/functions/ncm.usb0", GADGET "/configs/c.1/ncm.usb0") && errno != EEXIST)
		say("usbdrive: NCM not linked back\n", NULL);
	/* Unbound: ZyperOS binds it when it is asked to (its usblink.h) */
}

/* POWER, grabbed from powerkey while the card is the computer's: its press
 * is this program's, so the card is un-exported before the power goes */
static int power_open(void)
{
	for (int i = 0; i < 32; i++) {
		char path[32];
		unsigned char keys[KEY_MAX / 8 + 1] = { 0 };
		int fd;

		snprintf(path, sizeof path, "/dev/input/event%d", i);
		if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && (keys[KEY_POWER / 8] & (1 << (KEY_POWER % 8)))) {
			ioctl(fd, EVIOCGRAB, 1);
			return fd;
		}
		close(fd);
	}
	return -1;
}

static bool power_pressed(int fd)
{
	struct input_event ev;
	bool down = false;

	while (fd >= 0 && read(fd, &ev, sizeof ev) == sizeof ev)
		if (ev.type == EV_KEY && ev.code == KEY_POWER && ev.value == 0) down = true;
	return down;
}

static void back_to_zyperos(const char *launch)
{
	/* For ZyperOS: this plug was answered (its usblink_skip_ask) */
	int fd = open("/tmp/zyperos_usbdrive_back", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

	if (fd >= 0) close(fd);
	if (still > 0) { kill(still, SIGTERM); waitpid(still, NULL, 0); still = 0; }
	say("usbdrive: back to ZyperOS\n", NULL);
	execl("/bin/sh", "sh", launch, (char *)NULL);
	say("usbdrive: %s did not start\n", launch);
	_exit(1);
}

int main(int argc, char **argv)
{
	drv_wait w = { 0 };
	drv_end end = DRV_STAY;
	char v[64], st[32];
	int pfd;

	if (argc < 3) {
		fprintf(stderr, "usage: usbdrive FRAMES LAUNCH\n");
		return 2;
	}
	snprintf(frames, sizeof frames, "%s", argv[1]);
	/* Nothing of launch.sh's kept open (its script is on the card) */
	for (int fd = 3; fd < 1024; fd++) close(fd);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	show("finishing.raw");
	say("usbdrive: the card for the computer: freeing it\n", NULL);
	/* Fail closed: not exported unless unmounted and nothing holds it */
	if (!card_off() || !drv_may_export(mounted(), holders(0))) {
		say("usbdrive: the card is still in use: not exported\n", NULL);
		show("failed.raw");
		sleep(5);
		if (!mounted()) card_on();
		back_to_zyperos(argv[2]);
	}
	if (!export_on()) {
		export_off();
		show("failed.raw");
		sleep(5);
		card_on();
		back_to_zyperos(argv[2]);
	}
	say("usbdrive: the card is the computer's\n", NULL);
	/* The live page when the atlas is there; else the page as ZyperOS drew
	 * it, as before. The page goes up before splash lets go, so the panel
	 * never goes black between them. */
	if (live_start()) {
		if (still > 0) { kill(still, SIGTERM); waitpid(still, NULL, 0); still = 0; }
		say("usbdrive: the page's rows follow the card\n", NULL);
		live_follow();
	} else {
		show("connected.raw");
	}
	pfd = power_open();
	while (end == DRV_STAY) {
		usleep(200000);
		get(VBUS_IN, v, sizeof v);
		{
			char p[160], file[PATH_MAX];

			snprintf(p, sizeof p, "/sys/class/udc/%s/state", udc);
			get(p, st, sizeof st);
			get(LUN "/file", file, sizeof file);
			end = drv_wait_step(&w, !strcmp(v, "1"), file[0] != '\0', !strcmp(st, "configured"),
			                    power_pressed(pfd), now_ms());
		}
		live_follow();
	}
	say(end == DRV_EJECTED ? "usbdrive: ejected by the computer\n"
	    : end == DRV_UNPLUGGED ? "usbdrive: the cable came out\n"
	    : end == DRV_POWER ? "usbdrive: POWER\n" : "usbdrive: no computer took it\n", NULL);
	show("finishing.raw");
	/* splash has the display now; the live page's buffer can go */
	usleep(100000);
	live_stop();
	export_off();
	sync();
	if (pfd >= 0) { ioctl(pfd, EVIOCGRAB, 0); close(pfd); }
	if (end == DRV_POWER) {
		if (still > 0) { kill(still, SIGTERM); waitpid(still, NULL, 0); }
		execl("/sbin/poweroff", "poweroff", (char *)NULL);
		return 1;
	}
	card_on();
	back_to_zyperos(argv[2]);
	return 0;
}
