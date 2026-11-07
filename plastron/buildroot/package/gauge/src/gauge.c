/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * gauge: the four lights on the side show the battery, and what ZyperOS asks.
 *
 * Bottom to top they are battery0 and battery1, the bottom light's red and
 * green, then battery2 to battery4, three green ones (the device tree says
 * which pin is which). What they show is lights.c's, pure and checked on the
 * host (check.c); this file reads, sleeps and writes the pins. In short:
 *
 *   - Plugged in: a bar filling from the bottom over the lights held for each
 *     full quarter, still when full and at the charge limit.
 *   - On the battery, under 10%: the bottom light flashes red, once every
 *     five seconds; under 5%, twice every two.
 *   - From ZyperOS, one line at a time on the FIFO /run/gauge: the volume or
 *     brightness while it changes, a climb when files or covers finish, a
 *     sparkle for an achievement. My picks in ZyperOS's side lights lab
 *     (2026-11-06; its DESIGN.md 11.27 has them all).
 *   - Asked (SIGUSR1, which ZyperOS sends on L2+R2 outside a game): the level
 *     flashes four times in two seconds, over everything.
 *   - Otherwise nothing, so they cost nothing.
 *
 * Which parts show is ZyperOS's MENU > Side Lights, words in MODE on the card
 * (lights.h, lights_parse). ZyperOS writes them and sends SIGHUP, which reads
 * them again; they are read at start too, since the card is mounted before
 * init starts us. Being asked always shows: it is a press made on purpose,
 * and I chose it that way (2026-11-05).
 *
 * It sleeps on the kernel's uevents (a charger plugged in or out, the battery
 * reporting), the FIFO and its signals, and wakes on a timer only while
 * something moves: every step of the bar or a pattern, each flash, and every
 * half minute otherwise to look at the level. Started by init, with respawn.
 *
 * For trying each state without waiting on the battery, /tmp/gauge-test
 * holding "PERCENT PLUGGED [HELD]" ("60 1", "5 0", "85 1 1") stands in for
 * the battery while it is there; /tmp is empty again at every boot. SIGHUP
 * looks at once.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "lights.h"

#define BATTERY "/sys/class/power_supply/battery/capacity"
#define STATUS  "/sys/class/power_supply/battery/status"
#define CHARGER "/sys/class/power_supply/rk817-charger/online"
#define TEST    "/tmp/gauge-test"
#define MODE    "/mnt/SDCARD/.userdata/pixel2/side-lights"
#define FIFO    "/run/gauge"

#define READ_MS 1000   /* while patterns step, the battery at most this often */

static int led_fd[5] = { -1, -1, -1, -1, -1 };
static int shown = -1;   /* what the lights show now, as bits */

static long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000L + t.tv_nsec / 1000000;
}

static void log_to_kernel(const char *text)
{
	int fd = open("/dev/kmsg", O_WRONLY | O_CLOEXEC);

	if (fd >= 0) {
		if (write(fd, text, strlen(text)) < 0) { /* nowhere to say so */ }
		close(fd);
	}
}

/* A small file's text, or "" */
static void read_text(const char *path, char *buf, size_t cap)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got = fd >= 0 ? read(fd, buf, cap - 1) : -1;

	if (fd >= 0)
		close(fd);
	buf[got > 0 ? got : 0] = '\0';
}

static int read_int(const char *path, int fallback)
{
	char buf[16];

	read_text(path, buf, sizeof buf);
	return buf[0] ? atoi(buf) : fallback;
}

/* The battery: its level, the cable, and whether the charger holds it at
 * the charge limit ("Not charging" with the cable in), or the test file's */
static void read_battery(int *pct, int *plugged, int *held)
{
	char buf[32];
	int p, c, h = 0;

	*pct = read_int(BATTERY, 100);
	*plugged = read_int(CHARGER, 0) == 1;
	read_text(STATUS, buf, sizeof buf);
	*held = *plugged && !strncmp(buf, "Not charging", 12);
	read_text(TEST, buf, sizeof buf);
	if (sscanf(buf, "%d %d %d", &p, &c, &h) >= 2) {
		*pct = p;
		*plugged = c != 0;
		*held = h != 0;
	}
}

static void read_mode(struct lights_cfg *c)
{
	char buf[256];

	read_text(MODE, buf, sizeof buf);
	lights_parse(c, buf);
}

/* Written only when it changes: a write is a GPIO toggle, and most wakes
 * change nothing */
static void show(int bits)
{
	if (bits == shown)
		return;
	for (int i = 0; i < 5; i++)
		if (led_fd[i] >= 0) {
			const char *v = bits & (1 << i) ? "1" : "0";

			if (pwrite(led_fd[i], v, 1, 0) < 0) { /* the light stays as it was */ }
		}
	shown = bits;
}

