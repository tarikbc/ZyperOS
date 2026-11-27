/* SPDX-License-Identifier: GPL-3.0-only */
/* See wdog.h. */
#include "wdog.h"

#include "klog.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int wdog_judge(unsigned now, unsigned fed, bool held, const wdog_cfg *c)
{
	unsigned age = now - fed;

	if (held) return WDOG_FINE;
	if (age >= c->reboot_ms) return WDOG_REBOOT;
	if (age >= c->dark_ms) return WDOG_DARK;
	return WDOG_FINE;
}

static unsigned mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
}

static void nap_ms(unsigned ms)
{
	struct timespec t = { (time_t)(ms / 1000), (long)(ms % 1000) * 1000000L };

	while (nanosleep(&t, &t) < 0 && errno == EINTR) { }
}

static atomic_uint           g_fed;
static atomic_int            g_held;
static atomic_bool           g_run;
static volatile sig_atomic_t g_stall;
static wdog_cfg              g_cfg;
static pthread_t             g_thread, g_hands_thread;
static bool                  g_started;
static atomic_uint           g_darks, g_lights, g_reboots;

wdog_seen wdog_done(void)
{
	return (wdog_seen){ atomic_load(&g_darks), atomic_load(&g_lights), atomic_load(&g_reboots) };
}

void wdog_feed(void)
{
	if (g_stall) {
		g_stall = 0;
		klog_raw("watchdog: the loop stopped on purpose (SIGUSR2), to show what the watchdog does");
		nap_ms(g_cfg.stall_ms ? g_cfg.stall_ms : WDOG_STALL_S * 1000u);
	}
	atomic_store_explicit(&g_fed, mono_ms(), memory_order_relaxed);
}

void wdog_hold(const char *what)
{
	(void)what;
	atomic_fetch_add(&g_held, 1);
}

void wdog_release(void)
{
	if (atomic_fetch_sub(&g_held, 1) <= 0) atomic_store(&g_held, 0);
	wdog_feed();
}

/* ---- its hands: what may wait (the card, the display) ---------------- */

/* The watchdog thread itself never waits on anything the stuck loop could
 * hold: the kernel's log takes its lines at once, and its restart is the
 * kernel's. The log file's line (the card) and the backlight (a display
 * driver) are this thread's, which can wait on them for as long as it takes
 * while the watchdog goes on to the restart. */
enum { HAND_LINE, HAND_DARK, HAND_LIGHT };
#define HANDS 8
static struct { int kind; char text[512]; } g_hand[HANDS];
static unsigned        g_hand_in, g_hand_out;
static pthread_mutex_t g_hand_mx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  g_hand_cv = PTHREAD_COND_INITIALIZER;

static void hand(int kind, const char *text)
{
	pthread_mutex_lock(&g_hand_mx);
	if (g_hand_in - g_hand_out < HANDS) {
		unsigned k = g_hand_in++ % HANDS;

		g_hand[k].kind = kind;
		snprintf(g_hand[k].text, sizeof g_hand[k].text, "%s", text ? text : "");
		pthread_cond_signal(&g_hand_cv);
	}
	pthread_mutex_unlock(&g_hand_mx);
}

static void *hands_run(void *arg)
{
	(void)arg;
	for (;;) {
		char text[512];
		int kind;

		pthread_mutex_lock(&g_hand_mx);
		while (g_hand_in == g_hand_out) pthread_cond_wait(&g_hand_cv, &g_hand_mx);
		kind = g_hand[g_hand_out % HANDS].kind;
		memcpy(text, g_hand[g_hand_out % HANDS].text, sizeof text);
		g_hand_out++;
		pthread_mutex_unlock(&g_hand_mx);
		if (kind == HAND_LINE) {
			size_t n = strlen(text);

			if (write(2, text, n) < 0 || write(2, "\n", 1) < 0) { /* the kernel's log has it */ }
		} else if (g_cfg.backlight) {
			g_cfg.backlight(kind == HAND_LIGHT);
		}
	}
	return NULL;
}

