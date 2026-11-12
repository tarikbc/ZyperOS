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
 * how much each way, once a second, from what the gadget itself served
 * (drivestat.h: the f_mass_storage thread's own bytes, the card's /sys
 * stat only as a fallback); only a change is drawn, so an idle drive costs
 * nothing.
 *
 * The handheld's own buttons work on it too (drive.h's drv_key): FUNCTION
 * and volume step the brightness on the launcher's ladder (bright.txt), its
 * bar shown for a moment (bright.raw) and the rung left for ZyperOS to keep;
 * B ejects from the handheld (f_mass_storage's forced_eject, so the computer
 * sees the medium go), asking first on confirm.raw if the computer wrote in
 * the last 5 s.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
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
#define BACKLIGHT  "/sys/class/backlight/backlight/brightness"
#define BRIGHT_OUT "/tmp/zyperos_usbdrive_bright"   /* the rung, for ZyperOS to keep */
#define BTN_FN     BTN_TRIGGER_HAPPY1               /* FUNCTION, the launcher's MENU */
#define BAR_MS     1500u                            /* the bar up this long after a press */

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
	uint32_t *confirm;           /* the question's page, NULL for none */
	bool covered;                /* the question is up over the page */
	dst_state d;
	unsigned looked;
} live;

/* The brightness: the launcher's ladder and rung, and its bar at each rung */
static struct {
	bool on;
	at_bright b;
	uint32_t *bars;              /* rungs x bar.w x bar.h, NULL for no bar */
	unsigned until;              /* the bar comes down then, 0 not up */
} bri;

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
	/* The question's page, for B while the computer may still be copying */
	if ((live.confirm = malloc(n * 4))) {
		snprintf(path, sizeof path, "%s/confirm.raw", frames);
		if (!at_load(path, live.l.w, live.l.h, true, live.confirm)) {
			free(live.confirm);
			live.confirm = NULL;
		}
	}
	if (!panel_open(live.page, live.l.w, live.l.h)) return false;
	live.on = true;
	return true;
}

/* What is on the panel now: the page, or the question over it */
static const uint32_t *live_shown(void)
{
	return live.covered && live.confirm ? live.confirm : live.page;
}

/* bright.txt, and on the live page the bar's pictures */
static void bright_start(void)
{
	char path[PATH_MAX + 32], text[1024];
	size_t n;
	FILE *f;

	snprintf(path, sizeof path, "%s/bright.txt", frames);
	if (!(f = fopen(path, "r"))) return;
	n = fread(text, 1, sizeof text - 1, f);
	fclose(f);
	text[n] = '\0';
	if (!at_parse_bright(text, live.on ? live.l.w : 640, live.on ? live.l.h : 480, &bri.b)) {
		say("usbdrive: bright.txt is not usable: no brightness\n", NULL);
		return;
	}
	bri.on = true;
	if (!live.on) return;
	n = (size_t)bri.b.rungs * bri.b.bar.w * bri.b.bar.h;
	snprintf(path, sizeof path, "%s/bright.raw", frames);
	if ((bri.bars = malloc(n * 4)) && (f = fopen(path, "rb"))) {
		if (fread(bri.bars, 4, n, f) != n) { free(bri.bars); bri.bars = NULL; }
		fclose(f);
	} else {
		free(bri.bars);
		bri.bars = NULL;
	}
}

/* The rung set, kept for ZyperOS, and its bar shown */
static void bright_set(int level, unsigned now)
{
	char text[16];

	snprintf(text, sizeof text, "%d\n", bri.b.ladder[level]);
	put(BACKLIGHT, text);
	{
		int fd = open(BRIGHT_OUT, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

		snprintf(text, sizeof text, "%d\n", level);
		if (fd >= 0) {
			if (write(fd, text, strlen(text)) < 0) { /* the next start keeps its own */ }
			close(fd);
		}
	}
	if (live.on && bri.bars) {
		const at_rect *r = &bri.b.bar;

		panel_blit(bri.bars + (size_t)level * r->w * r->h, r->w, r->h, r->x, r->y);
		bri.until = now + BAR_MS;
		if (!bri.until) bri.until = 1;
	}
}

/* The bar down once its time is up: the panel's own picture back under it */
static void bright_follow(unsigned now)
{
	const at_rect *r = &bri.b.bar;

	if (!bri.until || (int)(now - bri.until) < 0) return;
	bri.until = 0;
	if (live.on) panel_update(live_shown(), r->x, r->y, r->w, r->h);
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
		/* Under the question, the page is kept up to date out of sight */
		if (!live.covered) panel_update(live.page, r->x, r->y, r->w, r->h);
	}
}

