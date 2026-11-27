/* SPDX-License-Identifier: GPL-3.0-only */
/* The launcher never stops on the USB controller, and says where it would
 * have (2026-11-21: a Pixel 2 hung in a write to soft_connect as File
 * Transfer opened over a link in use, the screen frozen, POWER dead, no line
 * in the log):
 *
 *     make check-usbhang
 *
 *   - hwput.h: a write that never returns (a FIFO nobody reads, as a driver
 *     that never answers) costs the caller HWPUT_TIMEOUT_MS and no more, is
 *     said before it is made and when it is late, keeps every other job out
 *     while it hangs, and its child is reaped once it ends: no zombie.
 *   - cablefresh.h: File Transfer's fresh link from fake controller files:
 *     a link the computer answers on is left alone; one it does not, or one
 *     not whole, is made fresh ("disconnect", then "connect"), without the
 *     caller waiting; a link bound a moment ago is left; the Brick's.
 *   - klog.h: the kept lines (usb:, xfer:, ...) reach the kernel's log (a
 *     file here) whole and in order, the others do not, the log is synced
 *     after them by its own thread, a flood is held to the rate; and what
 *     the filter costs a line that is not kept, measured.
 *   - wdog.h: with short times, a loop that feeds is left alone, a held long
 *     task too, a stuck one (the test hook, SIGUSR2) has its screen put out
 *     and back on when it comes round, and one that never comes round is
 *     synced and restarted (sysrq "s" then "b", into a file here).
 *
 * Built with -DUSBHANG_BEFORE, the hwput and fresh-link parts run 1.1.0's
 * plat_cable_reconnect instead (the writes on the caller's thread, every
 * time), which is what they fail on. */
#include "../src/cablefresh.h"
#include "../src/hwput.h"
#include "../src/klog.h"
#include "../src/linksteps.h"
#include "../src/wdog.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails;

#define CHECK(cond, ...) do { \
	if (!(cond)) { fails++; printf("  FAIL %s:%d: ", __FILE__, __LINE__); \
	               printf(__VA_ARGS__); putchar('\n'); fflush(stdout); } \
} while (0)

static char g_dir[64], g_log[128], g_kmsg[128];

static unsigned ms_now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
}

static void nap(unsigned ms)
{
	struct timespec t = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };

	while (nanosleep(&t, &t) < 0 && errno == EINTR) { }
}

static void put_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");

	if (f) { fputs(text, f); fclose(f); }
}

/* A whole file, into a static buffer */
static const char *slurp(const char *path)
{
	static char buf[1 << 18];
	int fd = open(path, O_RDONLY);
	ssize_t n = fd >= 0 ? read(fd, buf, sizeof buf - 1) : 0;

	if (fd >= 0) close(fd);
	buf[n > 0 ? n : 0] = '\0';
	return buf;
}

static bool log_has(const char *what) { fflush(stderr); return strstr(slurp(g_log), what) != NULL; }
static bool kmsg_has(const char *what) { return strstr(slurp(g_kmsg), what) != NULL; }

/* No child left: every one reaped */
static bool no_children(void)
{
	return waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD;
}

static void wait_idle(unsigned ms)
{
	unsigned t0 = ms_now();

	while (hwput_busy() && ms_now() - t0 < ms) nap(5);
}

/* ---- a fake soft_connect: a FIFO, read by a thread that can stop --------- */

static char            g_sc[160];
static char            g_got[256];
static volatile bool   g_paused, g_reader_on;
static pthread_mutex_t g_got_mx = PTHREAD_MUTEX_INITIALIZER;

static void *reader(void *arg)
{
	(void)arg;
	while (g_reader_on) {
		char buf[64];
		ssize_t n;
		int fd;

		if (g_paused) { nap(5); continue; }
		/* Waits for a writer, as a driver's file waits for nobody: then
		 * all it writes, to its close */
		if ((fd = open(g_sc, O_RDONLY)) < 0) { nap(5); continue; }
		while ((n = read(fd, buf, sizeof buf - 1)) > 0) {
			buf[n] = '\0';
			pthread_mutex_lock(&g_got_mx);
			if (g_got[0]) strncat(g_got, " ", sizeof g_got - strlen(g_got) - 1);
			strncat(g_got, buf, sizeof g_got - strlen(g_got) - 1);
			pthread_mutex_unlock(&g_got_mx);
		}
		close(fd);
	}
	return NULL;
}

