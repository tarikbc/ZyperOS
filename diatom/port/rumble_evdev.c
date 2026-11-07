/* See rumble_evdev.h.
 *
 * The GKD Pixel 2's motor is on PWM0, owned by the joypad driver
 * (ROCKNIX's rocknix-singleadc-joypad, given `pwms` in plastron's device
 * tree), which makes the joypad itself an FF_RUMBLE device - the device SDL
 * rumbles too, so a port, a standalone emulator and Diatom all reach the
 * motor the same way. The driver runs the motor from the strong magnitude
 * when there is one, the weak one otherwise, so the stronger of the two goes
 * in as strong. */
#include "rumble_evdev.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "diatom_port.h"

#define BITS_PER_LONG   (8 * (int)sizeof(long))
#define NLONGS(max)     (((max) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(a,b) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static int      g_fd = -1;
static int      g_id = -1;          /* the uploaded effect, -1 before the first */
static bool     g_playing;
static uint16_t g_strong, g_weak;   /* what the motor was last told */

bool evdev_rumble_open(int fd, const char *path)
{
	unsigned long ff[NLONGS(FF_MAX)];
	int w;

	memset(ff, 0, sizeof ff);
	if (fd < 0 || !path || ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) < 0 ||
	    !BIT_IS_SET(ff, FF_RUMBLE))
		return false;
	if ((w = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) return false;
	pthread_mutex_lock(&g_mx);
	g_fd = w;
	g_id = -1;
	g_playing = false;
	g_strong = g_weak = 0;
	pthread_mutex_unlock(&g_mx);
	return true;
}

static bool play(int on)
{
	struct input_event ev;

	memset(&ev, 0, sizeof ev);
	ev.type  = EV_FF;
	ev.code  = (uint16_t)g_id;
	ev.value = on;
	return write(g_fd, &ev, sizeof ev) == (ssize_t)sizeof ev;
}

bool evdev_rumble_set(uint16_t strong, uint16_t weak)
{
	bool ok = true;

	pthread_mutex_lock(&g_mx);
	if (g_fd < 0) { pthread_mutex_unlock(&g_mx); return false; }
	if (weak > strong) strong = weak;
	if (strong == g_strong && weak == g_weak) { pthread_mutex_unlock(&g_mx); return true; }

	if (!strong) {
		/* Stopped, not deleted: the next rumble updates the same effect. */
		if (g_playing && !play(0)) ok = false;
		g_playing = false;
	} else {
		struct ff_effect e;

		memset(&e, 0, sizeof e);
		e.type = FF_RUMBLE;
		e.id   = (int16_t)g_id;
		e.u.rumble.strong_magnitude = strong;
		e.u.rumble.weak_magnitude   = weak;
		/* replay.length 0 runs until stopped: the core says when, not a
		 * timer here. Uploading over a playing effect changes it in place. */
		if (ioctl(g_fd, EVIOCSFF, &e) < 0) {
			ok = false;
		} else {
			g_id = e.id;
			if (!g_playing) {
				if (play(1)) g_playing = true;
				else ok = false;
			}
		}
	}
	if (ok) {
		g_strong = strong;
		g_weak   = weak;
	} else {
		char msg[96];

		/* Said once, and the motor given up on: a device that refuses
		 * one write refuses the next sixty a second too. Closing stops
		 * whatever was uploaded. */
		snprintf(msg, sizeof msg, "rumble: %s; rumble off", strerror(errno));
		diatom_port_log(DIATOM_LOG_WARN, msg);
		close(g_fd);
		g_fd = -1;
	}
	pthread_mutex_unlock(&g_mx);
	return ok;
}

void evdev_rumble_close(void)
{
	pthread_mutex_lock(&g_mx);
	if (g_fd >= 0) {
		if (g_playing) play(0);
		close(g_fd);   /* and the kernel drops the effect with the file */
	}
	g_fd = -1;
	g_id = -1;
	g_playing = false;
	g_strong = g_weak = 0;
	pthread_mutex_unlock(&g_mx);
}
