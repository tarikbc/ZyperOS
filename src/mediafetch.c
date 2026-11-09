/* SPDX-License-Identifier: GPL-3.0-only */
/* What the sources offer for one game's Cover and Video page: see
 * src/mediafetch.h. */
#include "mediafetch.h"

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "artscrape.h"
#include "fileio.h"
#include "library.h"
#include "monoclock.h"
#include "net.h"
#include "sgdb.h"
#include "ss.h"
#include "ssfetch.h"
#include "ssrun.h"

#define MF_INDEX_MAX (8 * 1024 * 1024)   /* artscrape.c's INDEX_MAX: NES's is 4 MB */
#define MF_JSON_MAX  (1024 * 1024)
#define MF_WAIT_S    60
#define MF_API_S     20
#define MF_VIDEO_S   120
#define MF_HELD_MS   5000

typedef enum {
	PH_LR_BOX, PH_LR_INDEX, PH_LR_TITLE, PH_LR_SNAP, PH_SS, PH_SG_SEARCH, PH_SG_GRIDS, PH_DONE
} src_phase;

typedef enum { F_NONE, F_SRC, F_SS, F_THUMB, F_FULL, F_VIDEO } in_flight;

static mf_state S;
static struct {
	char folder[CFG_STR], file[LIB_PATH], stem[LIB_PATH], exts[CFG_STR];
	char roms[LIB_PATH], dir[LIB_PATH * 2];
	char lrname[LIB_PATH];      /* the name libretro files it under */
	int  lr_coll;               /* which of the console's collections */
	bool lr_indexed;            /* this collection's listing was read */
	long sg_id;
	char video_url[MF_URL];
	char pending[MF_PATH];      /* the file the request in flight writes */
} G;
static src_phase g_src;
static in_flight g_fly;
static int       g_fly_i;
static int       g_lo = -1, g_hi = -1, g_full = -1;
static bool      g_video;
static long      g_held_since;      /* another's request in the slot, since */

const mf_state *mf_get(void) { return &S; }

as_source mf_asking(void)
{
	int i;

	for (i = 0; i < AS_SOURCES; i++)
		if (S.q[i] == MFQ_ASKING) return (as_source)i;
	return AS_NONE;
}

static void tmp_path(char *out, size_t n, const char *what, int i)
{
	if (i >= 0) snprintf(out, n, "%s/%s%d.img", MF_DIR, what, i);
	else snprintf(out, n, "%s/%s", MF_DIR, what);
}

/* ---- the candidates --------------------------------------------------------- */

static mf_cand *add(as_source src, as_kind kind, const char *region, const char *by)
{
	mf_cand *c;

	if (S.n >= MF_MAX) return NULL;
	c = &S.c[S.n++];
	memset(c, 0, sizeof *c);
	c->as.src = src;
	c->as.kind = kind;
	snprintf(c->as.region, sizeof c->as.region, "%s", region ? region : "");
	snprintf(c->as.by, sizeof c->as.by, "%s", by ? by : "");
	return c;
}

static int count_of(as_source src)
{
	int i, n = 0;

	for (i = 0; i < S.n; i++) if (S.c[i].as.src == src) n++;
	return n;
}

/* ---- starting and ending ------------------------------------------------------ */

static void clear_dir(void)
{
	DIR *d = opendir(MF_DIR);
	struct dirent *e;

	if (!d) return;
	while ((e = readdir(d))) {
		char p[MF_PATH];

		if (e->d_name[0] == '.') continue;
		if (snprintf(p, sizeof p, "%s/%s", MF_DIR, e->d_name) < (int)sizeof p) unlink(p);
	}
	closedir(d);
}

static void abort_flight(void)
{
	if (g_fly == F_SS) ss_run_cancel();
	else if (g_fly != F_NONE) net_async_abort();
	g_fly = F_NONE;
}

void mf_end(void)
{
	abort_flight();
	clear_dir();
	rmdir(MF_DIR);
	memset(&S, 0, sizeof S);
	memset(&G, 0, sizeof G);
	g_lo = g_hi = g_full = -1;
	g_video = false;
}

