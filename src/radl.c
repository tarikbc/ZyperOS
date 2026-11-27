/* SPDX-License-Identifier: GPL-3.0-only */
/* See radl.h. The requests are net.c's, the JSON rajson.c's, the hashes
 * rahash.c's and the set file rafetch.c's converter's; this is the order a
 * whole card is asked in. */
#include "radl.h"
#include "rapack.h"

#include "cheevos.h"
#include "fileio.h"
#include "library.h"
#include "net.h"
#include "rafetch.h"
#include "rahash.h"
#include "rajson.h"

#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/resource.h>
#endif

/* Where a reply is kept while it is read: /tmp, memory on the device. A
 * check builds with a folder of its own. */
#ifndef RADL_TMP
#define RADL_TMP "/tmp"
#endif

/* A badge's home and address, as src/rabadge.h has them (rb_path, RB_URL):
 * written here again so the run needs no SDL, which rabadge.c's textures do */
#define RADL_BADGE_URL "https://media.retroachievements.org/Badge/"

/* radl.h cannot include library.h for it */
_Static_assert(RADL_SET_PATH == LIB_PATH * 2 + 64, "RADL_SET_PATH is a set's path in library.h's terms");

/* A ROM's path: Roms/<folder>/<file> */
#define RADL_PATH (LIB_PATH + CFG_STR + LIB_PATH + 8)

/* ---- consoles ------------------------------------------------------------- */

/* rcheevos's rc_consoles.h, for the tags in config/systems.cfg that
 * RetroAchievements has. A tag not here has none: Ports, PICO-8, J2ME,
 * ScummVM, OpenBOR, Doom. The NES shelf takes Famicom Disk System images,
 * a console of their own there. */
static const struct { const char *tag; int c[RADL_CONSOLES]; } CONSOLES[] = {
	{ "A2600", { 25 } },  { "GW", { 60 } },      { "COLECO", { 44 } },
	{ "A5200", { 50 } },  { "VECTREX", { 46 } }, { "SG1000", { 33 } },
	{ "NES", { 7, 81 } }, { "MSX2", { 29 } },    { "A7800", { 51 } },
	{ "SMS", { 11 } },    { "GB", { 4 } },       { "MD", { 1 } },
	{ "PCE", { 8 } },     { "LYNX", { 13 } },    { "GG", { 15 } },
	{ "NEOGEO", { 27 } }, { "SFC", { 3 } },      { "SEGACD", { 9 } },
	{ "3DO", { 43 } },    { "32X", { 10 } },     { "SATURN", { 39 } },
	{ "VB", { 28 } },     { "PS1", { 12 } },     { "NEOCD", { 56 } },
	{ "N64", { 2 } },     { "NGP", { 14 } },     { "GBC", { 6 } },
	{ "WS", { 53 } },     { "NGPC", { 14 } },    { "DC", { 40 } },
	{ "WSC", { 53 } },    { "GBA", { 5 } },      { "PKMINI", { 24 } },
	{ "NDS", { 18 } },    { "PSP", { 41 } },     { "TIC80", { 65 } },
	{ "ARCADE", { 27 } }, { "MAME", { 27 } },
};

int radl_consoles(const char *tag, int *out)
{
	size_t i;
	int n = 0;

	if (!tag) return 0;
	for (i = 0; i < sizeof CONSOLES / sizeof CONSOLES[0]; i++) {
		if (strcmp(CONSOLES[i].tag, tag)) continue;
		while (n < RADL_CONSOLES && CONSOLES[i].c[n]) {
			if (out) out[n] = CONSOLES[i].c[n];
			n++;
		}
		break;
	}
	return n;
}

/* ---- a set on the card ------------------------------------------------------ */

bool radl_read_set(const char *path, int *game, int *count, int *ids,
                   char (*badges)[24], int max, char *hash)
{
	FILE *f = fopen(path, "r");
	char *line = NULL;
	size_t cap = 0;
	ssize_t got;
	int n = 0;
	bool head = false;

	if (game) *game = 0;
	if (count) *count = 0;
	if (hash) hash[0] = '\0';
	if (!f) return false;
	while ((got = getline(&line, &cap, f)) > 0) {
		if (!head) {
			const char *p;

			/* "#! zyperos-cheevos 2\tgame=1459\tconsole=7\ttitle=...\thash=..." */
			if (strncmp(line, "#! zyperos-cheevos", 18)) break;
			head = true;
			if ((p = strstr(line, "\tgame=")) && game) *game = atoi(p + 6);
			if ((p = strstr(line, "\thash=")) && hash) {
				int k = 0;

				p += 6;
				while (k < 32 && isxdigit((unsigned char)p[k])) { hash[k] = p[k]; k++; }
				hash[k == 32 ? 32 : 0] = '\0';
			}
			continue;
		}
		/* "#:\t<id>\t<points>\t<title>\t<desc>\t<badge>" */
		if (line[0] != '#' || line[1] != ':') continue;
		if (n < max) {
			char *fld[6], *rest = line + 2, *t;
			int k = 0;

			line[strcspn(line, "\r\n")] = '\0';
			while (k < 6 && (t = strsep(&rest, "\t"))) fld[k++] = t;
			if (ids && k >= 2) ids[n] = atoi(fld[1]);
			if (badges) snprintf(badges[n], sizeof badges[n], "%s", k >= 6 ? fld[5] : "");
		}
		n++;
	}
	free(line);
	fclose(f);
	if (count) *count = n;
	return head;
}

bool radl_set_stale(int set_count, int published, int checked)
{
	if (published < 0) return false;          /* not said: leave it */
	if (checked >= 0) return published != checked;
	return published != set_count;
}

/* ---- the plan ---------------------------------------------------------------- */

static bool exists(const char *p) { return access(p, F_OK) == 0; }

/* The games of a shelf a download would ask about, by its listing */
static bool game_asked(const system_cfg *s, const game_entry *e)
{
	return ra_hash_support(s->tag, e->file, s->exts) == RA_HASH_OK;
}

static int name_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/* The names in a folder, sorted, for bsearch: one directory read in place
 * of a look for each game's file. On a FAT card every look for a name walks
 * the folder, so a look a game cost the folder's length a game, and opening
 * MENU > Downloads waited on it (Tarik, 1.1.0: "It took a while to open"). */
static char **dir_names(const char *dir, int *n)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char **v = NULL;
	int cap = 0;

	*n = 0;
	if (!d) return NULL;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		if (*n == cap) {
			char **nv = realloc(v, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *v);

			if (!nv) break;
			v = nv;
		}
		if (!(v[*n] = strdup(e->d_name))) break;
		(*n)++;
	}
	closedir(d);
	if (*n) qsort(v, (size_t)*n, sizeof *v, name_cmp);
	return v;
}

static bool dir_has(char **v, int n, const char *name)
{
	return n && bsearch(&name, v, (size_t)n, sizeof *v, name_cmp);
}

