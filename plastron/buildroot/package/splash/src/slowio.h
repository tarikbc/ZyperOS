/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * slowio.h: what slowline.h's run needs from the handheld, for splash.c and
 * splash-fb.c: the step the start's scripts are on, SELECT held, the
 * kernel's latest lines, and the beat into the kernel's log. Linux only.
 *
 * Nothing here is opened until a start is SLOW_STAGE_S in: a start that is
 * on time never reads a file or a key more than it did.
 */
#ifndef SPLASH_SLOWIO_H
#define SPLASH_SLOWIO_H

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "slowline.h"

/* One word, the step under way, from datapart and zyperos.init: a plain
 * echo (no fork at boot), so a read may find it empty for an instant */
#define SLOW_STAGE_FILE "/tmp/splash-stage"
#define SLOW_DIR        "/usr/share/splash/slow"
/* The tester's view's letters at this handheld's scale (tools/genbootslow.py),
 * else the start's line's */
#define SLOW_TESTER_GLYPHS SLOW_DIR "/glyphs.bin"
#define SLOW_GLYPHS     "/usr/share/splash/glyphs.bin"

/* A whole small file into a fresh buffer: its size, 0 for none */
static size_t slowio_slurp(const char *path, uint8_t **out)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	off_t n;
	uint8_t *b;

	*out = NULL;
	if (fd < 0) return 0;
	n = lseek(fd, 0, SEEK_END);
	if (n <= 0 || n > (4 << 20) || lseek(fd, 0, SEEK_SET) != 0 || !(b = malloc((size_t)n))) {
		close(fd);
		return 0;
	}
	if (read(fd, b, (size_t)n) != n) {
		free(b);
		close(fd);
		return 0;
	}
	close(fd);
	*out = b;
	return (size_t)n;
}

/* The language's lines (zylang, as the first start's line takes it), else
 * English's; and the letters. 0 when the lines are there. */
static int slowio_load(slow_run *r, const char *lang)
{
	char path[128];
	uint8_t *f;
	size_t n = 0;

	if (lang && *lang && strcmp(lang, "-") && strlen(lang) < 16 && !strchr(lang, '/')) {
		snprintf(path, sizeof path, SLOW_DIR "/%s.bin", lang);
		n = slowio_slurp(path, &f);
	}
	if (!n) n = slowio_slurp(SLOW_DIR "/en.bin", &f);
	if (!n) return -1;
	r->have = slow_load(f, n, &r->set) == 0;
	free(f);
	/* The letters stay in their buffer: slow_glyphs points into it */
	if ((n = slowio_slurp(SLOW_TESTER_GLYPHS, &f)) || (n = slowio_slurp(SLOW_GLYPHS, &f)))
		r->have_g = slow_glyphs_load(f, n, &r->g) == 0;
	return r->have ? 0 : -1;
}

/* The step, read again at most every quarter second */
static void slowio_stage(char out[4], double now, double *read_at)
{
	char text[16] = "";
	int fd;
	ssize_t n;

	if (now - *read_at < 0.25) return;
	*read_at = now;
	if ((fd = open(SLOW_STAGE_FILE, O_RDONLY | O_CLOEXEC)) < 0) return;
	n = read(fd, text, sizeof text - 1);
	close(fd);
	if (n <= 0) return;
	text[n] = 0;
	/* The scripts write it with a plain echo, so a read may meet it half
	 * written: only a whole step it knows replaces the last */
	{
		char word[4];

		slow_stage_word(text, word);
		if (slow_stage_line(word) > 0 || !strcmp(word, "U2")) memcpy(out, word, sizeof word);
	}
}

/* SELECT, on every input that has it, read without waiting */
typedef struct {
	int    fd[8], n, opened, down;
	double since;
} slowio_keys;

static void slowio_keys_open(slowio_keys *k)
{
	DIR *dir = opendir("/dev/input");
	struct dirent *e;

	k->opened = 1;
	while (dir && k->n < 8 && (e = readdir(dir))) {
		char path[300];
		unsigned long keys[KEY_MAX / (8 * sizeof(long)) + 1] = { 0 };
		int fd;

		if (strncmp(e->d_name, "event", 5)) continue;
		snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
		if ((fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0) continue;
		if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 &&
		    keys[BTN_SELECT / (8 * sizeof(long))] >> (BTN_SELECT % (8 * sizeof(long))) & 1)
			k->fd[k->n++] = fd;
		else
			close(fd);
	}
	if (dir) closedir(dir);
}

/* How long SELECT has been held at `now`, 0 while it is up */
static double slowio_select(slowio_keys *k, double now)
{
	struct input_event ev;

	if (!k->opened) slowio_keys_open(k);
	for (int i = 0; i < k->n; i++)
		while (read(k->fd[i], &ev, sizeof ev) == sizeof ev)
			if (ev.type == EV_KEY && ev.code == BTN_SELECT) {
				if (ev.value == 1 && !k->down) { k->down = 1; k->since = now; }
				else if (ev.value == 0) k->down = 0;
			}
	return k->down ? now - k->since : 0;
}

/* The kernel's log, the latest lines, read without waiting */
#define SLOWIO_KMSG_KEEP 8
typedef struct {
	int  fd, opened, n;
	char line[SLOWIO_KMSG_KEEP][160];
} slowio_kmsg;

static void slowio_kmsg_poll(slowio_kmsg *m)
{
	char rec[1024];
	ssize_t got;

	if (!m->opened) {
		m->opened = 1;
		m->fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	}
	if (m->fd < 0) return;
	for (;;) {
		got = read(m->fd, rec, sizeof rec - 1);
		if (got < 0 && errno == EPIPE) continue;     /* lines overwritten: go on */
		if (got <= 0) break;
		rec[got] = 0;
		if (m->n == SLOWIO_KMSG_KEEP) {
			memmove(m->line[0], m->line[1], sizeof m->line[0] * (SLOWIO_KMSG_KEEP - 1));
			m->n--;
		}
		if (slow_kmsg_line(rec, m->line[m->n], sizeof m->line[0]) == 0) m->n++;
	}
}

#endif
