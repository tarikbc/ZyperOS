/* SPDX-License-Identifier: GPL-3.0-only */
/* A set's badges in one file. See src/rapack.h. */
#include "rapack.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define RP_HEAD   12                      /* magic, version, count */
#define RP_ENTRY  (RP_NAME + 8)
#define RP_BIG    (1024L * 1024L)         /* no badge is near this: a bad file */

typedef struct {
	char     name[RP_NAME];
	uint32_t off, size;
} rp_entry;

static uint32_t get32(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void put32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

bool rp_pack_path(const char *set_path, char *out, size_t n)
{
	const char *slash, *base;
	size_t bl;

	if (!n) return false;
	out[0] = '\0';
	if (!set_path || !*set_path) return false;
	slash = strrchr(set_path, '/');
	base = slash ? slash + 1 : set_path;
	bl = strlen(base);
	if (bl > 4 && !strcmp(base + bl - 4, ".set")) bl -= 4;
	if (!bl) return false;
	return snprintf(out, n, "%.*sbadges/%.*s.pack", slash ? (int)(slash - set_path + 1) : 0, set_path,
	                (int)bl, base) < (int)n;
}

/* The pack's index, checked against its length: false for no pack or a bad
 * one (a cut write, another file), which is then as good as none */
static bool read_index(FILE *f, rp_entry *e, int *n)
{
	unsigned char head[RP_HEAD], raw[RP_ENTRY];
	long len;
	uint32_t count, i;

	*n = 0;
	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < RP_HEAD || fseek(f, 0, SEEK_SET) != 0) return false;
	if (fread(head, 1, RP_HEAD, f) != RP_HEAD || memcmp(head, RP_MAGIC, 4) || get32(head + 4) != 1)
		return false;
	count = get32(head + 8);
	if (count > RP_MAX || (long)(RP_HEAD + count * RP_ENTRY) > len) return false;
	for (i = 0; i < count; i++) {
		if (fread(raw, 1, RP_ENTRY, f) != RP_ENTRY) return false;
		memcpy(e[i].name, raw, RP_NAME);
		e[i].name[RP_NAME - 1] = '\0';
		e[i].off = get32(raw + RP_NAME);
		e[i].size = get32(raw + RP_NAME + 4);
		if (e[i].off < RP_HEAD + count * RP_ENTRY || (long)e[i].off + (long)e[i].size > len) return false;
	}
	*n = (int)count;
	return true;
}

static void *read_at(FILE *f, uint32_t off, uint32_t size)
{
	void *b = malloc(size ? size : 1);

	if (!b) return NULL;
	if (fseek(f, (long)off, SEEK_SET) != 0 || fread(b, 1, size, f) != size) {
		free(b);
		return NULL;
	}
	return b;
}

/* Badge `name` in the pack: its bytes when `want`, else only whether it is
 * there (the index alone) */
static void *find(const char *pack, const char *name, size_t *len, bool want, bool *there)
{
	rp_entry *e;
	FILE *f;
	void *b = NULL;
	int n, i;

	*there = false;
	if (len) *len = 0;
	if (!pack || !name || !*name || !(f = fopen(pack, "rb"))) return NULL;
	if (!(e = malloc(RP_MAX * sizeof *e))) { fclose(f); return NULL; }
	if (read_index(f, e, &n))
		for (i = 0; i < n; i++)
			if (!strcmp(e[i].name, name)) {
				*there = true;
				if (want && (b = read_at(f, e[i].off, e[i].size)) && len) *len = e[i].size;
				break;
			}
	free(e);
	fclose(f);
	return b;
}

void *rp_read(const char *pack, const char *name, size_t *len)
{
	bool there;

	return find(pack, name, len, true, &there);
}

bool rp_has(const char *pack, const char *name)
{
	bool there;

	find(pack, name, NULL, false, &there);
	return there;
}

/* A whole file, if it is a PNG of a badge's size */
static void *read_png(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	long n;
	void *b = NULL;

	*len = 0;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 8 && n < RP_BIG && fseek(f, 0, SEEK_SET) == 0 &&
	    (b = malloc((size_t)n)) && fread(b, 1, (size_t)n, f) == (size_t)n &&
	    !memcmp(b, "\x89PNG", 4)) {
		*len = (size_t)n;
	} else {
		free(b);
		b = NULL;
	}
	fclose(f);
	return b;
}

/* One badge going into the new pack: from the old pack, or loose */
typedef struct {
	char     name[RP_NAME];
	bool     loose;
	uint32_t old_off;      /* in the old pack */
	uint32_t size;
	uint32_t off;          /* in the new one */
} rp_item;

static void *item_bytes(const rp_item *it, FILE *old, const char *dir)
{
	char p[1100];
	size_t len;
	void *b;

	if (!it->loose) return old ? read_at(old, it->old_off, it->size) : NULL;
	snprintf(p, sizeof p, "%s/%s.png", dir, it->name);
	b = read_png(p, &len);
	if (b && len != it->size) { free(b); b = NULL; }
	return b;
}

static bool sync_dir(const char *dir)
{
	int fd = open(dir, O_RDONLY);

	if (fd < 0) return false;
	fsync(fd);
	close(fd);
	return true;
}

