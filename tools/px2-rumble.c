/* SPDX-License-Identifier: GPL-3.0-only */
/* px2-rumble: run the GKD Pixel 2's rumble motor from a shell, and read back
 * what the motor's PWM is doing.
 *
 *   px2-rumble [STRENGTH [MS]]   play a rumble, 0..65535 (default 65535), for
 *                                MS milliseconds (default 400)
 *   px2-rumble stress [MS]       change the strength every 2 ms for MS
 *                                (default 20000), as a game easing its
 *                                rumble every frame does, only faster
 *   px2-rumble gap               the stop ZyperOS 1.0.4's Diatom gave an N64
 *                                Rumble Pak (full on; the effect uploaded
 *                                again, weak 0, and stopped at once), the
 *                                PWM's state after it, then the stop the fix
 *                                makes (upload, play, stop) and the state
 *   px2-rumble pwm               print the motor PWM's state, once
 *   px2-rumble watch [MS]        print it every 20 ms for MS (default 3000)
 *
 * The motor is on PWM0 (GPIO0_B7), and the joypad driver owns it: the device
 * tree gives the joypad node `pwms` and `pwm-names = "enable"`, which makes
 * the joypad itself an FF_RUMBLE device (ROCKNIX's rocknix-singleadc-joypad).
 * So this does what a game's rumble does through Diatom or SDL: it finds the
 * device that can rumble, uploads an FF_RUMBLE effect and plays it.
 *
 * `pwm` and `watch` read the kernel's PWM debugfs (mounted here if it is not
 * yet), whose "actual configuration" line is read back from the controller:
 * enabled or not, duty / period is the strength, and the polarity. Not
 * /dev/mem: the controller's bus clock is off between changes, and a read
 * then returns the same word at every register. Nobody can feel the motor
 * over SSH; this is the next best thing.
 *
 * Build: the plastron toolchain, no libraries. Run as root on the device. */
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <time.h>
#include <unistd.h>

#define BITS_PER_LONG   (sizeof(long) * 8)
#define NLONGS(n)       (((n) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#define HAS(bits, b)    ((bits[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1)

#define PWM_DEBUGFS "/sys/kernel/debug/pwm"

static void nap_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
}

static int open_rumble(char *name, size_t cap)
{
	char path[32];
	int i;

	for (i = 0; i < 32; i++) {
		unsigned long ff[NLONGS(FF_MAX + 1)];
		int fd;

		snprintf(path, sizeof path, "/dev/input/event%d", i);
		if ((fd = open(path, O_RDWR | O_CLOEXEC)) < 0) continue;
		memset(ff, 0, sizeof ff);
		if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) >= 0 && HAS(ff, FF_RUMBLE)) {
			if (ioctl(fd, EVIOCGNAME(cap), name) < 0) snprintf(name, cap, "?");
			fprintf(stderr, "px2-rumble: %s (%s)\n", path, name);
			return fd;
		}
		close(fd);
	}
	return -1;
}

/* The "actual configuration" line of the PWM whose user is "enable": the
 * joypad's name for its motor PWM (pwm-names in the device tree). */
static int pwm_print(void)
{
	char line[256];
	int found = 0, mine = 0;
	FILE *f = fopen(PWM_DEBUGFS, "r");

	if (!f) {
		mount("none", "/sys/kernel/debug", "debugfs", 0, NULL);
		f = fopen(PWM_DEBUGFS, "r");
	}
	if (!f) { perror(PWM_DEBUGFS); return 1; }
	while (fgets(line, sizeof line, f)) {
		if (strstr(line, " pwm-")) mine = strstr(line, "(enable") != NULL;
		if (mine && strstr(line, "actual configuration:")) {
			fputs(strchr(line, ':') + 1, stdout);
			found = 1;
		}
	}
	fclose(f);
	if (!found) printf(" no PWM named \"enable\" (no motor in this device tree)\n");
	return !found;
}