bool radl_plan_one(const system_cfg *s, const char *roms_dir, radl_sys *o)
{
	game_list l;
	char dir[RADL_SET_PATH], **names;
	int k, nn = 0;

	memset(o, 0, sizeof *o);
	if (!s || !radl_consoles(s->tag, NULL)) return false;
	memset(&l, 0, sizeof l);
	if (!lib_scan_listing(roms_dir, s->folder, s->exts, &l)) return false;
	snprintf(o->folder, sizeof o->folder, "%s", s->folder);
	snprintf(o->tag, sizeof o->tag, "%s", s->tag);
	snprintf(dir, sizeof dir, "%s/%s/.cheevos", roms_dir, s->folder);
	names = dir_names(dir, &nn);
	for (k = 0; k < l.count; k++) {
		const char *name = l.items[k].name;
		bool set, none;

		if (!game_asked(s, &l.items[k])) continue;
		o->games++;
		if (strchr(name, '/')) {
			/* a game in a folder of its own: its set is in one too */
			char sp[RADL_SET_PATH], np[RADL_SET_PATH + 16];

			chv_path(roms_dir, s->folder, name, sp, sizeof sp);
			chv_none_path(sp, np, sizeof np);
			set = exists(sp);
			none = !set && exists(np);
		} else {
			char f[LIB_NAME + 8];

			snprintf(f, sizeof f, "%s.set", name);
			set = dir_has(names, nn, f);
			snprintf(f, sizeof f, "%s.none", name);
			none = !set && dir_has(names, nn, f);
		}
		if (set) o->ready++;
		else if (none) o->none++;
		else o->todo++;
	}
	for (k = 0; k < nn; k++) free(names[k]);
	free(names);
	lib_free(&l);
	return o->games > 0;
}

int radl_plan(const systems_cfg *sys, const char *roms_dir, radl_sys *out, int max)
{
	int i, n = 0;

	for (i = 0; sys && i < sys->count && n < max; i++)
		if (radl_plan_one(&sys->systems[i], roms_dir, &out[n])) n++;
	return n;
}

/* ---- the hashes kept --------------------------------------------------------- */

typedef struct {
	char file[LIB_PATH];      /* the launch path, under Roms/<folder> */
	char name[LIB_NAME];      /* the set's name */
	char set[RADL_SET_PATH];
	long long size;
	long mtime;
	char md5[33];             /* "" not made, "-" could not be */
	int  checked;             /* the set's count when last checked, -1 never */
	int  gid;
	bool from_lib;            /* the id is this system's library's: its counts were asked */
	bool had_set, had_none;
	int  published, unlocked; /* allprogress's, -1 not said */
} dl_game;

/* 2 since the DS hash (#235). A file of head 1 was written by 1.0.6's first
 * Download (#196), which hashed a DS game whole: its DS lines are a hash
 * RetroAchievements has never seen, and as the file is the same size and
 * time they were kept for good, so a DS game stayed none (Mario Kart DS on
 * the 1.0.6 test card, 2026-11-17). Its other shelves' lines are right. */
#define HASHES_HEAD "#! zyperos-rahash 2"

static bool hashes_head_ok(const char *line, const char *tag)
{
	if (!strcmp(line, HASHES_HEAD)) return true;
	if (!strcmp(line, "#! zyperos-rahash 1")) return strcmp(tag, "NDS") != 0;
	return false;
}

static void hashes_path(const char *roms, const char *folder, char *out, size_t n)
{
	snprintf(out, n, "%s/%s/.cheevos/.hashes", roms, folder);
}

/* The games' indices, sorted by file or by hash, for a lookup in a list
 * that is not in the shelf's order */
static const dl_game *g_sorting;
static int by_file_idx(const void *a, const void *b)
{
	return strcmp(g_sorting[*(const int *)a].file, g_sorting[*(const int *)b].file);
}
static int by_md5_idx(const void *a, const void *b)
{
	return strcmp(g_sorting[*(const int *)a].md5, g_sorting[*(const int *)b].md5);
}

/* What the file says for each game whose file is the same size and time,
 * on the shelf of `tag`. True when it said anything. */
static void hashes_load(const char *path, const char *tag, dl_game *games, int n)
{
	FILE *f = fopen(path, "r");
	char *line = NULL;
	size_t cap = 0;
	int *idx, i;
	bool head = false;

	if (!f) return;
	if (!(idx = malloc(sizeof *idx * (size_t)(n ? n : 1)))) { fclose(f); return; }
	for (i = 0; i < n; i++) idx[i] = i;
	g_sorting = games;
	qsort(idx, (size_t)n, sizeof *idx, by_file_idx);
	while (getline(&line, &cap, f) > 0) {
		long long size;
		long mtime;
		char md5[40];
		int checked, used = 0, lo = 0, hi = n - 1;
		const char *file;

		line[strcspn(line, "\r\n")] = '\0';
		if (!head) { head = hashes_head_ok(line, tag); if (!head) break; continue; }
		/* "<size>\t<mtime>\t<md5, - or ?>\t<checked>\t<file>" */
		if (sscanf(line, "%lld\t%ld\t%39s\t%d\t%n", &size, &mtime, md5, &checked, &used) != 4 ||
		    !used)
			continue;
		file = line + used;
		while (lo <= hi) {
			int mid = (lo + hi) / 2, d = strcmp(games[idx[mid]].file, file);

			if (!d) {
				dl_game *gm = &games[idx[mid]];

				if (gm->size != size || gm->mtime != mtime) break;  /* changed: again */
				if (strlen(md5) == 32 || !strcmp(md5, "-"))
					snprintf(gm->md5, sizeof gm->md5, "%.32s", md5);
				gm->checked = checked;
				break;
			}
			if (d < 0) lo = mid + 1; else hi = mid - 1;
		}
	}
	free(line);
	free(idx);
	fclose(f);
}

/* The games that have something to keep, through a temporary: a cut write
 * leaves the old file, never half a new one */
static void hashes_save(const char *roms, const char *folder, const dl_game *games, int n)
{
	char path[LIB_PATH * 2], tmp[LIB_PATH * 2 + 8], dir[LIB_PATH * 2];
	FILE *f;
	int i;
	bool bad = false;

	snprintf(dir, sizeof dir, "%s/%s/.cheevos", roms, folder);
	mkdir(dir, 0755);
	hashes_path(roms, folder, path, sizeof path);
	snprintf(tmp, sizeof tmp, "%s.part", path);
	if (!(f = fopen(tmp, "w"))) return;
	fprintf(f, "%s\n", HASHES_HEAD);
	for (i = 0; i < n; i++) {
		const dl_game *g = &games[i];

		if (!g->md5[0] && g->checked < 0) continue;
		fprintf(f, "%lld\t%ld\t%s\t%d\t%s\n", g->size, g->mtime,
		        g->md5[0] ? g->md5 : "?", g->checked, g->file);
	}
	if (ferror(f)) bad = true;
	if (fclose(f) != 0) bad = true;
	if (bad || rename(tmp, path) != 0) unlink(tmp);
}

/* ---- the hash thread ---------------------------------------------------------- */

