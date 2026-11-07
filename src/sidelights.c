/* SPDX-License-Identifier: LicenseRef-PolyForm-Shield-1.0.0 */
/* See sidelights.h. */
#include "sidelights.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* The words, in the order of side_light_part. The first two are the battery's,
 * which share the legacy first word; the rest stand alone. */
static const char *const WORD[LP_COUNT] = { "bar", "low", "level", "done", "cheer" };
static const char *const NAME[LP_COUNT] = {
	"Charging Bar", "Low Battery", "Volume and Brightness", "Files and Covers Done", "Achievements"
};
#define NEWER ((1u << LP_LEVEL) | (1u << LP_DONE) | (1u << LP_CHEER))

const char *side_lights_part_name(side_light_part p)
{
	return p >= 0 && p < LP_COUNT ? NAME[p] : "";
}

/* As plastron's gauge reads it (package/gauge/src/lights.c, lights_parse) */
side_lights side_lights_parse(const char *text)
{
	side_lights on = (1u << LP_BAR) | (1u << LP_LOW);
	bool newer = true, first = true;
	const char *p = text ? text : "";

	for (;;) {
		size_t n;
		bool yes;
		int i;

		p += strspn(p, " \t\r\n");
		n = strcspn(p, " \t\r\n");
		if (!n) break;
		if (first) {
			first = false;
			if (n == 4 && !strncmp(p, "auto", 4)) { p += n; continue; }
			if (n == 8 && !strncmp(p, "charging", 8)) { on = 1u << LP_BAR; newer = false; p += n; continue; }
			if (n == 3 && !strncmp(p, "low", 3)) { on = 1u << LP_LOW; newer = false; p += n; continue; }
			if (n == 3 && !strncmp(p, "off", 3)) { on = 0; newer = false; p += n; continue; }
			/* none of the four: auto, and the word is a part */
		}
		if (newer) on |= NEWER;
		newer = false;
		yes = *p != '-';
		if (!yes) { p++; n--; }
		for (i = 0; i < LP_COUNT; i++)
			if (strlen(WORD[i]) == n && !strncmp(p, WORD[i], n))
				on = yes ? on | (1u << i) : on & ~(1u << i);
		p += n;
	}
	if (newer) on |= NEWER;
	return on & LIGHTS_ALL;
}

void side_lights_line(side_lights on, char *out, size_t cap)
{
	bool bar = side_lights_has(on, LP_BAR), low = side_lights_has(on, LP_LOW);
	size_t used;
	int i;

	snprintf(out, cap, "%s", bar && low ? "auto" : bar ? "charging" : low ? "low" : "off");
	for (i = LP_LEVEL; i < LP_COUNT; i++) {
		used = strlen(out);
		snprintf(out + used, cap - used, " %s%s", side_lights_has(on, (side_light_part)i) ? "" : "-", WORD[i]);
	}
}

const char *side_lights_value(side_lights on, char *buf, size_t cap)
{
	int i, n = 0;

	for (i = 0; i < LP_COUNT; i++) n += side_lights_has(on, (side_light_part)i);
	if (n == LP_COUNT) return "All on";
	if (n == 0) return "Off";
	snprintf(buf, cap, "%d on", n);
	return buf;
}

int side_lights_level_line(char *out, size_t cap, bool volume, int val, int max)
{
	return snprintf(out, cap, "level %s %d %d", volume ? "volume" : "brightness", val, max);
}

bool side_lights_say_to(const char *fifo, const char *line)
{
	char buf[64];
	sigset_t pipe_sig, old;
	ssize_t w;
	int fd, n = snprintf(buf, sizeof buf, "%s\n", line);

	if (n <= 1 || n >= (int)sizeof buf) return false;
	/* O_NONBLOCK: open fails at once with no reader, and a full pipe is
	 * EAGAIN rather than a wait. No O_CREAT: only the gauge makes it. */
	fd = open(fifo, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (fd < 0) return false;
	sigemptyset(&pipe_sig);
	sigaddset(&pipe_sig, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &pipe_sig, &old);
	w = write(fd, buf, (size_t)n);   /* under PIPE_BUF: whole or not at all */
	if (w < 0 && errno == EPIPE) {
		sigset_t pending;
		int sig;

		/* Raised on this thread and held by the mask: taken off here, so
		 * it is not delivered when the mask comes back */
		if (!sigpending(&pending) && sigismember(&pending, SIGPIPE))
			sigwait(&pipe_sig, &sig);
	}
	pthread_sigmask(SIG_SETMASK, &old, NULL);
	close(fd);
	return w == n;
}

bool side_lights_batch_done(side_lights_batch *b, bool arriving, unsigned now_ms)
{
	if (arriving) {
		b->moved = true;
		b->last = now_ms;
		return false;
	}
	if (b->moved && now_ms - b->last >= SIDE_LIGHTS_BATCH_MS) {
		b->moved = false;
		return true;
	}
	return false;
}