/* The reader let out of its open, to see a pause or its end */
static void reader_poke(void)
{
	int fd = open(g_sc, O_WRONLY);

	if (fd >= 0) close(fd);
}

static void got(char *out, size_t n)
{
	pthread_mutex_lock(&g_got_mx);
	snprintf(out, n, "%s", g_got);
	g_got[0] = '\0';
	pthread_mutex_unlock(&g_got_mx);
}

#ifdef USBHANG_BEFORE
/* 1.1.0's plat_cable_reconnect (src/device/pixel2.c), the controller's
 * folder handed in: both writes on the caller's thread, every time */
static void old_reconnect(const char *udc)
{
	static const struct timespec gap = { 0, 300 * 1000000L };
	char path[300];
	int fd;

	snprintf(path, sizeof path, "%s/soft_connect", udc);
	if ((fd = open(path, O_WRONLY | O_CLOEXEC)) < 0) return;
	if (write(fd, "disconnect", 10) < 0) { }
	close(fd);
	nanosleep(&gap, NULL);
	if ((fd = open(path, O_WRONLY | O_CLOEXEC)) < 0) return;
	if (write(fd, "connect", 7) < 0) { }
	close(fd);
	fprintf(stderr, "wire: the cable connection made fresh\n");
}

#endif

/* ---- klog ------------------------------------------------------------- */

static void check_klog(void)
{
	char last[400], kept[400], big[900];

	printf("  klog: the kept lines in the kernel's log, synced\n");
	CHECK(klog_kept("usb: x", 6) && klog_kept("xfer: x", 7) && klog_kept("mdns: x", 7) &&
	      klog_kept("crash: x", 8) && klog_kept("power: x", 8) && klog_kept("wire: x", 7) &&
	      klog_kept("watchdog: x", 11), "a kept prefix not kept");
	CHECK(!klog_kept("night: x", 8) && !klog_kept("usbx: x", 7) && !klog_kept("usb:x", 5) &&
	      !klog_kept("  usb: x", 8), "a line kept that is not");

	fprintf(stderr, "night: Night Mode on\n");
	fprintf(stderr, "usb: soft_connect disconnect\n");
	fprintf(stderr, "xfer: a line ");
	fprintf(stderr, "in two writes\n");
	memset(big, 'z', sizeof big - 1);
	big[sizeof big - 1] = '\0';
	fprintf(stderr, "mdns: %s\n", big);
	CHECK(klog_settle(2000), "the log not synced in 2 s");
	CHECK(kmsg_has("<13>zyperos: usb: soft_connect disconnect\n"), "the usb: line not in the kernel's log");
	CHECK(kmsg_has("<13>zyperos: xfer: a line in two writes\n"), "a line in two writes not whole");
	CHECK(!kmsg_has("night:"), "a line not kept in the kernel's log");
	CHECK(kmsg_has("<13>zyperos: mdns: zzzz"), "a long kept line not there");
	CHECK(log_has("night: Night Mode on\nusb: soft_connect disconnect\nxfer: a line in two writes\n"),
	      "the log file not as written");
	CHECK(klog_synced() >= 1, "no sync after a kept line");
	klog_last(last, sizeof last, kept, sizeof kept);
	CHECK(!strncmp(last, "mdns: zzz", 9) && !strncmp(kept, "mdns: zzz", 9), "the last lines: \"%.20s\" \"%.20s\"",
	      last, kept);
	fprintf(stderr, "art: a line\n");
	klog_last(last, sizeof last, kept, sizeof kept);
	CHECK(!strcmp(last, "art: a line") && !strncmp(kept, "mdns:", 5), "the last kept line moved with another");

}