typedef struct {
	pthread_mutex_t lock;
	int  refs;
	bool cancel;
	int  n, done;
	char tag[8];
	char (*rom)[RADL_PATH];
	char (*md5)[33];
	int  *gi;
} hjob;

static void hjob_unref(hjob *j)
{
	int left;

	pthread_mutex_lock(&j->lock);
	left = --j->refs;
	pthread_mutex_unlock(&j->lock);
	if (left) return;
	free(j->rom);
	free(j->md5);
	free(j->gi);
	pthread_mutex_destroy(&j->lock);
	free(j);
}

static void *hjob_run(void *arg)
{
	hjob *j = arg;
	int i;

#if defined(__linux__)
	/* As a first play's (src/rahashjob.c): the page's frames come first */
	setpriority(PRIO_PROCESS, 0, 10);
#endif
	for (i = 0; i < j->n; i++) {
		char h[33] = "";
		bool stop, ok;

		pthread_mutex_lock(&j->lock);
		stop = j->cancel;
		pthread_mutex_unlock(&j->lock);
		if (stop) break;
		ok = ra_hash_rom(j->rom[i], j->tag, h);
		pthread_mutex_lock(&j->lock);
		snprintf(j->md5[i], sizeof j->md5[i], "%s", ok ? h : "-");
		j->done = i + 1;
		pthread_mutex_unlock(&j->lock);
	}
	hjob_unref(j);
	return NULL;
}

/* ---- the badge thread ----------------------------------------------------------- */

typedef struct {
	pthread_mutex_t lock;
	int  refs;
	bool done;
	int  got;
	char set[RADL_SET_PATH];
} bjob;

static void bjob_unref(bjob *j)
{
	int left;

	pthread_mutex_lock(&j->lock);
	left = --j->refs;
	pthread_mutex_unlock(&j->lock);
	if (left) return;
	pthread_mutex_destroy(&j->lock);
	free(j);
}

/* The set's badges the card lacks, in one curl (net_get_pngs) */
static void *bjob_run(void *arg)
{
	bjob *j = arg;
	static const int MAXB = CHV_MAX;
	char (*names)[24] = calloc((size_t)MAXB, sizeof *names);
	char (*url)[96] = calloc((size_t)MAXB, sizeof *url);
	char (*path)[LIB_PATH * 2 + 128] = calloc((size_t)MAXB, sizeof *path);
	const char **urls = calloc((size_t)MAXB, sizeof *urls);
	const char **paths = calloc((size_t)MAXB, sizeof *paths);
	int count = 0, n = 0, i, got = 0;

	if (names && url && path && urls && paths &&
	    radl_read_set(j->set, NULL, &count, NULL, names, MAXB, NULL)) {
		const char *slash = strrchr(j->set, '/');
		int dlen = slash ? (int)(slash - j->set + 1) : 0;
		char dir[LIB_PATH * 2 + 80], pack[RADL_SET_PATH + 32];

		if (count > MAXB) count = MAXB;
		/* Any the card has loose into the set's pack first (src/rapack.h) */
		rp_pack(j->set, (const char (*)[RP_NAME])names, count, NULL);
		rp_pack_path(j->set, pack, sizeof pack);
		for (i = 0; i < count; i++) {
			if (!names[i][0]) continue;
			snprintf(path[n], sizeof path[n], "%.*sbadges/%s.png", dlen, j->set, names[i]);
			if (access(path[n], R_OK) == 0 || rp_has(pack, names[i])) continue;
			snprintf(url[n], sizeof url[n], RADL_BADGE_URL "%s.png", names[i]);
			urls[n] = url[n];
			paths[n] = path[n];
			n++;
		}
		if (n) {
			snprintf(dir, sizeof dir, "%.*sbadges", dlen, j->set);
			mkdir(dir, 0755);
			/* As a set's badges after a game (src/rabadge.c) */
			got = net_get_pngs(urls, paths, n, 30 + n);
			if (got > 0) rp_pack(j->set, (const char (*)[RP_NAME])names, count, NULL);
		}
	}
	free(names); free(url); free(path); free(urls); free(paths);
	pthread_mutex_lock(&j->lock);
	j->got = got;
	j->done = true;
	pthread_mutex_unlock(&j->lock);
	bjob_unref(j);
	return NULL;
}

/* ---- the run ------------------------------------------------------------------- */

enum { RQ_LIB, RQ_PROG, RQ_GAMEID, RQ_PATCH, RQ_UNLOCKS };
enum { AS_IDLE, AS_READY, AS_FLIGHT, AS_WAIT };
enum { PH_NEXT_SYS, PH_SYS_ASK, PH_SYS_HASH, PH_GAMES, PH_BADGES, PH_DONE };
enum { G_LOOK, G_GAMEID, G_PATCH, G_UNLOCKS, G_BADGES, G_NEXT };

/* A ring of sets whose badges are wanted, for the one badge thread */
#define BQ 16

typedef struct { int gid, published, unlocked; } prog_row;

static struct {
	bool active;
	const systems_cfg *sys;
	char roms[LIB_PATH];
	char only[CFG_STR];
	char user[RA_USER_MAX], token[RA_TOKEN_MAX];
	int  si;                         /* the system, an index into sys */
	int  phase;
	radl_progress p;
	unsigned next_at;                /* the pace: no request before this */
	int  give_ups;                   /* requests given up on, in a row */

	/* the system now */
	dl_game *games;
	int  ng;
	int  cons[RADL_CONSOLES], ncons;
	bool need_lib, lib_failed;
	int  sys_asks[RADL_CONSOLES * 2][2]; /* kind, console */
	int  nsys_asks, sys_ask;
	char *lib_body[RADL_CONSOLES];
	size_t lib_len[RADL_CONSOLES];
	int  nlib;
	char *prog_body[RADL_CONSOLES];
	size_t prog_len[RADL_CONSOLES];
	int  nprog;
	prog_row *prog;
	int  nprog_rows;
	bool prog_ok;                    /* every console's counts came */
	hjob *hj;
	int  hseen;
	bool cache_dirty;                /* a hash or a count to keep */
	int  gi, gs;
	int  set_ids[CHV_MAX], set_count;

	/* the request in flight or waiting */
	struct {
		int  kind, arg, state, tries, why;
		unsigned at;
		char *body;
		size_t len;
	} ask;
	char tmp[256];

	/* badges */
	bjob *bj;
	char bq[BQ][RADL_SET_PATH];
	int  bq_head, bq_n;
} g;

static const unsigned BACKOFF[] = { 5000, 15000, 30000 };

static void log_line(const char *what)
{
	fprintf(stderr, "radl: %s\n", what);
}

static void free_system(void)
{
	int i;

	if (g.hj) {
		pthread_mutex_lock(&g.hj->lock);
		g.hj->cancel = true;
		pthread_mutex_unlock(&g.hj->lock);
		hjob_unref(g.hj);
		g.hj = NULL;
	}
	for (i = 0; i < RADL_CONSOLES; i++) {
		free(g.lib_body[i]); g.lib_body[i] = NULL;
		free(g.prog_body[i]); g.prog_body[i] = NULL;
	}
	g.nlib = g.nprog = 0;
	free(g.prog); g.prog = NULL; g.nprog_rows = 0;
	free(g.games); g.games = NULL; g.ng = 0;
}

