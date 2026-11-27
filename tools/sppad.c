/* SPDX-License-Identifier: GPL-3.0-only */
/* sppad: the TrimUI Smart Pro's buttons and sticks as one gamepad.
 *
 * Every control of the Smart Pro is on one of two small controllers, one in
 * each half of the body, which send what they read on serial ports
 * (tools/sppad.h). Nothing in the kernel reads them, so this does: it powers
 * both halves (GPIO 110 and 114, driven high), reads both ports, and sends
 * one gamepad through uinput, "Smart Pro Gamepad", with the GKD Pixel 2's key
 * codes as the Brick's brick-pad sends them, so the launcher and Diatom find
 * and read it as they do the Brick's (by its BTN_SOUTH), and the two sticks
 * as ABS_X/ABS_Y and ABS_RX/ABS_RY.
 *
 * A half that sends nothing (a ribbon out, a half not powered) leaves the
 * other working; the log says which half went silent, and when it speaks
 * again. Started by the Smart Pro's init before the launcher; runs until
 * killed. Linux only.
 *
 *   sppad [-v]      -v: each frame's buttons and raw sticks to stderr
 *
 * Without -v the log still has the sticks' readings: each half's centre and
 * the furthest raw reading seen each way, when they have moved on, at most
 * every STICK_LOG_MS, so a tester's logs say how the sticks read (the kit's
 * notes promise it) and the log stops growing once the ends are found.
 */
#include "sppad.h"

#ifndef __linux__
#include <stdio.h>
int main(void)
{
	fprintf(stderr, "sppad: the Smart Pro's reader runs on Linux only\n");
	return 1;
}
#else

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define QUIET_MS 2000   /* a half with no frame this long is said to be silent */
#define STICK_LOG_MS 5000   /* the most often a half's stick readings are logged */

static const struct {
	const char *port;
	int         power_gpio;
	const char *name;
} halves[SP_HALVES] = {
	[SP_LEFT]  = { "/dev/ttyS4", 110, "left" },
	[SP_RIGHT] = { "/dev/ttyS3", 114, "right" },
};

static volatile sig_atomic_t stop;

static void on_signal(int sig)
{
	(void)sig;
	stop = 1;
}

static unsigned long now_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned long)t.tv_sec * 1000 + (unsigned long)t.tv_nsec / 1000000;
}

static int write_text(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC), ok;

	if (fd < 0) return -1;
	ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text) ? 0 : -1;
	close(fd);
	return ok;
}

/* A half's power on: its GPIO an output, high. Exported through sysfs (the
 * Brick's 4.9 kernel has no character-device GPIO tools); one exported
 * already is used as it is. */
static void power_on(int gpio)
{
	char path[64], num[16];

	snprintf(num, sizeof num, "%d", gpio);
	snprintf(path, sizeof path, "/sys/class/gpio/gpio%d/direction", gpio);
	if (access(path, F_OK) != 0 && write_text("/sys/class/gpio/export", num) != 0)
		fprintf(stderr, "sppad: GPIO %d: no export (%s)\n", gpio, strerror(errno));
	if (write_text(path, "high") != 0)
		fprintf(stderr, "sppad: GPIO %d: not driven high (%s)\n", gpio, strerror(errno));
}

/* A half's port, raw, 19200 baud, 8N1 */
static int open_port(const char *port)
{
	struct termios t;
	int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0) return -1;
	if (tcgetattr(fd, &t) != 0) {
		close(fd);
		return -1;
	}
	cfmakeraw(&t);
	cfsetispeed(&t, B19200);
	cfsetospeed(&t, B19200);
	t.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
	t.c_cflag |= CS8 | CLOCAL | CREAD;
	t.c_cc[VMIN] = 0;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSANOW, &t) != 0) {
		close(fd);
		return -1;
	}
	tcflush(fd, TCIFLUSH);
	return fd;
}

static void abs_axis(struct uinput_user_dev *d, int code)
{
	d->absmin[code] = -SP_AXIS_MAX;
	d->absmax[code] = SP_AXIS_MAX;
}