/* A flood held to the rate, and what the filter costs */
static void check_klog_rate(void)
{
	printf("  klog: a flood held to the rate; the cost\n");
	klog_rate_reset();
	/* A flood: held to the rate, the rest said once */
	{
		unsigned m0 = klog_mirrored(), d0 = klog_dropped();

		for (int i = 0; i < 120; i++) fprintf(stderr, "usb: flood %d\n", i);
		CHECK(klog_mirrored() - m0 <= 41 && klog_dropped() - d0 >= 79,
		      "a flood of 120: %u copied, %u left out", klog_mirrored() - m0, klog_dropped() - d0);
		nap(300);
		fprintf(stderr, "usb: after the flood\n");
		CHECK(kmsg_has("zyperos: log: ") && kmsg_has("kept lines not copied here, over the rate") &&
		      kmsg_has("zyperos: usb: after the flood"), "the line after a flood, or its note, missing");
	}

	/* What it costs: a line that is not kept, the filter against a plain
	 * stream on the same descriptor (both to /dev/null) */
	{
		int keep = dup(2), null = open("/dev/null", O_WRONLY);
		FILE *plain;
		unsigned t0, t_filter, t_plain, t_kept;
		const int N = 200000;

		dup2(null, 2);
		plain = fdopen(dup(2), "w");
		setvbuf(plain, NULL, _IOLBF, 4096);
		t0 = ms_now();
		for (int i = 0; i < N; i++) fprintf(stderr, "art: cover %d of a shelf, nothing kept\n", i);
		t_filter = ms_now() - t0;
		t0 = ms_now();
		for (int i = 0; i < N; i++) fprintf(plain, "art: cover %d of a shelf, nothing kept\n", i);
		t_plain = ms_now() - t0;
		fclose(plain);
		/* A kept line costs its caller no sync: the thread does it */
		t0 = ms_now();
		for (int i = 0; i < 20; i++) fprintf(stderr, "usb: kept %d\n", i);
		t_kept = ms_now() - t0;
		dup2(keep, 2);
		close(keep);
		close(null);
		printf("    cost: %.0f ns a line not kept through the filter, %.0f ns plain; "
		       "20 kept lines %u ms to their caller\n",
		       t_filter * 1e6 / N, t_plain * 1e6 / N, t_kept);
		CHECK(t_filter <= t_plain * 2 + 200, "the filter costs too much: %u ms against %u ms for %d lines",
		      t_filter, t_plain, N);
		CHECK(t_kept < 50, "20 kept lines took %u ms of their caller's", t_kept);
		klog_settle(2000);
	}}

/* ---- hwput ------------------------------------------------------------ */

static void check_hwput(void)
{
	char fifo[160], data[64];
	hwput_step step;
	unsigned t0, took;
	int r, fd;

	printf("  hwput: a write that never returns costs its caller the timeout and no more\n");
	snprintf(fifo, sizeof fifo, "%s/hang", g_dir);
	mkfifo(fifo, 0600);
	step = (hwput_step){ "usb: soft_connect disconnect", fifo, "disconnect", 0 };
	hwput_set_timeout(400);
#ifdef USBHANG_BEFORE
	(void)step; (void)r; (void)data; (void)fd;
	{
		/* 1.1.0: the write on the caller's own thread, here a process of
		 * its own so the check can go on past it */
		pid_t p = fork();

		if (p == 0) {
			int w = open(fifo, O_WRONLY);

			if (w >= 0) { if (write(w, "disconnect", 10) < 0) { } close(w); }
			_exit(0);
		}
		t0 = ms_now();
		while (waitpid(p, NULL, WNOHANG) == 0 && ms_now() - t0 < 3000) nap(10);
		took = ms_now() - t0;
		CHECK(took < 700, "the caller was stuck in the controller's write for %u ms, and on", took);
		kill(p, SIGKILL);
		waitpid(p, NULL, 0);
	}
	return;
#else
	t0 = ms_now();
	r = hwput_run(&(hwput_job){ "soft_connect", NULL, NULL, &step, 1 }, true);
	took = ms_now() - t0;
	CHECK(r == HWPUT_TIMEOUT, "a hung write: %d, not HWPUT_TIMEOUT", r);
	CHECK(took >= 390 && took < 700, "the caller waited %u ms for a 400 ms timeout", took);
	CHECK(log_has("usb: soft_connect disconnect\n"), "the write not said before it was made");
	CHECK(log_has("usb: the controller did not answer (soft_connect"), "the late write not said");
	CHECK(kmsg_has("zyperos: usb: soft_connect disconnect"), "the step not in the kernel's log");
	CHECK(hwput_busy(), "the hung child forgotten");
	t0 = ms_now();
	r = hwput_run(&(hwput_job){ "the gadget's bind", NULL, NULL, &step, 1 }, true);
	CHECK(r == HWPUT_BUSY && ms_now() - t0 < 50, "a second job while one hangs: %d after %u ms", r,
	      ms_now() - t0);
	CHECK(log_has("usb: the gadget's bind left out: the controller is still in a write"),
	      "the job left out not said");
	/* The driver answers at last: the child ends, and is reaped */
	fd = open(fifo, O_RDONLY | O_NONBLOCK);
	data[0] = '\0';
	for (t0 = ms_now(); ms_now() - t0 < 1000;) {
		ssize_t n = read(fd, data, sizeof data - 1);

		if (n > 0) { data[n] = '\0'; break; }
		nap(5);
	}
	close(fd);
	CHECK(!strcmp(data, "disconnect"), "the late write wrote \"%s\"", data);
	wait_idle(1000);
	CHECK(!hwput_busy(), "the child not reaped once its write ended");
	CHECK(no_children(), "a child left a zombie");
	CHECK(log_has("usb: the controller answered after all (soft_connect"), "its end not said");
	CHECK(hwput_last() == HWPUT_DONE, "its result: %d", hwput_last());

	/* A failed write is said with its errno, and the job is over */
	step = (hwput_step){ "usb: the gadget's UDC, bind", "/nonexistent/UDC", "x", 0 };
	r = hwput_run(&(hwput_job){ "the gadget's bind", NULL, NULL, &step, 1 }, true);
	CHECK(r == HWPUT_FAILED && log_has("usb: the gadget's UDC, bind failed (errno"),
	      "a failed write: %d", r);
	CHECK(no_children(), "a failed job left a zombie");
#endif
}

