/* SPDX-License-Identifier: GPL-3.0-only */
/* See hwput.h. */
#include "hwput.h"

#include "klog.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static unsigned g_timeout = HWPUT_TIMEOUT_MS;

void hwput_set_timeout(unsigned ms) { g_timeout = ms ? ms : HWPUT_TIMEOUT_MS; }

static unsigned g_test_delay;

void hwput_test_delay(unsigned ms) { g_test_delay = ms; }

static unsigned mono_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned)(ts.tv_sec * 1000u + (unsigned)(ts.tv_nsec / 1000000));
}

/* The one job's child at a time */
static pid_t       g_pid;
static unsigned    g_t0;
static const char *g_what = "";
static bool        g_late_said;
static int         g_last = -1;
static bool        g_untaken;

int hwput_last(void) { return g_last; }

int hwput_take(void)
{
	hwput_reap();
	if (!g_untaken) return -1;
	g_untaken = false;
	return g_last;
}

/* What the child's exit says: 0 done, 1 skipped, 2 + k a write that failed
 * at step k */
static int result_of(int status)
{
	if (!WIFEXITED(status)) return HWPUT_FAILED;
	switch (WEXITSTATUS(status)) {
	case 0:  return HWPUT_DONE;
	case 1:  return HWPUT_SKIPPED;
	default: return HWPUT_FAILED;
	}
}

static void ended(int status)
{
	unsigned took = mono_ms() - g_t0;

	g_last = result_of(status);
	g_untaken = true;
	/* A late one is said again when it ends: the controller came back */
	if (g_late_said)
		fprintf(stderr, "usb: the controller answered after all (%s, %u.%u s)\n", g_what,
		        took / 1000, took % 1000 / 100);
	g_pid = 0;
	g_late_said = false;
}

void hwput_reap(void)
{
	int status;
	pid_t r;

	if (!g_pid) return;
	r = waitpid(g_pid, &status, WNOHANG);
	if (r == g_pid) {
		ended(status);
		return;
	}
	if (r < 0 && errno == ECHILD) {          /* somebody else reaped it */
		g_pid = 0;
		g_last = HWPUT_FAILED;
		g_untaken = true;
		g_late_said = false;
		return;
	}
	if (!g_late_said && mono_ms() - g_t0 >= g_timeout) {
		fprintf(stderr, "usb: the controller did not answer (%s, %u.%u s): carrying on without it\n",
		        g_what, g_timeout / 1000, g_timeout % 1000 / 100);
		g_late_said = true;
	}
}

bool hwput_busy(void)
{
	hwput_reap();
	return g_pid != 0;
}

/* ---- the child: async-signal-safe calls only ------------------------- */

/* "<say> failed (errno N)" */
static void say_failed(const char *say, int err)
{
	char line[256], num[12];
	size_t n = 0, k = strlen(say), d = 0;
	unsigned e = err < 0 ? 0u : (unsigned)err;

	if (k > sizeof line - 40) k = sizeof line - 40;
	memcpy(line, say, k);
	n = k;
	memcpy(line + n, " failed (errno ", 15);
	n += 15;
	do { num[d++] = (char)('0' + e % 10); e /= 10; } while (e && d < sizeof num);
	while (d) line[n++] = num[--d];
	line[n++] = ')';
	line[n] = '\0';
	klog_raw(line);
}

static int child_run(const hwput_job *job)
{
	int k;

	if (job->need && !job->need(job->arg)) return 1;
	for (k = 0; k < job->n; k++) {
		const hwput_step *s = &job->steps[k];
		int fd, err = 0;
		size_t len;

		if (s->say) klog_raw(s->say);
		if (!s->path || !s->text) continue;      /* a line only */
		len = strlen(s->text);
		if (g_test_delay) {
			struct timespec d = { (time_t)(g_test_delay / 1000), (long)(g_test_delay % 1000) * 1000000L };

			while (nanosleep(&d, &d) < 0 && errno == EINTR) { }
		}
		if ((fd = open(s->path, O_WRONLY | O_CLOEXEC)) < 0) {
			err = errno;
		} else {
			if (write(fd, s->text, len) < 0) err = errno;
			close(fd);
		}
		if (err) {
			say_failed(s->say ? s->say : s->path, err);
			return 2 + k;
		}
		if (s->pause_ms) {
			struct timespec gap = { (time_t)(s->pause_ms / 1000),
			                        (long)(s->pause_ms % 1000) * 1000000L };

			while (nanosleep(&gap, &gap) < 0 && errno == EINTR) { }
		}
	}
	return 0;
}

/* ---- the launcher's side --------------------------------------------- */

int hwput_run(const hwput_job *job, bool wait)
{
	int maxfd, status;
	pid_t pid;

	hwput_reap();
	if (g_pid) {
		unsigned ago = mono_ms() - g_t0;

		fprintf(stderr, "usb: %s left out: the controller is still in a write (%s, %u.%u s ago)\n",
		        job->what, g_what, ago / 1000, ago % 1000 / 100);
		return HWPUT_BUSY;
	}
	maxfd = klog_maxfd();
	fflush(stderr);
	pid = fork();
	if (pid < 0) {
		fprintf(stderr, "usb: %s left out: no process for it (%s)\n", job->what, strerror(errno));
		return HWPUT_NOFORK;
	}
	if (pid == 0) {
		klog_child_fds(maxfd);
		_exit(child_run(job));
	}
	g_pid = pid;
	g_t0 = mono_ms();
	g_what = job->what;
	g_late_said = false;
	g_last = -1;
	g_untaken = false;
	if (!wait) return HWPUT_STARTED;
	/* A write is a few ms when the controller is well: looked at often at
	 * first, then every 10 ms */
	for (unsigned spins = 0;; spins++) {
		pid_t r = waitpid(pid, &status, WNOHANG);
		struct timespec nap = { 0, (spins < 20 ? 1 : 10) * 1000000L };

		if (r == pid) {
			ended(status);
			g_untaken = false;            /* its caller has it */
			return g_last;
		}
		if (r < 0 && errno != EINTR) {
			g_pid = 0;
			return HWPUT_FAILED;
		}
		if (mono_ms() - g_t0 >= g_timeout) break;
		nanosleep(&nap, NULL);
	}
	hwput_reap();                            /* says it is late */
	return HWPUT_TIMEOUT;
}
