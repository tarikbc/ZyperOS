/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * hwdog: pets the PX30's hardware watchdog, so a handheld that stops is
 * reset rather than left frozen with its backlight on.
 *
 * The chain, from power on (linux-px2.config, boot-prelude.cmd): U-Boot
 * starts the watchdog; the kernel pets it from its driver's probe until
 * something opens /dev/watchdog, for watchdog.open_timeout=60 s at most;
 * this opens it at init's first step and pets it every PET_S from then on,
 * with a WATCHDOG_S timeout. A kernel that stops anywhere, before init or
 * after, stops the petting, and the chip resets itself; pstore keeps the
 * kernel's console across that warm reset, and crashlog reports it at the
 * next start.
 *
 * What it must never do is reset a handheld that is only busy. The false
 * reset plastron PR #115 feared was a petter late on a slow card under a
 * long copy: so this touches no file after it starts (its every page locked
 * in RAM, mlockall, so not even a page of its own code is read back from the
 * card), runs at a real-time priority (SCHED_FIFO) above anything the copy
 * or a game runs, and has 50 s to spare at each pet. And never the
 * launcher's main loop: a launcher that stops is the launcher's own
 * watchdog's (ZyperOS's src/wdog.h), which restarts it cleanly first.
 *
 *   hwdog -b   from init's first sysinit line: in the background at once
 *   hwdog      from a respawn line: waits for the device while another
 *              hwdog has it (EBUSY), and takes over if that one goes
 *
 * A shutdown's TERM stops the watchdog (the magic close: no NOWAYOUT), as a
 * shutdown waits on the card for as long as it takes. No /dev/watchdog (an
 * older kernel, QEMU): it says so once and does nothing more, and its
 * respawn copy waits for good rather than respawning.
 *
 * A test of the chain, zytest on the kernel's line (U-Boot's boot.scr takes
 * it from /zytest.txt on the boot partition, once, and empties the file):
 *   zytest=hang    init held at this step for good, the device never
 *                  opened: the kernel stops petting at open_timeout, and
 *                  the chip resets about 60 s into the start
 *   zytest=stall   armed and never petted: a reset about 60 s after
 */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <fcntl.h>
#include <linux/watchdog.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#define DEVICE     "/dev/watchdog"
#define KMSG       "/dev/kmsg"
#define WATCHDOG_S 60
#define PET_S      10
#define WAIT_S     5         /* between two tries while another hwdog has it */
#define RT_PRIO    10

static volatile sig_atomic_t g_stop;
/* ZyperOS's tools/hwdog-check.sh: a file for the device and one for the
 * kernel's log, from the environment, in a build for it only */
static const char *g_device = DEVICE, *g_kmsg = KMSG;

static void on_term(int sig)
{
	(void)sig;
	g_stop = 1;
}

/* One line in the kernel's log, which pstore keeps across a reset */
static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	char line[256];
	va_list ap;
	int n, fd;

	n = snprintf(line, sizeof line, "<13>hwdog: ");
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof line - (size_t)n - 1, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof line - 2) n = (int)sizeof line - 2;
	line[n++] = '\n';
	if ((fd = open(g_kmsg, O_WRONLY | O_APPEND | O_CLOEXEC)) >= 0) {
		if (write(fd, line, (size_t)n) < 0) { /* nowhere else to say it */ }
		close(fd);
	}
}

static void nap(unsigned s)
{
	struct timespec t = { (time_t)s, 0 };

	while (!g_stop && nanosleep(&t, &t) < 0 && errno == EINTR) { }
}

static void forever(void)
{
	for (;;) pause();
}

int main(int argc, char **argv)
{
	const char *test = getenv("zytest");
	bool background = argc > 1 && !strcmp(argv[1], "-b");
	struct sigaction sa;
	struct sched_param sp = { .sched_priority = RT_PRIO };
	int fd, timeout = WATCHDOG_S;
	bool waited = false;

#ifdef HWDOG_CHECK
	if (getenv("HWDOG_DEVICE")) g_device = getenv("HWDOG_DEVICE");
	if (getenv("HWDOG_KMSG")) g_kmsg = getenv("HWDOG_KMSG");
#endif

	if (background && test && !strncmp(test, "hang", 4)) {
		say("zytest=hang: init held here, %s never opened: the kernel pets it until "
		    "watchdog.open_timeout, then the chip resets (a test)", g_device);
		forever();
	}
	if (background) {
		pid_t pid = fork();

		if (pid < 0) return 1;
		if (pid > 0) return 0;               /* init goes on at once */
		setsid();
	}
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_term;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	signal(SIGHUP, SIG_IGN);
	/* Nothing of this read from the card again, and first in line for the CPU */
	if (mlockall(MCL_CURRENT | MCL_FUTURE)) say("mlockall failed (%s): going on", strerror(errno));
	if (sched_setscheduler(0, SCHED_FIFO, &sp)) say("no real-time priority (%s): going on", strerror(errno));

	for (;;) {
		if ((fd = open(g_device, O_WRONLY | O_CLOEXEC)) >= 0) break;
		if (errno == EBUSY) {                /* the other hwdog has it */
			waited = true;
			nap(WAIT_S);
			if (g_stop) return 0;
			continue;
		}
		if (background) {
			say("no %s (%s): nothing to pet", g_device, strerror(errno));
			return 0;
		}
		forever();                           /* respawned: wait for good, quietly */
	}
	if (ioctl(fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
		say("the timeout not set (%s)", strerror(errno));
		timeout = 0;
		ioctl(fd, WDIOC_GETTIMEOUT, &timeout);
	}
	say("%s armed%s: %d s, petted every %d s", g_device, waited ? ", taken over" : "", timeout, PET_S);
	if (test && !strncmp(test, "stall", 5)) {
		say("zytest=stall: never petted from here: the chip resets in %d s (a test)", timeout);
		forever();
	}
	while (!g_stop) {
		ioctl(fd, WDIOC_KEEPALIVE, 0);
		nap(PET_S);
	}
	/* A shutdown: stopped, so its wait on the card is never cut short */
	if (write(fd, "V", 1) != 1) say("the magic close failed (%s)", strerror(errno));
	close(fd);
	say("stopped at the shutdown");
	return 0;
}