/* ---- the fresh link ---------------------------------------------------- */

static int  g_answer;               /* the fake computer: 1 answers */
static bool answered(void) { return g_answer == 1; }

static void fresh_case(const char *state, const char *carrier, bool state_tells, bool just_bound,
                       int answer, const char *want, const char *say, const char *what)
{
	char udc[160], net[160], path[200], wrote[256];
	unsigned t0, took;

	snprintf(udc, sizeof udc, "%s/udc", g_dir);
	snprintf(net, sizeof net, "%s/net", g_dir);
	snprintf(path, sizeof path, "%s/state", udc);
	put_file(path, state);
	snprintf(path, sizeof path, "%s/usb0/carrier", net);
	put_file(path, carrier);
	g_answer = answer;
	got(wrote, sizeof wrote);
	t0 = ms_now();
#ifdef USBHANG_BEFORE
	(void)state_tells; (void)just_bound;
	old_reconnect(udc);
#else
	cable_fresh(&(cable_fresh_cfg){ udc, "usb0", "10.42.0.2", state_tells, just_bound, net });
#endif
	took = ms_now() - t0;
	CHECK(took < 100, "%s: the caller waited %u ms", what, took);
	wait_idle(2000);
	nap(80);                           /* the reader's last read */
	got(wrote, sizeof wrote);
	CHECK(!strcmp(wrote, want), "%s: wrote \"%s\", not \"%s\"", what, wrote, want);
	CHECK(!say || log_has(say), "%s: \"%s\" not said", what, say);
}

