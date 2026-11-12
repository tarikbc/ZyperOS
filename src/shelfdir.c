/* SPDX-License-Identifier: GPL-3.0-only */
#include "shelfdir.h"

#include "titletext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

bool sd_any(const game_list *all)
{
	for (int i = 0; all && i < all->count; i++)
		if (all->items[i].dir) return true;
	return false;
}

bool sd_in(const game_entry *g, const char *dir)
{
	size_t dl = strlen(dir);

	return g->dir == dl && (!dl || (!strncmp(g->file, dir, dl) && g->file[dl] == '/'));
}

void sd_dir_of(const game_entry *g, char *out, size_t n)
{
	snprintf(out, n, "%.*s", (int)g->dir, g->file);
}

void sd_parent(const char *dir, char *out, size_t n)
{
	const char *slash = strrchr(dir, '/');

	snprintf(out, n, "%.*s", slash ? (int)(slash - dir) : 0, dir);
}

const char *sd_base(const char *dir)
{
	const char *slash = strrchr(dir, '/');

	return slash ? slash + 1 : dir;
}

/* A folder in the one being built: its name, as a part of the games' paths
 * (not ended), and its count */
typedef struct {
	const char *name;
	int len, games;
} sd_child;

static int child_cmp(const void *pa, const void *pb)
{
	const sd_child *a = pa, *b = pb;
	int n = a->len < b->len ? a->len : b->len;
	int c = strncasecmp(a->name, b->name, (size_t)n);

	if (!c) c = a->len - b->len;
	if (!c) c = strncmp(a->name, b->name, (size_t)n);
	return c;
}

int sd_build(const game_list *all, const char *dir, game_list *out)
{
	size_t dl = strlen(dir);
	sd_child *kids = NULL;
	int nk = 0, ck = 0, ng = 0, i, k;

	lib_free(out);
	out->scanned = true;
	if (!all || all->count <= 0) return 0;

	/* One pass: the games in `dir` itself counted, and the folders in it
	 * gathered from the paths of the games deeper down. Linear in the
	 * shelf's games; a folder's few children are searched as they come. */
	for (i = 0; i < all->count; i++) {
		const game_entry *g = &all->items[i];
		const char *part, *end;
		int len;

		if (g->dir < dl || (dl && (strncmp(g->file, dir, dl) || g->file[dl] != '/')))
			continue;
		if (g->dir == dl) { ng++; continue; }
		part = g->file + (dl ? dl + 1 : 0);
		end = memchr(part, '/', (size_t)(g->file + g->dir - part));
		len = (int)((end ? end : g->file + g->dir) - part);
		for (k = 0; k < nk; k++)
			if (kids[k].len == len && !strncmp(kids[k].name, part, (size_t)len)) break;
		if (k == nk) {
			if (nk == ck) {
				int more = ck ? ck * 2 : 16;
				sd_child *bigger = realloc(kids, (size_t)more * sizeof *bigger);

				if (!bigger) { free(kids); return -1; }
				kids = bigger;
				ck = more;
			}
			kids[nk++] = (sd_child){ part, len, 0 };
		}
		kids[k].games++;
	}
	if (nk + ng == 0) { free(kids); return 0; }
	if (!(out->items = calloc((size_t)(nk + ng), sizeof *out->items))) {
		free(kids);
		return -1;
	}
	if (nk > 1) qsort(kids, (size_t)nk, sizeof *kids, child_cmp);
	for (k = 0; k < nk; k++) {
		game_entry *f = &out->items[k];
		char name[LIB_NAME];

		snprintf(f->file, sizeof f->file, "%.*s%s%.*s", (int)dl, dir, dl ? "/" : "",
		         kids[k].len, kids[k].name);
		snprintf(name, sizeof name, "%.*s", kids[k].len, kids[k].name);
		snprintf(f->name, sizeof f->name, "%s", name);
		/* Its name as it is on the card: a folder is named by whoever
		 * made it, and its brackets are theirs, not a catalog's */
		title_text(name, f->title, sizeof f->title);
		if (!f->title[0]) snprintf(f->title, sizeof f->title, "%s", name);
		f->dir = (unsigned short)dl;
		f->folder = kids[k].games;
		f->added = LIB_ADDED_UNKNOWN;
	}
	for (i = 0, k = nk; i < all->count; i++)
		if (sd_in(&all->items[i], dir)) out->items[k++] = all->items[i];
	out->count = nk + ng;
	free(kids);
	return nk;
}
