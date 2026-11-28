/* SPDX-License-Identifier: GPL-3.0-only */
/* What MENU > Downloads counts: src/dlcount.h */
#include "dlcount.h"

#include "cheatsdl.h"
#include "db.h"
#include "home.h"
#include "platform.h"
#include "radl.h"
#include "rafetch.h"
#include "scraper_screen.h"
#include "ss.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

/* A cover and a snap until the card's own say (the Scraper's guesses) */
#define DL_MB_COVER 0.25
#define DL_MB_VIDEO 2.5

static dl_counts g_lib, g_one, g_work, g_scope;
static const dl_counts *g_fx;
static bool g_lib_ok, g_one_ok, g_working, g_stale;
static int g_next;
static unsigned g_gen;
static long long g_now;
static unsigned long long g_cov_bytes, g_vid_bytes;
static int g_cov_n, g_vid_n;
static unsigned g_t0;

void dl_count_again(app *a, const char *only)
{
	struct statvfs vfs;

	memset(&g_work, 0, sizeof g_work);
	snprintf(g_work.only, sizeof g_work.only, "%s", only ? only : "");
	g_work.ss_in = ss_signed_in();
	g_work.ra_in = ra_signed_in();
	g_work.free = statvfs(P_ROMS, &vfs) == 0 ? (unsigned long long)vfs.f_bavail * vfs.f_frsize : 0;
	g_cov_bytes = g_vid_bytes = 0;
	g_cov_n = g_vid_n = 0;
	g_now = (long long)time(NULL);
	g_next = 0;
	g_t0 = plat_now_ms();
	/* A card with no system (no games yet) is counted too, at once: its
	 * count is one of nothing. It was never counted, so the page said
	 * Counting until it closed (Tarik's R36S, 1.1.1-rc.1, an empty card) */
	g_working = true;
	scraper_count_begin();
	if (!a || a->sys.count <= 0) dl_count_step(a, 0);
}

/* One of a's systems into the count: the Scraper's count and the
 * achievements download's, side by side */
static void count_one(app *a, int i)
{
	const system_cfg *sc = &a->sys.systems[i];
	scraper_sys_count c;
	radl_sys r;
	dl_sys *s;
	int w;

	if (g_work.only[0] && strcmp(sc->folder, g_work.only)) return;
	if (g_work.n >= CFG_MAX_SYSTEMS) return;
	if (!scraper_count_sys(a, i, false, g_work.ss_in, g_now, &c)) return;
	s = &g_work.s[g_work.n++];
	memset(s, 0, sizeof *s);
	snprintf(s->folder, sizeof s->folder, "%s", sc->folder);
	snprintf(s->tag, sizeof s->tag, "%s", sc->tag);
	snprintf(s->name, sizeof s->name, "%s", sc->name);
	snprintf(s->key, sizeof s->key, "%s", home_art_key(sc->tag));
	s->games = c.games;
	s->cov_miss = c.miss;
	for (w = 0; w < SNAPS_FOR_COUNT; w++) {
		s->vid_need[w] = c.vneed[w];
		s->vid_both[w] = c.both[w];
	}
	s->vid_have = c.nvideos;
	s->vid_bytes = c.vroom;
	s->names_todo = c.names;
	s->names_have = c.named;
	s->names_ok = c.ss_named;
	g_cov_bytes += c.cover_bytes;
	g_cov_n += c.ncovers;
	g_vid_bytes += c.vroom;
	g_vid_n += c.nvideos;
	{
		cheats_count cc;

		cheats_count_sys(sc->folder, sys_games(&a->view[i]), &cc);
		s->cht_sys = cc.mapped;
		s->cht_have = cc.have;
		s->cht_none = cc.none;
		s->cht_todo = cc.todo;
	}
	if (radl_plan_one(sc, P_ROMS, &r)) {
		s->ach_sys = true;
		s->ach_games = r.games;
		s->ach_ready = r.ready;
		s->ach_todo = r.todo;
		s->ach_none = r.none;
	}
}

static void count_end(void)
{
	fprintf(stderr, "downloads: counted %d systems%s%s in %u ms\n", g_work.n, g_work.only[0] ? " of " : "",
	        g_work.only, plat_now_ms() - g_t0);
	g_work.mb_cover = g_cov_n >= 5 ? (double)g_cov_bytes / g_cov_n / 1e6 : DL_MB_COVER;
	g_work.mb_video = g_vid_n >= 5 ? (double)g_vid_bytes / g_vid_n / 1e6 : DL_MB_VIDEO;
	if (g_work.only[0]) {
		g_one = g_work;
		g_one_ok = true;
	} else {
		g_lib = g_work;
		g_lib_ok = true;
		g_stale = false;
	}
	g_working = false;
	g_gen++;
}

bool dl_count_step(app *a, unsigned budget_ms)
{
	unsigned t0 = plat_now_ms();

	if (!g_working) return false;
	while (a && g_next < a->sys.count) {
		count_one(a, g_next++);
		if (plat_now_ms() - t0 >= budget_ms) break;
	}
	if (!a || g_next >= a->sys.count) count_end();
	return g_working;
}