static void check_fresh(void)
{
	char path[200];
	pthread_t t;

	printf("  cablefresh: File Transfer's fresh link only when the link needs it\n");
	snprintf(path, sizeof path, "%s/udc", g_dir);
	mkdir(path, 0700);
	snprintf(path, sizeof path, "%s/net", g_dir);
	mkdir(path, 0700);
	snprintf(path, sizeof path, "%s/net/usb0", g_dir);
	mkdir(path, 0700);
	snprintf(g_sc, sizeof g_sc, "%s/udc/soft_connect", g_dir);
	mkfifo(g_sc, 0600);
	g_reader_on = true;
	pthread_create(&t, NULL, reader, NULL);
	cable_fresh_test_probe(answered);
	hwput_set_timeout(600);

	fresh_case("configured\n", "1\n", true, false, 1, "",
	           "usb: the computer answers over the cable: the link left as it is",
	           "a link in use, the computer answering (the 2026-11-21 hang)");
	fresh_case("configured\n", "1\n", true, false, 0, "disconnect connect",
	           "usb: the computer did not answer over the cable: a fresh link",
	           "a computer that dropped the handheld, still configured (2026-10-08)");
	CHECK(log_has("usb: soft_connect disconnect\nusb: soft_connect connect\nwire: the cable connection made fresh\n"),
	      "the fresh link's steps not said in order");
	fresh_case("not attached\n", "0\n", true, false, 1, "disconnect connect",
	           "usb: the link is not whole (not attached, carrier off): a fresh link", "a link not whole");
	fresh_case("configured\n", "0\n", true, false, 1, "disconnect connect", NULL, "configured, no carrier");
	fresh_case("default\n", "0\n", true, true, 0, "",
	           "usb: the link was bound a moment ago: no fresh link", "a link bound a moment ago");
	/* The Brick: "configured" whatever happened, so the carrier counts */
	fresh_case("configured\n", "0\n", false, false, 1, "disconnect connect", NULL, "the Brick, no carrier");
	fresh_case("configured\n", "1\n", false, false, 1, "", NULL, "the Brick, the computer answering");

#ifndef USBHANG_BEFORE
	/* The controller hangs in the fresh link's write: the caller goes on at
	 * once, the late write is said, the next open asks nothing of it */
	{
		char wrote[256];
		unsigned t0;

		char udc[160], net[160], st[200];

		snprintf(udc, sizeof udc, "%s/udc", g_dir);
		snprintf(net, sizeof net, "%s/net", g_dir);
		snprintf(st, sizeof st, "%s/state", udc);
		put_file(st, "not attached\n");
		g_paused = true;
		reader_poke();
		nap(20);
		t0 = ms_now();
		cable_fresh(&(cable_fresh_cfg){ udc, "usb0", "10.42.0.2", true, false, net });
		CHECK(ms_now() - t0 < 100, "a hung fresh link held its caller %u ms", ms_now() - t0);
		nap(700);
		hwput_reap();
		CHECK(log_has("usb: the controller did not answer (soft_connect"), "the hung fresh link not said");
		cable_fresh(&(cable_fresh_cfg){ udc, "usb0", "10.42.0.2", true, false, net });
		CHECK(log_has("usb: no fresh link: the controller is still in a write"),
		      "a second fresh link while the first hangs not left out");
		g_paused = false;
		wait_idle(2000);
		nap(80);
		got(wrote, sizeof wrote);
		CHECK(!strcmp(wrote, "disconnect connect"), "the hung fresh link, at last, wrote \"%s\"", wrote);
		CHECK(!hwput_busy() && no_children(), "the hung fresh link's child not reaped");
	}
#endif
	g_reader_on = false;
	reader_poke();
	pthread_join(t, NULL);
}

/* ---- the watchdog ------------------------------------------------------ */

static volatile int g_light = -1, g_lights_seen;

static void fake_backlight(bool on)
{
	g_light = on;
	g_lights_seen++;
}