/* The hash thread's answers since the last look; true when it is done */
static bool hashes_take(void)
{
	bool done;
	int k;

	if (!g.hj) return true;
	pthread_mutex_lock(&g.hj->lock);
	for (k = g.hseen; k < g.hj->done; k++) {
		snprintf(g.games[g.hj->gi[k]].md5, sizeof g.games[0].md5, "%s", g.hj->md5[k]);
		g.cache_dirty = true;
	}
	g.hseen = g.hj->done;
	g.p.hashed = g.hj->done;
	done = g.hj->done >= g.hj->n;
	pthread_mutex_unlock(&g.hj->lock);
	if (done) { hjob_unref(g.hj); g.hj = NULL; }
	return done;
}

/* ---- asking ---- */

static void ask_start(int kind, int arg, unsigned now)
{
	g.ask.kind = kind;
	g.ask.arg = arg;
	g.ask.state = AS_READY;
	g.ask.tries = 0;
	g.ask.at = now;
	free(g.ask.body);
	g.ask.body = NULL;
	g.ask.len = 0;
}

static bool ask_send(void)
{
	static char a1[40], a2[40];       /* static: the fields outlive the call */
	net_field f[6];
	int n = 0;

	f[n].k = "r";
	switch (g.ask.kind) {
	case RQ_LIB:
		/* No account: it is public, and the account's limit is not spent */
		f[n++].v = "hashlibrary";
		snprintf(a1, sizeof a1, "%d", g.ask.arg);
		f[n].k = "c"; f[n++].v = a1;
		break;
	case RQ_PROG:
		f[n++].v = "allprogress";
		f[n].k = "u"; f[n++].v = g.user;
		f[n].k = "t"; f[n++].v = g.token;
		snprintf(a1, sizeof a1, "%d", g.ask.arg);
		f[n].k = "c"; f[n++].v = a1;
		break;
	case RQ_GAMEID:
		f[n++].v = "gameid";
		f[n].k = "m"; f[n++].v = g.games[g.ask.arg].md5;
		break;
	case RQ_PATCH:
		f[n++].v = "patch";
		f[n].k = "u"; f[n++].v = g.user;
		f[n].k = "t"; f[n++].v = g.token;
		snprintf(a2, sizeof a2, "%d", g.games[g.ask.arg].gid);
		f[n].k = "g"; f[n++].v = a2;
		break;
	case RQ_UNLOCKS:
		f[n++].v = "unlocks";
		f[n].k = "u"; f[n++].v = g.user;
		f[n].k = "t"; f[n++].v = g.token;
		snprintf(a2, sizeof a2, "%d", g.games[g.ask.arg].gid);
		f[n].k = "g"; f[n++].v = a2;
		f[n].k = "h"; f[n++].v = "0";
		break;
	default:
		return false;
	}
	/* A console's hash library is a big reply: a minute for it */
	return net_post_async_status(f, n, g.tmp, g.ask.kind == RQ_LIB ? 60 : 30);
}

/* 0 still going, 1 answered (the body in g.ask), -1 given up on, -2 the
 * account refused: the run stops */
static int ask_drive(unsigned now)
{
	int rc, http;

	if (g.ask.state == AS_WAIT) {
		if (now < g.ask.at) return 0;
		if (g.ask.why == RADL_NO_NETWORK && !net_online()) {
			g.ask.at = now + 1000;           /* a look a second */
			return 0;
		}
		g.ask.state = AS_READY;
	}
	if (g.ask.state == AS_READY) {
		g.p.state = RADL_WORKING;
		if (now < g.ask.at || now < g.next_at) return 0;
		/* The one async slot is somebody else's for now: their turn first */
		if (net_async_held()) return 0;
		g.next_at = now + RADL_GAP_MS;
		if (!ask_send()) {
			/* curl did not start: as no answer */
			if (!net_online()) {
				g.ask.state = AS_WAIT; g.ask.why = RADL_NO_NETWORK; g.ask.at = now + 1000;
				g.p.state = RADL_NO_NETWORK;
				return 0;
			}
			if (g.ask.tries < 3) {
				g.ask.at = now + BACKOFF[g.ask.tries++];
				g.ask.state = AS_WAIT; g.ask.why = RADL_RETRYING;
				g.p.state = RADL_RETRYING; g.p.resume_at = g.ask.at;
				return 0;
			}
			g.ask.state = AS_IDLE;
			return -1;
		}
		g.p.requests++;
		g.ask.state = AS_FLIGHT;
		return 0;
	}
	if (g.ask.state != AS_FLIGHT) return -1;

	rc = net_async_poll();
	if (rc == 0) return 0;
	http = net_async_http();
	/* One line a request, for "why was it slow" on a card read afterwards:
	 * what was asked, the answer, how long it took */
	{
		static const char *KIND[] = { "hashlibrary", "allprogress", "gameid", "patch", "unlocks" };
		int what = g.ask.kind >= RQ_GAMEID && g.games ? g.games[g.ask.arg].gid : g.ask.arg;

		fprintf(stderr, "radl: %s %d: HTTP %d, %d ms\n",
		        g.ask.kind >= 0 && g.ask.kind <= RQ_UNLOCKS ? KIND[g.ask.kind] : "?", what, http,
		        net_async_ms());
	}
	if (rc == 1 && (http == 0 || (http >= 200 && http < 300))) {
		g.ask.body = file_read(g.tmp, LONG_MAX, &g.ask.len);
		unlink(g.tmp);
		g.ask.state = AS_IDLE;
		g.give_ups = 0;
		g.p.state = RADL_WORKING;
		return g.ask.body ? 1 : -1;
	}
	unlink(g.tmp);
	if (http == 429) {
		/* Too many: the server's minute, and the same request again. Not a
		 * try: it is an answer, and a polite one. */
		fprintf(stderr, "radl: RetroAchievements asked to slow down; a minute\n");
		g.ask.state = AS_WAIT; g.ask.why = RADL_SLOWED;
		g.ask.at = now + RADL_SLOW_MS;
		g.p.state = RADL_SLOWED; g.p.resume_at = g.ask.at;
		return 0;
	}
	if (http == 401 || http == 403) {
		fprintf(stderr, "radl: the account was refused (HTTP %d)\n", http);
		g.ask.state = AS_IDLE;
		return -2;
	}
	if (http == 0 && !net_online()) {
		fprintf(stderr, "radl: the network went; waiting for it\n");
		g.ask.state = AS_WAIT; g.ask.why = RADL_NO_NETWORK; g.ask.at = now + 1000;
		g.p.state = RADL_NO_NETWORK;
		return 0;
	}
	if ((http == 0 || http >= 500) && g.ask.tries < 3) {
		g.ask.at = now + BACKOFF[g.ask.tries++];
		g.ask.state = AS_WAIT; g.ask.why = RADL_RETRYING;
		g.p.state = RADL_RETRYING; g.p.resume_at = g.ask.at;
		fprintf(stderr, "radl: no answer (HTTP %d, curl %d); again in %u s\n",
		        http, net_async_exit(), (g.ask.at - now) / 1000);
		return 0;
	}
	fprintf(stderr, "radl: gave up on a request (HTTP %d)\n", http);
	g.ask.state = AS_IDLE;
	g.give_ups++;
	return -1;
}

