/* The GKD Pixel 2's rumble (port/rumble_evdev.c) against a model of the
 * kernel under it: that every stop a game asks for reaches the motor, that
 * a close stops it, and that a game's call never waits on the device.
 *
 * The bug this holds: on ZyperOS 1.0.4 an N64 Rumble Pak game's first
 * rumble never stopped - not when the game stopped it, not under MENU, not
 * after quitting; only a power-off ended it (a player's report, 2026-11-12).
 * The motor's driver is a memoryless one, and the kernel's ff-memless
 * (drivers/input/ff-memless.c) takes an upload over a playing effect off
 * FF_EFFECT_PLAYING until its timer replays it at the next jiffy; a stop in
 * that gap only clears FF_EFFECT_STARTED, and the driver never hears 0.
 * ParallelN64 stops in two calls, weak to 0 and then strong to 0: the first
 * re-uploaded, the second stopped, microseconds apart, in the gap every time.
 *
 * The model below is that behaviour, in this file's words, for the one
 * effect rumble_evdev.c uses: STARTED, PLAYING and ABORTING as ml_ff_upload,
 * ml_ff_playback and ml_get_combo_effect set them, a timer that fires only
 * when the test says a jiffy went by (k_tick), and the level the driver
 * (rocknix-singleadc-joypad's rumble_play_effect) was last told. A jiffy is
 * 3.3 ms on the Pixel 2 (HZ=300), and a core's two calls take microseconds,
 * so no tick between calls is the device's ordinary case, not a rare one.
 *
 * rumble_evdev.c itself is built unchanged, with -include test/rumble_fake.h
 * sending its open, ioctl, write and close here and test/fakeinc giving it a
 * <linux/input.h> where there is none (macOS). Offline and instant. */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/input.h>        /* test/fakeinc's */

#include "diatom_port.h"
#include "rumble_evdev.h"

#define FAKE_PATH "/dev/input/fake-rumble"
#define READ_FD   700           /* what the port holds, asked what the device can do */
#define WRITE_FD  701           /* what rumble_evdev.c opens to rumble */

/* ---- the kernel ---------------------------------------------------------- */

static pthread_mutex_t k_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  k_cv = PTHREAD_COND_INITIALIZER;
static bool          k_gated;           /* calls on the device wait while set */
static bool          k_refuse;          /* uploads and plays fail, EIO, while set */
static unsigned long k_calls;           /* calls on the write descriptor, all */
static bool          k_is_open;
static bool          k_uploaded;        /* the one effect exists */
static uint16_t      k_strong, k_weak;  /* and its magnitudes */
static bool          k_started, k_playing, k_aborting;
static bool          k_timer;           /* armed: fires at the next jiffy */
static uint16_t      k_motor;           /* the driver's level, as last told */

static void k_reset(void)
{
	pthread_mutex_lock(&k_mx);
	k_gated = k_refuse = false;
	k_calls = 0;
	k_is_open = k_uploaded = false;
	k_strong = k_weak = 0;
	k_started = k_playing = k_aborting = k_timer = false;
	k_motor = 0;
	pthread_mutex_unlock(&k_mx);
}

/* ml_play_effects, for one rumble effect with no delay, length or envelope:
 * a started effect is due at once; aborting it tells the driver an empty
 * combination, 0; otherwise it plays, and the driver is told its strength
 * (the strong magnitude when there is one, as rumble_play_effect reads it).
 * Nothing started, nothing told. And the timer: ml_schedule_timer finds no
 * event once every started effect plays, so it is deleted. */
static void k_play_effects(void)
{
	if (k_started) {
		if (k_aborting) {
			k_aborting = k_playing = k_started = false;
			k_motor = 0;
		} else {
			k_playing = true;
			k_motor = k_strong ? k_strong : k_weak;
		}
	}
	k_timer = false;
}

/* ml_ff_upload: over a started effect, PLAYING goes and the timer is armed
 * for now, which is the next jiffy; nothing is told until then. */
static void k_upload(uint16_t strong, uint16_t weak)
{
	k_uploaded = true;
	k_strong = strong;
	k_weak = weak;
	if (k_started) {
		k_playing = false;
		k_timer = true;
	}
}

/* ml_ff_playback */
static void k_playback(int value)
{
	if (value > 0) k_started = true;
	else if (k_playing) k_aborting = true;
	else k_started = false;
	k_play_effects();
}