static void check_wdog(void)
{
	wdog_cfg c = { 300, 900, 20, 50, NULL, NULL, 600 };
	char sysrq[160];
	wdog_seen s;
	unsigned t0;

	printf("  wdog: the screen out for a stuck loop, a restart for one that stays stuck\n");
	/* The rule */
	CHECK(wdog_judge(1000, 1000, false, &c) == WDOG_FINE, "fed now: not fine");
	CHECK(wdog_judge(1299, 1000, false, &c) == WDOG_FINE, "under the dark time: not fine");
	CHECK(wdog_judge(1300, 1000, false, &c) == WDOG_DARK, "at the dark time: not dark");
	CHECK(wdog_judge(1900, 1000, false, &c) == WDOG_REBOOT, "at the restart time: no restart");
	CHECK(wdog_judge(999999, 1000, true, &c) == WDOG_FINE, "a held task timed");
	CHECK(wdog_judge(100, 0xffffff00u, false, &c) == WDOG_DARK, "across the clock's wrap (356 ms)");
	CHECK(WDOG_DARK_MS == 60000u && WDOG_REBOOT_MS == 120000u, "the device's times are 60 s and 120 s");

	snprintf(sysrq, sizeof sysrq, "%s/sysrq-trigger", g_dir);
	put_file(sysrq, "");
	c.sysrq = sysrq;
	CHECK(wdog_start(&c, fake_backlight), "the watchdog did not start");
	/* A loop that comes round */
	for (t0 = ms_now(); ms_now() - t0 < 700;) {
		wdog_feed();
		nap(16);
	}
	s = wdog_done();
	CHECK(s.darks == 0 && g_lights_seen == 0, "a loop that feeds put out (%u)", s.darks);
	/* A long task held */
	wdog_hold("Bluetooth");
	nap(1100);
	wdog_release();
	s = wdog_done();
	CHECK(s.darks == 0 && s.reboots == 0, "a held task put out or restarted");
	/* The test hook: SIGUSR2 stops the loop for 600 ms, past the dark time */
	fprintf(stderr, "xfer: the last line before the stop\n");
	raise(SIGUSR2);
	t0 = ms_now();
	wdog_feed();                        /* stops here */
	CHECK(ms_now() - t0 >= 590, "the test hook did not stop the loop (%u ms)", ms_now() - t0);
	for (t0 = ms_now(); ms_now() - t0 < 300 && wdog_done().lights == 0;) {
		wdog_feed();
		nap(10);
	}
	s = wdog_done();
	CHECK(s.darks == 1, "a loop stuck past the dark time not put out (%u)", s.darks);
	CHECK(s.lights == 1 && g_light == 1, "the loop back, the screen not on again (%u, %d)", s.lights,
	      g_light);
	CHECK(s.reboots == 0 && slurp(sysrq)[0] == '\0', "restarted for a stop under the restart time");
	CHECK(log_has("watchdog: the loop stopped on purpose (SIGUSR2)"), "the test hook not said");
	CHECK(kmsg_has("zyperos: watchdog: the launcher's loop has not come round for 0 s") ||
	      kmsg_has("zyperos: watchdog: the launcher's loop has not come round"),
	      "the stuck loop not in the kernel's log");
	CHECK(kmsg_has("the last kept one \"xfer: the last line before the stop\""),
	      "the stuck loop's report has not the last kept line");
	CHECK(log_has("watchdog: the loop came round again after"), "the loop's return not said");
	/* A loop that never comes round: put out, synced, restarted */
	nap(1300);
	s = wdog_done();
	CHECK(s.darks == 2 && g_light == 0, "a loop that stays stuck not put out (%u)", s.darks);
	CHECK(s.reboots == 1, "a loop stuck past the restart time not restarted");
	CHECK(!strcmp(slurp(sysrq), "sb"), "the restart's sysrq: \"%s\", not \"sb\"", slurp(sysrq));
	CHECK(kmsg_has("watchdog: the loop has not come round for 0 s: an emergency sync, and a restart") ||
	      kmsg_has(": an emergency sync, and a restart"), "the restart not in the kernel's log");
	wdog_stop();
}

int main(void)
{
	int fd;

	setvbuf(stdout, NULL, _IOLBF, 0);
	snprintf(g_dir, sizeof g_dir, "/tmp/usbhang-check-XXXXXX");
	if (!mkdtemp(g_dir)) return 2;
	snprintf(g_log, sizeof g_log, "%s/zyperos.log", g_dir);
	snprintf(g_kmsg, sizeof g_kmsg, "%s/kmsg", g_dir);
	put_file(g_kmsg, "");
	/* stderr is zyperos.log, as launch.sh makes it */
	if ((fd = open(g_log, O_WRONLY | O_CREAT | O_APPEND, 0600)) < 0) return 2;
	dup2(fd, 2);
	close(fd);
	printf("usbhang-check: %s\n", g_dir);
#ifndef USBHANG_BEFORE
	if (!klog_open(g_kmsg)) {
		printf("  FAIL: no filter in front of stderr on this system\n");
		return 1;
	}
	check_klog();
#endif
	check_hwput();
#ifndef USBHANG_BEFORE
	klog_rate_reset();
#endif
	check_fresh();
#ifndef USBHANG_BEFORE
	klog_rate_reset();
	check_wdog();
	check_klog_rate();
#else
	(void)check_wdog;
	(void)check_klog_rate;
	(void)check_klog;
#endif
	if (fails) {
		printf("usbhang-check: %d failed (the log: %s)\n", fails, g_log);
		return 1;
	}
	printf("usbhang-check: ok\n");
	return 0;
}