/* ---- a system ---- */

static void stop(int problem)
{
	g.p.problem = problem;
	g.phase = PH_DONE;
}

/* The next system with games to ask about, read off the card. False at the
 * end of the list. */
static bool system_open(void)
{
	char hp[LIB_PATH * 2];
	game_list l;
	int k;

	free_system();
	for (; g.si < g.sys->count; g.si++) {
		const system_cfg *s = &g.sys->systems[g.si];

		if (g.only[0] && strcmp(g.only, s->folder)) continue;
		if (!(g.ncons = radl_consoles(s->tag, g.cons))) continue;
		memset(&l, 0, sizeof l);
		if (!lib_scan_listing(g.roms, s->folder, s->exts, &l)) continue;
		g.games = calloc((size_t)(l.count ? l.count : 1), sizeof *g.games);
		if (!g.games) { lib_free(&l); continue; }
		g.ng = 0;
		for (k = 0; k < l.count; k++) {
			dl_game *d = &g.games[g.ng];
			char rom[RADL_PATH], none[sizeof d->set + 16];
			struct stat st;

			if (!game_asked(s, &l.items[k])) continue;
			snprintf(d->file, sizeof d->file, "%s", l.items[k].file);
			snprintf(d->name, sizeof d->name, "%s", l.items[k].name);
			chv_path(g.roms, s->folder, d->name, d->set, sizeof d->set);
			snprintf(rom, sizeof rom, "%s/%s/%s", g.roms, s->folder, d->file);
			if (stat(rom, &st) == 0) { d->size = (long long)st.st_size; d->mtime = (long)st.st_mtime; }
			d->checked = d->published = d->unlocked = -1;
			d->had_set = exists(d->set);
			chv_none_path(d->set, none, sizeof none);
			d->had_none = !d->had_set && exists(none);
			g.ng++;
		}
		lib_free(&l);
		if (!g.ng) { free(g.games); g.games = NULL; continue; }
		break;
	}
	if (g.si >= g.sys->count) return false;

	{
		const system_cfg *s = &g.sys->systems[g.si];
		char msg[CFG_STR + 64];
		int i, want = 0;

		snprintf(g.p.folder_now, sizeof g.p.folder_now, "%s", s->folder);
		hashes_path(g.roms, s->folder, hp, sizeof hp);
		hashes_load(hp, s->tag, g.games, g.ng);

		/* A game with a set needs no hash: its id is in the set. The rest
		 * are looked up by theirs. */
		g.need_lib = false;
		for (i = 0; i < g.ng; i++)
			if (!g.games[i].had_set) {
				g.need_lib = true;
				if (!g.games[i].md5[0]) want++;
			}
		g.hseen = 0;
		g.cache_dirty = false;
		g.p.hashing = want;
		g.p.hashed = 0;
		if (want) {
			hjob *j = calloc(1, sizeof *j);
			pthread_t t;
			pthread_attr_t at;
			int k2 = 0;
			bool ok = false;

			if (j) {
				j->rom = calloc((size_t)want, sizeof *j->rom);
				j->md5 = calloc((size_t)want, sizeof *j->md5);
				j->gi = calloc((size_t)want, sizeof *j->gi);
			}
			if (j && j->rom && j->md5 && j->gi) {
				pthread_mutex_init(&j->lock, NULL);
				j->n = want;
				j->refs = 2;             /* this side, and the thread */
				snprintf(j->tag, sizeof j->tag, "%s", s->tag);
				for (i = 0; i < g.ng; i++) {
					if (g.games[i].had_set || g.games[i].md5[0]) continue;
					snprintf(j->rom[k2], sizeof j->rom[k2], "%s/%s/%s", g.roms, s->folder,
					         g.games[i].file);
					j->gi[k2++] = i;
				}
				pthread_attr_init(&at);
				pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
				ok = pthread_create(&t, &at, hjob_run, j) == 0;
				pthread_attr_destroy(&at);
				if (ok) g.hj = j;
				else { j->refs = 1; hjob_unref(j); }
			} else if (j) {
				free(j->rom); free(j->md5); free(j->gi); free(j);
			}
			if (!ok) g.p.hashing = 0;   /* no thread: those games fail below */
		}

		/* The system's requests, while the hashes are made */
		g.nsys_asks = 0;
		g.sys_ask = 0;
		g.lib_failed = false;
		g.prog_ok = true;
		for (i = 0; i < g.ncons && g.need_lib; i++) {
			g.sys_asks[g.nsys_asks][0] = RQ_LIB;
			g.sys_asks[g.nsys_asks++][1] = g.cons[i];
		}
		for (i = 0; i < g.ncons; i++) {
			g.sys_asks[g.nsys_asks][0] = RQ_PROG;
			g.sys_asks[g.nsys_asks++][1] = g.cons[i];
		}
		snprintf(msg, sizeof msg, "%s: %d games, %d to hash", s->folder, g.ng, want);
		log_line(msg);
	}
	return true;
}

static int cmp_prog(const void *a, const void *b)
{
	const prog_row *x = a, *y = b;
	return (x->gid > y->gid) - (x->gid < y->gid);
}