/* What moves, counted where the gadget serves it: the f_mass_storage
 * kernel thread's own I/O counters (drivestat.h). Found by name once the
 * gadget is bound, looked for again until then; while it cannot be read
 * the block stat stands in for the tiles and B always asks. */
static struct {
	int pid;                     /* "file-storage", 0 not found yet */
	bool sure;                   /* the last look read its counters */
	int source;                  /* what live.d counts: 0 none yet, 1 the thread, 2 the stat */
} io;

static int io_find(void)
{
	DIR *d = opendir("/proc");
	struct dirent *e;
	int pid = 0;

	while (d && (e = readdir(d)) && !pid) {
		char p[64], comm[32];
		int n = atoi(e->d_name);

		if (n <= 1) continue;
		snprintf(p, sizeof p, "/proc/%d/comm", n);
		if (!get(p, comm, sizeof comm) && !strcmp(comm, "file-storage")) pid = n;
	}
	if (d) closedir(d);
	return pid;
}

/* The bytes read from the card and written to it: the thread's, or the
 * stat's sectors when it cannot be read (io.sure says which) */
static bool io_read(unsigned long long *rd, unsigned long long *wr)
{
	char text[512], p[64];
	unsigned long long f[7] = { 0 };
	int fd;
	ssize_t n;

	if (!io.pid) io.pid = io_find();
	io.sure = false;
	if (io.pid) {
		snprintf(p, sizeof p, "/proc/%d/io", io.pid);
		if ((fd = open(p, O_RDONLY | O_CLOEXEC)) >= 0) {
			n = read(fd, text, sizeof text - 1);
			close(fd);
			text[n > 0 ? n : 0] = '\0';
			io.sure = n > 0 && dst_parse_io(text, rd, wr);
		}
		if (!io.sure) io.pid = 0;     /* gone, or not that thread: looked for again */
	}
	if (io.sure) return true;
	if (get(PART_STAT, text, sizeof text) ||
	    sscanf(text, "%llu %llu %llu %llu %llu %llu %llu", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) != 7)
		return false;
	*rd = f[2] * 512;
	*wr = f[6] * 512;
	return true;
}

/* /proc/meminfo's Dirty: written by the computer, not on the card yet */
static unsigned long io_dirty_kb(void)
{
	char text[2048];
	unsigned long kb = 0;
	int fd = open("/proc/meminfo", O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0) return 0;
	n = read(fd, text, sizeof text - 1);
	close(fd);
	text[n > 0 ? n : 0] = '\0';
	return n > 0 && drv_parse_dirty(text, &kb) ? kb : 0;
}

/* Once a second; the tiles drawn when what they say changed */
static void live_follow(void)
{
	unsigned long long rd, wr;
	unsigned now = now_ms();
	int source;

	/* Followed with or without the live page: B's question needs when the
	 * computer last wrote */
	if (live.looked && now - live.looked < 1000u) return;
	live.looked = now;
	if (!io_read(&rd, &wr)) return;
	/* A new source counts from where it is: the thread's bytes and the
	 * stat's are not the same count */
	source = io.sure ? 1 : 2;
	if (source != io.source) {
		if (io.source) say("usbdrive: counting %s\n", io.sure ? "the gadget's own bytes" : "the card's blocks");
		memset(&live.d, 0, sizeof live.d);
		io.source = source;
	}
	if (!dst_step(&live.d, rd, wr, now) || !live.on) return;
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

/* The joypad (A, B, FUNCTION) and the volume keys, read but not grabbed:
 * nothing else reads them while ZyperOS is away */
static int input_with(int code)
{
	for (int i = 0; i < 32; i++) {
		char path[32];
		unsigned char keys[KEY_MAX / 8 + 1] = { 0 };
		int fd;

		snprintf(path, sizeof path, "/dev/input/event%d", i);
		if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && (keys[code / 8] & (1 << (code % 8))))
			return fd;
		close(fd);
	}
	return -1;
}