/* A line now in the kernel's log, and in the log file when the card takes it */
static void say(const char *line)
{
	klog_kmsg(line);
	hand(HAND_LINE, line);
}

static void sysrq(char what)
{
	int fd = open(g_cfg.sysrq, O_WRONLY | O_APPEND | O_CLOEXEC);

	if (fd < 0) return;
	if (write(fd, &what, 1) < 0) { /* nothing more to try */ }
	close(fd);
}

/* ---- the watchdog ---------------------------------------------------- */

static void *wdog_run(void *arg)
{
	bool dark = false;
	unsigned dark_at = 0;

	(void)arg;
	while (atomic_load(&g_run)) {
		unsigned now, fed;
		int v;

		nap_ms(g_cfg.tick_ms);
		if (!atomic_load(&g_run)) break;
		now = mono_ms();
		fed = atomic_load_explicit(&g_fed, memory_order_relaxed);
		v = wdog_judge(now, fed, atomic_load(&g_held) > 0, &g_cfg);
		if (v == WDOG_FINE) {
			if (dark) {
				char line[160];

				snprintf(line, sizeof line, "watchdog: the loop came round again after %u s: the screen on",
				         (now - dark_at + g_cfg.dark_ms) / 1000);
				say(line);
				hand(HAND_LIGHT, NULL);
				atomic_fetch_add(&g_lights, 1);
				dark = false;
			}
			continue;
		}
		if (!dark) {
			char line[1000], last[400], kept[400];

			klog_last(last, sizeof last, kept, sizeof kept);
			snprintf(line, sizeof line,
			         "watchdog: the launcher's loop has not come round for %u s: the screen off, "
			         "a restart at %u s; its last line \"%s\", the last kept one \"%s\"",
			         (now - fed) / 1000, g_cfg.reboot_ms / 1000, last, kept);
			say(line);
			hand(HAND_DARK, NULL);
			atomic_fetch_add(&g_darks, 1);
			dark = true;
			dark_at = now;
		}
		if (v == WDOG_REBOOT) {
			char line[160];

			snprintf(line, sizeof line,
			         "watchdog: the loop has not come round for %u s: an emergency sync, and a restart",
			         (now - fed) / 1000);
			say(line);
			atomic_fetch_add(&g_reboots, 1);
			sysrq('s');
			nap_ms(g_cfg.sync_ms);
			sysrq('b');
			break;              /* a check's restart is a file; a handheld's is the end */
		}
	}
	return NULL;
}

static void on_usr2(int sig)
{
	(void)sig;
	g_stall = 1;
}

bool wdog_start(const wdog_cfg *c, void (*backlight)(bool on))
{
	struct sigaction sa;

	if (g_started) return true;
	if (c) {
		g_cfg = *c;
	} else {
		g_cfg = (wdog_cfg){ WDOG_DARK_MS, WDOG_REBOOT_MS, 1000, 3000, "/proc/sysrq-trigger", NULL, 0 };
	}
	if (backlight) g_cfg.backlight = backlight;
	if (!g_cfg.tick_ms) g_cfg.tick_ms = 1000;
	atomic_store(&g_fed, mono_ms());
	atomic_store(&g_run, true);
	if (pthread_create(&g_hands_thread, NULL, hands_run, NULL)) return false;
	pthread_detach(g_hands_thread);
	if (pthread_create(&g_thread, NULL, wdog_run, NULL)) {
		atomic_store(&g_run, false);
		return false;
	}
	pthread_detach(g_thread);
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = on_usr2;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGUSR2, &sa, NULL);
	g_started = true;
	return true;
}

/* Not joined: the thread sees it at its next look (a second at most) and
 * ends, and an exit does not wait for that */
void wdog_stop(void)
{
	if (!g_started) return;
	atomic_store(&g_run, false);
	g_started = false;
}