/* The hash libraries and the account's counts, into the games */
static void system_match(void)
{
	int *idx = malloc(sizeof *idx * (size_t)(g.ng ? g.ng : 1));
	int n = 0, i, c;

	if (idx) {
		for (i = 0; i < g.ng; i++)
			if (!g.games[i].had_set && strlen(g.games[i].md5) == 32) idx[n++] = i;
		g_sorting = g.games;
		qsort(idx, (size_t)n, sizeof *idx, by_md5_idx);
	}
	for (c = 0; c < g.nlib && idx; c++) {
		jsv root, list, it = { 0 }, k, v;

		if (!g.lib_body[c]) continue;
		root = js_root(g.lib_body[c], g.lib_len[c]);
		if (!js_member(root, "MD5List", &list)) { g.lib_failed = true; continue; }
		while (js_next_member(list, &it, &k, &v)) {
			char md5[40];
			int lo = 0, hi = n - 1;

			if (!js_str(k, md5, sizeof md5) || strlen(md5) != 32) continue;
			for (char *p = md5; *p; p++) *p = (char)tolower((unsigned char)*p);
			while (lo <= hi) {
				int mid = (lo + hi) / 2, d = strcmp(g.games[idx[mid]].md5, md5);

				if (!d) {
					/* Every game with this hash: the same ROM twice */
					int a = mid, b = mid;

					while (a > 0 && !strcmp(g.games[idx[a - 1]].md5, md5)) a--;
					while (b < n - 1 && !strcmp(g.games[idx[b + 1]].md5, md5)) b++;
					for (; a <= b; a++) {
						g.games[idx[a]].gid = (int)js_int(v);
						g.games[idx[a]].from_lib = true;
					}
					break;
				}
				if (d < 0) lo = mid + 1; else hi = mid - 1;
			}
		}
	}
	free(idx);

	/* The account's counts, by game id, for each game's look */
	for (c = 0; c < g.nprog; c++) {
		jsv root, resp, it = { 0 }, k, v, f;
		prog_row *grown;

		if (!g.prog_body[c]) continue;
		root = js_root(g.prog_body[c], g.prog_len[c]);
		/* `[]`, not `{}`, when the console has none: PHP's empty array */
		if (!js_member(root, "Response", &resp)) { g.prog_ok = false; continue; }
		while (js_next_member(resp, &it, &k, &v)) {
			char id[24];

			if (!js_str(k, id, sizeof id)) continue;
			grown = realloc(g.prog, sizeof *g.prog * (size_t)(g.nprog_rows + 1));
			if (!grown) break;
			g.prog = grown;
			g.prog[g.nprog_rows].gid = atoi(id);
			g.prog[g.nprog_rows].published =
				js_member(v, "Achievements", &f) ? (int)js_int(f) : -1;
			g.prog[g.nprog_rows].unlocked =
				js_member(v, "Unlocked", &f) ? (int)js_int(f) : 0;
			g.nprog_rows++;
		}
	}
	if (g.nprog_rows) qsort(g.prog, (size_t)g.nprog_rows, sizeof *g.prog, cmp_prog);
	for (c = 0; c < RADL_CONSOLES; c++) {
		free(g.lib_body[c]); g.lib_body[c] = NULL;
		free(g.prog_body[c]); g.prog_body[c] = NULL;
	}
}

static const prog_row *prog_of(int gid)
{
	prog_row key = { gid, 0, 0 };

	if (!g.prog || gid <= 0) return NULL;
	return bsearch(&key, g.prog, (size_t)g.nprog_rows, sizeof *g.prog, cmp_prog);
}

/* ---- a game ---- */

static void game_none(dl_game *d)
{
	if (!d->had_none) chv_none_mark(d->set, true);
	g.p.none++;
}

static void game_next(void)
{
	g.gs = G_LOOK;
	g.gi++;
	g.p.games_done++;
}

/* The set just read for this game: its id, count and achievement ids */
static bool game_read_set(dl_game *d, char *hash)
{
	int gid = 0;

	if (!radl_read_set(d->set, &gid, &g.set_count, g.set_ids, NULL, CHV_MAX, hash))
		return false;
	if (gid > 0) d->gid = gid;
	return true;
}

/* A game with no set whose id is known now: its set is asked for, unless the
 * account's counts, which list every game of the console that has
 * achievements, do not list it - nothing to earn, and no request to learn
 * that again on every run. Only for an id from this system's own library:
 * one r=gameid found is another console's game, whose counts were not asked. */
static void game_known(dl_game *d)
{
	const prog_row *pr = prog_of(d->gid);

	if (pr) {
		d->published = pr->published;
		d->unlocked = pr->unlocked;
	} else if (g.prog_ok && d->from_lib) {
		game_none(d);
		g.gs = G_NEXT;
		return;
	}
	g.gs = G_PATCH;
}

static void game_look(dl_game *d)
{
	const prog_row *pr;

	snprintf(g.p.now, sizeof g.p.now, "%s", d->name);
	if (d->had_set) {
		if (!game_read_set(d, NULL) || d->gid <= 0) {
			/* Not a set we can read: left as it is */
			g.p.ready++;
			g.gs = G_NEXT;
			return;
		}
		if ((pr = prog_of(d->gid))) { d->published = pr->published; d->unlocked = pr->unlocked; }
		if (radl_set_stale(g.set_count, d->published, d->checked)) {
			g.gs = G_PATCH;
			return;
		}
		if (d->published >= 0 && d->checked != d->published) {
			d->checked = d->published;
			g.cache_dirty = true;
		}
		g.p.ready++;
		g.gs = G_UNLOCKS;
		return;
	}
	if (!strcmp(d->md5, "-")) { game_none(d); g.gs = G_NEXT; return; }
	if (strlen(d->md5) != 32) { g.p.failed++; g.gs = G_NEXT; return; }
	if (d->gid > 0) { game_known(d); return; }
	/* Not in the system's hash library. Asked once by its hash alone, as a
	 * first play would ask it: a game on another console's shelf (a Game Boy
	 * game among the Game Boy Color's, a Master System one among the Game
	 * Gear's) is in that console's library, not this one's. Once marked none,
	 * the library is all a later run asks - unless it never came. */
	if (d->had_none && !g.lib_failed) { game_none(d); g.gs = G_NEXT; return; }
	g.gs = G_GAMEID;
}

static void game_patched(dl_game *d)
{
	char hash[33] = "", msg[LIB_NAME + 64];
	bool ok, had = d->had_set;

	if (had) {
		/* A set from before keeps its hash in the new one */
		radl_read_set(d->set, NULL, NULL, NULL, NULL, 0, hash);
	} else {
		snprintf(hash, sizeof hash, "%s", d->md5);
	}
	ok = ra_set_from_json_hash(g.ask.body, g.ask.len, d->gid, hash, d->set);
	if (!ok) {
		if (ra_set_json_empty(g.ask.body, g.ask.len)) {
			/* Nothing a player can earn. A set from before stays. */
			if (had) g.p.ready++;
			else game_none(d);
		} else {
			if (had) g.p.ready++;
			g.p.failed++;
		}
		g.gs = G_NEXT;
		return;
	}
	if (!had) chv_none_mark(d->set, false);    /* a set now: no marker */
	d->had_set = true;
	d->had_none = false;
	if (had) g.p.updated++; else g.p.fetched++;
	g.p.ready++;
	game_read_set(d, NULL);
	d->checked = d->published >= 0 ? d->published : g.set_count;
	g.cache_dirty = true;
	if (!prog_of(d->gid)) d->unlocked = -1;
	g.p.set_seq++;
	snprintf(g.p.set_path, sizeof g.p.set_path, "%s", d->set);
	snprintf(g.p.set_title, sizeof g.p.set_title, "%.*s", (int)sizeof g.p.set_title - 1, d->name);
	g.p.set_count = g.set_count;
	snprintf(msg, sizeof msg, "%s: %d achievements (%s)", d->name, g.set_count,
	         had ? "updated" : "new");
	log_line(msg);
	g.gs = G_UNLOCKS;
}

