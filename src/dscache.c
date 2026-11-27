/* SPDX-License-Identifier: GPL-3.0-only */
/* The zipped DS games DSperate unpacked, kept under a cap. See src/dscache.h. */
#include "dscache.h"

#include <dirent.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#define DSC_PATH 1200

typedef struct {
	char path[DSC_PATH];
	unsigned long long bytes;
	long stamp;
} dsc_entry;

static int oldest_first(const void *a, const void *b)
{
	const dsc_entry *x = a, *y = b;

	if (x->stamp != y->stamp) return x->stamp < y->stamp ? -1 : 1;
	return strcmp(x->path, y->path);
}

unsigned long long dsc_trim(const char *dir, unsigned long long cap, const char *keep,
                            int *removed)
{
	dsc_entry *v = NULL;
	unsigned long long total = 0, freed = 0;
	int n = 0, room = 0, i, went = 0;
	struct dirent *e;
	DIR *d;

	if (removed) *removed = 0;
	if (!dir || !(d = opendir(dir))) return 0;
	/* Every image, as DSperate's list_cache: a name ending .nds */
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		char tag[DSC_PATH + 8];
		struct stat st;
		dsc_entry *x;

		if (len < 5 || strcmp(e->d_name + len - 4, ".nds")) continue;
		if (n == room) {
			dsc_entry *nv = realloc(v, (size_t)(room = room ? room * 2 : 16) * sizeof *v);

			if (!nv) break;
			v = nv;
		}
		x = &v[n];
		if (snprintf(x->path, sizeof x->path, "%s/%s", dir, e->d_name) >= (int)sizeof x->path)
			continue;
		if (stat(x->path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		x->bytes = (unsigned long long)st.st_size;
		x->stamp = (long)st.st_mtime;
		/* Its tag: the path and ".tag", which `tag` has room for */
		snprintf(tag, sizeof tag, "%s.tag", x->path);
		if (stat(tag, &st) == 0) x->stamp = (long)st.st_mtime;
		total += x->bytes;
		n++;
	}
	closedir(d);

	if (total > cap) {
		qsort(v, (size_t)n, sizeof *v, oldest_first);
		for (i = 0; i < n && total > cap; i++) {
			char tag[DSC_PATH + 8];

			if (keep && !strcmp(v[i].path, keep)) continue;
			if (unlink(v[i].path) != 0) continue;
			snprintf(tag, sizeof tag, "%s.tag", v[i].path);
			unlink(tag);
			total -= v[i].bytes;
			freed += v[i].bytes;
			went++;
		}
	}
	free(v);
	if (removed) *removed = went;
	return freed;
}
