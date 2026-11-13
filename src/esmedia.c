/* SPDX-License-Identifier: GPL-3.0-only */
#include "esmedia.h"
#include "titletext.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* gamelist.xml larger than this is not read: 64 MB is some fifty thousand
 * games with their synopses, far past a handheld's card */
#define ES_LIST_MAX (64L * 1024 * 1024)
/* An entry's text past this is cut: a synopsis is GAME_SYNOPSIS_MAX */
#define ES_FIELD_MAX GAME_SYNOPSIS_MAX

/* ---- where ES's scrapers put things, by its name -------------------------
 *
 * Each media folder once, by bit, so an index knows which it has; then the
 * names looked for in each, in the order es_media takes them. */
enum {
	D_IMAGES, D_BOX2D, D_MEDIA_IMAGES, D_BOXART, D_DL_IMAGES, D_MARQUEES, D_WHEEL,
	D_VIDEOS, D_MEDIA_VIDEOS, D_DL_VIDEOS, D_COUNT
};
static const char *const DIRS[D_COUNT] = {
	"images", "media/box2dfront", "media/images", "boxart", "downloaded_images",
	"media/marquees", "media/wheel", "videos", "media/videos", "downloaded_videos",
};

typedef struct { int dir; const char *suffix; } es_name;

static const es_name COVER_NAMES[] = {
	{ D_IMAGES, "-thumb.png" }, { D_IMAGES, "-thumb.jpg" },
	{ D_IMAGES, "-image.png" }, { D_IMAGES, "-image.jpg" },
	{ D_IMAGES, ".png" }, { D_IMAGES, ".jpg" },
	{ D_BOX2D, ".png" }, { D_BOX2D, ".jpg" },
	{ D_MEDIA_IMAGES, ".png" }, { D_MEDIA_IMAGES, ".jpg" },
	{ D_BOXART, ".png" }, { D_BOXART, ".jpg" },
	{ D_DL_IMAGES, ".png" }, { D_DL_IMAGES, ".jpg" },
};
static const es_name LOGO_NAMES[] = {
	{ D_IMAGES, "-marquee.png" }, { D_IMAGES, "-marquee.jpg" },
	{ D_MARQUEES, ".png" }, { D_WHEEL, ".png" },
};
static const es_name VIDEO_NAMES[] = {
	{ D_VIDEOS, "-video.mp4" }, { D_VIDEOS, ".mp4" },
	{ D_MEDIA_VIDEOS, ".mp4" }, { D_DL_VIDEOS, ".mp4" },
};
#define NAMES(a) ((int)(sizeof a / sizeof a[0]))

/* The media an entry names, in es_media's order for each kind */
enum { M_THUMB, M_IMAGE, M_MARQUEE, M_VIDEO, M_COUNT };
static const char *const MEDIA_TAGS[M_COUNT] = { "thumbnail", "image", "marquee", "video" };
/* and every one Rename moves with the game (Batocera's full set) */
static const char *const ALL_MEDIA_TAGS[] = {
	"thumbnail", "image", "marquee", "video", "manual", "fanart", "boxback", "titleshot",
	"map", "bezel", "cartridge", "boxart", "wheel", "mix", "magazine", NULL
};

typedef struct {
	char *key;               /* its path in the list, relative and lowercased */
	char *stem;              /* the name it is found by, lowercased */
	char *media[M_COUNT];    /* relative to the folder, as written; NULL for none */
	long  off;               /* its <game> element in gamelist.xml */
	int   len;
} es_game;

typedef struct es_index {
	char      folder[LIB_NAME];
	unsigned  dirs;          /* the D_ bits of the media folders it has */
	es_game  *g;
	int       n;
	int      *by_key, *by_stem;   /* open addressing, -1 empty, `mask` + 1 slots */
	unsigned  mask;
	/* The names its video folders hold a video for, lowercased and sorted,
	 * "-video" and ".mp4" off: what es_has_video asks without a stat */
	char    **vstems;
	int       nv;
	struct es_index *next;
} es_index;

static es_index *g_idx;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ---- text ---------------------------------------------------------------- */

