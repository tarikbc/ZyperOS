/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* The theme file. See src/themefile.h. */
#include "themefile.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif

/* Whether the n bytes at s, trimmed of whitespace at both ends, are exactly
 * `word`. By length and not by strcmp, so a NUL in the file is not the end of
 * the word: "dark\0junk" is not dark. */
static bool is_word(const char *s, size_t n, const char *word)
{
	size_t len = strlen(word);

	while (n && isspace((unsigned char)*s)) s++, n--;
	while (n && isspace((unsigned char)s[n - 1])) n--;
	return n == len && !memcmp(s, word, len);
}

/* Six colors, "rrggbb" each and a space before each, into the bar's roles */
static bool read_bar(const char *s, size_t n, ds_theme *bar)
{
	ds_rgb *role[6] = { &bar->surface, &bar->edge, &bar->track,
	                    &bar->accent_ink, &bar->light, &bar->ink };
	ds_rgb got[6];
	size_t at = 3;
	int i, k;

	if (n < 3 || memcmp(s, "bar", 3)) return false;
	for (i = 0; i < 6; i++) {
		unsigned v = 0;

		if (at >= n || s[at] != ' ') return false;
		at++;
		for (k = 0; k < 6; k++, at++) {
			int c = at < n ? (unsigned char)s[at] : 0;

			if (!isxdigit(c)) return false;
			v = v * 16 + (unsigned)(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
		}
		got[i] = (ds_rgb){ (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
	}
	while (at < n && isspace((unsigned char)s[at])) at++;
	if (at != n) return false;
	for (i = 0; i < 6; i++) *role[i] = got[i];
	return true;
}

int themefile_read_bar(const char *path, ds_theme *bar)
{
	/* A word, and the bar's line under it, is under seventy bytes. A file
	 * as long as this is not ours, and is not read further to find out: it
	 * is the default, which is also what makes the read one syscall, on
	 * every key press of a program that has no time to spare for a file. */
	char buf[96];
	ssize_t n;
	int fd, theme = DS_LIGHT;

	if (bar) *bar = DS_THEME[DS_LIGHT];
	if (!path) return DS_LIGHT;
	/* O_NONBLOCK: a FIFO left at this name makes a plain open wait for a
	 * writer that may never come, on the key press that asked. With it the
	 * open returns at once and the read finds nothing, which is the default. */
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0) return DS_LIGHT;
	n = read(fd, buf, sizeof buf);
	close(fd);
	if (n <= 0 || n >= (ssize_t)sizeof buf) return DS_LIGHT;

	/* The bar's line, when there is one, starts with "bar" at the start of
	 * a line; the word is everything before it, read as it always was. A
	 * bar line that is not whole leaves the tokens' colors. */
	{
		size_t i, word = (size_t)n;
		const char *line = NULL;

		for (i = 0; i + 3 <= (size_t)n; i++)
			if ((i == 0 || buf[i - 1] == '\n') && !memcmp(buf + i, "bar", 3)) {
				line = buf + i;
				word = i;
				break;
			}
		if (is_word(buf, word, "dark")) theme = DS_DARK;
		if (bar) *bar = DS_THEME[theme];
		if (line && (theme == DS_DARK || is_word(buf, word, "light"))) {
			size_t len = (size_t)n - word;
			ds_theme t = DS_THEME[theme];

			while (len && isspace((unsigned char)line[len - 1])) len--;
			if (read_bar(line, len, &t) && bar) *bar = t;
		}
	}
	return theme;
}

int themefile_read(const char *path)
{
	return themefile_read_bar(path, NULL);
}

bool themefile_write(const char *path, int theme)
{
	return themefile_write_bar(path, theme, NULL);
}

bool themefile_write_bar(const char *path, int theme, const ds_theme *bar)
{
	char text[96];
	char tmp[1024];
	size_t len;
	int fd, saved;
	bool ok;

	if (theme != DS_DARK && theme != DS_LIGHT) { errno = EINVAL; return false; }
	if (bar) {
		const ds_rgb c[6] = { bar->surface, bar->edge, bar->track, bar->accent_ink,
		                      bar->light, bar->ink };

		snprintf(text, sizeof text,
		         "%s\nbar %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x %02x%02x%02x\n",
		         theme == DS_DARK ? "dark" : "light",
		         c[0].r, c[0].g, c[0].b, c[1].r, c[1].g, c[1].b, c[2].r, c[2].g, c[2].b,
		         c[3].r, c[3].g, c[3].b, c[4].r, c[4].g, c[4].b, c[5].r, c[5].g, c[5].b);
	} else {
		snprintf(text, sizeof text, "%s\n", theme == DS_DARK ? "dark" : "light");
	}
	if (!path) { errno = EINVAL; return false; }
	if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) {
		errno = ENAMETOOLONG;
		return false;
	}

	/* The temporary is made new each time, and not written through whatever
	 * is there: /tmp is every user's on a development host, and a link left
	 * at this name would have this write to its target. O_EXCL after an
	 * unlink refuses to follow one, and a stale temporary of a launcher that
	 * was killed mid-write is simply replaced. If somebody wins the race
	 * between the two, the open fails and the write is not made, which the
	 * caller logs and carries on from. */
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644);
	if (fd < 0) return false;

	/* No fsync: this is /tmp, a RAM filesystem on the device, and the file
	 * is a fact about this boot that the next start of the launcher writes
	 * again. Nothing is lost that a flush would have kept. */
	len = strlen(text);
	ok = write(fd, text, len) == (ssize_t)len;
	if (close(fd) != 0) ok = false;
	if (ok && rename(tmp, path) != 0) ok = false;
	if (!ok) {
		saved = errno;       /* the unlink must not be what the caller reads */
		unlink(tmp);
		errno = saved;
	}
	return ok;
}