void mf_begin(const char *folder, const char *file, const char *stem, const char *exts,
              const char *roms, bool net)
{
	int i;

	mf_end();
	snprintf(G.folder, sizeof G.folder, "%s", folder ? folder : "");
	snprintf(G.file, sizeof G.file, "%s", file ? file : "");
	snprintf(G.stem, sizeof G.stem, "%s", stem ? stem : "");
	snprintf(G.exts, sizeof G.exts, "%s", exts ? exts : "");
	snprintf(G.roms, sizeof G.roms, "%s", roms ? roms : "");
	snprintf(G.dir, sizeof G.dir, "%s/%s", G.roms, G.folder);
	snprintf(G.lrname, sizeof G.lrname, "%s", G.stem);
	for (i = 0; i < AS_SOURCES; i++) S.q[i] = MFQ_WAIT;
	S.offline = !net;
	g_src = PH_LR_BOX;
	mkdir(MF_DIR, 0700);
	clear_dir();
}

void mf_want(int lo, int hi)
{
	g_lo = lo;
	g_hi = hi;
}

bool mf_want_full(int i)
{
	if (i < 0 || i >= S.n) return false;
	if (S.c[i].full_st == MFT_HAVE) return true;
	if (S.c[i].full_st == MFT_FAILED) return false;
	g_full = i;
	return false;
}

bool mf_get_video(void)
{
	if (!S.video_offer || S.offline || S.video_coming) return false;
	g_video = true;
	S.video_done = 0;
	S.video_got = 0;
	S.video_coming = true;
	return true;
}

/* ---- the requests -------------------------------------------------------------- */

static bool get(const char *url, const char *path, int wait_s, in_flight kind, int i)
{
	snprintf(G.pending, sizeof G.pending, "%s", path);
	unlink(path);
	if (!net_get_async(url, path, wait_s)) return false;
	g_fly = kind;
	g_fly_i = i;
	return true;
}

static bool get_sg(const char *url, const char *path)
{
	char hdr[SGDB_KEY_MAX + 32];
	bool ok;

	if (!sgdb_auth_header(hdr, sizeof hdr)) return false;
	snprintf(G.pending, sizeof G.pending, "%s", path);
	unlink(path);
	ok = net_get_async_hdr(url, path, MF_API_S, hdr);
	memset(hdr, 0, sizeof hdr);
	if (ok) g_fly = F_SRC;
	return ok;
}

static const char *lr_dir(src_phase p)
{
	return p == PH_LR_TITLE ? "Named_Titles" : p == PH_LR_SNAP ? "Named_Snaps" : "Named_Boxarts";
}

/* The next of the sources' requests; false when none could start, the
 * phase moved on */
static bool start_source(void)
{
	char url[MF_URL * 2], path[MF_PATH];
	const char *coll = art_collection(G.folder, G.lr_coll);

	switch (g_src) {
	case PH_LR_BOX:
	case PH_LR_TITLE:
	case PH_LR_SNAP:
		if (!coll) {
			S.q[AS_LR] = MFQ_OFF;
			g_src = PH_SS;
			return false;
		}
		S.q[AS_LR] = MFQ_ASKING;
		tmp_path(path, sizeof path, g_src == PH_LR_BOX ? "lr-box" : g_src == PH_LR_TITLE ? "lr-title" : "lr-snap", -1);
		if (!art_lr_url(coll, lr_dir(g_src), G.lrname, url, sizeof url) ||
		    !get(url, path, MF_WAIT_S, F_SRC, -1)) {
			g_src = g_src == PH_LR_SNAP || g_src == PH_LR_BOX ? PH_SS : PH_LR_SNAP;
			if (g_src == PH_SS) S.q[AS_LR] = count_of(AS_LR) ? MFQ_DONE : MFQ_NO_ANSWER;
			return false;
		}
		return true;
	case PH_LR_INDEX:
		S.q[AS_LR] = MFQ_ASKING;
		tmp_path(path, sizeof path, "lr-index.html", -1);
		if (!coll || !art_lr_index_url(coll, url, sizeof url) || !get(url, path, MF_WAIT_S, F_SRC, -1)) {
			S.q[AS_LR] = MFQ_NONE;
			g_src = PH_SS;
			return false;
		}
		return true;
	case PH_SS:
		if (!ss_signed_in() || !ss_system_id(G.folder)) {
			S.q[AS_SS] = MFQ_OFF;
			g_src = PH_SG_SEARCH;
			return false;
		}
		S.q[AS_SS] = MFQ_ASKING;
		if (!ss_run_begin_lookup(G.folder, G.file, G.stem, G.dir, G.exts)) {
			S.q[AS_SS] = MFQ_NO_ANSWER;
			g_src = PH_SG_SEARCH;
			return false;
		}
		g_fly = F_SS;
		return true;
	case PH_SG_SEARCH: {
		char want[512];

		if (!sgdb_have_key()) {
			S.q[AS_SG] = MFQ_OFF;
			g_src = PH_DONE;
			return false;
		}
		sgdb_title(G.stem, want, sizeof want);
		S.q[AS_SG] = MFQ_ASKING;
		tmp_path(path, sizeof path, "sg.json", -1);
		if (!want[0] || !sgdb_search_url(want, url, sizeof url) || !get_sg(url, path)) {
			S.q[AS_SG] = want[0] ? MFQ_NO_ANSWER : MFQ_NONE;
			g_src = PH_DONE;
			return false;
		}
		return true;
	}
	case PH_SG_GRIDS:
		tmp_path(path, sizeof path, "sg.json", -1);
		if (!sgdb_grids_url(G.sg_id, url, sizeof url) || !get_sg(url, path)) {
			S.q[AS_SG] = MFQ_NO_ANSWER;
			g_src = PH_DONE;
			return false;
		}
		return true;
	case PH_DONE:
		break;
	}
	return false;
}

