/* SPDX-License-Identifier: GPL-3.0-only */
/* Each zyperos.log held to about 2 MB. See src/logcap.h. */
#include "logcap.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MARK_FMT "... %ld lines dropped ...\n"

/* An earlier trim's marker in the head that was read, `b`: where its line
 * starts, and the count it holds; -1 for none. A later trim puts its own in
 * the same place, so the head stays the boot's and nothing more. */
static long find_marker(const char *b, long n, long *count)
{
	long i;

	for (i = 0; i + 4 < n; i++) {
		char line[64];
		long k;

		if ((i && b[i - 1] != '\n') || memcmp(b + i, "... ", 4)) continue;
		k = n - i < (long)sizeof line - 1 ? n - i : (long)sizeof line - 1;
		memcpy(line, b + i, (size_t)k);
		line[k] = '\0';
		if (sscanf(line, "... %ld lines dropped ...", count) == 1 && strstr(line, " lines dropped ...\n"))
			return i;
	}
	return -1;
}

/* The lines in [from, to) */
static long lines_in(int fd, long from, long to)
{
	char b[65536];
	long lines = 0;

	while (from < to) {
		ssize_t got = pread(fd, b, (size_t)(to - from < (long)sizeof b ? to - from : (long)sizeof b), from);

		if (got <= 0) break;
		for (ssize_t i = 0; i < got; i++) lines += b[i] == '\n';
		from += got;
	}
	return lines;
}

long logcap_trim(const char *path, long max, long head, long tail)
{
	struct stat st;
	char *hb = NULL, *buf = NULL, mark[64];
	long size, h, at, prior = 0, gone, len, cap, off, mlen;
	ssize_t got;
	int fd, k;

	if (!path || !*path || head <= 0 || tail <= 0 || head + tail >= max) return -1;
	if ((fd = open(path, O_RDWR)) < 0) return 0;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= max) { close(fd); return 0; }
	size = (long)st.st_size;

	/* The head, to its last whole line, or to an earlier marker */
	if (!(hb = malloc((size_t)head)) || (got = pread(fd, hb, (size_t)head, 0)) <= 0) goto fail;
	if ((h = find_marker(hb, (long)got, &prior)) < 0) {
		prior = 0;
		for (h = (long)got; h > 0 && hb[h - 1] != '\n'; h--) ;
	}
	free(hb);
	hb = NULL;

	/* The newest `tail`, from a whole line, and room for what comes meanwhile */
	at = size - tail;
	cap = tail + 256L * 1024;
	if (!(buf = malloc((size_t)cap)) || (got = pread(fd, buf, (size_t)tail, at)) <= 0) goto fail;
	len = (long)got;
	for (off = 0; off < len && buf[off] != '\n'; off++) ;
	off = off < len ? off + 1 : 0;
	/* What was written while this read: a few tries, then let it go */
	for (k = 0; k < 4 && fstat(fd, &st) == 0 && (long)st.st_size > at + len; k++) {
		long more = (long)st.st_size - (at + len);

		if (len + more > cap) {
			char *bigger = realloc(buf, (size_t)(len + more));

			if (!bigger) break;
			buf = bigger;
			cap = len + more;
		}
		if ((got = pread(fd, buf + len, (size_t)more, at + len)) <= 0) break;
		len += (long)got;
	}

	/* Every line from the head's end to the tail's start, the old marker's
	 * own not counted */
	gone = lines_in(fd, h, at + off) - (prior > 0 ? 1 : 0);
	if (gone < 0) gone = 0;
	mlen = snprintf(mark, sizeof mark, MARK_FMT, prior + gone);
	if (pwrite(fd, mark, (size_t)mlen, h) != mlen ||
	    pwrite(fd, buf + off, (size_t)(len - off), h + mlen) != len - off ||
	    ftruncate(fd, h + mlen + (len - off)) != 0)
		goto fail;
	free(buf);
	close(fd);
	return gone;

fail:
	free(hb);
	free(buf);
	close(fd);
	return -1;
}

static char g_path[1024];

/* The log, still the one stderr writes to: launch.sh moves it aside only
 * at a boot, but a trim of another file would be a trim of the wrong one */
static void *watch(void *arg)
{
	(void)arg;
	for (;;) {
		struct stat fd_st, path_st;

		sleep(LOGCAP_EVERY_S);
		if (fstat(STDERR_FILENO, &fd_st) != 0 || fd_st.st_size <= LOGCAP_MAX) continue;
		if (stat(g_path, &path_st) != 0 || path_st.st_ino != fd_st.st_ino ||
		    path_st.st_dev != fd_st.st_dev)
			continue;
		logcap_trim(g_path, LOGCAP_MAX, LOGCAP_HEAD, LOGCAP_TAIL);
	}
	return NULL;
}

void logcap_start(void)
{
	struct stat st;
	pthread_attr_t at;
	pthread_t t;
	ssize_t n;

	if (fstat(STDERR_FILENO, &st) != 0 || !S_ISREG(st.st_mode)) return;
	n = readlink("/proc/self/fd/2", g_path, sizeof g_path - 1);
	if (n <= 0) return;
	g_path[n] = '\0';
	pthread_attr_init(&at);
	pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
	pthread_attr_setstacksize(&at, 128 * 1024);
	if (pthread_create(&t, &at, watch, NULL) != 0) g_path[0] = '\0';
	pthread_attr_destroy(&at);
}
