/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > Storage's numbers: src/storage.h says what and why. */
#include "storage.h"

#include "atomic.h"
#include "gamedel.h"
#include "portinfo.h"

#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef __linux__
#include <sched.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#endif

#define CACHE_NAME  "storage.cache"
#define CACHE_MAGIC "zyperos-storage 1"
#define PATH_MAX_ST 1200

/* ---- small things ---------------------------------------------------------- */

static unsigned long long alloc_of(const struct stat *st)
{
	/* The clusters it holds; a filesystem that does not say has its length */
	if (st->st_blocks > 0) return (unsigned long long)st->st_blocks * 512ull;
	return (unsigned long long)st->st_size;
}

static long long mtime_ns(const struct stat *st)
{
#ifdef __APPLE__
	return (long long)st->st_mtimespec.tv_sec * 1000000000LL + st->st_mtimespec.tv_nsec;
#else
	return (long long)st->st_mtim.tv_sec * 1000000000LL + st->st_mtim.tv_nsec;
#endif
}

static uint64_t fnv(const char *s, uint64_t h)
{
	for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ull; }
	return h;
}

static uint64_t fnv_lower(const char *s)
{
	uint64_t h = 1469598103934665603ull;

	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;

		if (c >= 'A' && c <= 'Z') c = (unsigned char)(c - 'A' + 'a');
		h ^= c;
		h *= 1099511628211ull;
	}
	return h;
}

static bool ends_ci(const char *s, size_t n, const char *suf)
{
	size_t k = strlen(suf);

	return n >= k && !strcasecmp(s + n - k, suf);
}

size_t st_media_suffix(const char *name, bool *video)
{
	/* The longest first: ".png.part" before ".part" would never be asked,
	 * but ".mp4.snap" must not read as ".snap" */
	static const char *const SUF[] = { ".png.part", ".mp4.part", ".mp4.snap", ".png.sgdb",
	                                   ".png.tmp", ".png", ".mp4" };
	size_t n = strlen(name), i;

	for (i = 0; i < sizeof SUF / sizeof SUF[0]; i++)
		if (ends_ci(name, n, SUF[i]) && n > strlen(SUF[i])) {
			if (video) *video = !strncasecmp(SUF[i], ".mp4", 4);
			return strlen(SUF[i]);
		}
	return 0;
}

static bool ext_in(const char *name, const char *const *list)
{
	const char *dot = strrchr(name, '.');

	if (!dot) return false;
	for (; *list; list++)
		if (!strcasecmp(dot + 1, *list)) return true;
	return false;
}

static const char *const AUDIO_EXT[] = { "mp3", "m4a", "m4b", "aac", "flac", "ogg", "opus", "wav", NULL };
static const char *const VIDEO_EXT[] = { "mp4", "m4v", "mkv", "avi", "mov", "webm", NULL };

unsigned long long st_nominal(unsigned long long total)
{
	/* As cards are sold: powers of two to 512 GB, then whole terabytes */
	static const unsigned SOLD[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1000, 1500, 2000, 4000 };
	size_t i;

	for (i = 0; i < sizeof SOLD / sizeof SOLD[0]; i++) {
		unsigned long long p = (unsigned long long)SOLD[i] * 1000 * 1000 * 1000;

		if (total <= p && total * 10 >= p * 9) return p;
	}
	return total;
}

static int g_listed;               /* folders listed by the last refresh */

int st_listed(void)
{
	return g_listed;
}

/* ---- a string map, open addressing, values owned by the caller ------------- */

typedef struct {
	char **keys;
	void **vals;
	size_t cap, n;
} smap;

static void **smap_slot(smap *m, const char *key, bool add)
{
	size_t i;

	if (add && (m->n + 1) * 2 > m->cap) {
		smap o = *m;
		size_t cap = o.cap ? o.cap * 2 : 1024, k;

		m->keys = calloc(cap, sizeof *m->keys);
		m->vals = calloc(cap, sizeof *m->vals);
		if (!m->keys || !m->vals) {
			free(m->keys);
			free(m->vals);
			*m = o;
			return NULL;
		}
		m->cap = cap;
		m->n = 0;
		for (k = 0; k < o.cap; k++)
			if (o.keys[k]) {
				for (i = fnv(o.keys[k], 1469598103934665603ull) & (cap - 1); m->keys[i]; i = (i + 1) & (cap - 1)) {}
				m->keys[i] = o.keys[k];
				m->vals[i] = o.vals[k];
				m->n++;
			}
		free(o.keys);
		free(o.vals);
	}
	if (!m->cap) return NULL;
	for (i = fnv(key, 1469598103934665603ull) & (m->cap - 1); m->keys[i]; i = (i + 1) & (m->cap - 1))
		if (!strcmp(m->keys[i], key)) return &m->vals[i];
	if (!add) return NULL;
	if (!(m->keys[i] = strdup(key))) return NULL;
	m->vals[i] = NULL;
	m->n++;
	return &m->vals[i];
}

/* Every value freed with `fr`, and the map emptied */
static void smap_clear(smap *m, void (*fr)(void *))
{
	size_t i;

	for (i = 0; i < m->cap; i++)
		if (m->keys[i]) {
			if (fr) fr(m->vals[i]);
			free(m->keys[i]);
		}
	free(m->keys);
	free(m->vals);
	memset(m, 0, sizeof *m);
}

/* ---- the folders, as last walked -------------------------------------------- */

typedef struct {
	long long mtime;                 /* ns */
	long listed;                     /* unix s it was listed at */
	unsigned long long own, video;   /* its own files: all, and video snaps among them */
	int audio_n, video_n;            /* its own songs and films */
	char *kids;                      /* its folders' names, each ending in NUL */
	size_t kids_len;
	bool seen;                       /* walked this time */
} dnode;

static void dnode_free(void *v)
{
	dnode *d = v;

	if (d) free(d->kids);
	free(d);
}

/* A game's bytes, and the folders' times they were counted at */
typedef struct {
	st_game_size sz;
	unsigned long long sig;
	bool seen;
} gnode;

/* A system's slower answers: its unused art, against its .media's time and
 * its games' names; its states' games, against its states folder's time;
 * and the time its games were sized at */
typedef struct {
	char *name;
	unsigned long long bytes;
	long newest;
} sbase;

typedef struct {
	char folder[ST_FOLDER];
	long long media_mt;
	uint64_t names_sig;
	bool have_unused;
	unsigned long long unused;
	int ucovers, usnaps;
	long long states_mt;
	bool have_bases;
	sbase *bases;
	int nbases;
	unsigned long long sizes_sig;
} sysc;

/* The walk's own state: only the thread that walks touches it (the map of
 * games, read by st_game, under g_mu) */
static smap g_dirs;
static smap g_games;               /* "folder\tfile" -> gnode */
static sysc g_sc[ST_MAX_SYS];
static int g_nsc;
static unsigned long long g_other_prev;
static bool g_have_prev;
static bool g_cache_read;
static char g_card_root[PATH_MAX_ST];

static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static st_orphan g_orph[ST_ORPHANS];   /* the Ports shelf's, under g_mu */
static int g_norph;
static st_media *g_media[STM_KINDS];   /* the media folders' items, under g_mu */
static int g_nmedia[STM_KINDS];
static st_card g_pub;
static atomic_int g_cancel;          /* the page asks the walk to end */

static sysc *sys_cache(const char *folder)
{
	int i;

	for (i = 0; i < g_nsc; i++)
		if (!strcmp(g_sc[i].folder, folder)) return &g_sc[i];
	if (g_nsc >= ST_MAX_SYS) return NULL;
	memset(&g_sc[g_nsc], 0, sizeof g_sc[g_nsc]);
	snprintf(g_sc[g_nsc].folder, sizeof g_sc[g_nsc].folder, "%s", folder);
	return &g_sc[g_nsc++];
}

static void bases_free(sysc *c)
{
	int i;

	for (i = 0; i < c->nbases; i++) free(c->bases[i].name);
	free(c->bases);
	c->bases = NULL;
	c->nbases = 0;
	c->have_bases = false;
}

/* A path as the cache keeps it: under the card, from it */
static const char *rel_of(const char *abs)
{
	size_t n = strlen(g_card_root);

	if (n && !strncmp(abs, g_card_root, n) && abs[n] == '/') return abs + n + 1;
	return abs;
}