static void game_unlocks(dl_game *d)
{
	jsv root, arr, it = { 0 }, e;
	int added = 0;

	root = js_root(g.ask.body, g.ask.len);
	if (!js_member(root, "UserUnlocks", &arr)) { g.gs = G_BADGES; return; }
	while (js_next(arr, &it, &e)) {
		int id = (int)js_int(e), k;

		/* Only the set's: the account carries RetroAchievements' notices
		 * too (ra_merge_unlocks says why) */
		for (k = 0; k < g.set_count && k < CHV_MAX; k++)
			if (g.set_ids[k] == id) break;
		if (k == g.set_count || k == CHV_MAX) continue;
		if (chv_note_earned(d->gid, id, true)) added++;
	}
	if (added) chv_earned_save();
	g.p.unlocks += added;
	g.gs = G_BADGES;
}

static bool badges_queue(const char *set)
{
	if (g.bq_n >= BQ) return false;
	snprintf(g.bq[(g.bq_head + g.bq_n) % BQ], sizeof g.bq[0], "%s", set);
	g.bq_n++;
	return true;
}

/* The badge thread: the last one's count in, the next one out */
static void badges_pump(void)
{
	if (g.bj) {
		bool done;
		int got;

		pthread_mutex_lock(&g.bj->lock);
		done = g.bj->done;
		got = g.bj->got;
		pthread_mutex_unlock(&g.bj->lock);
		if (!done) return;
		g.p.badges += got;
		bjob_unref(g.bj);
		g.bj = NULL;
	}
	if (g.bq_n && net_online()) {
		bjob *j = calloc(1, sizeof *j);
		pthread_t t;
		pthread_attr_t at;
		bool ok;

		if (!j) return;
		pthread_mutex_init(&j->lock, NULL);
		j->refs = 2;
		snprintf(j->set, sizeof j->set, "%s", g.bq[g.bq_head]);
		g.bq_head = (g.bq_head + 1) % BQ;
		g.bq_n--;
		pthread_attr_init(&at);
		pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
		ok = pthread_create(&t, &at, bjob_run, j) == 0;
		pthread_attr_destroy(&at);
		if (ok) g.bj = j;
		else { j->refs = 1; bjob_unref(j); }
	}
}

/* Whether the set's badges are all on the card already: a read of the set
 * and an access() a badge, so a set that has them all costs no thread */
static bool badges_missing(const char *set)
{
	char (*names)[24] = calloc(CHV_MAX, sizeof *names);
	const char *slash = strrchr(set, '/');
	int dlen = slash ? (int)(slash - set + 1) : 0, n = 0, i;
	char pack[RADL_SET_PATH + 32] = "";
	bool any = false;

	if (!names) return false;
	rp_pack_path(set, pack, sizeof pack);
	if (radl_read_set(set, NULL, &n, NULL, names, CHV_MAX, NULL)) {
		if (n > CHV_MAX) n = CHV_MAX;
		for (i = 0; i < n && !any; i++) {
			char p[LIB_PATH * 2 + 128];

			if (!names[i][0]) continue;
			snprintf(p, sizeof p, "%.*sbadges/%s.png", dlen, set, names[i]);
			any = access(p, R_OK) != 0 && !rp_has(pack, names[i]);
		}
	}
	free(names);
	return any;
}

_Static_assert(RP_NAME == 24, "a pack's names are a set's badge names");

/* Whether the folder has a loose badge */
static bool loose_in(const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	bool any = false;

	while (d && !any && (e = readdir(d))) {
		size_t l = strlen(e->d_name);

		any = l > 4 && !strcmp(e->d_name + l - 4, ".png");
	}
	if (d) closedir(d);
	return any;
}

int radl_badges_migrate(const char *roms)
{
	char (*names)[24] = calloc(CHV_MAX, sizeof *names);
	DIR *rd = roms ? opendir(roms) : NULL;
	struct dirent *f;
	int packed = 0, sets = 0;

	while (names && rd && (f = readdir(rd))) {
		char dir[RADL_SET_PATH], bdir[RADL_SET_PATH + 16];
		DIR *cd;
		struct dirent *e;

		if (f->d_name[0] == '.') continue;
		snprintf(dir, sizeof dir, "%s/%s/.cheevos", roms, f->d_name);
		snprintf(bdir, sizeof bdir, "%s/badges", dir);
		if (!loose_in(bdir) || !(cd = opendir(dir))) continue;
		while ((e = readdir(cd))) {
			char set[RADL_SET_PATH + 300];
			size_t l = strlen(e->d_name);
			int n = 0, took = 0;

			if (l <= 4 || strcmp(e->d_name + l - 4, ".set")) continue;
			snprintf(set, sizeof set, "%s/%s", dir, e->d_name);
			if (!radl_read_set(set, NULL, &n, NULL, names, CHV_MAX, NULL)) continue;
			if (n > CHV_MAX) n = CHV_MAX;
			rp_pack(set, (const char (*)[RP_NAME])names, n, &took);
			if (took > 0) { packed += took; sets++; }
		}
		closedir(cd);
	}
	if (rd) closedir(rd);
	free(names);
	if (sets) fprintf(stderr, "badges: %d loose badges of %d sets into their packs\n", packed, sets);
	return packed;
}

/* One step of the game at g.gi: 0 it is waiting, 1 it moved on */
static int game_step(unsigned now)
{
	dl_game *d = &g.games[g.gi];
	int rc;

	switch (g.gs) {
	case G_LOOK:
		game_look(d);
		return 1;
	case G_GAMEID:
	case G_PATCH:
	case G_UNLOCKS: {
		int kind = g.gs == G_GAMEID ? RQ_GAMEID : g.gs == G_PATCH ? RQ_PATCH : RQ_UNLOCKS;

		if (g.gs == G_UNLOCKS &&
		    !(d->gid > 0 && d->had_set && d->unlocked > chv_synced_for(d->gid))) {
			g.gs = G_BADGES;
			return 1;
		}
		if (g.ask.state == AS_IDLE || g.ask.kind != kind || g.ask.arg != g.gi)
			ask_start(kind, g.gi, now);
		rc = ask_drive(now);
		if (rc == 0) return 0;
		if (rc == -2) { stop(RADL_SIGN_IN); return 1; }
		if (rc < 0) {
			if (g.give_ups >= 5) { stop(RADL_NO_ANSWER); return 1; }
			if (g.gs != G_UNLOCKS) {
				if (d->had_set) g.p.ready++;
				g.p.failed++;
				g.gs = G_NEXT;
			} else {
				g.gs = G_BADGES;
			}
			g.ask.state = AS_IDLE;
			return 1;
		}
		if (kind == RQ_GAMEID) {
			jsv v;

			d->gid = js_member(js_root(g.ask.body, g.ask.len), "GameID", &v) ? (int)js_int(v) : -1;
			d->from_lib = false;
			if (d->gid < 0) { d->gid = 0; g.p.failed++; g.gs = G_NEXT; }
			else if (d->gid == 0) { game_none(d); g.gs = G_NEXT; }
			else game_known(d);
		} else if (kind == RQ_PATCH) {
			game_patched(d);
		} else {
			game_unlocks(d);
		}
		free(g.ask.body);
		g.ask.body = NULL;
		g.ask.kind = -1;
		return 1;
	}
	case G_BADGES:
		if (d->had_set && badges_missing(d->set) && !badges_queue(d->set)) return 0;
		g.gs = G_NEXT;
		return 1;
	case G_NEXT:
	default:
		game_next();
		/* The card's room, every sixteen games */
		if ((g.p.games_done & 15) == 0) {
			struct statvfs vfs;

			if (statvfs(g.roms, &vfs) == 0 &&
			    (unsigned long long)vfs.f_bavail * vfs.f_frsize < RADL_FLOOR)
				stop(RADL_NO_ROOM);
		}
		return 1;
	}
}