int main(int argc, char **argv)
{
	struct ff_effect e;
	struct input_event play;
	char name[64];
	long strength = 65535, ms = 400;
	int fd;

	if (argc > 1 && (!strcmp(argv[1], "pwm") || !strcmp(argv[1], "watch"))) {
		long t, total = argc > 2 ? atol(argv[2]) : 3000;

		if (!strcmp(argv[1], "pwm")) return pwm_print();
		for (t = 0; t <= total; t += 20) {
			printf("%5ld ms ", t);
			if (pwm_print()) return 1;
			fflush(stdout);
			nap_ms(20);
		}
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "gap")) {
		/* ff-memless takes an upload over a playing effect off PLAYING
		 * until its timer's next jiffy; a stop in that gap never tells the
		 * driver 0 (diatom/port/rumble_evdev.c says more). Expect the PWM
		 * still running after the first stop, and off after the second. */
		if ((fd = open_rumble(name, sizeof name)) < 0) return 1;
		memset(&e, 0, sizeof e);
		e.type = FF_RUMBLE;
		e.id = -1;
		e.u.rumble.strong_magnitude = e.u.rumble.weak_magnitude = 0xffff;
		memset(&play, 0, sizeof play);
		play.type = EV_FF;
		if (ioctl(fd, EVIOCSFF, &e) < 0) { perror("EVIOCSFF"); return 1; }
		play.code = (uint16_t)e.id;
		play.value = 1;
		if (write(fd, &play, sizeof play) != (ssize_t)sizeof play) { perror("play"); return 1; }
		nap_ms(300);
		printf("on:                       ");
		pwm_print();
		e.u.rumble.weak_magnitude = 0;          /* the Rumble Pak's off, first call */
		if (ioctl(fd, EVIOCSFF, &e) < 0) { perror("EVIOCSFF"); return 1; }
		play.value = 0;                         /* and its second */
		(void)!write(fd, &play, sizeof play);
		nap_ms(200);
		printf("stopped as 1.0.4 stopped: ");
		pwm_print();
		if (ioctl(fd, EVIOCSFF, &e) < 0) { perror("EVIOCSFF"); return 1; }
		play.value = 1;                         /* the fix: a play after the upload */
		(void)!write(fd, &play, sizeof play);
		play.value = 0;
		(void)!write(fd, &play, sizeof play);
		nap_ms(50);
		printf("stopped as the fix stops: ");
		pwm_print();
		close(fd);
		return 0;
	}
	if (argc > 1 && !strcmp(argv[1], "stress")) {
		long t, total = argc > 2 ? atol(argv[2]) : 20000;

		if ((fd = open_rumble(name, sizeof name)) < 0) return 1;
		memset(&e, 0, sizeof e);
		e.type = FF_RUMBLE;
		e.id = -1;
		memset(&play, 0, sizeof play);
		play.type = EV_FF;
		for (t = 0; t < total; t += 2) {
			/* a sawtooth, with a stop and a start every 100 ms */
			e.u.rumble.strong_magnitude = (uint16_t)((t % 100) * 655);
			if (ioctl(fd, EVIOCSFF, &e) < 0) { perror("EVIOCSFF"); return 1; }
			play.code = (uint16_t)e.id;
			play.value = e.u.rumble.strong_magnitude != 0;
			if (write(fd, &play, sizeof play) != (ssize_t)sizeof play) { perror("play"); return 1; }
			nap_ms(2);
		}
		play.value = 0;
		(void)!write(fd, &play, sizeof play);
		printf("px2-rumble: %ld changes in %ld ms\n", total / 2, total);
		return 0;
	}
	if (argc > 1) strength = atol(argv[1]);
	if (argc > 2) ms = atol(argv[2]);
	if (strength < 0 || strength > 65535 || ms < 0) {
		fprintf(stderr, "usage: px2-rumble [0..65535 [ms]] | gap | stress [ms] | pwm | watch [ms]\n");
		return 2;
	}
	if ((fd = open_rumble(name, sizeof name)) < 0) {
		fprintf(stderr, "px2-rumble: no input device can rumble (no FF_RUMBLE)\n");
		return 1;
	}

	memset(&e, 0, sizeof e);
	e.type = FF_RUMBLE;
	e.id = -1;
	e.u.rumble.strong_magnitude = (uint16_t)strength;
	e.replay.length = (uint16_t)(ms > 65535 ? 65535 : ms);
	if (ioctl(fd, EVIOCSFF, &e) < 0) { perror("EVIOCSFF"); return 1; }

	memset(&play, 0, sizeof play);
	play.type = EV_FF;
	play.code = (uint16_t)e.id;
	play.value = 1;
	if (write(fd, &play, sizeof play) != (ssize_t)sizeof play) { perror("play"); return 1; }
	nap_ms(ms + 50);
	play.value = 0;
	(void)!write(fd, &play, sizeof play);
	ioctl(fd, EVIOCRMFF, e.id);
	close(fd);
	return 0;
}