static void lower(char *s)
{
	for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static unsigned hash(const char *s)
{
	unsigned h = 2166136261u;

	for (; *s; s++) h = (h ^ (unsigned char)tolower((unsigned char)*s)) * 16777619u;
	return h;
}

static void put_utf8(char **o, char *end, unsigned cp)
{
	char b[4];
	int n;

	if (cp < 0x80) { b[0] = (char)cp; n = 1; }
	else if (cp < 0x800) { b[0] = (char)(0xC0 | cp >> 6); b[1] = (char)(0x80 | (cp & 63)); n = 2; }
	else if (cp < 0x10000) {
		b[0] = (char)(0xE0 | cp >> 12); b[1] = (char)(0x80 | (cp >> 6 & 63));
		b[2] = (char)(0x80 | (cp & 63)); n = 3;
	} else if (cp < 0x110000) {
		b[0] = (char)(0xF0 | cp >> 18); b[1] = (char)(0x80 | (cp >> 12 & 63));
		b[2] = (char)(0x80 | (cp >> 6 & 63)); b[3] = (char)(0x80 | (cp & 63)); n = 4;
	} else return;
	if (*o + n >= end) return;
	memcpy(*o, b, (size_t)n);
	*o += n;
}

/* XML's text, its five entities and its numbered ones, and a CDATA section
 * as it stands, into `out`, cut at `n`. False when it was cut: a path cut
 * short names another file, and is no path (field_path). */
static bool unescape(const char *p, const char *e, char *out, size_t n)
{
	char *o = out, *end = out + n;

	if (!n) return false;
	while (p < e && o + 1 < end) {
		if (e - p >= 9 && !memcmp(p, "<![CDATA[", 9)) {
			const char *c = p + 9, *x = c;

			while (x + 2 < e && memcmp(x, "]]>", 3)) x++;
			while (c < x && o + 1 < end) *o++ = *c++;
			p = x + 3 <= e ? x + 3 : e;
			continue;
		}
		if (*p == '&') {
			const char *semi = memchr(p, ';', (size_t)(e - p));
			size_t len = semi ? (size_t)(semi - p) : 0;

			if (semi && len <= 10) {
				if (len == 4 && !memcmp(p, "&amp", 4)) { *o++ = '&'; p = semi + 1; continue; }
				if (len == 3 && !memcmp(p, "&lt", 3)) { *o++ = '<'; p = semi + 1; continue; }
				if (len == 3 && !memcmp(p, "&gt", 3)) { *o++ = '>'; p = semi + 1; continue; }
				if (len == 5 && !memcmp(p, "&quot", 5)) { *o++ = '"'; p = semi + 1; continue; }
				if (len == 5 && !memcmp(p, "&apos", 5)) { *o++ = '\''; p = semi + 1; continue; }
				if (len > 2 && p[1] == '#') {
					unsigned long cp = p[2] == 'x' || p[2] == 'X' ? strtoul(p + 3, NULL, 16)
					                                             : strtoul(p + 2, NULL, 10);
					if (cp) put_utf8(&o, end, (unsigned)cp);
					p = semi + 1;
					continue;
				}
			}
		}
		*o++ = *p++;
	}
	*o = '\0';
	return p >= e;
}

/* `tag`'s text in an element's body, where [b, e) is the body: the first
 * <tag> or <tag attr="..."> of it. False when it has none; an empty one is
 * found, and empty. */
static bool field_at(const char *b, const char *e, const char *tag,
                     const char **vb, const char **ve)
{
	size_t tl = strlen(tag);
	const char *p = b;

	while (p < e && (p = memchr(p, '<', (size_t)(e - p)))) {
		const char *gt, *close;
		char after;

		/* Text that only looks like a tag: a comment, a CDATA section */
		if ((size_t)(e - p) >= 4 && !memcmp(p, "<!--", 4)) {
			const char *x = memmem(p + 4, (size_t)(e - p - 4), "-->", 3);

			if (!x) return false;
			p = x + 3;
			continue;
		}
		if ((size_t)(e - p) >= 9 && !memcmp(p, "<![CDATA[", 9)) {
			const char *x = memmem(p + 9, (size_t)(e - p - 9), "]]>", 3);

			if (!x) return false;
			p = x + 3;
			continue;
		}
		if ((size_t)(e - p) < tl + 2 || memcmp(p + 1, tag, tl)) { p++; continue; }
		after = p[1 + tl];
		if (after != '>' && after != ' ' && after != '\t' && after != '/' && after != '\n' &&
		    after != '\r') { p++; continue; }
		if (!(gt = memchr(p, '>', (size_t)(e - p)))) return false;
		if (gt[-1] == '/') { *vb = *ve = gt + 1; return true; }       /* <tag/> */
		for (close = gt + 1; close < e; close++) {
			close = memchr(close, '<', (size_t)(e - close));
			if (!close) return false;
			if (close + 2 + tl < e && close[1] == '/' && !memcmp(close + 2, tag, tl) &&
			    close[2 + tl] == '>')
				break;
			/* a CDATA section may hold anything, a "<" included */
			if ((size_t)(e - close) >= 9 && !memcmp(close, "<![CDATA[", 9)) {
				const char *x = close + 9;

				while (x + 2 < e && memcmp(x, "]]>", 3)) x++;
				close = x + 2;
			}
		}
		if (close >= e) return false;
		*vb = gt + 1;
		*ve = close;
		return true;
	}
	return false;
}

static bool field(const char *b, const char *e, const char *tag, char *out, size_t n)
{
	const char *vb, *ve;

	if (n) out[0] = '\0';
	if (!field_at(b, e, tag, &vb, &ve)) return false;
	unescape(vb, ve, out, n);
	/* ES's own spaces around a value are no part of it */
	{
		char *s = out, *t;

		while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
		if (s != out) memmove(out, s, strlen(s) + 1);
		t = out + strlen(out);
		while (t > out && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '\n' || t[-1] == '\r')) *--t = '\0';
	}
	return true;
}

/* A path-like value: false too when it did not fit `out` whole */
static bool field_path(const char *b, const char *e, const char *tag, char *out, size_t n)
{
	const char *vb, *ve;
	char *s, *t;

	if (n) out[0] = '\0';
	if (!field_at(b, e, tag, &vb, &ve) || !unescape(vb, ve, out, n)) {
		if (n) out[0] = '\0';
		return false;
	}
	for (s = out; *s == ' ' || *s == '\t' || *s == '\n' || *s == '\r'; s++) {}
	if (s != out) memmove(out, s, strlen(s) + 1);
	t = out + strlen(out);
	while (t > out && (t[-1] == ' ' || t[-1] == '\t' || t[-1] == '\n' || t[-1] == '\r')) *--t = '\0';
	return true;
}