/* ---- the run's outside ---- */

bool radl_begin(const systems_cfg *sys, const char *roms_dir, const char *only,
                unsigned now_ms)
{
	radl_sys *plan;
	int n, i;

	if (g.active || !sys || !roms_dir) return false;
	if (!ra_signed_in() || !net_online()) return false;
	memset(&g.p, 0, sizeof g.p);
	g.sys = sys;
	snprintf(g.roms, sizeof g.roms, "%s", roms_dir);
	snprintf(g.only, sizeof g.only, "%s", only ? only : "");
	ra_creds_copy(g.user, sizeof g.user, g.token, sizeof g.token);
	snprintf(g.tmp, sizeof g.tmp, RADL_TMP "/zyperos-radl-%ld.json", (long)getpid());

	/* What there is, for the bar */
	plan = calloc(CFG_MAX_SYSTEMS, sizeof *plan);
	if (!plan) return false;
	n = radl_plan(sys, roms_dir, plan, CFG_MAX_SYSTEMS);
	for (i = 0; i < n; i++) {
		if (g.only[0] && strcmp(g.only, plan[i].folder)) continue;
		g.p.systems++;
		g.p.games += plan[i].games;
	}
	free(plan);

	g.si = 0;
	g.phase = PH_NEXT_SYS;
	g.next_at = now_ms;
	g.give_ups = 0;
	g.ask.state = AS_IDLE;
	g.ask.kind = -1;
	g.bq_head = g.bq_n = 0;
	g.active = true;
	fprintf(stderr, "radl: a run, %d systems, %d games\n", g.p.systems, g.p.games);
	return true;
}

static void save_system(void)
{
	if (g.games && g.si < g.sys->count) {
		hashes_take();
		if (g.cache_dirty)
			hashes_save(g.roms, g.sys->systems[g.si].folder, g.games, g.ng);
		g.cache_dirty = false;
	}
}

int radl_step(unsigned now)
{
	int guard;

	if (!g.active) return -1;
	badges_pump();

	/* A few moves a step, never a wait: a cached game is a look and a move */
	for (guard = 0; guard < 8; guard++) {
		switch (g.phase) {
		case PH_NEXT_SYS:
			if (!system_open()) { g.phase = PH_BADGES; break; }
			g.phase = PH_SYS_ASK;
			break;
		case PH_SYS_ASK: {
			int rc;

			hashes_take();
			if (g.sys_ask >= g.nsys_asks) { g.phase = PH_SYS_HASH; break; }
			if (g.ask.state == AS_IDLE)
				ask_start(g.sys_asks[g.sys_ask][0], g.sys_asks[g.sys_ask][1], now);
			rc = ask_drive(now);
			if (rc == 0) return 1;
			if (rc == -2) { stop(RADL_SIGN_IN); break; }
			if (g.sys_asks[g.sys_ask][0] == RQ_LIB) {
				if (rc == 1 && g.nlib < RADL_CONSOLES) {
					g.lib_body[g.nlib] = g.ask.body;
					g.lib_len[g.nlib++] = g.ask.len;
					g.ask.body = NULL;
				} else {
					g.lib_failed = true;
				}
			} else if (rc == 1 && g.nprog < RADL_CONSOLES) {
				g.prog_body[g.nprog] = g.ask.body;
				g.prog_len[g.nprog++] = g.ask.len;
				g.ask.body = NULL;
			} else {
				g.prog_ok = false;
			}
			free(g.ask.body);
			g.ask.body = NULL;
			g.ask.state = AS_IDLE;
			g.sys_ask++;
			if (g.give_ups >= 5) stop(RADL_NO_ANSWER);
			break;
		}
		case PH_SYS_HASH:
			if (!hashes_take()) { g.p.state = RADL_HASHING; return 1; }
			g.p.state = RADL_WORKING;
			/* Kept now: a run stopped later has them */
			if (g.cache_dirty)
				hashes_save(g.roms, g.sys->systems[g.si].folder, g.games, g.ng);
			g.cache_dirty = false;
			system_match();
			g.gi = 0;
			g.gs = G_LOOK;
			g.phase = PH_GAMES;
			break;
		case PH_GAMES:
			if (g.gi >= g.ng) {
				save_system();
				g.p.systems_done++;
				g.si++;
				g.phase = PH_NEXT_SYS;
				break;
			}
			if (!game_step(now)) return 1;
			break;
		case PH_BADGES:
			free_system();
			if (g.bj || g.bq_n) {
				if (!net_online()) { g.bq_n = 0; return 1; }
				return 1;
			}
			g.phase = PH_DONE;
			break;
		case PH_DONE:
		default:
			if (g.ask.state == AS_FLIGHT) net_async_abort();
			save_system();
			free_system();
			free(g.ask.body);
			g.ask.body = NULL;
			if (g.bj) { bjob_unref(g.bj); g.bj = NULL; }
			g.bq_n = 0;
			memset(g.token, 0, sizeof g.token);
			g.active = false;
			snprintf(g.p.now, sizeof g.p.now, "%s", "");
			fprintf(stderr, "radl: done: %d new, %d updated, %d ready, %d none, %d failed, "
			                "%d unlocks, %d badges, %d requests%s\n",
			        g.p.fetched, g.p.updated, g.p.ready, g.p.none, g.p.failed,
			        g.p.unlocks, g.p.badges, g.p.requests,
			        g.p.problem == RADL_SIGN_IN ? " (the account was refused)"
			        : g.p.problem == RADL_NO_ROOM ? " (the card is full)"
			        : g.p.problem == RADL_NO_ANSWER ? " (no answer)" : "");
			return 0;
		}
	}
	return 1;
}

void radl_cancel(void)
{
	if (!g.active) return;
	if (g.ask.state == AS_FLIGHT) {
		net_async_abort();
		unlink(g.tmp);
	}
	g.ask.state = AS_IDLE;
	save_system();
	free_system();
	free(g.ask.body);
	g.ask.body = NULL;
	if (g.bj) { bjob_unref(g.bj); g.bj = NULL; }
	g.bq_n = 0;
	memset(g.token, 0, sizeof g.token);
	g.active = false;
	fprintf(stderr, "radl: stopped: %d new, %d updated, %d unlocks, %d requests\n",
	        g.p.fetched, g.p.updated, g.p.unlocks, g.p.requests);
}

bool radl_running(void) { return g.active; }

void radl_status(radl_progress *out)
{
	if (out) *out = g.p;
}
