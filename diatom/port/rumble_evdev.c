/* See rumble_evdev.h.
 *
 * The GKD Pixel 2's motor is on PWM0, owned by the joypad driver
 * (ROCKNIX's rocknix-singleadc-joypad, given `pwms` in plastron's device
 * tree), which makes the joypad itself an FF_RUMBLE device - the device SDL
 * rumbles too, so a port, a standalone emulator and Diatom all reach the
 * motor the same way. The driver runs the motor from the strong magnitude
 * when there is one, the weak one otherwise, so the stronger of the two goes
 * in as strong.
 *
 * The driver is a "memoryless" one: the kernel's ff-memless
 * (drivers/input/ff-memless.c) keeps the effect and tells the driver a
 * strength each time what plays changes. Two things about it shape this
 * file.
 *
 * Every upload is played again at once. An upload over a playing effect
 * takes it off FF_EFFECT_PLAYING until ff-memless's timer replays it at the
 * next jiffy (3.3 ms at the Pixel 2's HZ=300). A stop in that gap only
 * clears FF_EFFECT_STARTED, and the driver is never told 0: the motor keeps
 * the old strength, through the menu, through quitting (the kernel's own
 * flush stops it the same way), until the next rumble that stops properly
 * or the power goes. ParallelN64 and Mupen64Plus-Next stop a Rumble Pak in
 * two calls, weak to 0 and then strong to 0, so every N64 stop landed in
 * that gap: the first call re-uploads (strong is still full), the second
 * stops. Reported on ZyperOS 1.0.4, 2026-11-12. Playing again after the
 * upload puts the effect back on FF_EFFECT_PLAYING there and then, so the
 * stop that follows aborts it, and an abort is what tells the driver 0. SDL
 * does the same: it writes a play after every upload.
 *
 * And the device is written from a thread of its own. A core calls
 * set_rumble_state from inside retro_run, on the game's thread (or from its
 * own, as flycast2021 does), and an ioctl and a write on an input device
 * wait on the kernel's locks for it - the device's, its force feedback's -
 * which a close or an open of the same device elsewhere also takes. The
 * game's thread only says what it wants, under a lock nobody holds across a
 * system call; this thread makes it so, the latest wish first: a rumble
 * changed twice before the thread woke is said once, as its last state. */
#include "rumble_evdev.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "diatom_port.h"

#define BITS_PER_LONG   (8 * (int)sizeof(long))
#define NLONGS(max)     (((max) + BITS_PER_LONG) / BITS_PER_LONG)
#define BIT_IS_SET(a,b) (((a)[(b) / BITS_PER_LONG] >> ((b) % BITS_PER_LONG)) & 1UL)

/* How long a close waits for the thread to stop the motor and go. The
 * stop is a system call or two; a device that keeps the thread longer than
 * this is not going to stop anything, and Diatom does not hang at its exit
 * on it: the thread is left to the process's end, whose close of the
 * descriptor is the kernel's last word to the motor. */
#define CLOSE_WAIT_MS   500

static pthread_mutex_t g_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_cv = PTHREAD_COND_INITIALIZER;     /* to the thread */
static pthread_cond_t  g_gone_cv = PTHREAD_COND_INITIALIZER;
static pthread_t       g_thread;
static bool            g_have_thread;   /* created, and not yet joined or let go */
static bool            g_stuck;         /* let go at a close it did not answer */
static bool            g_running;       /* the thread takes wishes */
static bool            g_quit;
static bool            g_gone;          /* the thread has stopped the motor and left */
static uint16_t        g_want_strong, g_want_weak;  /* what the game asks for */
static uint16_t        g_said_strong, g_said_weak;  /* what the device was told */

/* The thread's own: nothing else touches these while it runs. */
static int  g_fd = -1;
static int  g_id = -1;          /* the uploaded effect, -1 before the first */
static bool g_playing;

static bool play(int on)
{
	struct input_event ev;

	memset(&ev, 0, sizeof ev);
	ev.type  = EV_FF;
	ev.code  = (uint16_t)g_id;
	ev.value = on;
	return write(g_fd, &ev, sizeof ev) == (ssize_t)sizeof ev;
}

/* Tell the device: the thread's half of evdev_rumble_set. */
static bool apply(uint16_t strong, uint16_t weak)
{
	struct ff_effect e;

	if (!strong) {
		/* Stopped, not deleted: the next rumble updates the same effect.
		 * The effect is on FF_EFFECT_PLAYING here (the play below), so
		 * this aborts it, and the driver is told 0. */
		if (g_playing && !play(0)) return false;
		g_playing = false;
		return true;
	}
	memset(&e, 0, sizeof e);
	e.type = FF_RUMBLE;
	e.id   = (int16_t)g_id;
	e.u.rumble.strong_magnitude = strong;
	e.u.rumble.weak_magnitude   = weak;
	/* replay.length 0 runs until stopped: the core says when, not a timer
	 * here. Uploading over a playing effect changes it in place. */
	if (ioctl(g_fd, EVIOCSFF, &e) < 0) return false;
	g_id = e.id;
	/* Every time, playing or not: see the top of the file. */
	if (!play(1)) return false;
	g_playing = true;
	return true;
}