/* Inside one of the folders ES's scrapers fill (images/, videos/, media/...,
 * lib_es_media_dir): the only place a file of the game's that gamelist.xml
 * names is moved from by Rename, never a game of the folder. `rel` has
 * been through rel_of (no "..", "." or empty part); and where the file
 * really is, links followed, is that media folder of Roms/<folder> too, as
 * Delete Game holds its paths (a host's card can have links where exFAT
 * has none). */
static bool in_media_dir(const char *roms, const char *folder, const char *rel)
{
	const char *slash = strchr(rel, '/');
	char first[LIB_NAME], sys[ES_PATH], dir[ES_PATH], file[ES_PATH];
	char rs[PATH_MAX], rd[PATH_MAX], rf[PATH_MAX];
	size_t ns, nd;

	if (!slash || slash == rel || (size_t)(slash - rel) >= sizeof first) return false;
	snprintf(first, sizeof first, "%.*s", (int)(slash - rel), rel);
	if (!lib_es_media_dir(first)) return false;
	if (snprintf(sys, sizeof sys, "%s/%s", roms, folder) >= (int)sizeof sys ||
	    snprintf(dir, sizeof dir, "%s/%s", sys, first) >= (int)sizeof dir ||
	    snprintf(file, sizeof file, "%s/%s", sys, rel) >= (int)sizeof file ||
	    !realpath(sys, rs) || !realpath(dir, rd) || !realpath(file, rf))
		return false;
	/* the media folder a folder of the system's own, not a link to the
	 * system's folder or anywhere else; the file under it */
	ns = strlen(rs);
	nd = strlen(rd);
	return !strncmp(rd, rs, ns) && rd[ns] == '/' && !strchr(rd + ns + 1, '/') &&
	       !strncmp(rf, rd, nd) && rf[nd] == '/';
}

/* The next <game> element at or after `p`: its start and end (past
 * </game>), and its body. <gameList> and <game/> are not one. */
static bool next_game(const char *p, const char *e, const char **gs, const char **ge,
                      const char **bb, const char **be)
{
	while (p < e) {
		const char *gt, *close;
		char c;

		if (!(p = memmem(p, (size_t)(e - p), "<game", 5))) return false;
		c = p + 5 < e ? p[5] : '\0';
		if (c != '>' && c != ' ' && c != '\t' && c != '\n' && c != '\r') { p += 5; continue; }
		if (!(gt = memchr(p, '>', (size_t)(e - p)))) return false;
		if (gt[-1] == '/') { p = gt + 1; continue; }
		if (!(close = memmem(gt, (size_t)(e - gt), "</game>", 7))) return false;
		*gs = p;
		*ge = close + 7;
		*bb = gt + 1;
		*be = close;
		return true;
	}
	return false;
}

/* A path as gamelist.xml writes it, relative to the console's folder:
 * "./x" and "x" as they are, and another system's absolute path
 * ("/userdata/roms/nds/x", "~/RetroPie/roms/nds/x", "C:\RetroBat\roms\nds\x")
 * from past the folder's name, else past roms/ and the folder after it.
 * False for one that names nothing in the folder, or climbs out of it. */
static bool rel_of(const char *path, const char *folder, char *out, size_t n)
{
	char p[ES_PATH], low[ES_PATH], f[LIB_NAME + 3];
	const char *r = p, *hit;
	size_t i;

	snprintf(p, sizeof p, "%s", path);
	for (i = 0; p[i]; i++) if (p[i] == '\\') p[i] = '/';
	while (r[0] == '.' && r[1] == '/') r += 2;
	if (r[0] == '/' || r[0] == '~' || (isalpha((unsigned char)r[0]) && r[1] == ':')) {
		snprintf(low, sizeof low, "%s", r);
		lower(low);
		snprintf(f, sizeof f, "/%s/", folder);
		lower(f);
		if ((hit = strstr(low, f))) {
			r += (hit - low) + strlen(f);
		} else if ((hit = strstr(low, "/roms/")) && strchr(hit + 6, '/')) {
			r += (strchr(hit + 6, '/') - low) + 1;
		} else {
			return false;
		}
	}
	while (r[0] == '.' && r[1] == '/') r += 2;
	if (!r[0]) return false;
	/* never out of the folder, and one spelling for each file: no "..",
	 * no ".", no empty part (backslashes are slashes above) */
	for (const char *s = r; ; ) {
		const char *slash = strchr(s, '/');
		size_t len = slash ? (size_t)(slash - s) : strlen(s);

		if (len == 0 || (len == 1 && s[0] == '.') || (len == 2 && s[0] == '.' && s[1] == '.'))
			return false;
		if (!slash) break;
		s = slash + 1;
	}
	snprintf(out, n, "%s", r);
	return strlen(r) < n;
}

/* The name a game is found by: its path's first part, without its
 * extension when that part is the file */
static void stem_of(const char *rel, char *out, size_t n)
{
	const char *slash = strchr(rel, '/');
	size_t len = slash ? (size_t)(slash - rel) : strlen(rel);
	char *dot;

	snprintf(out, n, "%.*s", (int)len, rel);
	if (!slash && (dot = strrchr(out, '.')) && dot != out) *dot = '\0';
	lower(out);
}