/* One pack written at a time, whichever thread asks (the start's pass, a
 * set's fetch): two at once would write the same .new */
static pthread_mutex_t g_pack_mu = PTHREAD_MUTEX_INITIALIZER;

static int pack_locked(const char *set_path, const char (*names)[RP_NAME], int n, int *took);

int rp_pack(const char *set_path, const char (*names)[RP_NAME], int n, int *took)
{
	int r, t = 0;

	pthread_mutex_lock(&g_pack_mu);
	r = pack_locked(set_path, names, n, &t);
	if (took) *took = r < 0 ? 0 : t;
	pthread_mutex_unlock(&g_pack_mu);
	return r;
}

static int pack_locked(const char *set_path, const char (*names)[RP_NAME], int n, int *took)
{
	static rp_entry old[RP_MAX];
	static rp_item items[RP_MAX];
	char pack[1100], tmp[1110], dir[1100], p[1100];
	FILE *of, *nf = NULL;
	int nold = 0, nitems = 0, nloose = 0, i, k;
	uint32_t at;
	bool ok = false;

	if (!rp_pack_path(set_path, pack, sizeof pack)) return -1;
	snprintf(dir, sizeof dir, "%s", pack);
	*strrchr(dir, '/') = '\0';
	of = fopen(pack, "rb");
	if (of && !read_index(of, old, &nold)) nold = 0;

	/* The loose ones first, each once: they are the newer */
	for (i = 0; i < n && nitems < RP_MAX; i++) {
		size_t len;
		void *b;

		if (!names[i][0] || strchr(names[i], '/')) continue;
		for (k = 0; k < nitems && strcmp(items[k].name, names[i]); k++) {}
		if (k < nitems) continue;
		snprintf(p, sizeof p, "%s/%s.png", dir, names[i]);
		if (!(b = read_png(p, &len))) continue;
		free(b);
		memset(&items[nitems], 0, sizeof items[nitems]);
		snprintf(items[nitems].name, RP_NAME, "%s", names[i]);
		items[nitems].loose = true;
		items[nitems].size = (uint32_t)len;
		nitems++;
	}
	nloose = nitems;
	if (!nloose) {
		if (of) fclose(of);
		return nold;
	}
	/* ...then what the pack holds that no loose one replaces */
	for (i = 0; i < nold && nitems < RP_MAX; i++) {
		for (k = 0; k < nloose && strcmp(items[k].name, old[i].name); k++) {}
		if (k < nloose) continue;
		memset(&items[nitems], 0, sizeof items[nitems]);
		snprintf(items[nitems].name, RP_NAME, "%s", old[i].name);
		items[nitems].old_off = old[i].off;
		items[nitems].size = old[i].size;
		nitems++;
	}

	/* Written beside it */
	snprintf(tmp, sizeof tmp, "%s.new", pack);
	at = RP_HEAD + (uint32_t)nitems * RP_ENTRY;
	for (i = 0; i < nitems; i++) {
		items[i].off = at;
		at += items[i].size;
	}
	if ((nf = fopen(tmp, "wb"))) {
		unsigned char head[RP_HEAD], raw[RP_ENTRY];

		memcpy(head, RP_MAGIC, 4);
		put32(head + 4, 1);
		put32(head + 8, (uint32_t)nitems);
		ok = fwrite(head, 1, RP_HEAD, nf) == RP_HEAD;
		for (i = 0; ok && i < nitems; i++) {
			memset(raw, 0, sizeof raw);
			memcpy(raw, items[i].name, strlen(items[i].name));
			put32(raw + RP_NAME, items[i].off);
			put32(raw + RP_NAME + 4, items[i].size);
			ok = fwrite(raw, 1, RP_ENTRY, nf) == RP_ENTRY;
		}
		for (i = 0; ok && i < nitems; i++) {
			void *b = item_bytes(&items[i], of, dir);

			ok = b && fwrite(b, 1, items[i].size, nf) == items[i].size;
			free(b);
		}
		ok = fflush(nf) == 0 && ok;
		ok = fsync(fileno(nf)) == 0 && ok;
		ok = fclose(nf) == 0 && ok;
	}
	/* Read back whole, each badge against where it came from */
	if (ok) {
		static rp_entry got[RP_MAX];
		FILE *rf = fopen(tmp, "rb");
		int ng = 0;

		ok = rf && read_index(rf, got, &ng) && ng == nitems;
		for (i = 0; ok && i < nitems; i++) {
			void *a = read_at(rf, got[i].off, got[i].size), *b = item_bytes(&items[i], of, dir);

			ok = a && b && !strcmp(got[i].name, items[i].name) && got[i].size == items[i].size &&
			     !memcmp(a, b, items[i].size);
			free(a);
			free(b);
		}
		if (rf) fclose(rf);
	}
	if (of) fclose(of);
	if (!ok || rename(tmp, pack) != 0) {
		remove(tmp);
		fprintf(stderr, "badges: %s not written; the loose badges stay\n", pack);
		return -1;
	}
	sync_dir(dir);
	*took = nloose;
	/* Only now, the pack safe: the loose files it took */
	for (i = 0; i < nloose; i++) {
		snprintf(p, sizeof p, "%s/%s.png", dir, items[i].name);
		unlink(p);
	}
	return nitems;
}
