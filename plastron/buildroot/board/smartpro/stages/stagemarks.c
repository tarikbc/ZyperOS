/* stagemarks: the Smart Pro stage card's /rdinit, a diagnostic.
 *
 * The stage card (scripts/build-smartpro-stages.sh) is a Smart Pro card that
 * says on its own boot partition how far one power-on got, for a handheld
 * with a black screen and no serial cable: U-Boot's boot.scr leaves zy-1 to
 * zy-7 there (boot-marks.py), and this program, the initramfs's first, leaves
 * zy-8 and the rest, then runs the card's own rootswitch (/rootswitch here)
 * as /rdinit would have.
 *
 * It writes to the card's boot partition and nowhere else: the partition
 * named "boot" in the GPT of an mmc device the kernel calls an SD card,
 * mounted as vfat, with boot.scr and a/initramfs.cpio.gz on it. The Smart
 * Pro's eMMC has no node in smartpro.dtb, so Linux never sees it; this
 * checks the card anyway before every write.
 *
 * zy-8-rdinit.txt, before rootswitch: the kernel, its command line, the
 * partitions and the whole kernel log. Then a child stays behind in the
 * initramfs, whose root it keeps after rootswitch hands / to the system, and
 * watches the start for 5 minutes: each step of the Brick inittab it sees
 * gets a file of its own (zy-9 to zy-16) with the processes, the start's own
 * marks (/tmp/boot-times) and the end of the kernel log; zy-watch.txt has
 * every process it saw, and zy-dmesg.txt the whole log.
 *
 * Once the system has the boot partition mounted read-only at /boot
 * (/etc/init.d/brick), a second mount read-write is refused, and the
 * system's own mount would fail while one of these held the partition. So
 * before init this mounts the partition itself, after the system's mount it
 * remounts the system's mount read-write for each write and read-only again
 * (as ZyperOS's System Update does), and in between, from the root's mount
 * to the system's mount of /boot, it waits, 20 s at most.
 */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/klog.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define ZDEV "/zydev"
#define ZSYS "/zysys"
#define ZPROC "/zyproc"
#define ZBOOT "/zyboot"
/* The system's /boot, from this root, once init runs */
#define SYSBOOT ZPROC "/1/root/boot"
#define SYSTMP ZPROC "/1/root/tmp"
#define NEXT "/rootswitch"
#ifndef WATCH_S
#define WATCH_S 300
#endif
#define FAST_S (WATCH_S * 2 / 5)
#define WAIT_MOUNT_S 20
#define PIDS 65536
#ifdef STAGEMARKS_TEST
/* The host's test of this, in a container: a loop device for the card, and
 * the time from the test's start */
#define DISK "loop"
#else
#define DISK "mmcblk"
#endif
static double t_base;

struct buf {
	char *p;
	size_t n, cap;
};