/* The launcher's FIFO, opened for reading and writing both, so it never
 * reads as closed between writers and poll never spins on its hang-up. A
 * writer opens it O_NONBLOCK and gets ENXIO while no gauge runs. */
static int open_fifo(void)
{
	struct stat st;

	if (lstat(FIFO, &st) == 0 && !S_ISFIFO(st.st_mode))
		unlink(FIFO);
	if (mkfifo(FIFO, 0600) < 0 && errno != EEXIST)
		return -1;
	return open(FIFO, O_RDWR | O_NONBLOCK | O_CLOEXEC);
}

/* Whatever lines are waiting. A line cut by a full buffer is dropped: these
 * are glances, and the next change sends another. */
static void read_lines(int fd, struct lights_state *s, const struct lights_cfg *c)
{
	static char part[128];
	static size_t have;
	char buf[512];
	ssize_t got;

	while ((got = read(fd, buf, sizeof buf)) > 0)
		for (ssize_t i = 0; i < got; i++) {
			if (buf[i] == '\n') {
				part[have < sizeof part ? have : sizeof part - 1] = '\0';
				if (have < sizeof part)
					lights_line(s, c, part, now_ms());
				have = 0;
			} else if (have < sizeof part) {
				part[have++] = buf[i];
			} else {
				have = sizeof part;   /* too long: dropped at its newline */
			}
		}
}

int main(void)
{
	struct sockaddr_nl nl = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	struct pollfd pf[3];
	struct lights_cfg cfg;
	struct lights_state st;
	sigset_t sigs;
	long read_at = -READ_MS;

	for (int i = 0; i < 5; i++) {
		char path[64];

		snprintf(path, sizeof path, "/sys/class/leds/battery%d/brightness", i);
		led_fd[i] = open(path, O_WRONLY | O_CLOEXEC);
	}
	if (led_fd[0] < 0 && led_fd[4] < 0) {
		/* No lights to drive: wait here rather than let init respawn us
		 * over and over */
		log_to_kernel("gauge: no battery lights in this device tree\n");
		for (;;)
			pause();
	}

	sigemptyset(&sigs);
	sigaddset(&sigs, SIGUSR1);
	sigaddset(&sigs, SIGTERM);
	sigaddset(&sigs, SIGHUP);
	sigprocmask(SIG_BLOCK, &sigs, NULL);
	pf[0].fd = signalfd(-1, &sigs, SFD_CLOEXEC | SFD_NONBLOCK);
	pf[0].events = POLLIN;
	pf[1].fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
	                  NETLINK_KOBJECT_UEVENT);
	if (pf[1].fd >= 0 && bind(pf[1].fd, (struct sockaddr *)&nl, sizeof nl) < 0) {
		close(pf[1].fd);
		pf[1].fd = -1;
	}
	pf[1].events = POLLIN;
	pf[2].fd = open_fifo();
	pf[2].events = POLLIN;
	if (pf[2].fd < 0)
		log_to_kernel("gauge: no " FIFO ", so only the battery shows\n");
	lights_init(&st);
	read_mode(&cfg);
	log_to_kernel("gauge: showing the battery on the side lights\n");

	for (;;) {
		long now = now_ms();
		int bits, wait;

		/* The battery, at most once a READ_MS while patterns step: each
		 * read is three files */
		if (now - read_at >= READ_MS) {
			int pct, plugged, held;

			read_battery(&pct, &plugged, &held);
			lights_battery(&st, pct, plugged, held, now);
			read_at = now;
		}
		bits = lights_frame(&cfg, &st, now, &wait);
		show(bits);

		if (poll(pf, 3, wait) <= 0) {
			if (wait >= IDLE_MS)
				read_at = -READ_MS;   /* a quiet half minute: look again */
			continue;
		}
		if (pf[0].revents & POLLIN) {
			struct signalfd_siginfo si;

			while (read(pf[0].fd, &si, sizeof si) == (ssize_t)sizeof si) {
				if (si.ssi_signo == SIGTERM) {
					show(0);
					return 0;
				}
				if (si.ssi_signo == SIGUSR1) {
					lights_ask(&st, now_ms());
					read_at = -READ_MS;   /* the level as it is now */
				}
				if (si.ssi_signo == SIGHUP) {
					read_mode(&cfg);
					read_at = -READ_MS;
				}
			}
		}
		if (pf[1].revents & POLLIN) {
			char buf[2048];

			/* Any uevent is a reason to look again; reading them is cheaper
			 * than sorting them */
			while (recv(pf[1].fd, buf, sizeof buf, 0) > 0) { }
			read_at = -READ_MS;
		}
		if (pf[2].revents & POLLIN)
			read_lines(pf[2].fd, &st, &cfg);
	}
}