bool dl_counting(void) { return g_working; }

void dl_count_progress(int *done, int *of)
{
	*done = g_next;
	*of = g_next;
	if (g_working) *of = g_next + 1 > *of ? g_next + 1 : *of;
}

void dl_count_fixture(const dl_counts *fx) { g_fx = fx; }

const dl_counts *dl_count_shown(const char *only)
{
	int i;

	if (g_fx) return g_fx;
	if (!only || !only[0]) return g_lib_ok ? &g_lib : NULL;
	if (g_one_ok && !strcmp(g_one.only, only)) return &g_one;
	if (!g_lib_ok) return NULL;
	/* The console's own line of the library's count */
	g_scope = g_lib;
	g_scope.n = 0;
	snprintf(g_scope.only, sizeof g_scope.only, "%s", only);
	for (i = 0; i < g_lib.n; i++)
		if (!strcmp(g_lib.s[i].folder, only)) g_scope.s[g_scope.n++] = g_lib.s[i];
	return &g_scope;
}

unsigned dl_count_gen(void) { return g_gen; }
void dl_count_stale(void) { g_stale = true; g_one_ok = false; }
bool dl_count_is_stale(void) { return g_stale || !g_lib_ok; }

/* ---- the switches ---------------------------------------------------------------- */

static const char *const KIND_KEY[DLK_KINDS] = { "covers", "videos", "names", "ach", "cheats" };

static bool db_flag(const char *key, const char *want)
{
	char v[8];

	return db_get_str(db_dev(), key, v, sizeof v, NULL) && !strcmp(v, want);
}

bool dl_kind_in(dl_kind k)
{
	char key[48];

	if (g_fx) return g_fx->fx_in[k];
	if (k == DLK_NAMES) return scraper_names_on();
	snprintf(key, sizeof key, "dl.off.%s", KIND_KEY[k]);
	return !db_flag(key, "1");
}

void dl_kind_set(dl_kind k, bool in)
{
	char key[48];

	if (k == DLK_NAMES) { scraper_names_set(in); return; }
	snprintf(key, sizeof key, "dl.off.%s", KIND_KEY[k]);
	if (in) db_del(db_dev(), key);
	else db_set_str(db_dev(), key, "1");
}

bool dl_sys_on(dl_kind k, const char *folder)
{
	char key[CFG_STR + 24];

	if (g_fx) {
		for (int i = 0; i < g_fx->n; i++)
			if (!strcmp(g_fx->s[i].folder, folder)) return g_fx->s[i].fx_on[k];
		return false;
	}
	switch (k) {
	case DLK_COVERS: return snaps_covers(folder);
	case DLK_VIDEOS: return snaps_get(folder);
	case DLK_NAMES:
		/* the system's covers switch, until it is set apart */
		snprintf(key, sizeof key, "dl.names.%s", folder);
		if (db_flag(key, "on")) return true;
		if (db_flag(key, "off")) return false;
		return snaps_covers(folder);
	case DLK_ACH:
	case DLK_CHEATS:
		snprintf(key, sizeof key, "dl.off.%s.%s", KIND_KEY[k], folder);
		return !db_flag(key, "1");
	default: return false;
	}
}

void dl_sys_set(dl_kind k, const char *folder, bool on)
{
	char key[CFG_STR + 24];

	switch (k) {
	case DLK_COVERS: snaps_set_covers(folder, on); break;
	case DLK_VIDEOS: snaps_set_get(folder, on); break;
	case DLK_NAMES:
		snprintf(key, sizeof key, "dl.names.%s", folder);
		db_set_str(db_dev(), key, on ? "on" : "off");
		break;
	case DLK_ACH:
	case DLK_CHEATS:
		snprintf(key, sizeof key, "dl.off.%s.%s", KIND_KEY[k], folder);
		if (on) db_del(db_dev(), key);
		else db_set_str(db_dev(), key, "1");
		break;
	default: break;
	}
}

const char *dl_kind_blocked(const dl_counts *c, dl_kind k)
{
	if ((k == DLK_VIDEOS || k == DLK_NAMES) && !c->ss_in) return T(S_DOWNLOADS_NEEDS_SS);
	if (k == DLK_ACH && !c->ra_in) return T(S_DOWNLOADS_SIGN_IN_FIRST);
	return NULL;
}

/* ---- the totals ------------------------------------------------------------------- */