static void bput(struct buf *b, const char *s, size_t n)
{
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap : 4096;
		char *q;

		while (c < b->n + n + 1)
			c *= 2;
		q = realloc(b->p, c);
		if (!q)
			return;
		b->p = q;
		b->cap = c;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

static void bprintf(struct buf *b, const char *fmt, ...)
{
	char tmp[4096];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if ((size_t)n >= sizeof tmp)
		n = sizeof tmp - 1;
	bput(b, tmp, n);
}

/* A file's text, up to max bytes, or a line saying there is none */
static void bfile(struct buf *b, const char *path, size_t max)
{
	char tmp[4096];
	size_t total = 0;
	ssize_t n;
	int fd = open(path, O_RDONLY);

	if (fd < 0) {
		bprintf(b, "(no %s: %s)\n", path, strerror(errno));
		return;
	}
	while (total < max && (n = read(fd, tmp, sizeof tmp)) > 0) {
		if (total + n > max)
			n = max - total;
		bput(b, tmp, n);
		total += n;
	}
	close(fd);
	if (total && b->p[b->n - 1] != '\n')
		bput(b, "\n", 1);
}

static double uptime(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9 - t_base;
}

static void say(const char *fmt, ...)
{
	char tmp[512];
	va_list ap;
	int fd = open(ZDEV "/kmsg", O_WRONLY);

	if (fd < 0)
		return;
	va_start(ap, fmt);
	vsnprintf(tmp, sizeof tmp, fmt, ap);
	va_end(ap);
	dprintf(fd, "stagemarks: %s\n", tmp);
	close(fd);
}

/* The kernel log, all of it or its last tail bytes from a line's start */
static void bdmesg(struct buf *b, size_t tail)
{
	int size = klogctl(10, NULL, 0), n;
	size_t start = 0;
	char *k;

	if (size <= 0)
		size = 1 << 20;
	k = malloc(size);
	if (!k)
		return;
	n = klogctl(3, k, size);
	if (n < 0) {
		bprintf(b, "(no kernel log: %s)\n", strerror(errno));
		free(k);
		return;
	}
	if (tail && (size_t)n > tail) {
		start = n - tail;
		while (start < (size_t)n && k[start - 1] != '\n')
			start++;
	}
	bput(b, k + start, n - start);
	free(k);
}

/* The card's boot partition, "mmcblk0p1": the GPT's "boot" on an SD card */
static char part[64];

static int read_line(const char *path, char *out, size_t size)
{
	FILE *f = fopen(path, "r");

	out[0] = 0;
	if (!f)
		return 0;
	if (!fgets(out, size, f))
		out[0] = 0;
	fclose(f);
	return out[0] != 0;
}

static int find_boot(void)
{
	DIR *dir = opendir(ZSYS "/class/block");
	struct dirent *e;
	int found = 0;

	if (!dir)
		return 0;
	while (!found && (e = readdir(dir))) {
		char path[300], line[128], disk[64], type[32];
		int named = 0;
		size_t n;
		FILE *uevent;

		if (strncmp(e->d_name, DISK, strlen(DISK)) != 0)
			continue;
		snprintf(path, sizeof path, ZSYS "/class/block/%s/uevent", e->d_name);
		uevent = fopen(path, "r");
		if (!uevent)
			continue;
		while (fgets(line, sizeof line, uevent))
			if (strcmp(line, "PARTNAME=boot\n") == 0)
				named = 1;
		fclose(uevent);
		if (!named)
			continue;
		/* mmcblk0p1's disk, mmcblk0, has to be an SD card */
		n = strlen(e->d_name);
		if (n >= sizeof disk)
			continue;
		memcpy(disk, e->d_name, n + 1);
		while (n && disk[n - 1] >= '0' && disk[n - 1] <= '9')
			n--;
		if (n < 2 || disk[n - 1] != 'p')
			continue;
		disk[n - 1] = 0;
		snprintf(path, sizeof path, ZSYS "/block/%s/device/type", disk);
#ifdef STAGEMARKS_TEST
		snprintf(type, sizeof type, "SD\n");
#else
		read_line(path, type, sizeof type);
#endif
		if (strcmp(type, "SD\n") == 0) {
			memcpy(part, e->d_name, strlen(e->d_name) + 1);
			found = 1;
		}
	}
	closedir(dir);
	return found;
}

/* This card's boot partition, by what is on it */
static int is_card(const char *dir)
{
	char a[300], b[300];

	snprintf(a, sizeof a, "%s/boot.scr", dir);
	snprintf(b, sizeof b, "%s/a/initramfs.cpio.gz", dir);
	return access(a, F_OK) == 0 && access(b, F_OK) == 0;
}

static int own_mount(void)
{
	char dev[128];

	if (!part[0] && !find_boot())
		return -1;
	snprintf(dev, sizeof dev, ZDEV "/%s", part);
	mkdir(ZBOOT, 0755);
	if (mount(dev, ZBOOT, "vfat", MS_NOATIME | MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) != 0)
		return -1;
	if (!is_card(ZBOOT)) {
		umount(ZBOOT);
		return -1;
	}
	return 0;
}

static int write_file(const char *dir, const char *name, const char *s, size_t n)
{
	char path[300];
	size_t done = 0;
	int fd, ok = 1;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	while (done < n) {
		ssize_t w = write(fd, s + done, n - done);

		if (w <= 0) {
			ok = 0;
			break;
		}
		done += w;
	}
	if (fsync(fd) != 0)
		ok = 0;
	close(fd);
	return ok ? 0 : -1;
}

/* Init's /boot when it is this card's boot partition: 1, with ro whether
 * it is mounted read-only and flags its other mount flags */
static int sys_boot(int *ro, unsigned long *flags)
{
	char line[1024], dev[256], mnt[256], type[64], opts[512];
	int found = 0;
	FILE *f;

	if (!part[0])
		return 0;
	f = fopen(ZPROC "/1/mounts", "r");
	if (!f)
		return 0;
	while (fgets(line, sizeof line, f)) {
		const char *base;
		char *o, *save = NULL;

		if (sscanf(line, "%255s %255s %63s %511s", dev, mnt, type, opts) != 4 ||
		    strcmp(mnt, "/boot") != 0 || strcmp(type, "vfat") != 0)
			continue;
		base = strrchr(dev, '/');
		if (strcmp(base ? base + 1 : dev, part) != 0)
			continue;
		found = 1;
		*ro = 0;
		*flags = 0;
		for (o = strtok_r(opts, ",", &save); o; o = strtok_r(NULL, ",", &save)) {
			if (strcmp(o, "ro") == 0)
				*ro = 1;
			else if (strcmp(o, "noatime") == 0)
				*flags |= MS_NOATIME;
			else if (strcmp(o, "nodiratime") == 0)
				*flags |= MS_NODIRATIME;
			else if (strcmp(o, "nosuid") == 0)
				*flags |= MS_NOSUID;
			else if (strcmp(o, "nodev") == 0)
				*flags |= MS_NODEV;
			else if (strcmp(o, "noexec") == 0)
				*flags |= MS_NOEXEC;
			else if (strcmp(o, "sync") == 0)
				*flags |= MS_SYNCHRONOUS;
		}
	}
	fclose(f);
	return found;
}

static int comm_is(int pid, const char *name)
{
	char path[64], line[64];

	snprintf(path, sizeof path, ZPROC "/%d/comm", pid);
	if (!read_line(path, line, sizeof line))
		return 0;
	line[strcspn(line, "\n")] = 0;
	return strcmp(line, name) == 0;
}

/* The watch */

enum { K_ROOT, K_INIT, K_CMD, K_COMM };

struct stage {
	const char *file, *what, *match;
	int kind;
	double t;
	int pending;
	struct buf text;
};

static struct stage stages[] = {
	{ .file = "zy-9-root.txt", .what = "9: the root partition is mounted (rootswitch, squashfs)",
	  .match = NULL, .kind = K_ROOT },
	{ .file = "zy-10-init.txt", .what = "10: /sbin/init runs (rootswitch handed over)",
	  .match = NULL, .kind = K_INIT },
	{ .file = "zy-11-splash.txt", .what = "11: init runs /etc/init.d/splash start",
	  .match = "/etc/init.d/splash", .kind = K_CMD },
	{ .file = "zy-12-brick.txt", .what = "12: init runs /etc/init.d/brick start",
	  .match = "/etc/init.d/brick", .kind = K_CMD },
	{ .file = "zy-13-datapart.txt", .what = "13: init runs /etc/init.d/datapart start",
	  .match = "/etc/init.d/datapart", .kind = K_CMD },
	{ .file = "zy-14-zyperos.txt", .what = "14: init runs /etc/init.d/zyperos start",
	  .match = "/etc/init.d/zyperos", .kind = K_CMD },
	{ .file = "zy-15-launcher.txt", .what = "15: the launcher runs (a process named zyperos.elf)",
	  .match = "zyperos.elf", .kind = K_COMM },
	{ .file = "zy-16-usbgadget.txt", .what = "16: init runs /etc/init.d/usbgadget start, its last start step",
	  .match = "/etc/init.d/usbgadget", .kind = K_CMD },
};
#define NSTAGES (sizeof stages / sizeof stages[0])

static int boot_number;
static double t_start, t_root, t_init;
static struct buf proclog, errors;
static uint32_t cmd_hash[PIDS];

static uint32_t hash(const char *s, size_t n)
{
	uint32_t h = 2166136261u;

	while (n--)
		h = (h ^ (unsigned char)*s++) * 16777619u;
	return h ? h : 1;
}

/* A process's command line, its NULs as spaces; 0 bytes for a kernel thread */
static size_t cmdline_of(int pid, char *out, size_t size)
{
	char path[64];
	ssize_t n;
	int fd, i;

	snprintf(path, sizeof path, ZPROC "/%d/cmdline", pid);
	fd = open(path, O_RDONLY);
	out[0] = 0;
	if (fd < 0)
		return 0;
	n = read(fd, out, size - 1);
	close(fd);
	if (n <= 0)
		return 0;
	while (n > 0 && out[n - 1] == 0)
		n--;
	for (i = 0; i < n; i++)
		if (out[i] == 0)
			out[i] = ' ';
	out[n] = 0;
	return n;
}

static void bprocesses(struct buf *b)
{
	DIR *dir = opendir(ZPROC);
	struct dirent *e;
	char cmd[1024];

	if (!dir)
		return;
	while ((e = readdir(dir))) {
		int pid = atoi(e->d_name);

		if (pid > 0 && cmdline_of(pid, cmd, sizeof cmd))
			bprintf(b, "%6d %s\n", pid, cmd);
	}
	closedir(dir);
}

/* What a stage's file holds, as it is when the stage is seen */
static void hit(struct stage *s, double t, const char *detail)
{
	struct buf *b = &s->text;

	s->t = t;
	s->pending = 1;
	bprintf(b, "%s\nseen at %.3f s after the kernel started, boot %d of this card\n", s->what, t, boot_number);
	if (detail)
		bprintf(b, "%s\n", detail);
	bprintf(b, "\n--- processes now (pid, command line) ---\n");
	bprocesses(b);
	if (t_init) {
		bprintf(b, "\n--- the start's own marks, /tmp/boot-times ---\n");
		bfile(b, SYSTMP "/boot-times", 16384);
		bprintf(b, "\n--- /tmp/splash-say ---\n");
		bfile(b, SYSTMP "/splash-say", 1024);
	}
	bprintf(b, "\n--- the kernel log's last 16 KB ---\n");
	bdmesg(b, 16384);
	say("stage %s at %.3f s", s->file, t);
}

static int scan(double t)
{
	DIR *dir;
	struct dirent *e;
	char cmd[2048], line[512];
	int changed = 0;
	size_t i;

	if (!stages[0].t) {
		FILE *f = fopen(ZPROC "/self/mounts", "r");

		while (f && fgets(line, sizeof line, f))
			if (strstr(line, " squashfs ")) {
				line[strcspn(line, "\n")] = 0;
				t_root = t;
				hit(&stages[0], t, line);
				changed = 1;
				break;
			}
		if (f)
			fclose(f);
	}
	if (!stages[1].t && comm_is(1, "init")) {
		t_init = t;
		hit(&stages[1], t, NULL);
		changed = 1;
	}
	dir = opendir(ZPROC);
	if (!dir)
		return changed;
	while ((e = readdir(dir))) {
		int pid = atoi(e->d_name);
		size_t n;
		uint32_t h;

		if (pid <= 0)
			continue;
		n = cmdline_of(pid, cmd, sizeof cmd);
		h = hash(cmd, n);
		if (cmd_hash[pid % PIDS] == h)
			continue;
		cmd_hash[pid % PIDS] = h;
		if (!n)
			continue;
		if (proclog.n < (256 << 10))
			bprintf(&proclog, "%9.3f %6d %s\n", t, pid, cmd);
		changed = 1;
		for (i = 2; i < NSTAGES; i++) {
			struct stage *s = &stages[i];
			char detail[2200];

			if (s->t)
				continue;
			if ((s->kind == K_CMD && strstr(cmd, s->match)) ||
			    (s->kind == K_COMM && comm_is(pid, s->match))) {
				snprintf(detail, sizeof detail, "process %d: %s", pid, cmd);
				hit(s, t, detail);
			}
		}
	}
	closedir(dir);
	return changed;
}

static const char *const backlight_files[] = { "brightness", "actual_brightness", "max_brightness", "bl_power", NULL };
static const char *const graphics_files[] = { "name", "virtual_size", "bits_per_pixel", NULL };

/* Each device of a sysfs class with a few of its files */
static void bclass(struct buf *b, const char *class, const char *const *files)
{
	char path[300];
	struct dirent *e;
	DIR *dir;
	int i, any = 0;

	snprintf(path, sizeof path, ZSYS "/class/%s", class);
	dir = opendir(path);
	while (dir && (e = readdir(dir))) {
		if (e->d_name[0] == '.')
			continue;
		any = 1;
		for (i = 0; files[i]; i++) {
			snprintf(path, sizeof path, ZSYS "/class/%s/%s/%s", class, e->d_name, files[i]);
			bprintf(b, "%s/%s/%s: ", class, e->d_name, files[i]);
			bfile(b, path, 128);
		}
	}
	if (dir)
		closedir(dir);
	if (!any)
		bprintf(b, "(no %s devices)\n", class);
}

static void bwatch(struct buf *b, double t)
{
	size_t i;

	bprintf(b, "The stage card's watch, boot %d of this card, written at %.3f s\n\n", boot_number, t);
	bprintf(b, "--- stages (the last one seen is how far this start got) ---\n");
	for (i = 0; i < NSTAGES; i++) {
		if (stages[i].t)
			bprintf(b, "%9.3f %s\n", stages[i].t, stages[i].what);
		else
			bprintf(b, "  not seen %s\n", stages[i].what);
	}
	bprintf(b, "\n--- every process seen (first seen at, pid, command line) ---\n");
	if (proclog.n)
		bput(b, proclog.p, proclog.n);
	if (t_init) {
		bprintf(b, "\n--- /tmp/boot-times ---\n");
		bfile(b, SYSTMP "/boot-times", 16384);
		bprintf(b, "\n--- /tmp/splash-say ---\n");
		bfile(b, SYSTMP "/splash-say", 1024);
		bprintf(b, "\n--- /tmp/zyperos-no-start ---\n");
		bfile(b, SYSTMP "/zyperos-no-start", 1024);
		bprintf(b, "\n--- init's mounts ---\n");
		bfile(b, ZPROC "/1/mounts", 8192);
	}
	bprintf(b, "\n--- the panel ---\n");
	bclass(b, "backlight", backlight_files);
	bclass(b, "graphics", graphics_files);
	if (errors.n) {
		bprintf(b, "\n--- this program's own trouble ---\n");
		bput(b, errors.p, errors.n);
	}
	bprintf(b, "\n--- the kernel log's last 32 KB ---\n");
	bdmesg(b, 32768);
}

/* Every file waiting, onto the card's boot partition, by whichever way is
 * safe now: 0 when written, -1 to try again later */
static int write_all(const char *dir, const struct buf *w, const struct buf *d)
{
	size_t s;
	int bad = 0;

	for (s = 0; s < NSTAGES; s++)
		if (stages[s].pending && stages[s].text.n) {
			if (write_file(dir, stages[s].file, stages[s].text.p, stages[s].text.n) == 0)
				stages[s].pending = 0;
			else
				bad = 1;
		}
	if (write_file(dir, "zy-watch.txt", w->p, w->n) != 0)
		bad = 1;
	if (d->n && write_file(dir, "zy-dmesg.txt", d->p, d->n) != 0)
		bad = 1;
	return bad ? -1 : 0;
}

static int flush(double t, int with_dmesg)
{
	unsigned long flags = 0;
	struct buf w = { 0 }, d = { 0 };
	int ro = 0, i, ret = -1;

	/* What the watch says, before any remount: the mounts it lists are
	 * the system's own */
	bwatch(&w, t);
	if (with_dmesg) {
		bprintf(&d, "The whole kernel log at %.3f s, boot %d of this card\n\n", t, boot_number);
		bdmesg(&d, 0);
	}

	if (sys_boot(&ro, &flags)) {
		if (!is_card(SYSBOOT))
			goto out;
		if (!ro) {
			ret = write_all(SYSBOOT, &w, &d);
			goto out;
		}
		if (mount("none", SYSBOOT, NULL, MS_REMOUNT | flags, NULL) != 0) {
			if (errors.n < 8192)
				bprintf(&errors, "%.3f remount read-write: %s\n", t, strerror(errno));
			goto out;
		}
		ret = write_all(SYSBOOT, &w, &d);
		for (i = 0; i < 50; i++) {
			if (mount("none", SYSBOOT, NULL, MS_REMOUNT | MS_RDONLY | flags, NULL) == 0)
				break;
			usleep(100000);
		}
		if (i == 50 && errors.n < 8192)
			bprintf(&errors, "%.3f remount read-only again: %s\n", t, strerror(errno));
	} else if ((!t_root && t - t_start > 5) || (t_root && t - t_root > WAIT_MOUNT_S)) {
		/* rootswitch waited its 3 s for the root and stopped, or the system
		 * got no further than its own mount of /boot in 20 s: nothing else
		 * mounts the partition now */
		if (own_mount() != 0)
			goto out;
		ret = write_all(ZBOOT, &w, &d);
		umount(ZBOOT);
	}
	sync();
out:
	free(w.p);
	free(d.p);
	return ret;
}

static void watch(void)
{
	static const double dmesg_at[] = { WATCH_S / 10, WATCH_S / 5, WATCH_S * 2 / 5, WATCH_S };
	double t, last_try = 0, last_ok = 0;
	size_t next_dmesg = 0, s;
	int changed = 1, pending;

	for (;;) {
		int dmesg_due;

		t = uptime();
		if (scan(t))
			changed = 1;
		pending = 0;
		for (s = 0; s < NSTAGES; s++)
			pending |= stages[s].pending;
		dmesg_due = next_dmesg < sizeof dmesg_at / sizeof dmesg_at[0] && t >= dmesg_at[next_dmesg];
		if ((pending && t - last_try >= 0.5) || (changed && t - last_ok >= 5) || dmesg_due) {
			last_try = t;
			if (flush(t, dmesg_due) == 0) {
				last_ok = t;
				changed = 0;
				if (dmesg_due)
					next_dmesg++;
			}
		}
		if (t >= WATCH_S && next_dmesg >= sizeof dmesg_at / sizeof dmesg_at[0])
			break;
		usleep(t < FAST_S ? 100000 : 1000000);
	}
	say("watch over at %.3f s", uptime());
}

/* The first mark, before rootswitch: zy-8-rdinit.txt */
static void first_mark(double found_at)
{
	struct buf b = { 0 };
	struct utsname u;
	char line[64];
	struct stat st;
	int n = 0;

	if (own_mount() != 0) {
		say("no boot partition to write to (%s)", part[0] ? part : "none found");
		return;
	}
	if (read_line(ZBOOT "/zy-boot-count.txt", line, sizeof line))
		n = atoi(line);
	boot_number = n + 1;
	snprintf(line, sizeof line, "%d\n", boot_number);
	write_file(ZBOOT, "zy-boot-count.txt", line, strlen(line));

	bprintf(&b, "8: rdinit runs: Linux started and ran the initramfs's first program\n");
	bprintf(&b, "boot %d of this card, %.3f s after the kernel started\n", boot_number, uptime());
	bprintf(&b, "the boot partition, %s, there at %.3f s\n", part, found_at);
	if (uname(&u) == 0)
		bprintf(&b, "uname -a: %s %s %s %s %s\n", u.sysname, u.nodename, u.release, u.version, u.machine);
	bprintf(&b, "cmdline: ");
	bfile(&b, ZPROC "/cmdline", 4096);
	bprintf(&b, "next: %s, %s\n", NEXT,
		stat(NEXT, &st) == 0 ? "there" : "MISSING, nothing will start");
	bprintf(&b, "\n--- /proc/partitions ---\n");
	bfile(&b, ZPROC "/partitions", 8192);
	bprintf(&b, "\n--- /proc/meminfo ---\n");
	bfile(&b, ZPROC "/meminfo", 1024);
	bprintf(&b, "\n--- the whole kernel log ---\n");
	bdmesg(&b, 0);
	if (write_file(ZBOOT, "zy-8-rdinit.txt", b.p, b.n) != 0)
		say("zy-8-rdinit.txt not written: %s", strerror(errno));
	free(b.p);
	umount(ZBOOT);
	sync();
}

int main(void)
{
	double found_at = 0;
	pid_t child;
	int i;

#ifdef STAGEMARKS_TEST
	t_base = uptime();
#endif
	mkdir(ZDEV, 0755);
	mkdir(ZSYS, 0755);
	mkdir(ZPROC, 0755);
	mount("devtmpfs", ZDEV, "devtmpfs", 0, NULL);
	mount("sysfs", ZSYS, "sysfs", 0, NULL);
	mount("proc", ZPROC, "proc", 0, NULL);

	/* The card is probed alongside the rest of the boot: up to 10 s */
	for (i = 0; i < 1000; i++) {
		if (find_boot()) {
			found_at = uptime();
			break;
		}
		usleep(10000);
	}
	if (part[0])
		first_mark(found_at);
	else
		say("no boot partition on an SD card after 10 s");

	t_start = uptime();
	child = fork();
	if (child == 0) {
		int fd = open(ZDEV "/null", O_RDWR);

		setsid();
		if (nice(5) == -1)
			say("nice: %s", strerror(errno));
		if (chdir("/") != 0)
			_exit(1);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
		signal(SIGHUP, SIG_IGN);
		watch();
		_exit(0);
	}
	execl(NEXT, "rdinit", (char *)NULL);
	say("can't run %s: %s", NEXT, strerror(errno));
	for (;;)
		pause();
}