/* A source's request is in: what it found, and the next */
static void source_done(int r)
{
	char got[MF_PATH];
	int http = net_async_http();

	snprintf(got, sizeof got, "%s", G.pending);
	switch (g_src) {
	case PH_LR_BOX:
	case PH_LR_TITLE:
	case PH_LR_SNAP:
		if (r > 0 && art_is_cover(got)) {
			static const as_kind K[] = { [PH_LR_BOX] = AK_BOX, [PH_LR_TITLE] = AK_TITLE, [PH_LR_SNAP] = AK_SHOT };
			mf_cand *c = add(AS_LR, K[g_src], "", G.lrname);
			char keep[MF_PATH];

			if (c) {
				/* Whole as it comes: no smaller copy to ask for */
				tmp_path(keep, sizeof keep, "lr", S.n - 1);
				rename(got, keep);
				snprintf(c->thumb, sizeof c->thumb, "%s", keep);
				snprintf(c->full, sizeof c->full, "%s", keep);
				c->thumb_st = c->full_st = MFT_HAVE;
			}
		} else {
			unlink(got);
		}
		if (g_src == PH_LR_BOX && !count_of(AS_LR)) {
			/* Not by this name: the collection's listing names it, as a Box
			 * Art run's second pass does */
			if (!G.lr_indexed) { g_src = PH_LR_INDEX; return; }
			if (art_collection(G.folder, G.lr_coll + 1)) {
				G.lr_coll++;
				G.lr_indexed = false;
				snprintf(G.lrname, sizeof G.lrname, "%s", G.stem);
				g_src = PH_LR_BOX;
				return;
			}
			S.q[AS_LR] = r < 0 && http == 0 ? MFQ_NO_ANSWER : MFQ_NONE;
			g_src = PH_SS;
			return;
		}
		g_src = g_src == PH_LR_BOX ? PH_LR_TITLE : g_src == PH_LR_TITLE ? PH_LR_SNAP : PH_SS;
		if (g_src == PH_SS) S.q[AS_LR] = count_of(AS_LR) ? MFQ_DONE : MFQ_NONE;
		return;
	case PH_LR_INDEX: {
		size_t n;
		char *html = r > 0 ? file_read(got, MF_INDEX_MAX, &n) : NULL;
		char name[LIB_PATH];

		unlink(got);
		G.lr_indexed = true;
		if (html && art_match_index(html, G.stem, name, sizeof name) && strcmp(name, G.lrname)) {
			snprintf(G.lrname, sizeof G.lrname, "%s", name);
			g_src = PH_LR_BOX;
		} else if (art_collection(G.folder, G.lr_coll + 1)) {
			G.lr_coll++;
			G.lr_indexed = false;
			snprintf(G.lrname, sizeof G.lrname, "%s", G.stem);
			g_src = PH_LR_BOX;
		} else {
			S.q[AS_LR] = !html && http == 0 ? MFQ_NO_ANSWER : MFQ_NONE;
			g_src = PH_SS;
		}
		free(html);
		return;
	}
	case PH_SG_SEARCH:
	case PH_SG_GRIDS: {
		size_t n;
		char *body = r > 0 ? file_read(got, MF_JSON_MAX, &n) : NULL;

		unlink(got);
		if (!body) {
			if (http == 401 || http == 403) sgdb_key_mark(SGDB_REFUSED);
			S.q[AS_SG] = http == 404 ? MFQ_NONE : MFQ_NO_ANSWER;
			g_src = PH_DONE;
			return;
		}
		sgdb_key_mark(SGDB_WORKS);
		if (g_src == PH_SG_SEARCH) {
			char want[512], name[256];
			double score;

			sgdb_title(G.stem, want, sizeof want);
			if (sgdb_pick_game(body, n, want, &G.sg_id, name, sizeof name, &score)) {
				g_src = PH_SG_GRIDS;
			} else {
				S.q[AS_SG] = MFQ_NONE;
				g_src = PH_DONE;
			}
		} else {
			sgdb_grid gr[MF_SG_MAX];
			int k, ng = sgdb_list_grids(body, n, gr, MF_SG_MAX);

			for (k = 0; k < ng; k++) {
				mf_cand *c = add(AS_SG, AK_FAN, "", gr[k].author);

				if (!c) break;
				snprintf(c->thumb_url, sizeof c->thumb_url, "%s", gr[k].thumb);
				snprintf(c->full_url, sizeof c->full_url, "%s", gr[k].url);
			}
			S.q[AS_SG] = ng ? MFQ_DONE : MFQ_NONE;
			g_src = PH_DONE;
		}
		free(body);
		return;
	}
	default:
		return;
	}
}