/* A jiffy went by: ml_effect_timer. */
static void k_tick(void)
{
	pthread_mutex_lock(&k_mx);
	if (k_timer) k_play_effects();
	pthread_mutex_unlock(&k_mx);
}

static uint16_t k_level(void)
{
	uint16_t m;

	pthread_mutex_lock(&k_mx);
	m = k_motor;
	pthread_mutex_unlock(&k_mx);
	return m;
}

static void k_gate(bool on)
{
	pthread_mutex_lock(&k_mx);
	k_gated = on;
	pthread_cond_broadcast(&k_cv);
	pthread_mutex_unlock(&k_mx);
}

/* Calls on the device wait at the gate, as on the kernel's locks. */
static void k_enter(void)
{
	pthread_mutex_lock(&k_mx);
	while (k_gated) pthread_cond_wait(&k_cv, &k_mx);
	k_calls++;
}

int fake_open(const char *path, int flags, ...)
{
	(void)flags;
	if (strcmp(path, FAKE_PATH)) { errno = ENOENT; return -1; }
	pthread_mutex_lock(&k_mx);
	k_is_open = true;
	pthread_mutex_unlock(&k_mx);
	return WRITE_FD;
}

int fake_ioctl(int fd, unsigned long req, ...)
{
	va_list ap;
	void *arg;

	va_start(ap, req);
	arg = va_arg(ap, void *);
	va_end(ap);
	if ((req & 0xffffu) == FAKE_EVIOCGBIT_BASE + EV_FF) {
		/* What the device can do, from any descriptor on it */
		unsigned long *bits = arg;
		size_t n = (req >> 16) & 0xffffu;

		memset(bits, 0, n);
		bits[FF_RUMBLE / (8 * sizeof(long))] |= 1UL << (FF_RUMBLE % (8 * sizeof(long)));
		return 0;
	}
	if (fd != WRITE_FD || req != EVIOCSFF) { errno = EINVAL; return -1; }
	{
		struct ff_effect *e = arg;

		k_enter();
		if (k_refuse) { pthread_mutex_unlock(&k_mx); errno = EIO; return -1; }
		if (!k_is_open || e->type != FF_RUMBLE || (e->id != -1 && e->id != 0)) {
			pthread_mutex_unlock(&k_mx);
			errno = EINVAL;
			return -1;
		}
		e->id = 0;
		k_upload(e->u.rumble.strong_magnitude, e->u.rumble.weak_magnitude);
		pthread_mutex_unlock(&k_mx);
	}
	return 0;
}

ssize_t fake_write(int fd, const void *buf, size_t n)
{
	const struct input_event *ev = buf;

	if (fd != WRITE_FD || n != sizeof *ev) { errno = EINVAL; return -1; }
	k_enter();
	if (k_refuse) { pthread_mutex_unlock(&k_mx); errno = EIO; return -1; }
	if (!k_is_open || ev->type != EV_FF || ev->code != 0 || !k_uploaded) {
		pthread_mutex_unlock(&k_mx);
		errno = EINVAL;
		return -1;
	}
	k_playback(ev->value);
	pthread_mutex_unlock(&k_mx);
	return (ssize_t)n;
}

/* The last close of the file: input_ff_flush erases what it uploaded, and
 * erasing is a playback of 0 first (ff-core's erase_effect). */
int fake_close(int fd)
{
	if (fd != WRITE_FD) { errno = EBADF; return -1; }
	k_enter();
	if (k_uploaded) k_playback(0);
	k_uploaded = false;
	k_is_open = false;
	pthread_mutex_unlock(&k_mx);
	return 0;
}

/* ---- the port's log, which rumble_evdev.c writes to ----------------------- */

static int g_warns;

void diatom_port_log(diatom_log_level lvl, const char *msg)
{
	if (lvl >= DIATOM_LOG_WARN) g_warns++;
	fprintf(stderr, "    (log) %s\n", msg);
}

/* ---- the test ------------------------------------------------------------ */

static int g_fails;

static void check(const char *name, bool ok, long got)
{
	printf("  %s %s", ok ? "ok  " : "FAIL", name);
	if (!ok) printf(" (got %ld)", got);
	printf("\n");
	if (!ok) g_fails++;
}

static double now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void nap_ms(long ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	nanosleep(&ts, NULL);
}

/* What src/env.c does with a core's call: the motor it names set, and the
 * port told both, the whole state each time. */
static uint16_t g_core[2];      /* [RETRO_RUMBLE_STRONG = 0], [WEAK = 1] */

enum { STRONG = 0, WEAK = 1 };