int dl_sys_todo(const dl_counts *c, int i, dl_kind k, bool *on)
{
	const dl_sys *s = &c->s[i];
	bool o = dl_sys_on(k, s->folder);

	if (on) *on = o;
	switch (k) {
	case DLK_COVERS: return s->cov_miss;
	case DLK_VIDEOS: return s->vid_need[snaps_for()];
	case DLK_NAMES:
		if (!s->names_ok) return 0;
		/* a system whose covers are out asks its games with no cover too */
		return s->names_todo + (dl_sys_on(DLK_COVERS, s->folder) && dl_kind_in(DLK_COVERS) ? 0 : s->cov_miss);
	case DLK_ACH: return s->ach_sys ? s->ach_todo : 0;
	case DLK_CHEATS: return s->cht_sys ? s->cht_todo : 0;
	default: return 0;
	}
}

static bool kind_of_sys(const dl_sys *s, dl_kind k)
{
	if (k == DLK_ACH) return s->ach_sys;
	if (k == DLK_CHEATS) return s->cht_sys;
	if (k == DLK_NAMES) return s->names_ok;
	return true;
}

void dl_sum_of(const dl_counts *c, dl_kind k, dl_sum *o)
{
	int i;

	memset(o, 0, sizeof *o);
	for (i = 0; i < c->n; i++) {
		const dl_sys *s = &c->s[i];
		bool on;
		int todo = dl_sys_todo(c, i, k, &on), have = 0;

		if (!kind_of_sys(s, k)) continue;
		o->all++;
		switch (k) {
		case DLK_COVERS: have = s->games - s->cov_miss; break;
		case DLK_VIDEOS: have = s->vid_have; o->bytes += s->vid_bytes; break;
		case DLK_NAMES:  have = s->names_have; break;
		case DLK_ACH:    have = s->ach_ready; o->none += s->ach_none; break;
		case DLK_CHEATS: have = s->cht_have; o->none += s->cht_none; break;
		default: break;
		}
		o->have += have;
		if (have) o->have_sys++;
		if (!on) continue;
		o->on++;
		o->todo += todo;
		if (todo) o->todo_sys++;
	}
	if (k == DLK_COVERS) o->bytes = (unsigned long long)(o->have * c->mb_cover * 1e6);
	if (k == DLK_ACH) o->bytes = (unsigned long long)(o->have * DL_MB_SET * 1e6);
	if (k == DLK_CHEATS) o->bytes = (unsigned long long)(o->have * CHT_MB_FILE * 1e6);
}

void dl_plan_of(const dl_counts *c, int only, dl_plan *o)
{
	int i, fw = snaps_for();
	bool in[DLK_KINDS];

	memset(o, 0, sizeof *o);
	for (i = 0; i < DLK_KINDS; i++)
		in[i] = (only < 0 ? dl_kind_in((dl_kind)i) : only == i) && !dl_kind_blocked(c, (dl_kind)i);
	for (i = 0; i < c->n; i++) {
		const dl_sys *s = &c->s[i];
		bool cov = in[DLK_COVERS] && dl_sys_on(DLK_COVERS, s->folder);
		bool vid = in[DLK_VIDEOS] && dl_sys_on(DLK_VIDEOS, s->folder);
		int nm = 0;

		if (cov) o->covers += s->cov_miss;
		if (vid) o->videos += s->vid_need[fw];
		if (in[DLK_NAMES] && s->names_ok && dl_sys_on(DLK_NAMES, s->folder))
			nm = s->names_todo + (cov ? 0 : s->cov_miss);
		o->names += nm;
		/* a game asked once for its names and its video */
		o->looks += nm + (vid ? s->vid_need[fw] - (nm ? s->vid_both[fw] : 0) : 0);
		if (in[DLK_ACH] && s->ach_sys && dl_sys_on(DLK_ACH, s->folder) && s->ach_todo) {
			o->ach += s->ach_todo;
			o->ach_sys++;
		}
		if (in[DLK_CHEATS] && s->cht_sys && dl_sys_on(DLK_CHEATS, s->folder)) o->cheats += s->cht_todo;
	}
	o->n = o->covers + o->videos + o->names + o->ach + o->cheats;
	o->bytes = (o->covers * c->mb_cover + o->videos * c->mb_video + o->ach * DL_MB_SET) * 1e6;
	/* cheats: the list's index once, then a few KB a game that has some
	 * (about six in ten, the lab's measure) */
	if (o->cheats) o->bytes += (CHT_MB_INDEX + o->cheats * DL_CHT_HITS * CHT_MB_FILE) * 1e6;
	o->art_secs = o->covers * (c->ss_in ? DL_S_SS : DL_S_LR) + o->looks * DL_S_SS + o->videos * DL_S_VIDEO;
	o->ra_secs = o->ach ? o->ach * DL_S_SET + o->ach_sys * DL_S_SYSTEM : 0;
	o->cht_secs = o->cheats ? DL_S_CHT_INDEX + o->cheats * DL_CHT_HITS * DL_S_CHT : 0;
	/* side by side: the longest lane is the time */
	o->secs = o->art_secs > o->ra_secs ? o->art_secs : o->ra_secs;
	if (o->cht_secs > o->secs) o->secs = o->cht_secs;
	o->fits = !c->free || o->bytes + (double)DL_RESERVE <= (double)c->free;
}