static void *rumble_main(void *arg)
{
	int err = 0;

	(void)arg;
	pthread_mutex_lock(&g_mx);
	for (;;) {
		uint16_t strong, weak;

		while (!g_quit && g_want_strong == g_said_strong && g_want_weak == g_said_weak)
			pthread_cond_wait(&g_cv, &g_mx);
		if (g_want_strong == g_said_strong && g_want_weak == g_said_weak)
			break;                          /* told to go, and nothing left to say */
		strong = g_want_strong;
		weak   = g_want_weak;
		pthread_mutex_unlock(&g_mx);
		if (!apply(strong, weak)) {
			err = errno ? errno : EIO;      /* a short write sets none */
			pthread_mutex_lock(&g_mx);
			break;
		}
		pthread_mutex_lock(&g_mx);
		g_said_strong = strong;
		g_said_weak   = weak;
	}
	g_running = false;                      /* evdev_rumble_set stops queueing */
	pthread_mutex_unlock(&g_mx);

	if (err) {
		char msg[96];

		/* Said once, and the motor given up on: a device that refuses one
		 * write refuses the next sixty a second too. Closing stops whatever
		 * was uploaded. */
		snprintf(msg, sizeof msg, "rumble: %s; rumble off", strerror(err));
		diatom_port_log(DIATOM_LOG_WARN, msg);
	}

	/* The motor stopped (the last wish at a close is 0), and the file closed,
	 * which drops the effect. */
	if (g_fd >= 0) {
		if (g_playing) play(0);
		close(g_fd);
	}
	g_fd = -1;
	g_id = -1;
	g_playing = false;

	pthread_mutex_lock(&g_mx);
	g_gone = true;
	pthread_cond_broadcast(&g_gone_cv);
	pthread_mutex_unlock(&g_mx);
	return NULL;
}

bool evdev_rumble_open(int fd, const char *path)
{
	unsigned long ff[NLONGS(FF_MAX)];
	bool busy;
	int w;

	memset(ff, 0, sizeof ff);
	if (fd < 0 || !path || ioctl(fd, EVIOCGBIT(EV_FF, sizeof ff), ff) < 0 ||
	    !BIT_IS_SET(ff, FF_RUMBLE))
		return false;
	/* One motor, opened once: a thread let go at a close may still hold the
	 * last descriptor, and the thread's state is this file's. */
	pthread_mutex_lock(&g_mx);
	busy = g_have_thread || g_stuck;
	pthread_mutex_unlock(&g_mx);
	if (busy) return false;
	if ((w = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC)) < 0) return false;
	g_fd = w;
	g_id = -1;
	g_playing = false;
	pthread_mutex_lock(&g_mx);
	g_quit = false;
	g_gone = false;
	g_want_strong = g_want_weak = 0;
	g_said_strong = g_said_weak = 0;
	g_have_thread = g_running = pthread_create(&g_thread, NULL, rumble_main, NULL) == 0;
	pthread_mutex_unlock(&g_mx);
	if (!g_have_thread) {
		close(w);
		g_fd = -1;
		return false;
	}
	return true;
}

bool evdev_rumble_set(uint16_t strong, uint16_t weak)
{
	bool there;

	if (weak > strong) strong = weak;
	pthread_mutex_lock(&g_mx);
	there = g_running && !g_quit;
	if (there && (strong != g_want_strong || weak != g_want_weak)) {
		g_want_strong = strong;
		g_want_weak   = weak;
		pthread_cond_signal(&g_cv);
	}
	pthread_mutex_unlock(&g_mx);
	return there;
}

void evdev_rumble_close(void)
{
	struct timespec until;
	bool gone;

	pthread_mutex_lock(&g_mx);
	if (!g_have_thread) { pthread_mutex_unlock(&g_mx); return; }
	/* The motor stops at every close: the last wish is 0, said before the
	 * thread goes (unless it already went, on a refused write). */
	g_want_strong = g_want_weak = 0;
	g_quit = true;
	pthread_cond_signal(&g_cv);
	clock_gettime(CLOCK_REALTIME, &until);
	until.tv_nsec += (long)CLOSE_WAIT_MS * 1000000L;
	until.tv_sec  += until.tv_nsec / 1000000000L;
	until.tv_nsec %= 1000000000L;
	while (!g_gone)
		if (pthread_cond_timedwait(&g_gone_cv, &g_mx, &until) == ETIMEDOUT) break;
	gone = g_gone;
	g_have_thread = false;
	g_stuck = !gone;
	pthread_mutex_unlock(&g_mx);
	if (gone) {
		pthread_join(g_thread, NULL);
	} else {
		diatom_port_log(DIATOM_LOG_WARN, "rumble: the device did not answer a stop; "
		                "left to the exit");
		pthread_detach(g_thread);
	}
}