static bool is_file(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

static bool is_dir(const char *p)
{
	struct stat st;

	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* ---- the index ----------------------------------------------------------- */

static void index_free(es_index *x)
{
	if (!x) return;
	for (int i = 0; i < x->n; i++) {
		free(x->g[i].key);
		free(x->g[i].stem);
		for (int k = 0; k < M_COUNT; k++) free(x->g[i].media[k]);
	}
	free(x->g);
	free(x->by_key);
	free(x->by_stem);
	for (int i = 0; i < x->nv; i++) free(x->vstems[i]);
	free(x->vstems);
	free(x);
}

static int str_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The video folders' names, once, for es_has_video */
static void video_stems(es_index *x, const char *roms)
{
	static const int VDIRS[] = { D_VIDEOS, D_MEDIA_VIDEOS, D_DL_VIDEOS };
	int cap = 0;

	for (size_t k = 0; k < sizeof VDIRS / sizeof VDIRS[0]; k++) {
		char p[ES_PATH];
		DIR *d;
		struct dirent *e;

		if (!(x->dirs & (1u << VDIRS[k]))) continue;
		snprintf(p, sizeof p, "%s/%s/%s", roms, x->folder, DIRS[VDIRS[k]]);
		if (!(d = opendir(p))) continue;
		while ((e = readdir(d))) {
			size_t len = strlen(e->d_name);
			char *stem;

			if (e->d_name[0] == '.' || len < 5 || strcasecmp(e->d_name + len - 4, ".mp4")) continue;
			len -= 4;
			if (len > 6 && !strncasecmp(e->d_name + len - 6, "-video", 6)) len -= 6;
			if (x->nv == cap) {
				char **more = realloc(x->vstems, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *more);

				if (!more) break;
				x->vstems = more;
			}
			if (!(stem = strndup(e->d_name, len))) break;
			lower(stem);
			x->vstems[x->nv++] = stem;
		}
		closedir(d);
	}
	if (x->nv) qsort(x->vstems, (size_t)x->nv, sizeof *x->vstems, str_cmp);
}

static es_index *index_find(const char *folder)
{
	for (es_index *x = g_idx; x; x = x->next)
		if (!strcmp(x->folder, folder)) return x;
	return NULL;
}

static void table_put(int *t, unsigned mask, const char *k, int i, es_game *g, bool stem)
{
	unsigned h = hash(k) & mask;

	while (t[h] >= 0) {
		/* the first entry of a name keeps it */
		if (!strcmp(stem ? g[t[h]].stem : g[t[h]].key, k)) return;
		h = (h + 1) & mask;
	}
	t[h] = i;
}

static int table_get(const es_index *x, const int *t, const char *k, bool stem)
{
	unsigned h;

	if (!t || !x->n) return -1;
	for (h = hash(k) & x->mask; t[h] >= 0; h = (h + 1) & x->mask)
		if (!strcmp(stem ? x->g[t[h]].stem : x->g[t[h]].key, k)) return t[h];
	return -1;
}

/* gamelist.xml whole, or NULL; its length in `len` */
static char *list_read(const char *roms, const char *folder, size_t *len)
{
	char p[ES_PATH];
	struct stat st;
	char *buf;
	FILE *f;

	snprintf(p, sizeof p, "%s/%s/gamelist.xml", roms, folder);
	if (stat(p, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > ES_LIST_MAX)
		return NULL;
	if (!(f = fopen(p, "rb"))) return NULL;
	if (!(buf = malloc((size_t)st.st_size + 1))) { fclose(f); return NULL; }
	*len = fread(buf, 1, (size_t)st.st_size, f);
	fclose(f);
	buf[*len] = '\0';
	return buf;
}

static es_index *index_make(const char *roms, const char *folder)
{
	es_index *x = calloc(1, sizeof *x);
	char p[ES_PATH], v[ES_PATH], rel[ES_PATH];
	const char *at, *gs, *ge, *bb, *be;
	size_t len = 0;
	char *buf;
	int cap = 0;

	if (!x) return NULL;
	snprintf(x->folder, sizeof x->folder, "%s", folder);
	for (int d = 0; d < D_COUNT; d++) {
		snprintf(p, sizeof p, "%s/%s/%s", roms, folder, DIRS[d]);
		if (is_dir(p)) x->dirs |= 1u << d;
	}
	video_stems(x, roms);
	if (!(buf = list_read(roms, folder, &len))) return x;
	for (at = buf; next_game(at, buf + len, &gs, &ge, &bb, &be); at = ge) {
		es_game *g;

		if (!field_path(bb, be, "path", v, sizeof v) || !rel_of(v, folder, rel, sizeof rel)) continue;
		if (x->n == cap) {
			int more = cap ? cap * 2 : 256;
			es_game *bigger = realloc(x->g, (size_t)more * sizeof *bigger);

			if (!bigger) break;
			x->g = bigger;
			cap = more;
		}
		g = &x->g[x->n];
		memset(g, 0, sizeof *g);
		g->key = strdup(rel);
		lower(g->key);
		stem_of(rel, v, sizeof v);
		g->stem = strdup(v);
		for (int k = 0; k < M_COUNT; k++)
			if (field_path(bb, be, MEDIA_TAGS[k], v, sizeof v) && v[0] &&
			    rel_of(v, folder, rel, sizeof rel))
				g->media[k] = strdup(rel);
		g->off = (long)(gs - buf);
		g->len = (int)(ge - gs);
		if (!g->key || !g->stem) { free(g->key); free(g->stem); continue; }
		x->n++;
	}
	free(buf);
	if (x->n) {
		unsigned slots = 64;

		while (slots < (unsigned)x->n * 2) slots <<= 1;
		x->mask = slots - 1;
		x->by_key = malloc(slots * sizeof *x->by_key);
		x->by_stem = malloc(slots * sizeof *x->by_stem);
		if (!x->by_key || !x->by_stem) {
			free(x->by_key); free(x->by_stem);
			x->by_key = x->by_stem = NULL;
		} else {
			memset(x->by_key, 0xff, slots * sizeof *x->by_key);
			memset(x->by_stem, 0xff, slots * sizeof *x->by_stem);
			for (int i = 0; i < x->n; i++) {
				table_put(x->by_key, x->mask, x->g[i].key, i, x->g, false);
				table_put(x->by_stem, x->mask, x->g[i].stem, i, x->g, true);
			}
		}
	}
	return x;
}

void es_build(const char *roms, const char *folder)
{
	es_index *x;

	if (!roms || !folder || !folder[0]) return;
	pthread_mutex_lock(&g_lock);
	x = index_find(folder);
	pthread_mutex_unlock(&g_lock);
	if (x) return;
	/* Built outside the lock: a frame asking without `wait` is never held
	 * behind a read of the card. Two threads may build one folder at once;
	 * the second's goes. */
	x = index_make(roms, folder);
	if (!x) return;
	pthread_mutex_lock(&g_lock);
	if (index_find(folder)) {
		pthread_mutex_unlock(&g_lock);
		index_free(x);
		return;
	}
	x->next = g_idx;
	g_idx = x;
	pthread_mutex_unlock(&g_lock);
}

bool es_ready(const char *folder)
{
	bool ready;

	pthread_mutex_lock(&g_lock);
	ready = folder && index_find(folder) != NULL;
	pthread_mutex_unlock(&g_lock);
	return ready;
}

void es_forget(void)
{
	es_index *x;

	pthread_mutex_lock(&g_lock);
	x = g_idx;
	g_idx = NULL;
	pthread_mutex_unlock(&g_lock);
	while (x) {
		es_index *next = x->next;

		index_free(x);
		x = next;
	}
}

static void forget_one(const char *folder)
{
	es_index **at, *x = NULL;

	pthread_mutex_lock(&g_lock);
	for (at = &g_idx; *at; at = &(*at)->next)
		if (!strcmp((*at)->folder, folder)) { x = *at; *at = x->next; break; }
	pthread_mutex_unlock(&g_lock);
	index_free(x);
}

/* The entry for a game, under the lock */
static int entry_of(const es_index *x, const char *file, const char *name)
{
	char k[ES_PATH];
	int i;

	snprintf(k, sizeof k, "%s", file);
	lower(k);
	if ((i = table_get(x, x->by_key, k, false)) >= 0) return i;
	snprintf(k, sizeof k, "%s", name);
	lower(k);
	return table_get(x, x->by_stem, k, true);
}

/* What es_media needs of a folder's index, copied out from under the lock */
typedef struct {
	unsigned dirs;
	bool     listed;
	char     media[M_COUNT][ES_PATH];
} es_seen;

static bool seen(const char *roms, const char *folder, const char *file, const char *name,
                 bool wait, es_seen *s)
{
	es_index *x;
	int i;

	memset(s, 0, sizeof *s);
	if (!folder || !folder[0] || !file || !name) return false;
	if (wait) es_build(roms, folder);
	pthread_mutex_lock(&g_lock);
	if (!(x = index_find(folder))) { pthread_mutex_unlock(&g_lock); return false; }
	s->dirs = x->dirs;
	if ((i = entry_of(x, file, name)) >= 0) {
		s->listed = true;
		for (int k = 0; k < M_COUNT; k++)
			if (x->g[i].media[k]) snprintf(s->media[k], ES_PATH, "%s", x->g[i].media[k]);
	}
	pthread_mutex_unlock(&g_lock);
	return true;
}

static bool by_names(const char *roms, const char *folder, const char *name, unsigned dirs,
                     const es_name *names, int nn, char *out, size_t n)
{
	for (int i = 0; i < nn; i++) {
		if (!(dirs & (1u << names[i].dir))) continue;
		if (snprintf(out, n, "%s/%s/%s/%s%s", roms, folder, DIRS[names[i].dir], name,
		             names[i].suffix) >= (int)n)
			continue;
		if (is_file(out)) return true;
	}
	return false;
}

static bool listed(const char *roms, const char *folder, const char *rel, char *out, size_t n)
{
	return rel[0] && snprintf(out, n, "%s/%s/%s", roms, folder, rel) < (int)n && is_file(out);
}

bool es_media(const char *roms, const char *folder, const char *file, const char *name,
              es_kind kind, bool wait, char *out, size_t n)
{
	es_seen s;

	if (!out || !n) return false;
	out[0] = '\0';
	if (!seen(roms, folder, file, name, wait, &s) || (!s.dirs && !s.listed)) return false;
	if (kind == ES_VIDEO)
		return listed(roms, folder, s.media[M_VIDEO], out, n) ||
		       by_names(roms, folder, name, s.dirs, VIDEO_NAMES, NAMES(VIDEO_NAMES), out, n) ||
		       (out[0] = '\0', false);
	return listed(roms, folder, s.media[M_THUMB], out, n) ||
	       listed(roms, folder, s.media[M_IMAGE], out, n) ||
	       by_names(roms, folder, name, s.dirs, COVER_NAMES, NAMES(COVER_NAMES), out, n) ||
	       listed(roms, folder, s.media[M_MARQUEE], out, n) ||
	       by_names(roms, folder, name, s.dirs, LOGO_NAMES, NAMES(LOGO_NAMES), out, n) ||
	       (out[0] = '\0', false);
}

bool es_has_video(const char *folder, const char *file, const char *name)
{
	char k[LIB_NAME], *key = k;
	es_index *x;
	bool has = false;
	int i;

	if (!folder || !file || !name) return false;
	snprintf(k, sizeof k, "%s", name);
	lower(k);
	pthread_mutex_lock(&g_lock);
	if ((x = index_find(folder))) {
		has = x->nv && bsearch(&key, x->vstems, (size_t)x->nv, sizeof *x->vstems, str_cmp);
		if (!has && (i = entry_of(x, file, name)) >= 0) has = x->g[i].media[M_VIDEO] != NULL;
	}
	pthread_mutex_unlock(&g_lock);
	return has;
}

/* ---- the details --------------------------------------------------------- */

/* The game's <game> element, read again from the list: [off, off + len) */
static char *element_read(const char *roms, const char *folder, long off, int len)
{
	char p[ES_PATH], *buf;
	FILE *f;

	snprintf(p, sizeof p, "%s/%s/gamelist.xml", roms, folder);
	if (len <= 0 || !(f = fopen(p, "rb"))) return NULL;
	if (fseek(f, off, SEEK_SET) != 0 || !(buf = malloc((size_t)len + 1))) { fclose(f); return NULL; }
	if (fread(buf, 1, (size_t)len, f) != (size_t)len || memcmp(buf, "<game", 5)) {
		free(buf);
		fclose(f);
		return NULL;
	}
	fclose(f);
	buf[len] = '\0';
	return buf;
}

static bool locate(const char *roms, const char *folder, const char *file, const char *name,
                   long *off, int *len)
{
	es_index *x;
	int i = -1;

	es_build(roms, folder);
	pthread_mutex_lock(&g_lock);
	if ((x = index_find(folder)) && (i = entry_of(x, file, name)) >= 0) {
		*off = x->g[i].off;
		*len = x->g[i].len;
	}
	pthread_mutex_unlock(&g_lock);
	return i >= 0;
}

/* The details of one element's body */
static void details_of(const char *b, const char *e, const char *name, game_meta *m, bool *fav)
{
	char v[ES_FIELD_MAX];

	memset(m, 0, sizeof *m);
	if (field(b, e, "name", v, sizeof v) && strcasecmp(v, name))
		title_text(v, m->title, sizeof m->title);
	field(b, e, "desc", m->synopsis, sizeof m->synopsis);
	/* 19961121T000000, or 1996-11-21, or 1996: the year either way */
	if (field(b, e, "releasedate", v, sizeof v) && isdigit((unsigned char)v[0]) &&
	    isdigit((unsigned char)v[1]) && isdigit((unsigned char)v[2]) && isdigit((unsigned char)v[3]))
		snprintf(m->year, sizeof m->year, "%.4s", v);
	if (!strcmp(m->year, "1970")) m->year[0] = '\0';        /* ES's "no date" */
	field(b, e, "genre", m->genres, sizeof m->genres);
	field(b, e, "players", m->players, sizeof m->players);
	field(b, e, "developer", m->developer, sizeof m->developer);
	field(b, e, "publisher", m->publisher, sizeof m->publisher);
	if (field(b, e, "rating", v, sizeof v) && v[0]) {
		double r = strtod(v, NULL);

		if (r > 0 && r <= 1) snprintf(m->note, sizeof m->note, "%d", (int)(r * 20 + 0.5));
	}
	if (fav) *fav = field(b, e, "favorite", v, sizeof v) && !strcasecmp(v, "true");
}

static bool meta_any(const game_meta *m)
{
	return m->title[0] || m->synopsis[0] || m->year[0] || m->genres[0] || m->players[0] ||
	       m->developer[0] || m->publisher[0] || m->note[0];
}

bool es_details(const char *roms, const char *folder, const char *file, const char *name,
                game_meta *out, bool *favorite)
{
	long off = 0;
	int len = 0;
	char *el;
	const char *gs, *ge, *bb, *be;

	if (out) memset(out, 0, sizeof *out);
	if (favorite) *favorite = false;
	if (!out || !locate(roms, folder, file, name, &off, &len) ||
	    !(el = element_read(roms, folder, off, len)))
		return false;
	if (!next_game(el, el + len, &gs, &ge, &bb, &be)) { free(el); return false; }
	details_of(bb, be, name, out, favorite);
	free(el);
	return true;
}

/* ---- into the library database ------------------------------------------ */

static bool got_add(es_import_list *l, const char *file, const char *title, bool fav)
{
	if (l->n == l->cap) {
		int more = l->cap ? l->cap * 2 : 64;
		es_got *bigger = realloc(l->got, (size_t)more * sizeof *bigger);

		if (!bigger) return false;
		l->got = bigger;
		l->cap = more;
	}
	snprintf(l->got[l->n].file, sizeof l->got[0].file, "%s", file);
	snprintf(l->got[l->n].title, sizeof l->got[0].title, "%s", title);
	l->got[l->n].favorite = fav;
	l->n++;
	return true;
}

void es_import_free(es_import_list *l)
{
	free(l->got);
	memset(l, 0, sizeof *l);
}

int es_import(const char *roms, const char *folder, const game_list *list, es_import_list *out)
{
	db_titles titles;
	size_t len = 0;
	char *buf;
	int rows = 0;
	bool began = false;
	es_index *x;

	if (!list || list->count <= 0 || !db_lib()) return 0;
	es_build(roms, folder);
	pthread_mutex_lock(&g_lock);
	x = index_find(folder);
	len = x ? (size_t)x->n : 0;
	pthread_mutex_unlock(&g_lock);
	if (!len) return 0;
	/* The list read once, its entries found by the index's offsets */
	if (!(buf = list_read(roms, folder, &len))) return 0;
	db_titles_load(db_lib(), folder, &titles);
	for (int i = 0; i < list->count; i++) {
		const game_entry *e = &list->items[i];
		game_meta m, have;
		long off = -1;
		int elen = 0, k;
		const char *gs, *ge, *bb, *be;
		bool fav = false;

		if (db_titles_find(&titles, e->file)) continue;
		pthread_mutex_lock(&g_lock);
		if ((x = index_find(folder)) && (k = entry_of(x, e->file, e->name)) >= 0) {
			off = x->g[k].off;
			elen = x->g[k].len;
		}
		pthread_mutex_unlock(&g_lock);
		if (off < 0 || (size_t)off + (size_t)elen > len) continue;
		if (!next_game(buf + off, buf + off + elen, &gs, &ge, &bb, &be)) continue;
		details_of(bb, be, e->name, &m, &fav);
		if (!meta_any(&m)) continue;
		/* a row without a title: a scrape's prose, or a rename's */
		if (db_game_get(db_lib(), folder, e->file, &have)) continue;
		if (!began) began = db_begin(db_lib());
		if (!db_game_set(db_lib(), folder, e->file, &m)) continue;
		rows++;
		if (out) got_add(out, e->file, m.title, fav);
	}
	if (began) db_commit(db_lib());
	db_titles_free(&titles);
	free(buf);
	return rows;
}

/* ---- for Rename ---------------------------------------------------------- */

/* `rel` with the game's name at the front of its last part changed to `to`,
 * into `out`; false when that part does not start with the name */
static bool renamed(const char *rel, const char *name, const char *to, char *out, size_t n)
{
	const char *base = strrchr(rel, '/');
	size_t nl = strlen(name);

	base = base ? base + 1 : rel;
	if (strncmp(base, name, nl) || !base[nl]) return false;
	return snprintf(out, n, "%.*s%s%s", (int)(base - rel), rel, to, base + nl) < (int)n;
}

static int pair_add(char (*rel)[ES_PATH], char (*rel_to)[ES_PATH], int k, int max,
                    const char *from, const char *name, const char *to)
{
	char t[ES_PATH];

	if (k >= max || !renamed(from, name, to, t, sizeof t)) return k;
	for (int i = 0; i < k; i++)
		if (!strcmp(rel[i], from)) return k;
	snprintf(rel[k], ES_PATH, "%s", from);
	snprintf(rel_to[k], ES_PATH, "%s", t);
	return k + 1;
}

int es_files(const char *roms, const char *folder, const char *file, const char *name,
             const char *to, char (*rel)[ES_PATH], char (*rel_to)[ES_PATH], int max)
{
	char p[ES_PATH], v[ES_PATH], r[ES_PATH];
	long off = 0;
	int len = 0, k = 0;
	es_seen s;
	const es_name *lists[] = { COVER_NAMES, LOGO_NAMES, VIDEO_NAMES };
	const int counts[] = { NAMES(COVER_NAMES), NAMES(LOGO_NAMES), NAMES(VIDEO_NAMES) };

	if (!seen(roms, folder, file, name, true, &s)) return 0;
	/* What its entry names, every kind of picture ES keeps */
	if (s.listed && locate(roms, folder, file, name, &off, &len)) {
		char *el = element_read(roms, folder, off, len);
		const char *gs, *ge, *bb, *be;

		if (el && next_game(el, el + len, &gs, &ge, &bb, &be))
			for (int t = 0; ALL_MEDIA_TAGS[t]; t++)
				if (field_path(bb, be, ALL_MEDIA_TAGS[t], v, sizeof v) && v[0] &&
				    rel_of(v, folder, r, sizeof r) && in_media_dir(roms, folder, r) &&
				    snprintf(p, sizeof p, "%s/%s/%s", roms, folder, r) < (int)sizeof p &&
				    is_file(p))
					k = pair_add(rel, rel_to, k, max, r, name, to);
		free(el);
	}
	/* and what is named after it where ES's scrapers put things */
	for (int l = 0; l < 3; l++)
		for (int i = 0; i < counts[l]; i++) {
			const es_name *e = &lists[l][i];

			if (!(s.dirs & (1u << e->dir))) continue;
			snprintf(r, sizeof r, "%s/%s%s", DIRS[e->dir], name, e->suffix);
			if (snprintf(p, sizeof p, "%s/%s/%s", roms, folder, r) < (int)sizeof p && is_file(p))
				k = pair_add(rel, rel_to, k, max, r, name, to);
		}
	return k;
}

/* XML's own escapes, for a value written back */
static size_t escape(const char *s, char *out, size_t n)
{
	size_t o = 0;

	for (; *s && o + 7 < n; s++) {
		const char *r = *s == '&' ? "&amp;" : *s == '<' ? "&lt;" : *s == '>' ? "&gt;" : NULL;

		if (r) { memcpy(out + o, r, strlen(r)); o += strlen(r); }
		else out[o++] = *s;
	}
	out[o] = '\0';
	return o;
}

/* One value of the element, [vb, ve), written as `to`'s escape; the rest
 * of the buffer as it was */
typedef struct { const char *vb, *ve; char to[ES_PATH * 2]; } es_edit;
/* Its path and one of each media tag */
#define ES_EDITS 17

static int edit_cmp(const void *a, const void *b)
{
	const es_edit *x = a, *y = b;

	return x->vb < y->vb ? -1 : x->vb > y->vb;
}

bool es_list_rename(const char *roms, const char *folder, const char *file, const char *name,
                    const char *new_file, char (*rel)[ES_PATH], char (*rel_to)[ES_PATH],
                    int nrel)
{
	char p[ES_PATH], tmp[ES_PATH + 8], v[ES_PATH], r[ES_PATH], raw[ES_PATH];
	es_edit *ed = NULL;
	long off = 0;
	int len = 0, ne = 0;
	size_t blen = 0;
	char *buf;
	const char *gs, *ge, *bb, *be, *vb, *ve;
	FILE *f;
	bool ok = false;

	if (!locate(roms, folder, file, name, &off, &len) || !(buf = list_read(roms, folder, &blen)))
		return false;
	if ((size_t)off + (size_t)len > blen || !next_game(buf + off, buf + off + len, &gs, &ge, &bb, &be) ||
	    !(ed = calloc(ES_EDITS, sizeof *ed))) {
		free(buf);
		free(ed);
		return false;
	}
	/* Its path, as ES writes one */
	if (field_at(bb, be, "path", &vb, &ve)) {
		char f2[ES_PATH];

		snprintf(f2, sizeof f2, "./%s", new_file);
		ed[ne].vb = vb;
		ed[ne].ve = ve;
		escape(f2, ed[ne].to, sizeof ed[ne].to);
		ne++;
	}
	/* Each picture or video it names that went with the game: the value as
	 * it was written, its last part the file's new name */
	for (int t = 0; ALL_MEDIA_TAGS[t] && ne < ES_EDITS; t++) {
		if (!field_at(bb, be, ALL_MEDIA_TAGS[t], &vb, &ve) || !unescape(vb, ve, v, sizeof v) ||
		    !rel_of(v, folder, r, sizeof r))
			continue;
		for (int i = 0; i < nrel; i++) {
			const char *slash, *base_to = strrchr(rel_to[i], '/');

			if (strcmp(rel[i], r)) continue;
			base_to = base_to ? base_to + 1 : rel_to[i];
			slash = strrchr(v, '/');
			if (!slash) slash = strrchr(v, '\\');
			snprintf(raw, sizeof raw, "%.*s%s", slash ? (int)(slash - v + 1) : 0, v, base_to);
			ed[ne].vb = vb;
			ed[ne].ve = ve;
			escape(raw, ed[ne].to, sizeof ed[ne].to);
			ne++;
			break;
		}
	}
	qsort(ed, (size_t)ne, sizeof *ed, edit_cmp);
	/* Each value its own stretch of the element, in order: a list whose
	 * values hold one another (a tag inside a <path>) is not one to write
	 * back, and is left as it was */
	for (int i = 0; i < ne; i++)
		if (ed[i].ve < ed[i].vb || ed[i].vb < gs || ed[i].ve > ge || (i && ed[i].vb < ed[i - 1].ve)) {
			free(ed);
			free(buf);
			forget_one(folder);
			return false;
		}
	snprintf(p, sizeof p, "%s/%s/gamelist.xml", roms, folder);
	snprintf(tmp, sizeof tmp, "%s.part", p);
	if ((f = fopen(tmp, "wb"))) {
		const char *at = buf;
		bool w = true;

		for (int i = 0; i < ne && w; i++) {
			w = fwrite(at, 1, (size_t)(ed[i].vb - at), f) == (size_t)(ed[i].vb - at) &&
			    fputs(ed[i].to, f) >= 0;
			at = ed[i].ve;
		}
		w = w && fwrite(at, 1, (size_t)(buf + blen - at), f) == (size_t)(buf + blen - at);
		w = fflush(f) == 0 && w;
		w = fsync(fileno(f)) == 0 && w;
		ok = fclose(f) == 0 && w && rename(tmp, p) == 0;
		if (!ok) unlink(tmp);
	}
	free(ed);
	free(buf);
	forget_one(folder);
	return ok;
}
