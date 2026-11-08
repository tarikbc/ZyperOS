/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-input: press the GKD Pixel 2's buttons from a shell.
 *
 *   px2-input BUTTON[+BUTTON...][:MS] ...   e.g.  px2-input down down a
 *                                                 px2-input start:600 wait:2000 a
 *                                                 px2-input select+start:300
 *
 * Buttons: up down left right a b x y l1 r1 l2 r2 select start menu, and the
 * volume keys volup voldown, which go to their own device ("gpio-keys") as
 * they come from it, so ZyperOS and Diatom read them as theirs, and power,
 * to the PMIC's ("rk805 pwrkey"): ZyperOS turns the handheld off for it on
 * every screen but the tutorial's power step, which is what it is for. Each is
 * held MS milliseconds (80 by default) and let go, with 150 ms between
 * presses; wait:MS only waits. Buttons joined by + go down together and come
 * up together, for the chords a port or a standalone emulator quits on
 * (SELECT+START, for many). For driving the launcher, a port or a game over
 * SSH with nobody at the device (px2-shot shows what it did).
 *
 * An event written to an evdev node goes to everything reading that device,
 * as if the joypad had sent it: ZyperOS, SDL in a port, gptokeyb. a, b, x and y
 * are the evdev codes ZyperOS reads as accept, back, X and Y (BTN_SOUTH, EAST,
 * NORTH, WEST); the joypad is found by its BTN_SOUTH, as ZyperOS finds it.
 *
 * Build: the plastron toolchain, no libraries. Run as root on the device. */
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NLONGS(n)       (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define HAS(bits, b)    ((bits[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)

static const struct { const char *name; int code; } buttons[] = {
	{ "up", BTN_DPAD_UP },     { "down", BTN_DPAD_DOWN },
	{ "left", BTN_DPAD_LEFT }, { "right", BTN_DPAD_RIGHT },
	{ "a", BTN_SOUTH },        { "b", BTN_EAST },
	{ "x", BTN_NORTH },        { "y", BTN_WEST },
	{ "l1", BTN_TL },          { "r1", BTN_TR },
	{ "l2", BTN_TL2 },         { "r2", BTN_TR2 },
	{ "select", BTN_SELECT },  { "start", BTN_START },
	{ "menu", BTN_TRIGGER_HAPPY1 },
	{ "volup", KEY_VOLUMEUP }, { "voldown", KEY_VOLUMEDOWN },
	{ "power", KEY_POWER },
};

static void sleep_ms(int ms)
{
	struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&t, NULL);
}

static void send(int fd, int code, int value)
{
	struct input_event ev[2];

	memset(ev, 0, sizeof ev);
	ev[0].type = EV_KEY;
	ev[0].code = (unsigned short)code;
	ev[0].value = value;
	ev[1].type = EV_SYN;
	ev[1].code = SYN_REPORT;
	if (write(fd, ev, sizeof ev) != (ssize_t)sizeof ev)
		perror("px2-input: write");
}

/* The device that has `code`: the joypad by its BTN_SOUTH, the volume keys
 * by their KEY_VOLUMEUP */
static int open_with(int code)
{
	for (int i = 0; i < 32; i++) {
		char path[32];
		unsigned long keys[NLONGS(KEY_MAX)] = { 0 };
		int fd;

		snprintf(path, sizeof path, "/dev/input/event%d", i);
		if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0)
			continue;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && HAS(keys, code))
			return fd;
		close(fd);
	}
	return -1;
}

int main(int argc, char **argv)
{
	int fd, keys = -1, power = -1;

	if (argc < 2) {
		fprintf(stderr, "usage: px2-input BUTTON[:MS] ... (wait:MS to pause)\n");
		return 2;
	}
	if ((fd = open_with(BTN_SOUTH)) < 0) {
		fprintf(stderr, "px2-input: no joypad\n");
		return 1;
	}
	for (int i = 1; i < argc; i++) {
		const char *colon = strchr(argv[i], ':');
		int ms = colon ? atoi(colon + 1) : 80, codes[8], n = 0;
		size_t len = colon ? (size_t)(colon - argv[i]) : strlen(argv[i]);

		if (len == 4 && !strncmp(argv[i], "wait", 4)) {
			sleep_ms(ms);
			continue;
		}
		for (const char *p = argv[i]; p < argv[i] + len && n < 8; ) {
			const char *end = memchr(p, '+', (size_t)(argv[i] + len - p));
			size_t k = end ? (size_t)(end - p) : (size_t)(argv[i] + len - p);
			char name[32];
			int code = -1;

			snprintf(name, sizeof name, "%.*s", (int)k, p);
			for (size_t b = 0; b < sizeof buttons / sizeof buttons[0]; b++)
				if (!strcmp(name, buttons[b].name))
					code = buttons[b].code;
			if (code < 0) {
				fprintf(stderr, "px2-input: no button \"%s\"\n", name);
				return 2;
			}
			if (code == KEY_POWER && power < 0 && (power = open_with(KEY_POWER)) < 0) {
				fprintf(stderr, "px2-input: no power key\n");
				return 1;
			}
			if (code < BTN_MISC && code != KEY_POWER && keys < 0 &&
			    (keys = open_with(KEY_VOLUMEUP)) < 0) {
				fprintf(stderr, "px2-input: no volume keys\n");
				return 1;
			}
			codes[n++] = code;
			p += k + (end ? 1 : 0);
		}
		for (int b = 0; b < n; b++)
			send(codes[b] == KEY_POWER ? power : codes[b] < BTN_MISC ? keys : fd, codes[b], 1);
		sleep_ms(ms);
		for (int b = 0; b < n; b++)
			send(codes[b] == KEY_POWER ? power : codes[b] < BTN_MISC ? keys : fd, codes[b], 0);
		sleep_ms(150);
	}
	close(fd);
	if (keys >= 0)
		close(keys);
	if (power >= 0)
		close(power);
	return 0;
}