static void ss_done(int r)
{
	const ss_result *res = r == 0 ? ss_run_result() : NULL;
	int http = net_async_http(), k;

	if (res) {
		static const as_kind K[] = { [SSM_BOX] = AK_BOX, [SSM_BOX3D] = AK_BOX3D,
		                             [SSM_TITLE] = AK_TITLE, [SSM_SHOT] = AK_SHOT };

		for (k = 0; k < res->nmedia; k++) {
			mf_cand *c = add(AS_SS, K[res->media[k].kind], res->media[k].region, "");

			if (!c) break;
			if (!ss_media_sized(res->media[k].url, MF_THUMB_PX, c->thumb_url, sizeof c->thumb_url) ||
			    !ss_media_sized(res->media[k].url, MF_FULL_PX, c->full_url, sizeof c->full_url))
				S.n--;
		}
		if (res->video[0] && strlen(res->video) < sizeof G.video_url) {
			snprintf(G.video_url, sizeof G.video_url, "%s", res->video);
			S.video_offer = true;
			S.video_size = res->video_size;
		}
		S.q[AS_SS] = count_of(AS_SS) ? MFQ_DONE : MFQ_NONE;
	} else {
		S.q[AS_SS] = http == 404 ? MFQ_UNKNOWN : http == 0 || http == 429 || http >= 500
		             ? MFQ_NO_ANSWER : MFQ_NONE;
	}
	ss_run_cancel();
	g_src = PH_SG_SEARCH;
}

static bool is_mp4(const char *path)
{
	unsigned char h[8];
	FILE *f = fopen(path, "rb");
	size_t n;

	if (!f) return false;
	n = fread(h, 1, sizeof h, f);
	fclose(f);
	return n == 8 && !memcmp(h + 4, "ftyp", 4);
}

static void video_done(int r)
{
	char dest[MF_PATH];

	S.video_coming = false;
	if (r > 0 && is_mp4(G.pending) &&
	    mf_media_path(G.roms, G.folder, G.stem, ".mp4", dest, sizeof dest) &&
	    rename(G.pending, dest) == 0) {
		S.video_done = 1;
		return;
	}
	unlink(G.pending);
	S.video_done = -1;
}