/* List a folder again: its files' bytes and its folders' names */
static dnode *relist(const char *abs, long long mt)
{
	void **slot = smap_slot(&g_dirs, rel_of(abs), true);
	dnode *d;
	DIR *dir;
	struct dirent *e;
	int fd;

	if (!slot) return NULL;
	if (!(d = *slot)) {
		if (!(d = calloc(1, sizeof *d))) return NULL;
		*slot = d;
	}
	g_listed++;
	free(d->kids);
	d->kids = NULL;
	d->kids_len = 0;
	d->own = d->video = 0;
	d->audio_n = d->video_n = 0;
	d->mtime = mt;
	d->listed = (long)time(NULL);
	if (!(dir = opendir(abs))) return d;
	fd = dirfd(dir);
	while ((e = readdir(dir))) {
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (fstatat(fd, e->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			size_t k = strlen(e->d_name) + 1;
			char *nk = realloc(d->kids, d->kids_len + k);

			if (!nk) continue;
			memcpy(nk + d->kids_len, e->d_name, k);
			d->kids = nk;
			d->kids_len += k;
		} else if (S_ISREG(st.st_mode)) {
			bool video = false;
			unsigned long long b = alloc_of(&st);

			d->own += b;
			if (st_media_suffix(e->d_name, &video) && video) d->video += b;
			if (ext_in(e->d_name, AUDIO_EXT)) d->audio_n++;
			else if (ext_in(e->d_name, VIDEO_EXT)) d->video_n++;
		}
	}
	closedir(dir);
	return d;
}

typedef struct {
	unsigned long long bytes, video;
	int audio_n, video_n;
} tsum;

/* A folder and everything in it, from the cache where its time has not
 * moved. `trust` takes the cache as it is, with no look at the card;
 * `force` lists every folder again. `skip_media` leaves out the folder's own
 * .media, which is measured as the system's art. False when the walk was
 * asked to end. */
static bool walk(const char *abs, bool skip_media, bool trust, bool force, tsum *out)
{
	dnode *d;
	char *kids, *k;
	size_t len;

	if (g_cancel) return false;
	if (trust) {
		void **slot = smap_slot(&g_dirs, rel_of(abs), false);

		if (!slot || !(d = *slot)) return true;
	} else {
		struct stat st;
		void **slot;
		long long mt;

		if (lstat(abs, &st) != 0 || !S_ISDIR(st.st_mode)) return true;
		mt = mtime_ns(&st);
		slot = smap_slot(&g_dirs, rel_of(abs), false);
		d = slot ? *slot : NULL;
		/* Listed within ST_RACY_S of its own time: a change in that same
		 * moment could be in the time and not in the listing */
		if (!d || force || d->mtime != mt || d->mtime / 1000000000LL + ST_RACY_S >= d->listed)
			d = relist(abs, mt);
		if (!d) return true;
		d->seen = true;
	}
	out->bytes += d->own;
	out->video += d->video;
	out->audio_n += d->audio_n;
	out->video_n += d->video_n;
	if (!d->kids_len) return true;
	/* A copy: walking a folder below may grow the map, never this node,
	 * but its list is relisted in place when its folder is walked again */
	len = d->kids_len;
	if (!(kids = malloc(len))) return true;
	memcpy(kids, d->kids, len);
	for (k = kids; k < kids + len; k += strlen(k) + 1) {
		char child[PATH_MAX_ST];

		if (skip_media && !strcmp(k, ".media")) continue;
		if (snprintf(child, sizeof child, "%s/%s", abs, k) >= (int)sizeof child) continue;
		if (!walk(child, false, trust, force, out)) { free(kids); return false; }
	}
	free(kids);
	return true;
}

static long long dir_mtime(const char *abs, bool trust)
{
	struct stat st;

	if (trust) {
		void **slot = smap_slot(&g_dirs, rel_of(abs), false);

		return slot && *slot ? ((dnode *)*slot)->mtime : 0;
	}
	return lstat(abs, &st) == 0 ? mtime_ns(&st) : 0;
}

/* Everything in a folder, uncached: a game that is a folder */
static unsigned long long tree_bytes(const char *abs)
{
	unsigned long long sum = 0;
	DIR *dir = opendir(abs);
	struct dirent *e;

	if (!dir) return 0;
	while ((e = readdir(dir))) {
		char p[PATH_MAX_ST];
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (snprintf(p, sizeof p, "%s/%s", abs, e->d_name) >= (int)sizeof p) continue;
		if (lstat(p, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) sum += tree_bytes(p);
		else if (S_ISREG(st.st_mode)) sum += alloc_of(&st);
	}
	closedir(dir);
	return sum;
}

/* A game's folder: from the walk's own listings, just made, when it has
 * them; else read here */
static unsigned long long folder_bytes(const char *abs)
{
	void **slot = smap_slot(&g_dirs, rel_of(abs), false);
	tsum t = { 0 };

	if (!slot || !*slot) return tree_bytes(abs);
	walk(abs, false, true, false, &t);
	return t.bytes;
}

/* ---- the card's size ------------------------------------------------------- */

static void card_size(const char *card, st_card *c)
{
	struct statvfs v;
	unsigned long long fs = 0;

	c->total = c->free = c->partitions = 0;
	if (statvfs(card, &v) == 0) {
		fs = (unsigned long long)v.f_blocks * v.f_frsize;
		c->free = (unsigned long long)v.f_bavail * v.f_frsize;
	}
	c->total = fs;
#ifdef __linux__
	{
		/* The device under the card's filesystem, and the partitions
		 * beside it: the boot and the two system slots */
		struct stat st;
		char p[160], real[PATH_MAX_ST], buf[32];
		unsigned long long part = 0, disk = 0;
		FILE *f;

		if (stat(card, &st) != 0) return;
		snprintf(p, sizeof p, "/sys/dev/block/%u:%u", major(st.st_dev), minor(st.st_dev));
		if (!realpath(p, real)) return;
		snprintf(p, sizeof p, "%.100s/size", real);
		if ((f = fopen(p, "r"))) { if (fgets(buf, sizeof buf, f)) part = strtoull(buf, NULL, 10); fclose(f); }
		snprintf(p, sizeof p, "%.100s/partition", real);
		if (access(p, F_OK) != 0) return;            /* the whole device: nothing beside it */
		snprintf(p, sizeof p, "%.100s/../size", real);
		if ((f = fopen(p, "r"))) { if (fgets(buf, sizeof buf, f)) disk = strtoull(buf, NULL, 10); fclose(f); }
		if (disk > part && part > 0) {
			c->total = disk * 512ull;
			c->partitions = (disk - part) * 512ull;
		}
	}
#endif
}

/* ---- a system's games: names, art, states ---------------------------------- */

typedef struct {
	const st_game_in *g;
	int n;
} glist;

static int ci_cmp(const void *a, const void *b)
{
	return strcasecmp(*(const char *const *)a, *(const char *const *)b);
}

/* Its games' names, sorted for a look-up, and their signature */
static const char **names_of(const char *folder, const st_game_in *g, int n, int *count,
                             uint64_t *sig)
{
	const char **v = malloc((size_t)(n > 0 ? n : 1) * sizeof *v);
	int i, k = 0;
	uint64_t s = 0;

	if (!v) { *count = 0; return NULL; }
	for (i = 0; i < n; i++)
		if (!strcmp(g[i].folder, folder)) {
			v[k++] = g[i].name;
			s += fnv_lower(g[i].name);
		}
	qsort(v, (size_t)k, sizeof *v, ci_cmp);
	*count = k;
	if (sig) *sig = s * 31 + (uint64_t)k;
	return v;
}

static bool has_name(const char **v, int n, const char *name)
{
	return n > 0 && bsearch(&name, v, (size_t)n, sizeof *v, ci_cmp) != NULL;
}

/* Its .media's files of no game on its shelf: counted, or deleted too */
static unsigned long long unused_art(const char *roms, const char *folder,
                                     const char **names, int nn, bool del,
                                     int *covers, int *snaps)
{
	char dir[PATH_MAX_ST];
	DIR *d;
	struct dirent *e;
	unsigned long long sum = 0;

	*covers = *snaps = 0;
	if (snprintf(dir, sizeof dir, "%s/%s/.media", roms, folder) >= (int)sizeof dir) return 0;
	if (!(d = opendir(dir))) return 0;
	while ((e = readdir(d))) {
		char p[PATH_MAX_ST], stem[PATH_MAX_ST];
		struct stat st;
		bool video = false;
		size_t suf, n;

		/* A dotfile is no cover: a Mac's "._" twin, the folder's own */
		if (e->d_name[0] == '.') continue;
		if (!(suf = st_media_suffix(e->d_name, &video))) continue;
		n = strlen(e->d_name) - suf;
		snprintf(stem, sizeof stem, "%.*s", (int)n, e->d_name);
		if (has_name(names, nn, stem)) continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
		if (lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		if (del && unlink(p) != 0) continue;
		sum += alloc_of(&st);
		if (video) (*snaps)++;
		else (*covers)++;
	}
	closedir(d);
	return sum;
}

/* "<base>.<slot>.state", ".bmp" or ".lang" (the language a DS game's Auto was
 * left in, src/dsresume.h): its base's length, 0 when it is not one */
static size_t state_base_len(const char *name)
{
	static const char *const SLOTS[] = { "auto", "1", "2", "3", "4", "5", "6" };
	size_t n = strlen(name), i;

	if (ends_ci(name, n, ".state")) n -= 6;
	else if (ends_ci(name, n, ".bmp")) n -= 4;
	else if (ends_ci(name, n, ".lang")) n -= 5;
	else return 0;
	for (i = 0; i < sizeof SLOTS / sizeof SLOTS[0]; i++) {
		size_t k = strlen(SLOTS[i]);

		if (n > k + 1 && name[n - k - 1] == '.' && !strncasecmp(name + n - k, SLOTS[i], k))
			return n - k - 1;
	}
	return 0;
}

static int base_index(sbase *v, int n, const char *name)
{
	int i;

	for (i = 0; i < n; i++)
		if (!strcasecmp(v[i].name, name)) return i;
	return -1;
}

/* A states folder's games: each one's bytes and its newest file */
static sbase *list_bases(const char *dir, int *count)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	sbase *v = NULL;
	int n = 0, cap = 0;

	*count = 0;
	if (!d) return NULL;
	while ((e = readdir(d))) {
		char p[PATH_MAX_ST], base[PATH_MAX_ST];
		struct stat st;
		size_t bl = state_base_len(e->d_name);
		int at;

		if (!bl || e->d_name[0] == '.') continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
		if (lstat(p, &st) != 0 || !S_ISREG(st.st_mode)) continue;
		snprintf(base, sizeof base, "%.*s", (int)bl, e->d_name);
		if ((at = base_index(v, n, base)) < 0) {
			if (n == cap) {
				sbase *nv = realloc(v, (size_t)(cap ? cap * 2 : 32) * sizeof *v);

				if (!nv) continue;
				v = nv;
				cap = cap ? cap * 2 : 32;
			}
			if (!(v[n].name = strdup(base))) continue;
			v[n].bytes = 0;
			v[n].newest = 0;
			at = n++;
		}
		v[at].bytes += alloc_of(&st);
		if ((long)st.st_mtime > v[at].newest) v[at].newest = (long)st.st_mtime;
	}
	closedir(d);
	*count = n;
	return v;
}

/* Whether a game's states are old: none written for ST_OLD_S, and no game
 * on its shelf that keeps them was played in that time */
static bool base_old(const sbase *b, const char *folder, const st_game_in *g, int n, long now)
{
	int i;

	if (b->newest >= now - ST_OLD_S) return false;
	for (i = 0; i < n; i++)
		if (!strcmp(g[i].folder, folder) &&
		    !strcasecmp(gd_state_base(g[i].file, g[i].name, g[i].hooked), b->name) &&
		    g[i].last_played >= now - ST_OLD_S)
			return false;
	return true;
}

/* ---- a game's bytes ------------------------------------------------------- */

static bool under(const char *path, const char *root)
{
	size_t n = strlen(root);

	return !strncmp(path, root, n) && path[n] == '/';
}

/* A game's stem as Saves/ names it: its file's base without the last
 * extension (src/gamedel.c's stem_of) */
static uint64_t stem_hash(const char *file)
{
	const char *b = strrchr(file, '/'), *dot;
	char stem[PATH_MAX_ST];

	b = b ? b + 1 : file;
	dot = strrchr(b, '.');
	snprintf(stem, sizeof stem, "%.*s", dot && dot != b ? (int)(dot - b) : (int)strlen(b), b);
	return fnv_lower(stem);
}

typedef struct {
	uint64_t h;
	int i;
} hix;

static int hix_cmp(const void *a, const void *b)
{
	const hix *x = a, *y = b;

	return x->h < y->h ? -1 : x->h > y->h;
}

/* The games a plan holds this one against. Of all the card's, only these
 * can share a file with it (src/gamedel.c's shares_*): its own folder's,
 * and other folders' games of its stem, whose saves are its saves. Asked
 * of every game on the card, a plan per game would be the card squared. */
static int others_of(const st_game_in *all, int n, int self, int lo, int hi,
                     const hix *hs, gd_other *out)
{
	const st_game_in *g = &all[self];
	uint64_t h = stem_hash(g->file);
	int no = 0, a = 0, b = n, i;

	for (i = lo; i < hi; i++)
		if (i != self) out[no++] = (gd_other){ all[i].folder, all[i].file, all[i].name, all[i].hooked };
	while (a < b) {
		int m = (a + b) / 2;

		if (hs[m].h < h) a = m + 1;
		else b = m;
	}
	for (i = a; i < n && hs[i].h == h; i++) {
		const st_game_in *o = &all[hs[i].i];

		if (hs[i].i == self || !strcmp(o->folder, g->folder)) continue;
		out[no++] = (gd_other){ o->folder, o->file, o->name, o->hooked };
	}
	return no;
}

static void size_game(const st_paths *p, const st_game_in *g, const gd_other *others, int no,
                      gd_plan *plan, st_game_size *out)
{
	char saves[PATH_MAX_ST], states[PATH_MAX_ST], romdir[PATH_MAX_ST], media[PATH_MAX_ST];
	gd_card card;
	gd_game gg;
	int i;

	memset(out, 0, sizeof *out);
	snprintf(saves, sizeof saves, "%s/Saves", p->card);
	snprintf(states, sizeof states, "%s/.zyperos", p->shared);
	snprintf(romdir, sizeof romdir, "%s/%s", p->roms, g->folder);
	snprintf(media, sizeof media, "%s/.media", romdir);
	card = (gd_card){ p->roms, saves, states };
	gg = (gd_game){ g->folder, g->tag, g->core, g->exts, g->file, g->name };
	if (!gd_plan_make(&card, &gg, others, no, plan) &&
	    plan->why != GD_RUNTIME && plan->why != GD_PORT_UNKNOWN) {
		/* A BIOS set, or a sheet that leads away: the file alone. PortMaster
		 * and a port nothing names folders for are refused too, and their
		 * plans are what they hold: PortMaster's folder, the script alone */
		struct stat st;
		char f[PATH_MAX_ST];

		snprintf(f, sizeof f, "%s/%s", romdir, g->file);
		if (lstat(f, &st) == 0 && S_ISREG(st.st_mode)) out->games = out->bytes = alloc_of(&st);
		return;
	}
	for (i = 0; i < plan->n; i++) {
		struct stat st;
		unsigned long long b = 0;

		if (plan->kind[i] == GD_EMPTY_DIR || lstat(plan->path[i], &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (plan->kind[i] == GD_TREE) b = folder_bytes(plan->path[i]);
		} else if (S_ISREG(st.st_mode)) {
			b = alloc_of(&st);
		}
		if (under(plan->path[i], media))        out->art += b;
		else if (under(plan->path[i], saves))   out->saves += b;
		else if (under(plan->path[i], states))  out->states += b;
		else                                    out->games += b;
		out->bytes += b;
	}
}

/* ---- the cache ------------------------------------------------------------- */

static void cache_path(const st_paths *p, char *out, size_t n)
{
	snprintf(out, n, "%s/%s", p->shared, CACHE_NAME);
}

static bool clean_name(const char *s)
{
	return !strchr(s, '\n') && !strchr(s, '\t');
}

static void cache_write(const st_paths *p)
{
	char path[PATH_MAX_ST];
	FILE *f;
	size_t i;
	int k, j;

	cache_path(p, path, sizeof path);
	if (!(f = atomic_open(path, 0644))) return;
	fprintf(f, "%s\n", CACHE_MAGIC);
	fprintf(f, "C %llu %d\n", g_other_prev, g_have_prev ? 1 : 0);
	for (i = 0; i < g_dirs.cap; i++) {
		dnode *d = g_dirs.vals[i];
		char *kd;
		int nk = 0;
		bool ok;

		if (!g_dirs.keys[i] || !d || !clean_name(g_dirs.keys[i])) continue;
		ok = true;
		for (kd = d->kids; kd && kd < d->kids + d->kids_len; kd += strlen(kd) + 1, nk++)
			if (!clean_name(kd)) ok = false;
		/* A folder with a name the file cannot hold is listed again */
		if (!ok) continue;
		fprintf(f, "D %lld %ld %llu %llu %d %d %d\t%s\n", d->mtime, d->listed, d->own, d->video,
		        d->audio_n, d->video_n, nk, g_dirs.keys[i]);
		for (kd = d->kids; kd && kd < d->kids + d->kids_len; kd += strlen(kd) + 1)
			fprintf(f, "K\t%s\n", kd);
	}
	for (i = 0; i < g_games.cap; i++) {
		gnode *g = g_games.vals[i];

		if (!g_games.keys[i] || !g || strchr(g_games.keys[i], '\n')) continue;
		fprintf(f, "G %llu %llu %llu %llu %llu %llu\t%s\n", g->sz.bytes, g->sz.games, g->sz.saves,
		        g->sz.states, g->sz.art, g->sig, g_games.keys[i]);
	}
	for (k = 0; k < g_nsc; k++) {
		sysc *c = &g_sc[k];

		fprintf(f, "Y %llu\t%s\n", c->sizes_sig, c->folder);
		if (c->have_unused)
			fprintf(f, "U %lld %llu %llu %d %d\t%s\n", c->media_mt, (unsigned long long)c->names_sig,
			        c->unused, c->ucovers, c->usnaps, c->folder);
		if (c->have_bases) {
			int nb = 0;

			for (j = 0; j < c->nbases; j++) if (clean_name(c->bases[j].name)) nb++;
			fprintf(f, "S %lld %d\t%s\n", c->states_mt, nb, c->folder);
			for (j = 0; j < c->nbases; j++)
				if (clean_name(c->bases[j].name))
					fprintf(f, "B %llu %ld\t%s\n", c->bases[j].bytes, c->bases[j].newest, c->bases[j].name);
		}
	}
	if (ferror(f)) atomic_abort(f, path);
	else atomic_commit(f, path);
}

static void forget_all(void)
{
	int k;

	smap_clear(&g_dirs, dnode_free);
	pthread_mutex_lock(&g_mu);
	smap_clear(&g_games, free);
	pthread_mutex_unlock(&g_mu);
	for (k = 0; k < g_nsc; k++) bases_free(&g_sc[k]);
	g_nsc = 0;
	g_have_prev = false;
	g_other_prev = 0;
}

/* The cache into memory. Anything it does not understand drops all of it:
 * a wrong number is worse than a walk. */
static bool cache_read(const st_paths *p)
{
	char path[PATH_MAX_ST], *line = NULL;
	size_t cap = 0;
	ssize_t len;
	FILE *f;
	dnode *d = NULL;
	int kids_left = 0, bases_left = 0;
	sysc *sc = NULL;
	bool ok = true, first = true;

	cache_path(p, path, sizeof path);
	if (!(f = fopen(path, "r"))) return false;
	while (ok && (len = getline(&line, &cap, f)) > 0) {
		char *tab;

		if (line[len - 1] == '\n') line[--len] = '\0';
		if (first) { ok = !strcmp(line, CACHE_MAGIC); first = false; continue; }
		tab = strchr(line, '\t');
		if (kids_left > 0) {
			size_t k;
			char *nk;

			if (line[0] != 'K' || !tab) { ok = false; break; }
			k = strlen(tab + 1) + 1;
			if (!(nk = realloc(d->kids, d->kids_len + k))) { ok = false; break; }
			memcpy(nk + d->kids_len, tab + 1, k);
			d->kids = nk;
			d->kids_len += k;
			kids_left--;
			continue;
		}
		if (bases_left > 0) {
			sbase *b;

			if (line[0] != 'B' || !tab) { ok = false; break; }
			b = &sc->bases[sc->nbases];
			if (sscanf(line, "B %llu %ld", &b->bytes, &b->newest) != 2 || !(b->name = strdup(tab + 1))) {
				ok = false;
				break;
			}
			sc->nbases++;
			if (--bases_left == 0) sc->have_bases = true;
			continue;
		}
		if (line[0] == 'C') {
			int have = 0;

			ok = sscanf(line, "C %llu %d", &g_other_prev, &have) == 2;
			g_have_prev = have != 0;
		} else if (line[0] == 'D' && tab) {
			void **slot;

			if (!(slot = smap_slot(&g_dirs, tab + 1, true)) || !(d = calloc(1, sizeof *d))) { ok = false; break; }
			dnode_free(*slot);
			*slot = d;
			ok = sscanf(line, "D %lld %ld %llu %llu %d %d %d", &d->mtime, &d->listed, &d->own, &d->video,
			            &d->audio_n, &d->video_n, &kids_left) == 7 && kids_left >= 0;
		} else if (line[0] == 'G' && tab) {
			gnode *g = calloc(1, sizeof *g);
			void **slot;

			if (!g || sscanf(line, "G %llu %llu %llu %llu %llu %llu", &g->sz.bytes, &g->sz.games,
			                 &g->sz.saves, &g->sz.states, &g->sz.art, &g->sig) != 6) {
				free(g);
				ok = false;
				break;
			}
			pthread_mutex_lock(&g_mu);
			slot = smap_slot(&g_games, tab + 1, true);
			if (slot) { free(*slot); *slot = g; }
			pthread_mutex_unlock(&g_mu);
			if (!slot) { free(g); ok = false; }
		} else if (line[0] == 'Y' && tab) {
			ok = (sc = sys_cache(tab + 1)) && sscanf(line, "Y %llu", &sc->sizes_sig) == 1;
		} else if (line[0] == 'U' && tab) {
			unsigned long long sig;

			ok = (sc = sys_cache(tab + 1)) &&
			     sscanf(line, "U %lld %llu %llu %d %d", &sc->media_mt, &sig, &sc->unused,
			            &sc->ucovers, &sc->usnaps) == 5;
			if (ok) { sc->names_sig = sig; sc->have_unused = true; }
		} else if (line[0] == 'S' && tab) {
			ok = (sc = sys_cache(tab + 1)) && sscanf(line, "S %lld %d", &sc->states_mt, &bases_left) == 2 &&
			     bases_left >= 0;
			if (ok) {
				bases_free(sc);
				if (bases_left == 0) sc->have_bases = true;
				else ok = (sc->bases = calloc((size_t)bases_left, sizeof *sc->bases)) != NULL;
			}
		} else {
			ok = false;
		}
	}
	free(line);
	fclose(f);
	if (!ok || kids_left || bases_left) {
		fprintf(stderr, "storage: %s is not one this reads; measuring the card again\n", path);
		forget_all();
		return false;
	}
	return true;
}

/* ---- one refresh ----------------------------------------------------------- */

static void publish(const st_card *c)
{
	unsigned gen;

	pthread_mutex_lock(&g_mu);
	gen = g_pub.gen;
	g_pub = *c;
	g_pub.gen = gen + 1;
	pthread_mutex_unlock(&g_mu);
}

void st_totals(st_card *c)
{
	unsigned long long used, measured = 0;
	int i, k;

	memset(c->cat, 0, sizeof c->cat);
	c->unused_art = c->old_states = 0;
	c->unused_covers = c->unused_snaps = c->old_state_games = 0;
	for (i = 0; i < c->nsys; i++) {
		c->cat[STC_GAMES] += c->sys[i].games;
		c->cat[STC_ART] += c->sys[i].covers + c->sys[i].snaps;
		c->unused_art += c->sys[i].unused_art;
		c->unused_covers += c->sys[i].unused_covers;
		c->unused_snaps += c->sys[i].unused_snaps;
		c->old_states += c->sys[i].old_states;
		c->old_state_games += c->sys[i].old_state_games;
	}
	c->cat[STC_SAVES] = c->saves + c->states;
	c->cat[STC_MEDIA] = c->music + c->videos + c->books + c->bgm;
	c->cat[STC_ZYPEROS] = c->zyperos + c->emulators + c->partitions;
	for (k = 0; k < STC_OTHER; k++) measured += c->cat[k];
	used = c->total > c->free ? c->total - c->free : 0;
	c->cat[STC_OTHER] = used > measured ? used - measured : 0;
}

typedef struct {
	st_paths p;
	st_systems s;
	const st_game_in *g;
	int n;
	long now;
} job_view;

static unsigned long long root_bytes(const char *abs, bool trust, bool force, bool *cut, tsum *ts)
{
	tsum t = { 0 };

	if (!walk(abs, false, trust, force, &t)) *cut = true;
	if (ts) *ts = t;
	return t.bytes;
}

/* The folders the Ports shelf's scripts name (src/portinfo.h), and always
 * PortMaster's: none of them is an orphan, and none is one to delete */
static int ports_claimed(const char *dir, const st_game_in *g, int n, const char *folder,
                         char (*out)[PI_NAME], int max)
{
	pi_port *ports = malloc(PI_MAX * sizeof *ports);
	int np = ports ? pi_scan(dir, ports, PI_MAX) : 0, m = 0, k;

	if (max > 0) snprintf(out[m++], PI_NAME, "PortMaster");
	for (k = 0; k < n && ports && m < max; k++) {
		const char *slash = strchr(g[k].file, '/');

		if (strcmp(g[k].folder, folder)) continue;
		/* A folder the shelf lists as a game of its own is that game's */
		if (slash) snprintf(out[m++], PI_NAME, "%.*s", (int)(slash - g[k].file), g[k].file);
		else m += pi_folders(dir, ports, np, g[k].file, out + m, max - m);
	}
	free(ports);
	return m;
}

static bool claimed_by(char (*claimed)[PI_NAME], int n, const char *name)
{
	int i;

	for (i = 0; i < n; i++)
		if (!strcasecmp(claimed[i], name)) return true;
	return false;
}

/* The Ports shelf's folders no script on it names, from the walk's own
 * listings, titled by their port.json */
static void ports_orphans(const job_view *j)
{
	char dir[PATH_MAX_ST], (*claimed)[PI_NAME];
	pi_port *ports;
	st_orphan found[ST_ORPHANS];
	void **slot;
	dnode *d;
	char *kids = NULL, *k;
	size_t len = 0;
	int nc, np, nf = 0;

	snprintf(dir, sizeof dir, "%s/%s", j->p.roms, j->s.ports);
	claimed = malloc(PI_MAX * sizeof *claimed);
	ports = malloc(PI_MAX * sizeof *ports);
	if (!claimed || !ports) { free(claimed); free(ports); return; }
	nc = ports_claimed(dir, j->g, j->n, j->s.ports, claimed, PI_MAX);
	np = pi_scan(dir, ports, PI_MAX);
	if ((slot = smap_slot(&g_dirs, rel_of(dir), false)) && (d = *slot) && d->kids_len &&
	    (kids = malloc(d->kids_len))) {
		len = d->kids_len;
		memcpy(kids, d->kids, len);
	}
	for (k = kids; k && k < kids + len && nf < ST_ORPHANS; k += strlen(k) + 1) {
		char sub[PATH_MAX_ST];
		const pi_port *pt;
		tsum t = { 0 };

		if (k[0] == '.' || claimed_by(claimed, nc, k)) continue;
		snprintf(sub, sizeof sub, "%s/%s", dir, k);
		walk(sub, false, true, false, &t);
		memset(&found[nf], 0, sizeof found[nf]);
		snprintf(found[nf].folder, sizeof found[nf].folder, "%s", k);
		if ((pt = pi_of_folder(ports, np, k))) snprintf(found[nf].title, sizeof found[nf].title, "%s", pt->title);
		found[nf].bytes = t.bytes;
		nf++;
	}
	free(kids);
	free(claimed);
	free(ports);
	pthread_mutex_lock(&g_mu);
	memcpy(g_orph, found, (size_t)nf * sizeof *found);
	g_norph = nf;
	pthread_mutex_unlock(&g_mu);
}

const char *st_media_root(st_media_kind k)
{
	return k == STM_MUSIC ? "Music" : k == STM_VIDEOS ? "Videos" : "Audiobooks";
}

static bool media_add(st_media **v, int *n, int *cap, const char *rel, const char *name,
                      const char *parent, unsigned long long bytes, int count, bool folder)
{
	st_media *m;

	if (*n == *cap) {
		int nc = *cap ? *cap * 2 : 64;
		st_media *nv = realloc(*v, (size_t)nc * sizeof *nv);

		if (!nv) return false;
		*v = nv;
		*cap = nc;
	}
	m = &(*v)[*n];
	memset(m, 0, sizeof *m);
	/* A path that does not fit is no item: cut short it would name another
	 * file or folder, and st_delete_media deletes what an item names */
	if (snprintf(m->rel, sizeof m->rel, "%s", rel) >= (int)sizeof m->rel) return false;
	(*n)++;
	snprintf(m->name, sizeof m->name, "%s", name);
	snprintf(m->parent, sizeof m->parent, "%s", parent);
	m->bytes = bytes;
	m->count = count;
	m->folder = folder;
	return true;
}

/* A name without its last extension */
static void bare(const char *name, char *out, size_t n)
{
	const char *dot = strrchr(name, '.');

	snprintf(out, n, "%.*s", dot && dot != name ? (int)(dot - name) : (int)strlen(name), name);
}

/* A folder's dnode, as the walk left it: NULL when it has none */
static dnode *dir_node(const char *abs)
{
	void **slot = smap_slot(&g_dirs, rel_of(abs), false);

	return slot ? *slot : NULL;
}

/* Whether a folder has a folder inside that is not hidden */
static bool has_folders(const dnode *dn)
{
	const char *kd;

	for (kd = dn ? dn->kids : NULL; kd && kd < dn->kids + dn->kids_len; kd += strlen(kd) + 1)
		if (kd[0] != '.') return true;
	return false;
}

/* One media folder's items: its top read here, every folder's bytes from
 * the walk's listings (the root was walked just before), so the cache's
 * pass has them as the card's does */
static void media_items(const char *card, st_media_kind k)
{
	char rootp[PATH_MAX_ST];
	st_media *v = NULL;
	int n = 0, cap = 0;
	DIR *d;
	struct dirent *e;
	bool video = k == STM_VIDEOS;

	snprintf(rootp, sizeof rootp, "%s/%s", card, st_media_root(k));
	if ((d = opendir(rootp))) {
		while ((e = readdir(d)) && !g_cancel) {
			char p[PATH_MAX_ST], nm[ST_FOLDER];
			struct stat st;

			if (e->d_name[0] == '.') continue;
			if (snprintf(p, sizeof p, "%s/%s", rootp, e->d_name) >= (int)sizeof p) continue;
			if (lstat(p, &st) != 0) continue;
			if (S_ISREG(st.st_mode)) {
				/* A track or a film at the top; any other file is the rest */
				if (!(video ? ext_in(e->d_name, VIDEO_EXT) : ext_in(e->d_name, AUDIO_EXT))) continue;
				bare(e->d_name, nm, sizeof nm);
				media_add(&v, &n, &cap, e->d_name, nm, "", alloc_of(&st), 1, false);
			} else if (S_ISDIR(st.st_mode)) {
				dnode *dn = dir_node(p);
				char *kd;

				/* An artist's or an author's folder - folders and no track of
				 * its own - holds albums or books; any other folder at the top
				 * is one, as each of Videos' is */
				if (!video && dn && dn->audio_n == 0 && has_folders(dn)) {
					size_t len = dn->kids_len;
					char *kids = malloc(len);

					if (!kids) continue;
					memcpy(kids, dn->kids, len);
					for (kd = kids; kd < kids + len; kd += strlen(kd) + 1) {
						char sub[PATH_MAX_ST], rel[2 * ST_FOLDER + 2];
						tsum t = { 0 };

						if (kd[0] == '.') continue;
						if (snprintf(sub, sizeof sub, "%s/%s", p, kd) >= (int)sizeof sub) continue;
						walk(sub, false, true, false, &t);
						if (snprintf(rel, sizeof rel, "%s/%s", e->d_name, kd) >= (int)sizeof rel) continue;
						media_add(&v, &n, &cap, rel, kd, e->d_name, t.bytes, t.audio_n, true);
					}
					free(kids);
				} else {
					tsum t = { 0 };

					walk(p, false, true, false, &t);
					media_add(&v, &n, &cap, e->d_name, e->d_name, "", t.bytes,
					          video ? t.video_n : t.audio_n, true);
				}
			}
		}
		closedir(d);
	}
	pthread_mutex_lock(&g_mu);
	free(g_media[k]);
	g_media[k] = v;
	g_nmedia[k] = n;
	pthread_mutex_unlock(&g_mu);
}

/* Everything, once: from the cache alone with `trust`, else from the card.
 * Publishes as it goes when `live`. False when it was asked to end. */
static bool measure(const job_view *j, st_card *c, bool trust, bool force, bool live)
{
	char path[PATH_MAX_ST], saves[PATH_MAX_ST], states[PATH_MAX_ST];
	bool cut = false;
	gd_plan *plan = NULL;
	gd_other *others = NULL;
	hix *hs = NULL;
	tsum ts;
	int i, k;

	snprintf(saves, sizeof saves, "%s/Saves", j->p.card);
	snprintf(states, sizeof states, "%s/.zyperos", j->p.shared);
	if (!trust) card_size(j->p.card, c);
	c->nsys = 0;
	for (i = 0; i < j->s.n && i < ST_MAX_SYS; i++) {
		memset(&c->sys[i], 0, sizeof c->sys[i]);
		snprintf(c->sys[i].folder, sizeof c->sys[i].folder, "%s", j->s.folders[i]);
	}
	c->nsys = i;

	/* The roots that are not a system's */
	snprintf(path, sizeof path, "%s/Music", j->p.card);
	c->music = root_bytes(path, trust, force, &cut, &ts);
	c->nmusic = ts.audio_n;
	snprintf(path, sizeof path, "%s/Videos", j->p.card);
	c->videos = root_bytes(path, trust, force, &cut, &ts);
	c->nvideos = ts.video_n;
	snprintf(path, sizeof path, "%s/Audiobooks", j->p.card);
	c->books = root_bytes(path, trust, force, &cut, NULL);
	{
		/* A book is a folder or a file at the top of Audiobooks */
		void **slot = smap_slot(&g_dirs, rel_of(path), false);
		dnode *d = slot ? *slot : NULL;
		char *kd;
		int nb = 0;

		if (d) {
			for (kd = d->kids; kd && kd < d->kids + d->kids_len; kd += strlen(kd) + 1)
				if (kd[0] != '.') nb++;
			nb += d->audio_n;
		}
		c->nbooks = nb;
	}
	snprintf(path, sizeof path, "%s/BGM", j->p.roms);
	c->bgm = root_bytes(path, trust, force, &cut, &ts);
	c->nbgm = ts.audio_n;
	/* The media pages' items, now the roots are walked */
	for (i = 0; i < STM_KINDS && !cut; i++) media_items(j->p.card, (st_media_kind)i);
	snprintf(path, sizeof path, "%s/Bios", j->p.card);
	c->bios = root_bytes(path, trust, force, &cut, NULL);
	c->zyperos = root_bytes(j->p.root, trust, force, &cut, NULL);
	snprintf(path, sizeof path, "%s/Emulators", j->p.card);
	c->emulators = root_bytes(path, trust, force, &cut, NULL);
	c->saves = root_bytes(saves, trust, force, &cut, NULL);
	c->states = root_bytes(states, trust, force, &cut, NULL);
	if (cut) return false;
	if (live && !c->measured) { st_totals(c); publish(c); }

	/* Each system's folders */
	for (i = 0; i < c->nsys; i++) {
		st_sys *s = &c->sys[i];
		tsum t = { 0 };

		/* The first walk says where it is */
		snprintf(c->at, sizeof c->at, "%s", s->folder);
		c->at_done = 0;
		for (k = 0, c->at_total = 0; k < j->n; k++) if (!strcmp(j->g[k].folder, s->folder)) c->at_total++;
		snprintf(path, sizeof path, "%s/%s", j->p.roms, s->folder);
		if (!walk(path, true, trust, force, &t)) return false;
		s->games = t.bytes;
		memset(&t, 0, sizeof t);
		snprintf(path, sizeof path, "%s/%s/.media", j->p.roms, s->folder);
		if (!walk(path, false, trust, force, &t)) return false;
		s->snaps = t.video;
		s->covers = t.bytes - t.video;
		memset(&t, 0, sizeof t);
		snprintf(path, sizeof path, "%s/%s", states, s->folder);
		if (!walk(path, false, trust, force, &t)) return false;
		s->states = t.bytes;
		if (live && !c->measured) { st_totals(c); publish(c); }
	}

	/* Each system's unused art and old states, against its folders' times */
	for (i = 0; i < c->nsys; i++) {
		st_sys *s = &c->sys[i];
		sysc *sc = sys_cache(s->folder);
		const char **names;
		int nn, b;
		uint64_t sig;
		long long mt;

		if (g_cancel) return false;
		if (!sc) continue;
		names = names_of(s->folder, j->g, j->n, &nn, &sig);
		snprintf(path, sizeof path, "%s/%s/.media", j->p.roms, s->folder);
		mt = dir_mtime(path, trust);
		if (!trust && (!sc->have_unused || sc->media_mt != mt || sc->names_sig != sig || force)) {
			sc->unused = unused_art(j->p.roms, s->folder, names, nn, false, &sc->ucovers, &sc->usnaps);
			sc->media_mt = mt;
			sc->names_sig = sig;
			sc->have_unused = true;
		}
		free(names);
		if (sc->have_unused) {
			s->unused_art = sc->unused;
			s->unused_covers = sc->ucovers;
			s->unused_snaps = sc->usnaps;
		}
		snprintf(path, sizeof path, "%s/%s", states, s->folder);
		mt = dir_mtime(path, trust);
		if (!trust && (!sc->have_bases || sc->states_mt != mt || force)) {
			bases_free(sc);
			sc->bases = list_bases(path, &sc->nbases);
			sc->states_mt = mt;
			sc->have_bases = true;
		}
		for (b = 0; b < sc->nbases; b++)
			if (base_old(&sc->bases[b], s->folder, j->g, j->n, j->now)) {
				s->old_states += sc->bases[b].bytes;
				s->old_state_games++;
			}
	}

	/* Each game's bytes: again only for a system whose folders' times moved */
	if (!trust && j->n > 0) {
		plan = malloc(sizeof *plan);
		others = malloc((size_t)j->n * sizeof *others);
		if ((hs = malloc((size_t)j->n * sizeof *hs))) {
			for (k = 0; k < j->n; k++) hs[k] = (hix){ stem_hash(j->g[k].file), k };
			qsort(hs, (size_t)j->n, sizeof *hs, hix_cmp);
		}
	}
	for (i = 0; i < c->nsys; i++) {
		st_sys *s = &c->sys[i];
		sysc *sc = sys_cache(s->folder);
		unsigned long long sig;
		int total = 0, done = 0, lo = -1, hi = -1;

		if (!sc) continue;
		snprintf(path, sizeof path, "%s/%s", j->p.roms, s->folder);
		sig = (unsigned long long)dir_mtime(path, trust) * 31u;
		snprintf(path, sizeof path, "%s/%s/.media", j->p.roms, s->folder);
		sig = (sig ^ (unsigned long long)dir_mtime(path, trust)) * 31u;
		snprintf(path, sizeof path, "%s/%s", states, s->folder);
		sig = (sig ^ (unsigned long long)dir_mtime(path, trust)) * 31u;
		sig ^= (unsigned long long)dir_mtime(saves, trust);
		/* Its games are together in the list, as the shelves give them */
		for (k = 0; k < j->n; k++)
			if (!strcmp(j->g[k].folder, s->folder)) {
				if (lo < 0) lo = k;
				hi = k + 1;
				total++;
			}
		snprintf(c->at, sizeof c->at, "%s", s->folder);
		c->at_total = total;
		c->at_done = 0;
		for (k = 0; k < j->n; k++) {
			const st_game_in *g = &j->g[k];
			char key[PATH_MAX_ST];
			void **slot;
			gnode *gn;
			unsigned long long gsig = sig;
			const char *slash;

			if (strcmp(g->folder, s->folder)) continue;
			if (g_cancel) { free(plan); free(others); free(hs); return false; }
			snprintf(key, sizeof key, "%s\t%s", g->folder, g->file);
			/* A game that is a folder moves with its own folder too */
			if ((slash = strchr(g->file, '/'))) {
				char gdir[PATH_MAX_ST];

				snprintf(gdir, sizeof gdir, "%s/%s/%.*s", j->p.roms, g->folder, (int)(slash - g->file), g->file);
				gsig = (gsig * 31u) ^ (unsigned long long)dir_mtime(gdir, trust);
			}
			pthread_mutex_lock(&g_mu);
			slot = smap_slot(&g_games, key, false);
			gn = slot ? *slot : NULL;
			pthread_mutex_unlock(&g_mu);
			/* A port writes its saves and settings into its own folders,
			 * whose times no signature here follows: its size is made
			 * again each walk, from listings the walk has just made */
			if (!trust && plan && others && hs &&
			    (!gn || gn->sig != gsig || force || (j->s.ports && !strcmp(s->folder, j->s.ports)))) {
				st_game_size sz;
				int no = others_of(j->g, j->n, k, lo, hi, hs, others);

				size_game(&j->p, g, others, no, plan, &sz);
				pthread_mutex_lock(&g_mu);
				slot = smap_slot(&g_games, key, true);
				if (slot && !*slot) *slot = calloc(1, sizeof(gnode));
				gn = slot ? *slot : NULL;
				if (gn) { gn->sz = sz; gn->sig = gsig; }
				pthread_mutex_unlock(&g_mu);
			}
			if (gn) {
				gn->seen = true;
				s->saves += gn->sz.saves;
				done++;
			}
			c->at_done++;
			if (live && !c->measured && (c->at_done % 16) == 0) publish(c);
		}
		s->games_sized = done;
		sc->sizes_sig = sig;
		if (live && !c->measured) { st_totals(c); publish(c); }
	}
	free(plan);
	free(others);
	free(hs);
	if (!trust && j->s.ports) ports_orphans(j);
	c->at[0] = '\0';
	c->at_done = c->at_total = 0;
	st_totals(c);
	return true;
}

/* Drop what this walk did not see: folders and games no longer there */
static void prune(void)
{
	smap keep = { 0 };
	size_t i;

	for (i = 0; i < g_dirs.cap; i++) {
		dnode *d = g_dirs.vals[i];
		void **slot;

		if (!g_dirs.keys[i]) continue;
		if (d && d->seen && (slot = smap_slot(&keep, g_dirs.keys[i], true))) {
			d->seen = false;
			*slot = d;
		} else {
			dnode_free(d);
		}
	}
	smap_clear(&g_dirs, NULL);
	g_dirs = keep;
	memset(&keep, 0, sizeof keep);
	pthread_mutex_lock(&g_mu);
	for (i = 0; i < g_games.cap; i++) {
		gnode *g = g_games.vals[i];
		void **slot;

		if (!g_games.keys[i]) continue;
		if (g && g->seen && (slot = smap_slot(&keep, g_games.keys[i], true))) {
			g->seen = false;
			*slot = g;
		} else {
			free(g);
		}
	}
	smap_clear(&g_games, NULL);
	g_games = keep;
	pthread_mutex_unlock(&g_mu);
}

static void refresh(const job_view *j, bool use_cache)
{
	st_card c;
	bool done;

	snprintf(g_card_root, sizeof g_card_root, "%s", j->p.card);
	g_listed = 0;
	pthread_mutex_lock(&g_mu);
	c = g_pub;
	pthread_mutex_unlock(&g_mu);
	if (use_cache && !g_cache_read) {
		g_cache_read = true;
		if (cache_read(&j->p)) {
			st_card t;

			memset(&t, 0, sizeof t);
			card_size(j->p.card, &t);
			c = t;
			if (measure(j, &c, true, false, false)) {
				c.measured = true;
				c.walking = true;
				publish(&c);
			}
		}
	}
	/* With nothing cached, the card's used and free are known at once */
	if (!c.measured) card_size(j->p.card, &c);
	c.walking = true;
	publish(&c);
	done = measure(j, &c, false, false, true);
	if (done) {
		/* Other moved more than the folders explain: something changed
		 * that their times did not show. Everything again, listed afresh. */
		if (j->p.own_fs && g_have_prev && (c.cat[STC_OTHER] > g_other_prev + ST_UNEXPLAINED ||
		                    g_other_prev > c.cat[STC_OTHER] + ST_UNEXPLAINED)) {
			fprintf(stderr, "storage: Other moved %llu to %llu; listing every folder again\n",
			        g_other_prev, c.cat[STC_OTHER]);
			done = measure(j, &c, false, true, true);
		}
	}
	if (done) {
		prune();
		g_other_prev = c.cat[STC_OTHER];
		g_have_prev = true;
		c.measured = true;
	}
	c.walking = false;
	publish(&c);
	if (use_cache) cache_write(&j->p);
}

/* ---- the thread ------------------------------------------------------------ */

typedef struct {
	job_view v;
	char *strs;          /* every string the view points at, in one block */
	st_game_in *games;
	bool use_cache;
} job;

static pthread_t g_th;
static bool g_th_on, g_th_done;

static void job_free(job *jb)
{
	if (!jb) return;
	free(jb->strs);
	free(jb->games);
	free(jb);
}

/* Everything the walk will read, copied: the shelf can change under it */
static job *job_make(const st_paths *p, const st_systems *s, const st_game_in *g, int n, long now)
{
	job *jb = calloc(1, sizeof *jb);
	size_t need = 0, at = 0;
	int i;
	char *b;

#define LEN(x) (need += strlen((x) ? (x) : "") + 1)
#define PUT(dst, x) do { size_t k_ = strlen((x) ? (x) : "") + 1; memcpy(b + at, (x) ? (x) : "", k_); (dst) = b + at; at += k_; } while (0)
	if (!jb) return NULL;
	LEN(p->card); LEN(p->roms); LEN(p->shared); LEN(p->root);
	for (i = 0; i < s->n && i < ST_MAX_SYS; i++) LEN(s->folders[i]);
	if (s->ports) LEN(s->ports);
	for (i = 0; i < n; i++) {
		LEN(g[i].folder); LEN(g[i].tag); LEN(g[i].core); LEN(g[i].exts); LEN(g[i].file); LEN(g[i].name);
	}
	if (!(b = jb->strs = malloc(need ? need : 1)) ||
	    (n > 0 && !(jb->games = malloc((size_t)n * sizeof *jb->games)))) {
		job_free(jb);
		return NULL;
	}
	PUT(jb->v.p.card, p->card); PUT(jb->v.p.roms, p->roms);
	PUT(jb->v.p.shared, p->shared); PUT(jb->v.p.root, p->root);
	jb->v.p.own_fs = p->own_fs;
	for (i = 0; i < s->n && i < ST_MAX_SYS; i++) PUT(jb->v.s.folders[i], s->folders[i]);
	jb->v.s.n = i;
	if (s->ports) PUT(jb->v.s.ports, s->ports);
	for (i = 0; i < n; i++) {
		st_game_in *o = &jb->games[i];

		*o = g[i];
		PUT(o->folder, g[i].folder); PUT(o->tag, g[i].tag); PUT(o->core, g[i].core);
		PUT(o->exts, g[i].exts); PUT(o->file, g[i].file); PUT(o->name, g[i].name);
	}
#undef LEN
#undef PUT
	jb->v.g = jb->games;
	jb->v.n = n;
	jb->v.now = now;
	return jb;
}

static void *worker(void *arg)
{
	job *jb = arg;

#ifdef __linux__
	/* Behind everything: a frame never waits on this thread, nor a read of
	 * the card on its reads */
	{
		struct sched_param sp = { 0 };

		pthread_setschedparam(pthread_self(), SCHED_IDLE, &sp);
		setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 19);
#ifdef SYS_ioprio_set
		syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, (int)syscall(SYS_gettid),
		        (3 << 13) /* IOPRIO_CLASS_IDLE */);
#endif
	}
#endif
	refresh(&jb->v, jb->use_cache);
	job_free(jb);
	pthread_mutex_lock(&g_mu);
	g_th_done = true;
	pthread_mutex_unlock(&g_mu);
	return NULL;
}

void st_refresh(const st_paths *p, const st_systems *s, const st_game_in *games, int n, long now)
{
	job *jb;
	bool done;

	pthread_mutex_lock(&g_mu);
	done = g_th_done;
	pthread_mutex_unlock(&g_mu);
	if (g_th_on && !done) return;
	if (g_th_on) { pthread_join(g_th, NULL); g_th_on = false; }
	if (!(jb = job_make(p, s, games, n, now))) return;
	jb->use_cache = true;
	g_cancel = 0;
	g_th_done = false;
	if (pthread_create(&g_th, NULL, worker, jb) != 0) { job_free(jb); return; }
	g_th_on = true;
}

void st_stop(void)
{
	if (!g_th_on) return;
	g_cancel = 1;
	pthread_join(g_th, NULL);
	g_th_on = false;
	g_cancel = 0;
}

void st_snapshot(st_card *out)
{
	pthread_mutex_lock(&g_mu);
	*out = g_pub;
	pthread_mutex_unlock(&g_mu);
}

bool st_game(const char *folder, const char *file, st_game_size *out)
{
	char key[PATH_MAX_ST];
	void **slot;
	bool ok = false;

	snprintf(key, sizeof key, "%s\t%s", folder, file);
	pthread_mutex_lock(&g_mu);
	if ((slot = smap_slot(&g_games, key, false)) && *slot) {
		*out = ((gnode *)*slot)->sz;
		ok = true;
	}
	pthread_mutex_unlock(&g_mu);
	return ok;
}

void st_reset(void)
{
	st_stop();
	forget_all();
	g_cache_read = false;
	pthread_mutex_lock(&g_mu);
	memset(&g_pub, 0, sizeof g_pub);
	for (int k = 0; k < STM_KINDS; k++) {
		free(g_media[k]);
		g_media[k] = NULL;
		g_nmedia[k] = 0;
	}
	pthread_mutex_unlock(&g_mu);
}

void st_measure_now(const st_paths *p, const st_systems *s, const st_game_in *games, int n,
                    long now, bool use_cache)
{
	job *jb;

	st_stop();
	if (!(jb = job_make(p, s, games, n, now))) return;
	refresh(&jb->v, use_cache);
	job_free(jb);
}

/* ---- the recommendations' work --------------------------------------------- */

unsigned long long st_delete_unused_art(const st_paths *p, const st_systems *s,
                                        const st_game_in *games, int n, int *files)
{
	unsigned long long sum = 0;
	int i;

	*files = 0;
	for (i = 0; i < s->n; i++) {
		int nn, cv = 0, sn = 0;
		const char **names = names_of(s->folders[i], games, n, &nn, NULL);

		sum += unused_art(p->roms, s->folders[i], names, nn, true, &cv, &sn);
		*files += cv + sn;
		free(names);
	}
	return sum;
}

unsigned long long st_delete_old_states(const st_paths *p, const st_systems *s,
                                        const st_game_in *games, int n, long now, int *games_out)
{
	unsigned long long sum = 0;
	int i, b;

	*games_out = 0;
	for (i = 0; i < s->n; i++) {
		char dir[PATH_MAX_ST];
		int nb;
		sbase *v;
		DIR *d;
		struct dirent *e;

		if (snprintf(dir, sizeof dir, "%s/.zyperos/%s", p->shared, s->folders[i]) >= (int)sizeof dir) continue;
		/* Listed now, with each file's own time: the rule is held to the card
		 * as it is at this moment, not as the cache last saw it */
		if (!(v = list_bases(dir, &nb))) continue;
		for (b = 0; b < nb; b++) {
			if (!base_old(&v[b], s->folders[i], games, n, now)) { v[b].bytes = 0; continue; }
			(*games_out)++;
		}
		if ((d = opendir(dir))) {
			while ((e = readdir(d))) {
				char path[PATH_MAX_ST], base[PATH_MAX_ST];
				struct stat st;
				size_t bl = state_base_len(e->d_name);
				int at;

				if (!bl || e->d_name[0] == '.') continue;
				snprintf(base, sizeof base, "%.*s", (int)bl, e->d_name);
				if ((at = base_index(v, nb, base)) < 0 || v[at].bytes == 0) continue;
				if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path) continue;
				if (lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
				if ((long)st.st_mtime >= now - ST_OLD_S) continue;
				if (unlink(path) == 0) sum += alloc_of(&st);
			}
			closedir(d);
		}
		for (b = 0; b < nb; b++) free(v[b].name);
		free(v);
	}
	return sum;
}

int st_orphans(st_orphan *out, int max)
{
	int n;

	pthread_mutex_lock(&g_mu);
	n = g_norph < max ? g_norph : max;
	memcpy(out, g_orph, (size_t)n * sizeof *out);
	pthread_mutex_unlock(&g_mu);
	return n;
}

/* A folder and everything in it, never following a link; the bytes it held */
static unsigned long long tree_remove(const char *dir, int depth)
{
	unsigned long long sum = 0;
	DIR *d;
	struct dirent *e;

	if (depth > 16 || !(d = opendir(dir))) return 0;
	while ((e = readdir(d))) {
		char p[PATH_MAX_ST];
		struct stat st;

		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (snprintf(p, sizeof p, "%s/%s", dir, e->d_name) >= (int)sizeof p) continue;
		if (lstat(p, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) sum += tree_remove(p, depth + 1);
		else if (unlink(p) == 0) sum += alloc_of(&st);
	}
	closedir(d);
	rmdir(dir);
	return sum;
}

unsigned long long st_delete_port_folder(const st_paths *p, const st_systems *s,
                                         const st_game_in *games, int n, const char *folder)
{
	char dir[PATH_MAX_ST], sub[PATH_MAX_ST], (*claimed)[PI_NAME];
	struct stat st;
	unsigned long long b;
	int nc;

	char real[PI_NAME];

	if (!s->ports || !folder || !folder[0]) return 0;
	snprintf(dir, sizeof dir, "%s/%s", p->roms, s->ports);
	/* By the listing's own name, and PortMaster's never (src/portinfo.h) */
	if (!pi_real_folder(dir, folder, real, sizeof real) || pi_runtime(real)) return 0;
	if (snprintf(sub, sizeof sub, "%s/%s", dir, real) >= (int)sizeof sub) return 0;
	if (lstat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
	/* Held to the shelf as it is now: a script that names it makes it a
	 * port's, and it stays */
	if (!(claimed = malloc(PI_MAX * sizeof *claimed))) return 0;
	nc = ports_claimed(dir, games, n, s->ports, claimed, PI_MAX);
	b = claimed_by(claimed, nc, real) ? 0 : tree_remove(sub, 0);
	free(claimed);
	return b;
}

int st_media_items(st_media_kind k, st_media **out)
{
	int n;

	*out = NULL;
	if (k < 0 || k >= STM_KINDS) return 0;
	pthread_mutex_lock(&g_mu);
	n = g_nmedia[k];
	if (n > 0 && (*out = malloc((size_t)n * sizeof **out))) memcpy(*out, g_media[k], (size_t)n * sizeof **out);
	else n = 0;
	pthread_mutex_unlock(&g_mu);
	return n;
}

unsigned long long st_delete_media(const st_paths *p, st_media_kind k, const char *rel)
{
	char path[PATH_MAX_ST];
	const char *c;
	struct stat st;
	unsigned long long b = 0;
	int i, parts = 0;

	if (!rel || !rel[0] || k < 0 || k >= STM_KINDS) return 0;
	/* One or two names, none of them hidden, ".." or empty */
	for (c = rel; *c; ) {
		const char *slash = strchr(c, '/');
		size_t len = slash ? (size_t)(slash - c) : strlen(c);

		if (!len || c[0] == '.' || ++parts > 2) return 0;
		c += len + (slash ? 1 : 0);
		if (slash && !*c) return 0;
	}
	if (snprintf(path, sizeof path, "%s/%s/%s", p->card, st_media_root(k), rel) >= (int)sizeof path) return 0;
	/* Only what the walk found and listed: a path no item names exactly,
	 * whatever it is, is never deleted */
	{
		bool known = false;

		pthread_mutex_lock(&g_mu);
		for (i = 0; i < g_nmedia[k] && !known; i++)
			known = !strcmp(g_media[k][i].rel, rel);
		pthread_mutex_unlock(&g_mu);
		if (!known) return 0;
	}
	/* Neither it nor the folder it is in may be a link */
	if (parts == 2) {
		char up[PATH_MAX_ST];

		snprintf(up, sizeof up, "%s/%s/%.*s", p->card, st_media_root(k),
		         (int)(strchr(rel, '/') - rel), rel);
		if (lstat(up, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
	}
	if (lstat(path, &st) != 0) return 0;
	if (S_ISDIR(st.st_mode)) b = tree_remove(path, 0);
	else if (S_ISREG(st.st_mode) && unlink(path) == 0) b = alloc_of(&st);
	if (!b) return 0;

	/* Out of the numbers now: the item, its root's bytes, the card's room */
	pthread_mutex_lock(&g_mu);
	for (i = 0; i < g_nmedia[k]; i++)
		if (!strcmp(g_media[k][i].rel, rel)) {
			unsigned long long *root = k == STM_MUSIC ? &g_pub.music : k == STM_VIDEOS ? &g_pub.videos
			                                                                           : &g_pub.books;
			int *count = k == STM_MUSIC ? &g_pub.nmusic : k == STM_VIDEOS ? &g_pub.nvideos : &g_pub.nbooks;
			int gone = k != STM_BOOKS ? g_media[k][i].count : g_media[k][i].parent[0] ? 0 : 1;

			*root = *root > b ? *root - b : 0;
			*count = *count > gone ? *count - gone : 0;
			g_pub.free += b;
			if (g_pub.free > g_pub.total) g_pub.free = g_pub.total;
			memmove(&g_media[k][i], &g_media[k][i + 1], (size_t)(g_nmedia[k] - i - 1) * sizeof g_media[k][i]);
			g_nmedia[k]--;
			st_totals(&g_pub);
			g_pub.gen++;
			break;
		}
	pthread_mutex_unlock(&g_mu);
	return b;
}

/* ---- the lists' orders ----------------------------------------------------- */

static int item_cmp(const st_item *x, const st_item *y, st_sort mode)
{
	int byname = x->title && y->title ? strcasecmp(x->title, y->title) : 0;

	if (mode == ST_SORT_AZ && byname) return byname;
	if (mode == ST_SORT_LAST && x->last != y->last) return x->last > y->last ? -1 : 1;
	if (x->sized != y->sized) return x->sized ? -1 : 1;
	if (x->bytes != y->bytes) return x->bytes > y->bytes ? -1 : 1;
	if (byname) return byname;
	if (x->kind != y->kind) return x->kind < y->kind ? -1 : 1;
	return x->index < y->index ? -1 : x->index > y->index;
}

/* A merge sort, as qsort carries no mode to its comparator and a list of
 * three thousand arcade games is too long for anything slower */
bool st_sort_items(st_item *v, int n, st_sort mode)
{
	st_item *t;
	int w, lo;

	if (!v || n < 2) return true;
	if (!(t = malloc((size_t)n * sizeof *t))) return false;
	for (w = 1; w < n; w *= 2) {
		for (lo = 0; lo < n; lo += 2 * w) {
			int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n;
			int a = lo, b = mid, o = lo;

			while (a < mid && b < hi) t[o++] = item_cmp(&v[b], &v[a], mode) < 0 ? v[b++] : v[a++];
			while (a < mid) t[o++] = v[a++];
			while (b < hi) t[o++] = v[b++];
		}
		memcpy(v, t, (size_t)n * sizeof *v);
	}
	free(t);
	return true;
}

st_sort st_sort_next(st_sort mode, bool has_last)
{
	st_sort next = (st_sort)(((int)mode + 1) % ST_SORTS);

	if (next == ST_SORT_LAST && !has_last) next = ST_SORT_SIZE;
	if (mode < 0 || mode >= ST_SORTS) next = ST_SORT_SIZE;
	return next;
}
