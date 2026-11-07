/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef ZYPEROS_FILEIO_H
#define ZYPEROS_FILEIO_H

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Reading a file whole: the replies curl leaves in /tmp, a body the File
 * Transfer page sent, a sound, a track, a DS save. Header-only, so none of
 * the checks that link those modules needs another source on its line. */

/* The file in memory, NUL-terminated (the NUL not counted), or NULL: no such
 * file, an empty one, one of `max` bytes or more, a short read, no memory.
 * *len is the file's size whenever it has one, also when it is too big and
 * NULL comes back, so a log can say how big; 0 otherwise. */
static inline char *file_read(const char *path, long max, size_t *len)
{
	FILE *f = fopen(path, "rb");
	char *buf = NULL;
	long n;

	*len = 0;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0) {
		*len = (size_t)n;
		if (n < max && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)n + 1))) {
			if (fread(buf, 1, (size_t)n, f) == (size_t)n) {
				buf[n] = '\0';
			} else {
				free(buf);
				buf = NULL;
			}
		}
	}
	fclose(f);
	return buf;
}

/* The start of a small file, n - 1 bytes at most, into `out` and
 * NUL-terminated: a sysfs value, an address, a short log. "" when nothing
 * could be read. Returns the bytes read, 0 or less for none. */
static inline long file_head(const char *path, char *out, size_t n)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t got = -1;

	if (fd >= 0) {
		got = read(fd, out, n - 1);
		close(fd);
	}
	out[got > 0 ? got : 0] = '\0';
	return (long)got;
}

#endif