/* Each press and release on `fd` through drv_key; the first action that
 * ends or changes the page is acted on, the rest of the batch read */
static drv_act keys_read(int fd, drv_keys *k, unsigned now)
{
	struct input_event ev;
	drv_act act = DRV_A_NONE;

	while (fd >= 0 && read(fd, &ev, sizeof ev) == sizeof ev) {
		int key = -1;
		drv_act a;

		if (ev.type != EV_KEY || ev.value == 2) continue;
		switch (ev.code) {
		case BTN_FN:        key = DRV_K_FN; break;
		case KEY_VOLUMEUP:  key = DRV_K_VOLUP; break;
		case KEY_VOLUMEDOWN:key = DRV_K_VOLDN; break;
		case BTN_SOUTH:     key = DRV_K_A; break;
		case BTN_EAST:      key = DRV_K_B; break;
		default:            continue;
		}
		if (!bri.on && (key == DRV_K_VOLUP || key == DRV_K_VOLDN)) continue;
		a = drv_key(k, key, ev.value == 1,
		            drv_copying(io.sure, live.d.in.total > 0, now, live.d.in.moved_ms, io_dirty_kb()));
		if (a == DRV_A_BRIGHT) bright_set(k->level, now);
		else if (a != DRV_A_NONE && act == DRV_A_NONE) act = a;
	}
	return act;
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
	int pfd, pad, vol;
	drv_keys keys = { 0 };

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
	bright_start();
	pfd = power_open();
	pad = input_with(BTN_SOUTH);
	vol = input_with(KEY_VOLUMEUP);
	keys.level = bri.on ? bri.b.level : 0;
	keys.max = bri.on ? bri.b.rungs - 1 : 0;
	while (end == DRV_STAY) {
		struct pollfd pf[3] = { { pfd, POLLIN, 0 }, { pad, POLLIN, 0 }, { vol, POLLIN, 0 } };
		unsigned now;
		drv_act act;

		/* A press wakes it at once; else five looks a second */
		poll(pf, 3, 200);
		now = now_ms();
		act = keys_read(pad, &keys, now);
		if (act == DRV_A_NONE) act = keys_read(vol, &keys, now);
		else keys_read(vol, &keys, now);
		if (act == DRV_A_ASK) {
			char why[96];

			snprintf(why, sizeof why, "%s, last write %u ms ago, %lu kB dirty",
			         io.sure ? "the gadget's bytes" : "not sure", now - live.d.in.moved_ms, io_dirty_kb());
			say("usbdrive: B while the computer may still be copying: asked (%s)\n", why);
			if (live.on && live.confirm) {
				live.covered = true;
				panel_update(live.confirm, 0, 0, live.l.w, live.l.h);
			} else {
				show("confirm.raw");
			}
		} else if (act == DRV_A_KEEP) {
			if (live.on) {
				live.covered = false;
				panel_update(live.page, 0, 0, live.l.w, live.l.h);
			} else {
				show("connected.raw");
			}
		} else if (act == DRV_A_EJECT) {
			/* The medium taken away, as a computer sees a card pulled from
			 * its reader: it lets go of it, then the export ends as an eject */
			put(LUN "/forced_eject", "1");
			end = DRV_PIXEL;
			break;
		}
		bright_follow(now);
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
	    : end == DRV_PIXEL ? "usbdrive: ejected on the handheld\n"
	    : end == DRV_UNPLUGGED ? "usbdrive: the cable came out\n"
	    : end == DRV_POWER ? "usbdrive: POWER\n" : "usbdrive: no computer took it\n", NULL);
	show("finishing.raw");
	/* splash has the display now; the live page's buffer can go */
	usleep(100000);
	live_stop();
	export_off();
	sync();
	if (pfd >= 0) { ioctl(pfd, EVIOCGRAB, 0); close(pfd); }
	if (pad >= 0) close(pad);
	if (vol >= 0) close(vol);
	if (end == DRV_POWER) {
		if (still > 0) { kill(still, SIGTERM); waitpid(still, NULL, 0); }
		execl("/sbin/poweroff", "poweroff", (char *)NULL);
		return 1;
	}
	card_on();
	back_to_zyperos(argv[2]);
	return 0;
}