/* The gamepad: every button's key and the four stick axes */
static int open_pad(void)
{
	struct uinput_user_dev d;
	int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC), i;

	if (fd < 0) return -1;
	ioctl(fd, UI_SET_EVBIT, EV_KEY);
	ioctl(fd, UI_SET_EVBIT, EV_ABS);
	for (i = 0; i < sp_n_buttons; i++) ioctl(fd, UI_SET_KEYBIT, sp_buttons[i].code);
	ioctl(fd, UI_SET_ABSBIT, ABS_X);
	ioctl(fd, UI_SET_ABSBIT, ABS_Y);
	ioctl(fd, UI_SET_ABSBIT, ABS_RX);
	ioctl(fd, UI_SET_ABSBIT, ABS_RY);
	memset(&d, 0, sizeof d);
	snprintf(d.name, sizeof d.name, "Smart Pro Gamepad");
	d.id.bustype = BUS_VIRTUAL;
	abs_axis(&d, ABS_X);
	abs_axis(&d, ABS_Y);
	abs_axis(&d, ABS_RX);
	abs_axis(&d, ABS_RY);
	if (write(fd, &d, sizeof d) != (ssize_t)sizeof d || ioctl(fd, UI_DEV_CREATE) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void emit(int fd, int type, int code, int value)
{
	struct input_event e;

	memset(&e, 0, sizeof e);
	e.type = (unsigned short)type;
	e.code = (unsigned short)code;
	e.value = value;
	if (write(fd, &e, sizeof e) < 0) { /* the next frame says it again */ }
}

int main(int argc, char **argv)
{
	static const int stick_x[SP_HALVES] = { ABS_X, ABS_RX }, stick_y[SP_HALVES] = { ABS_Y, ABS_RY };
	sp_reader     reader[SP_HALVES];
	sp_axis       ax[SP_HALVES], ay[SP_HALVES];
	uint8_t       was[SP_HALVES] = { 0, 0 };
	int           port[SP_HALVES], last_x[SP_HALVES] = { 0, 0 }, last_y[SP_HALVES] = { 0, 0 };
	unsigned long last_frame[SP_HALVES];
	bool          silent[SP_HALVES] = { false, false };
	sp_axis       logged_x[SP_HALVES], logged_y[SP_HALVES];
	unsigned long logged_at[SP_HALVES] = { 0, 0 };
	bool          verbose = argc > 1 && !strcmp(argv[1], "-v");
	struct sigaction sa;
	int pad, h;

	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_signal;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);

	for (h = 0; h < SP_HALVES; h++) {
		power_on(halves[h].power_gpio);
		sp_reader_init(&reader[h]);
		sp_axis_init(&ax[h]);
		sp_axis_init(&ay[h]);
		logged_x[h] = ax[h];
		logged_y[h] = ay[h];
		last_frame[h] = now_ms();
	}
	/* The halves start as they are powered; a moment for them */
	usleep(100 * 1000);
	for (h = 0; h < SP_HALVES; h++) {
		port[h] = open_port(halves[h].port);
		if (port[h] < 0)
			fprintf(stderr, "sppad: the %s half's port %s: %s\n", halves[h].name, halves[h].port,
			        strerror(errno));
	}
	if (port[SP_LEFT] < 0 && port[SP_RIGHT] < 0) return 1;
	pad = open_pad();
	if (pad < 0) {
		fprintf(stderr, "sppad: no uinput gamepad: %s\n", strerror(errno));
		return 1;
	}
	fprintf(stderr, "sppad: Smart Pro Gamepad up, the left half on %s, the right on %s\n",
	        halves[SP_LEFT].port, halves[SP_RIGHT].port);

	while (!stop) {
		struct pollfd pf[SP_HALVES];
		int np = 0, which[SP_HALVES], i;
		unsigned long t;

		for (h = 0; h < SP_HALVES; h++)
			if (port[h] >= 0) {
				pf[np].fd = port[h];
				pf[np].events = POLLIN;
				which[np++] = h;
			}
		if (poll(pf, (nfds_t)np, 500) < 0 && errno != EINTR) break;
		for (i = 0; i < np; i++) {
			uint8_t buf[64];
			sp_frame fr[16];
			ssize_t got;
			int n, k;

			h = which[i];
			if (!(pf[i].revents & POLLIN)) continue;
			got = read(port[h], buf, sizeof buf);
			if (got <= 0) continue;
			n = sp_feed(&reader[h], buf, (size_t)got, fr, 16);
			for (k = 0; k < n; k++) {
				int codes[8], values[8], m = sp_key_changes((sp_half)h, was[h], fr[k].buttons,
				                                            codes, values, 8), j, x, y;

				for (j = 0; j < m; j++) emit(pad, EV_KEY, codes[j], values[j]);
				was[h] = fr[k].buttons;
				x = sp_axis_value(&ax[h], fr[k].x);
				y = sp_axis_value(&ay[h], fr[k].y);
				if (x != last_x[h]) emit(pad, EV_ABS, stick_x[h], last_x[h] = x);
				if (y != last_y[h]) emit(pad, EV_ABS, stick_y[h], last_y[h] = y);
				emit(pad, EV_SYN, SYN_REPORT, 0);
				if (verbose)
					fprintf(stderr, "sppad: %s 0x%02x x %u y %u\n", halves[h].name,
					        fr[k].buttons, fr[k].x, fr[k].y);
			}
			if (n > 0) {
				last_frame[h] = now_ms();
				if (silent[h]) {
					silent[h] = false;
					fprintf(stderr, "sppad: the %s half speaks again\n", halves[h].name);
				}
			}
		}
		t = now_ms();
		for (h = 0; h < SP_HALVES; h++) {
			const sp_axis *x = &ax[h], *y = &ay[h];

			if (!x->learned || t - logged_at[h] < STICK_LOG_MS) continue;
			if (x->lo == logged_x[h].lo && x->hi == logged_x[h].hi && x->centre == logged_x[h].centre &&
			    y->lo == logged_y[h].lo && y->hi == logged_y[h].hi && y->centre == logged_y[h].centre)
				continue;
			fprintf(stderr, "sppad: the %s stick, raw: x %d to %d round %d, y %d to %d round %d\n",
			        halves[h].name, x->lo, x->hi, x->centre, y->lo, y->hi, y->centre);
			logged_x[h] = *x;
			logged_y[h] = *y;
			logged_at[h] = t;
		}
		for (h = 0; h < SP_HALVES; h++)
			if (port[h] >= 0 && !silent[h] && sp_silent(last_frame[h], t, QUIET_MS)) {
				silent[h] = true;
				fprintf(stderr, "sppad: the %s half (%s) sent nothing for %d s (%lu frames, %lu bytes"
				        " passed over so far)\n", halves[h].name, halves[h].port, QUIET_MS / 1000,
				        reader[h].frames, reader[h].skipped);
			}
	}
	/* Every key up before the gamepad goes, so nothing stays held */
	for (h = 0; h < SP_HALVES; h++) {
		int codes[8], values[8], m = sp_key_changes((sp_half)h, was[h], 0, codes, values, 8), j;

		for (j = 0; j < m; j++) emit(pad, EV_KEY, codes[j], values[j]);
	}
	emit(pad, EV_SYN, SYN_REPORT, 0);
	ioctl(pad, UI_DEV_DESTROY);
	close(pad);
	for (h = 0; h < SP_HALVES; h++) {
		if (port[h] >= 0) close(port[h]);
		fprintf(stderr, "sppad: the %s half: %lu frames, %lu bytes passed over, %lu lost; its stick"
		        " %d to %d round %d, %d to %d round %d\n", halves[h].name, reader[h].frames,
		        reader[h].skipped, reader[h].lost, ax[h].lo, ax[h].hi, ax[h].centre, ay[h].lo, ay[h].hi,
		        ay[h].centre);
	}
	return 0;
}
#endif