static void core_says(int effect, uint16_t v)
{
	g_core[effect] = v;
	evdev_rumble_set(g_core[STRONG], g_core[WEAK]);
}

/* Has the device been told what the game asks for now? Stopped, or the
 * effect uploaded at that strength and playing. Asked of the model, not of
 * rumble_evdev.c, which may tell it from a thread of its own. */
static bool told(void)
{
	uint16_t w = g_core[WEAK], s = g_core[STRONG] > w ? g_core[STRONG] : w;
	bool ok;

	pthread_mutex_lock(&k_mx);
	ok = s ? (k_started && k_playing && k_strong == s && k_weak == w) : !k_started;
	pthread_mutex_unlock(&k_mx);
	return ok;
}

/* Until the device has been told (or has had no call for 100 ms: a port that
 * tells it something else never gets there), with no jiffy in between, so the
 * timer never helps. */
static void settle(void)
{
	double end = now_ms() + 2000, quiet_since = now_ms();
	unsigned long seen;

	pthread_mutex_lock(&k_mx);
	seen = k_calls;
	pthread_mutex_unlock(&k_mx);
	while (!told() && now_ms() < end && now_ms() - quiet_since < 100) {
		unsigned long c;

		nap_ms(1);
		pthread_mutex_lock(&k_mx);
		c = k_calls;
		pthread_mutex_unlock(&k_mx);
		if (c != seen) { seen = c; quiet_since = now_ms(); }
	}
}


/* ParallelN64's Rumble Pak (mupen64plus-core's libretro input plugin, the
 * write to 0xC000): weak, then strong, back to back. */
static void n64_pak(bool on, bool settle_between)
{
	core_says(WEAK, on ? 0xffff : 0);
	if (settle_between) settle();
	core_says(STRONG, on ? 0xffff : 0);
}

/* pcsx_rearmed's DualShock (plat_trigger_vibrate): the big motor's byte as
 * strong, then the small motor's on or off as weak. */
static void ps1_pad(unsigned big, bool small, bool settle_between)
{
	core_says(STRONG, (uint16_t)(big << 8));
	if (settle_between) settle();
	core_says(WEAK, small ? 0xffff : 0);
}

static bool open_motor(void)
{
	k_reset();
	g_core[0] = g_core[1] = 0;
	return evdev_rumble_open(READ_FD, FAKE_PATH);
}

