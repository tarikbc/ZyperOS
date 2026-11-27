/* SPDX-License-Identifier: GPL-3.0-only */
/* Each zyperos.log held to about 2 MB (src/logcap.h):
 *
 *     make check-logcap
 *
 * A 5 MB log comes back under 2 MB with its boot lines first, a marker
 * line with the right count, then its newest lines, every line whole; a
 * second trim adds to the count; a log under the cap is left as it is; a
 * writer that appends (">>", as launch.sh's are) goes on at the new end.
 * Writes its own files, offline. */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "logcap.h"

static int failures;

#define CHECK(c, ...) do { if (!(c)) { printf("  FAIL: "); printf(__VA_ARGS__); \
	printf("\n        at %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static long file_size(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 ? (long)st.st_size : -1;
}

/* "line 000123 ....\n", 100 bytes each, numbered from `from` */
static long write_lines(FILE *f, long from, long bytes)
{
	long i;

	for (i = from; (i - from) * 100 < bytes; i++)
		fprintf(f, "line %06ld %-87s\n", i, "launcher: something said every frame");
	return i;
}

/* The file read back: every line whole and numbered, in order, the marker
 * once between them. The marker's count, the first and last line numbers. */
static bool read_back(const char *p, long *count, long *first, long *last, long *before, long *after)
{
	char line[256];
	FILE *f = fopen(p, "r");
	long prev = -1, n;
	bool ok = true, marked = false;

	*count = -1;
	*first = *last = *before = *after = -1;
	if (!f) return false;
	while (fgets(line, sizeof line, f)) {
		if (!strchr(line, '\n')) { ok = false; break; }
		if (sscanf(line, "... %ld lines dropped ...", &n) == 1) {
			if (marked) ok = false;
			marked = true;
			*count = n;
			*before = prev;
			continue;
		}
		if (sscanf(line, "line %ld ", &n) != 1 || strlen(line) != 100) { ok = false; break; }
		if (prev >= 0 && n != prev + 1 && !(marked && *after < 0)) ok = false;
		if (marked && *after < 0) *after = n;
		if (*first < 0) *first = n;
		prev = *last = n;
	}
	fclose(f);
	return ok && marked;
}

int main(void)
{
	char p[96];
	long next, count, first, last, before, after, gone;
	FILE *f;

	printf("logcap: a log held to about 2 MB\n");
	snprintf(p, sizeof p, "/tmp/zyperos-logcap-%ld.log", (long)getpid());

	/* Under the cap: left alone */
	f = fopen(p, "w");
	write_lines(f, 0, 1024L * 1024);
	fclose(f);
	CHECK(logcap_trim(p, LOGCAP_MAX, LOGCAP_HEAD, LOGCAP_TAIL) == 0 && file_size(p) < LOGCAP_MAX &&
	      file_size(p) > 1000L * 1024, "a 1 MB log was touched");

	/* 5 MB */
	f = fopen(p, "w");
	next = write_lines(f, 0, 5L * 1024 * 1024);
	fclose(f);
	gone = logcap_trim(p, LOGCAP_MAX, LOGCAP_HEAD, LOGCAP_TAIL);
	CHECK(file_size(p) <= LOGCAP_MAX, "a 5 MB log is %ld bytes, over the cap", file_size(p));
	CHECK(file_size(p) >= LOGCAP_HEAD + LOGCAP_TAIL - 200, "too little kept: %ld bytes", file_size(p));
	CHECK(read_back(p, &count, &first, &last, &before, &after),
	      "the trimmed log has a cut line, lines out of order, or no marker");
	CHECK(first == 0, "the boot's first line is %ld, not 0", first);
	CHECK(before == LOGCAP_HEAD / 100 - 1, "the head ends at line %ld, not %ld", before, LOGCAP_HEAD / 100 - 1);
	CHECK(last == next - 1, "the newest line is %ld, not %ld", last, next - 1);
	CHECK(count == after - before - 1 && gone == count, "the marker says %ld (trim said %ld), %ld went",
	      count, gone, after - before - 1);

	/* Written on, as launch.sh's ">>" does: at the new end; and trimmed again */
	{
		int fd = open(p, O_WRONLY | O_APPEND);
		FILE *a = fd >= 0 ? fdopen(fd, "a") : NULL;
		long prior = count;

		if (a) { next = write_lines(a, next, 1024L * 1024); fclose(a); }
		CHECK(file_size(p) > LOGCAP_MAX, "the appended lines did not land at the end");
		gone = logcap_trim(p, LOGCAP_MAX, LOGCAP_HEAD, LOGCAP_TAIL);
		CHECK(file_size(p) <= LOGCAP_MAX, "the second trim left %ld bytes", file_size(p));
		CHECK(read_back(p, &count, &first, &last, &before, &after),
		      "after a second trim: a cut line, an order broken, or two markers");
		CHECK(first == 0 && before == LOGCAP_HEAD / 100 - 1 && last == next - 1,
		      "after a second trim: first %ld, head's end %ld, last %ld", first, before, last);
		CHECK(count == after - before - 1 && count == prior + gone,
		      "the second marker says %ld, %ld went (%ld + %ld)", count, after - before - 1, prior, gone);
	}

	CHECK(logcap_trim("/tmp/zyperos-logcap-none.log", LOGCAP_MAX, LOGCAP_HEAD, LOGCAP_TAIL) == 0,
	      "a log that is not there");
	unlink(p);
	if (failures) { printf("\n%d check(s) failed\n", failures); return 1; }
	printf("ok: every check passed\n");
	return 0;
}