/* The request in flight has finished, or not: true when it has */
static bool poll_flight(void)
{
	int r;

	if (g_fly == F_SS) {
		r = ss_run_step();
		if (r == 1) return false;
		g_fly = F_NONE;
		ss_done(r);
		return true;
	}
	if (g_fly == F_VIDEO) {
		char part[MF_PATH + 8];
		struct stat st;

		snprintf(part, sizeof part, "%s.part", G.pending);
		if (stat(part, &st) == 0 && st.st_size != S.video_got) {
			S.video_got = (long)st.st_size;
			r = net_async_poll();
			if (r == 0) return true;      /* more bytes: the bar moves */
		} else {
			r = net_async_poll();
			if (r == 0) return false;
		}
	} else {
		r = net_async_poll();
		if (r == 0) return false;
	}
	switch (g_fly) {
	case F_SRC:
		g_fly = F_NONE;
		source_done(r);
		break;
	case F_THUMB:
	case F_FULL: {
		mf_cand *c = &S.c[g_fly_i];
		bool ok = r > 0 && art_is_cover(G.pending);

		if (!ok) unlink(G.pending);
		if (g_fly == F_THUMB) {
			c->thumb_st = ok ? MFT_HAVE : MFT_FAILED;
			if (ok) snprintf(c->thumb, sizeof c->thumb, "%s", G.pending);
		} else {
			c->full_st = ok ? MFT_HAVE : MFT_FAILED;
			if (ok) snprintf(c->full, sizeof c->full, "%s", G.pending);
			/* The whole is the small copy too, when that never came */
			if (ok && c->thumb_st != MFT_HAVE) {
				c->thumb_st = MFT_HAVE;
				snprintf(c->thumb, sizeof c->thumb, "%s", G.pending);
			}
			if (g_full == g_fly_i) g_full = -1;
		}
		g_fly = F_NONE;
		break;
	}
	case F_VIDEO:
		g_fly = F_NONE;
		video_done(r);
		break;
	default:
		g_fly = F_NONE;
		break;
	}
	return true;
}

bool mf_step(void)
{
	char path[MF_PATH];
	int i;

	if (g_fly != F_NONE) return poll_flight();
	if (S.offline) return false;
	/* Another's request in the slot (an account answer, Muse's cover): its
	 * turn first, a few seconds at most, after which net_get_async clears a
	 * slot its owner walked away from */
	if (net_async_held()) {
		long now = now_ms();

		if (!g_held_since) g_held_since = now;
		if (now - g_held_since < MF_HELD_MS) return false;
	}
	g_held_since = 0;

	/* What the page asked for first: a whole picture, then a video */
	if (g_full >= 0 && g_full < S.n) {
		mf_cand *c = &S.c[g_full];

		if (c->full_st == MFT_NONE && c->full_url[0]) {
			tmp_path(path, sizeof path, "f", g_full);
			c->full_st = MFT_COMING;
			if (get(c->full_url, path, MF_WAIT_S, F_FULL, g_full)) return true;
			c->full_st = MFT_FAILED;
			g_full = -1;
			return true;
		}
		g_full = -1;
	}
	if (g_video) {
		char dest[MF_PATH];

		g_video = false;
		if (mf_media_path(G.roms, G.folder, G.stem, ".mp4.snap", dest, sizeof dest)) {
			char media[sizeof G.dir + 16];

			snprintf(media, sizeof media, "%s/.media", G.dir);
			mkdir(media, 0777);
			if (get(G.video_url, dest, MF_VIDEO_S, F_VIDEO, -1)) return true;
		}
		S.video_coming = false;
		S.video_done = -1;
		return true;
	}
	/* The sources, one request each step until each has answered */
	while (g_src != PH_DONE) {
		if (start_source()) return true;
	}
	/* The small copies of the tiles on screen */
	for (i = g_lo < 0 ? 0 : g_lo; i <= g_hi && i < S.n; i++) {
		mf_cand *c = &S.c[i];

		if (c->thumb_st != MFT_NONE || !c->thumb_url[0]) continue;
		tmp_path(path, sizeof path, "t", i);
		c->thumb_st = MFT_COMING;
		if (get(c->thumb_url, path, MF_WAIT_S, F_THUMB, i)) return true;
		c->thumb_st = MFT_FAILED;
		return true;
	}
	return false;
}

/* ---- using a picture: src/mediafile.c does the files -------------------- */

bool mf_use(int i, char *dest, int destn)
{
	if (i < 0 || i >= S.n || S.c[i].full_st != MFT_HAVE) return false;
	return mf_file_use(G.roms, G.folder, G.stem, S.c[i].full, &S.c[i].as, dest, destn);
}

bool mf_put_back(void)
{
	return mf_file_put_back(G.roms, G.folder, G.stem);
}