int main(void)
{
	int i;

	/* A call that waits on the device for good is a hang, not a wrong
	 * answer: the alarm's signal is the failure then. */
	alarm(30);
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("rumble (port/rumble_evdev.c on a model of ff-memless):\n");

	/* N64, the reported game's way: on, then off, two calls each. The
	 * reupload the off's first call makes, and its stop, in one jiffy. */
	check("opens a device that can rumble", open_motor(), 0);
	n64_pak(true, false);
	settle();
	check("N64 Rumble Pak on: the motor runs at full", k_level() == 0xffff, k_level());
	n64_pak(false, true);
	settle();
	check("N64 Rumble Pak off (weak 0, then strong 0, in one jiffy): the motor stops",
	      k_level() == 0, k_level());
	k_tick();
	check("and stays stopped a jiffy later", k_level() == 0, k_level());
	n64_pak(true, false);
	settle();
	n64_pak(false, false);
	settle();
	check("off again, the two calls with no pause: stopped", k_level() == 0, k_level());

	/* The Rumble Pak's own way to rumble softer: on and off every frame,
	 * frames faster than jiffies (a frame burst after a slow one). */
	for (i = 0; i < 50; i++) {
		n64_pak(true, i % 3 == 0);
		if (i % 2) settle();
		n64_pak(false, i % 5 == 0);
		if (i % 4) settle();
	}
	settle();
	check("50 pulses of the Rumble Pak, no jiffy between any: stopped at the end",
	      k_level() == 0, k_level());

	/* PS1: the big motor eased down, then off, two frames in one jiffy */
	ps1_pad(0x96, false, false);
	settle();
	check("PS1 DualShock big motor 0x96: the motor runs", k_level() == 0x9600, k_level());
	ps1_pad(0x40, false, true);
	settle();
	ps1_pad(0x00, false, true);
	settle();
	check("PS1 big motor 0x96, 0x40, off, in one jiffy: the motor stops", k_level() == 0, k_level());
	ps1_pad(0x60, true, true);
	settle();
	ps1_pad(0x60, false, true);
	settle();
	ps1_pad(0x00, false, true);
	settle();
	check("PS1 both motors, the small one off, then the big: stopped", k_level() == 0, k_level());

	/* Whatever the order, the motor follows the game: a thousand random
	 * calls of both kinds, jiffies at random, and after each settle the
	 * motor at the game's strength (the stronger of the two). */
	{
		unsigned seed = 2026;
		bool follows = true;
		long bad = 0;

		for (i = 0; i < 1000; i++) {
			seed = seed * 1103515245u + 12345u;
			if ((seed >> 16) % 2) n64_pak((seed >> 8) % 2, (seed >> 4) % 2);
			else ps1_pad((seed >> 8) % 256, (seed >> 12) % 2, (seed >> 4) % 2);
			if ((seed >> 20) % 3 == 0) k_tick();
			if ((seed >> 24) % 4 == 0) {
				uint16_t want = g_core[WEAK] > g_core[STRONG] ? g_core[WEAK] : g_core[STRONG];

				settle();
				if (k_level() != want) { follows = false; bad = k_level(); }
			}
		}
		check("1000 random calls: after each pause the motor is at the game's strength",
		      follows, bad);
		core_says(STRONG, 0);
		core_says(WEAK, 0);
		settle();
		check("and a stop at the end stops it", k_level() == 0, k_level());
	}

	/* The quit: the port's shutdown closes while the game had it going,
	 * just after a change (the upload's gap again). */
	n64_pak(true, false);
	settle();
	core_says(WEAK, 0);          /* half an off: a reupload, strong still full */
	settle();
	evdev_rumble_close();
	check("a close just after a reupload stops the motor", k_level() == 0, k_level());
	check("and lets the device go", !k_is_open, k_is_open);
	check("a set after the close says there is no motor", !evdev_rumble_set(0xffff, 0), 1);

	/* A device that refuses a write: said once, the motor given up on, and
	 * the close (the kernel's flush) still stops what it had going. */
	check("opens again", open_motor(), 0);
	n64_pak(true, false);
	settle();
	{
		int warns = g_warns;
		double end = now_ms() + 1000;

		pthread_mutex_lock(&k_mx);
		k_refuse = true;
		pthread_mutex_unlock(&k_mx);
		ps1_pad(0x40, false, false);
		while (evdev_rumble_set(0x4000, 0) && now_ms() < end) nap_ms(1);
		check("a refused write: the port says there is no motor any more",
		      !evdev_rumble_set(0x4000, 0), 1);
		evdev_rumble_close();           /* joins the thread, which has gone */
		check("and says so once", g_warns == warns + 1, g_warns - warns);
		check("and the device let go stops the motor", k_level() == 0 && !k_is_open, k_level());
	}

	/* A game's call never waits on the device: with the device stuck in a
	 * call (the gate), a hundred calls return at once, and the motor gets
	 * the last of them once the device answers. */
	check("opens again", open_motor(), 0);
	n64_pak(true, false);
	settle();
	k_gate(true);
	{
		double t0 = now_ms(), took;

		n64_pak(false, false);
		for (i = 0; i < 100; i++) ps1_pad((unsigned)(i * 7) % 256, i % 2, false);
		ps1_pad(0x80, false, false);
		took = now_ms() - t0;
		check("204 calls on a stuck device return at once (under 50 ms)",
		      took < 50, (long)took);
	}
	k_gate(false);
	settle();
	check("the device answers again: the motor has the last call, 0x80",
	      k_level() == 0x8000, k_level());
	ps1_pad(0, false, false);
	settle();
	check("and stops with the game", k_level() == 0, k_level());

	/* And a close never hangs on a stuck device (a close that joins a stuck
	 * thread is a hang): it gives up after its wait and says so. */
	n64_pak(true, false);
	settle();
	k_gate(true);
	{
		double t0 = now_ms(), took;
		int warns = g_warns;

		evdev_rumble_close();
		took = now_ms() - t0;
		check("a close on a stuck device returns (under 2 s)", took < 2000, (long)took);
		check("and says the device did not answer", g_warns == warns + 1, g_warns - warns);
	}
	k_gate(false);
	nap_ms(50);
	check("the stop it asked for reaches the motor once the device answers",
	      k_level() == 0, k_level());
	check("a second open after that is refused: the old thread had the device",
	      !evdev_rumble_open(READ_FD, FAKE_PATH), 1);

	printf(g_fails ? "rumble: %d FAILED\n" : "rumble: all passed\n", g_fails);
	return g_fails ? 1 : 0;
}
